#include "CallsignSpeech.h"

#include <QHash>
#include <QRegularExpression>

namespace AetherSDR::mobile::CallsignSpeech {

namespace {

// Words to characters: the NATO / ITU alphabet with the spellings
// recognisers write, digits and their radio forms, and the separators.
const QHash<QString, QString>& words()
{
    static const QHash<QString, QString> map = [] {
        QHash<QString, QString> m;
        const char* const letters[][2] = {
            {"alpha", "A"}, {"alfa", "A"}, {"bravo", "B"}, {"charlie", "C"}, {"delta", "D"}, {"echo", "E"},
            {"foxtrot", "F"}, {"fox", "F"}, {"golf", "G"}, {"hotel", "H"}, {"india", "I"}, {"juliet", "J"},
            {"juliett", "J"}, {"kilo", "K"}, {"lima", "L"}, {"mike", "M"}, {"november", "N"}, {"oscar", "O"},
            {"papa", "P"}, {"quebec", "Q"}, {"romeo", "R"}, {"sierra", "S"}, {"tango", "T"}, {"uniform", "U"},
            {"victor", "V"}, {"whiskey", "W"}, {"whisky", "W"}, {"xray", "X"}, {"x-ray", "X"}, {"yankee", "Y"},
            {"zulu", "Z"},
            {"zero", "0"}, {"one", "1"}, {"wun", "1"}, {"two", "2"}, {"three", "3"}, {"tree", "3"},
            {"four", "4"}, {"fower", "4"}, {"five", "5"}, {"fife", "5"}, {"six", "6"}, {"seven", "7"},
            {"eight", "8"}, {"ait", "8"}, {"nine", "9"}, {"niner", "9"},
            {"slash", "/"}, {"stroke", "/"},
        };
        for (const auto& w : letters) {
            m.insert(QString::fromLatin1(w[0]), QString::fromLatin1(w[1]));
        }
        return m;
    }();
    return map;
}

// A call: an optional prefix and "/", one to three characters, a digit,
// up to five more ending in a letter, an optional "/" suffix.
bool isCall(const QString& s)
{
    static const QRegularExpression re(
        QStringLiteral("^([A-Z0-9]{1,4}/)?[A-Z0-9]{1,3}[0-9][A-Z0-9]{0,5}[A-Z](/[A-Z0-9]{1,4})?$"));
    static const QRegularExpression letter(QStringLiteral("[A-Z]"));
    return re.match(s).hasMatch() && s.contains(letter);
}

} // namespace

Result parse(const QString& text)
{
    Result r;
    r.heard = text.simplified();
    // Lower case, the words apart: "X-ray" and "x ray" as one word, other
    // punctuation as a space; a run like "VA3MW" stays together.
    QString t = text.toLower();
    t.replace(QRegularExpression(QStringLiteral("\\bx[\\s-]ray\\b")), QStringLiteral("xray"));
    t.replace(QRegularExpression(QStringLiteral("[^a-z0-9/]+")), QStringLiteral(" "));
    // Each word as what it spells: a letter or digit word, a single letter,
    // digits, two letters (not a common English word), a separator; or a call
    // written whole ("va3mw"); or nothing ("log", "contact", "at").
    static const QStringList common{
        QStringLiteral("to"), QStringLiteral("at"), QStringLiteral("in"), QStringLiteral("on"), QStringLiteral("is"),
        QStringLiteral("it"), QStringLiteral("of"), QStringLiteral("an"), QStringLiteral("as"), QStringLiteral("be"),
        QStringLiteral("by"), QStringLiteral("do"), QStringLiteral("go"), QStringLiteral("he"), QStringLiteral("if"),
        QStringLiteral("me"), QStringLiteral("my"), QStringLiteral("no"), QStringLiteral("so"), QStringLiteral("up"),
        QStringLiteral("us"), QStringLiteral("we"), QStringLiteral("am"), QStringLiteral("or")};
    static const QRegularExpression letters(QStringLiteral("^[a-z]+$"));
    static const QRegularExpression digits(QStringLiteral("^[0-9]+$"));
    static const QRegularExpression whole(QStringLiteral("^[a-z0-9/]*[0-9][a-z0-9/]*$"));
    struct Token {
        QString chars;  // empty: not part of a call
        bool whole;     // a call written whole
    };
    QList<Token> tokens;
    for (const QString& w : t.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        if (const auto it = words().constFind(w); it != words().cend()) {
            tokens.append({it.value(), false});
        } else if (w == QLatin1String("portable")) {
            tokens.append({QStringLiteral("/P"), false});
        } else if (w == QLatin1String("mobile")) {
            tokens.append({QStringLiteral("/M"), false});
        } else if (w == QLatin1String("/")) {
            tokens.append({QStringLiteral("/"), false});
        } else if (digits.match(w).hasMatch()
                   || (letters.match(w).hasMatch() && (w.size() == 1 || (w.size() == 2 && !common.contains(w))))) {
            tokens.append({w.toUpper(), false});
        } else if (whole.match(w).hasMatch() && w.contains(QRegularExpression(QStringLiteral("[a-z]")))) {
            tokens.append({w.toUpper(), true});
        } else {
            tokens.append({QString(), false});
        }
    }
    // Candidates in the order spoken: a call written whole with the
    // separators and suffix after it, or a run of spelled words. The first
    // that reads as a call (a report after it split off) is the one.
    static const QRegularExpression tail(QStringLiteral("^(.*[A-Z])([1-5][1-9][1-9]?)$"));
    for (qsizetype i = 0; i < tokens.size(); ++i) {
        if (tokens.at(i).chars.isEmpty()) {
            continue;
        }
        QString chars = tokens.at(i).chars;
        qsizetype j = i + 1;
        for (; j < tokens.size() && !tokens.at(j).chars.isEmpty() && !tokens.at(j).whole; ++j) {
            chars += tokens.at(j).chars;
        }
        // "stroke portable": one slash.
        chars.replace(QStringLiteral("//"), QStringLiteral("/"));
        if (isCall(chars)) {
            r.call = chars;
            return r;
        }
        if (const QRegularExpressionMatch m = tail.match(chars); m.hasMatch() && isCall(m.captured(1))) {
            r.call = m.captured(1);
            r.report = m.captured(2);
            return r;
        }
        i = j - 1;
    }
    return r;
}

Result parseBest(const QStringList& alternatives)
{
    for (const QString& a : alternatives) {
        Result r = parse(a);
        if (!r.call.isEmpty()) {
            return r;
        }
    }
    Result none;
    none.heard = alternatives.isEmpty() ? QString() : alternatives.first().simplified();
    return none;
}

QStringList vocabulary()
{
    return {QStringLiteral("Alpha"), QStringLiteral("Bravo"), QStringLiteral("Charlie"), QStringLiteral("Delta"),
            QStringLiteral("Echo"), QStringLiteral("Foxtrot"), QStringLiteral("Golf"), QStringLiteral("Hotel"),
            QStringLiteral("India"), QStringLiteral("Juliett"), QStringLiteral("Kilo"), QStringLiteral("Lima"),
            QStringLiteral("Mike"), QStringLiteral("November"), QStringLiteral("Oscar"), QStringLiteral("Papa"),
            QStringLiteral("Quebec"), QStringLiteral("Romeo"), QStringLiteral("Sierra"), QStringLiteral("Tango"),
            QStringLiteral("Uniform"), QStringLiteral("Victor"), QStringLiteral("Whiskey"), QStringLiteral("X-ray"),
            QStringLiteral("Yankee"), QStringLiteral("Zulu"), QStringLiteral("Niner"), QStringLiteral("Stroke"),
            QStringLiteral("Portable")};
}

} // namespace AetherSDR::mobile::CallsignSpeech
