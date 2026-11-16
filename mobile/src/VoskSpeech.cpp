#include "VoskSpeech.h"

#include "CallsignSpeech.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThreadPool>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <type_traits>

// The C API's opaque types (vosk_api.h, 0.3.45).
struct VoskModel;
struct VoskRecognizer;

#if defined(AETHER_IOS_VOSK)
// iOS: libvosk linked in statically (no dlopen of an app's own library);
// the functions used, as vosk_api.h declares them.
extern "C" {
VoskModel* vosk_model_new(const char* model_path);
int vosk_model_find_word(VoskModel* model, const char* word);
VoskRecognizer* vosk_recognizer_new_grm(VoskModel* model, float sample_rate, const char* grammar);
void vosk_recognizer_set_max_alternatives(VoskRecognizer* recognizer, int max_alternatives);
int vosk_recognizer_accept_waveform(VoskRecognizer* recognizer, const char* data, int length);
const char* vosk_recognizer_result(VoskRecognizer* recognizer);
const char* vosk_recognizer_final_result(VoskRecognizer* recognizer);
void vosk_recognizer_free(VoskRecognizer* recognizer);
void vosk_set_log_level(int log_level);
}
#elif defined(Q_OS_LINUX) || defined(Q_OS_ANDROID)
#include <dlfcn.h>
#define AETHER_VOSK_DLOPEN 1
#endif

Q_DECLARE_LOGGING_CATEGORY(lcMobile)

namespace AetherSDR::mobile::VoskSpeech {

namespace {

// The functions used.

struct Api {
    void* lib = nullptr;
    VoskModel* (*modelNew)(const char*) = nullptr;
    int (*modelFindWord)(VoskModel*, const char*) = nullptr;
    VoskRecognizer* (*recognizerNewGrm)(VoskModel*, float, const char*) = nullptr;
    void (*setMaxAlternatives)(VoskRecognizer*, int) = nullptr;
    int (*acceptWaveform)(VoskRecognizer*, const char*, int) = nullptr;
    const char* (*result)(VoskRecognizer*) = nullptr;
    const char* (*finalResult)(VoskRecognizer*) = nullptr;
    void (*recognizerFree)(VoskRecognizer*) = nullptr;
    void (*setLogLevel)(int) = nullptr;
    QString error;
};

// The English model: in the APK's assets (Android), in the app bundle
// (iOS, the iPad app on a Mac), or OKSASDR_VOSK_MODEL.
constexpr const char* kModelName = "vosk-model-small-en-us-0.15";
constexpr const char* kAssetModel = "assets:/vosk/en-us";

#ifdef Q_OS_IOS
QString bundledModel()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/vosk/en-us");
}
#endif

std::mutex g_mutex;   // the library, the model and their first loads

const Api& api()
{
    static Api a;
    static bool tried = false;
    if (tried) {
        return a;
    }
    tried = true;
#if defined(AETHER_IOS_VOSK)
    a.modelNew = vosk_model_new;
    a.modelFindWord = vosk_model_find_word;
    a.recognizerNewGrm = vosk_recognizer_new_grm;
    a.setMaxAlternatives = vosk_recognizer_set_max_alternatives;
    a.acceptWaveform = vosk_recognizer_accept_waveform;
    a.result = vosk_recognizer_result;
    a.finalResult = vosk_recognizer_final_result;
    a.recognizerFree = vosk_recognizer_free;
    a.setLogLevel = vosk_set_log_level;
    a.setLogLevel(-1);   // warnings and errors only
#elif defined(AETHER_VOSK_DLOPEN)
    const QByteArray name = qEnvironmentVariableIsEmpty("OKSASDR_VOSK_LIB") ? QByteArray("libvosk.so")
                                                                          : qgetenv("OKSASDR_VOSK_LIB");
    a.lib = dlopen(name.constData(), RTLD_NOW | RTLD_LOCAL);
    if (!a.lib) {
        a.error = QStringLiteral("libvosk not loaded: %1").arg(QString::fromLocal8Bit(dlerror()));
        return a;
    }
    auto get = [](auto& fn, const char* symbol) {
        fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(dlsym(a.lib, symbol));
        if (!fn && a.error.isEmpty()) {
            a.error = QStringLiteral("libvosk has no %1").arg(QLatin1String(symbol));
        }
    };
    get(a.modelNew, "vosk_model_new");
    get(a.modelFindWord, "vosk_model_find_word");
    get(a.recognizerNewGrm, "vosk_recognizer_new_grm");
    get(a.setMaxAlternatives, "vosk_recognizer_set_max_alternatives");
    get(a.acceptWaveform, "vosk_recognizer_accept_waveform");
    get(a.result, "vosk_recognizer_result");
    get(a.finalResult, "vosk_recognizer_final_result");
    get(a.recognizerFree, "vosk_recognizer_free");
    get(a.setLogLevel, "vosk_set_log_level");
    if (a.error.isEmpty()) {
        a.setLogLevel(-1);   // warnings and errors only
    }
#else
    a.error = QStringLiteral("no Vosk in this build");
#endif
    return a;
}

#ifdef Q_OS_ANDROID
// The model out of the APK's assets into the app's data, once a version
// (Vosk reads files by path); a marker written last says it is whole.
QString copiedModel(QString* error)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/vosk/") + QLatin1String(kModelName);
    const QString marker = dir + QStringLiteral("/.copied");
    if (QFile::exists(marker)) {
        return dir;
    }
    QDir(dir).removeRecursively();
    // The model's files by the list the build wrote beside it.
    const QString assets = QString::fromLatin1(kAssetModel);
    QFile list(assets + QStringLiteral(".files"));
    if (!list.open(QIODevice::ReadOnly | QIODevice::Text)) {
        *error = QStringLiteral("the speech model is not in this build");
        return {};
    }
    qint64 bytes = 0;
    for (const QByteArray& line : list.readAll().split('\n')) {
        const QString rel = QString::fromUtf8(line.trimmed());
        if (rel.isEmpty()) {
            continue;
        }
        const QString from = assets + QLatin1Char('/') + rel;
        const QString to = dir + QLatin1Char('/') + rel;
        QDir().mkpath(QFileInfo(to).absolutePath());
        if (!QFile::copy(from, to)) {
            *error = QStringLiteral("the speech model could not be copied (%1)").arg(rel);
            return {};
        }
        bytes += QFileInfo(to).size();
    }
    QFile done(marker);
    if (bytes == 0 || !done.open(QIODevice::WriteOnly)) {
        *error = QStringLiteral("the speech model is not in this build");
        return {};
    }
    qCInfo(lcMobile).noquote() << "voice log: Vosk model copied out of the app," << bytes / 1000000 << "MB";
    return dir;
}
#endif

VoskModel* model(QString* error)
{
    static VoskModel* m = nullptr;
    static QString failed;
    if (m || !failed.isEmpty()) {
        *error = failed;
        return m;
    }
#if defined(Q_OS_ANDROID)
    const QString path = copiedModel(&failed);
#elif defined(Q_OS_IOS)
    const QString path = bundledModel();
#else
    const QString path = qEnvironmentVariable("OKSASDR_VOSK_MODEL");
    if (path.isEmpty()) {
        failed = QStringLiteral("no speech model (OKSASDR_VOSK_MODEL)");
    }
#endif
    if (failed.isEmpty()) {
        m = api().modelNew(QFile::encodeName(path).constData());
        if (!m) {
            failed = QStringLiteral("the speech model did not load");
        } else {
            qCInfo(lcMobile).noquote() << "voice log: Vosk model loaded:" << QFileInfo(path).fileName();
        }
    }
    *error = failed;
    return m;
}

// The texts in one Vosk result: its alternatives, or its one text.
QStringList texts(const char* json)
{
    QStringList out;
    // Vosk prints the confidences with the C library's locale: a decimal
    // comma under a Finnish one ("349,857300"), which is not JSON.
    static const QRegularExpression comma(QStringLiteral("(\"confidence\"\\s*:\\s*-?\\d+),(\\d)"));
    const QString fixed = QString::fromUtf8(json).replace(comma, QStringLiteral("\\1.\\2"));
    const QJsonObject o = QJsonDocument::fromJson(fixed.toUtf8()).object();
    for (const QJsonValue& alt : o.value(QStringLiteral("alternatives")).toArray()) {
        out << alt.toObject().value(QStringLiteral("text")).toString();
    }
    if (out.isEmpty()) {
        out << o.value(QStringLiteral("text")).toString();
    }
    for (QString& t : out) {
        t.replace(QStringLiteral("[unk]"), QString());
        t = t.simplified();
    }
    return out;
}

} // namespace

QString unavailableReason()
{
    std::lock_guard lock(g_mutex);
    if (!api().error.isEmpty()) {
        return api().error;
    }
#ifdef Q_OS_ANDROID
    if (!QFile::exists(QString::fromLatin1(kAssetModel) + QStringLiteral(".files"))
        && !QFile::exists(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                          + QStringLiteral("/vosk/") + QLatin1String(kModelName) + QStringLiteral("/.copied"))) {
        return QStringLiteral("the speech model is not in this build");
    }
#elif defined(Q_OS_IOS)
    if (!QFile::exists(bundledModel() + QStringLiteral("/am/final.mdl"))) {
        return QStringLiteral("the speech model is not in this build");
    }
#else
    if (qEnvironmentVariableIsEmpty("OKSASDR_VOSK_MODEL")) {
        return QStringLiteral("no speech model (OKSASDR_VOSK_MODEL)");
    }
#endif
    return {};
}

QStringList recognize(const QByteArray& pcm16k, const QStringList& words, QString* error)
{
    std::lock_guard lock(g_mutex);
    const Api& a = api();
    if (!a.error.isEmpty()) {
        *error = a.error;
        return {};
    }
    VoskModel* m = model(error);
    if (!m) {
        return {};
    }
    // The grammar: the words the model knows (a phrase when each of its
    // words is known), and [unk] for anything else said.
    QJsonArray grammar;
    QStringList unknown;
    for (const QString& w : words) {
        const QStringList parts = w.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        const bool known = std::all_of(parts.cbegin(), parts.cend(), [&](const QString& p) {
            return a.modelFindWord(m, p.toUtf8().constData()) >= 0;
        });
        if (known) {
            grammar.append(w);
        } else {
            unknown << w;
        }
    }
    grammar.append(QStringLiteral("[unk]"));
    static bool reported = false;
    if (!reported && !unknown.isEmpty()) {
        reported = true;
        qCInfo(lcMobile).noquote() << "voice log: not in the speech model:" << unknown.join(QLatin1Char(' ')).left(240);
    }
    VoskRecognizer* rec = a.recognizerNewGrm(m, 16000.0f, QJsonDocument(grammar).toJson(QJsonDocument::Compact).constData());
    if (!rec) {
        *error = QStringLiteral("the recogniser could not start");
        return {};
    }
    a.setMaxAlternatives(rec, 5);
    // A pause ends an utterance (accept returns 1): each one's alternatives
    // kept, then put together, the n-th alternative of the whole taking
    // each part's n-th where it has one.
    QList<QStringList> parts;
    constexpr int kChunk = 3200;   // 100 ms
    for (int at = 0; at < pcm16k.size(); at += kChunk) {
        const int n = std::min<int>(kChunk, int(pcm16k.size()) - at);
        if (a.acceptWaveform(rec, pcm16k.constData() + at, n) == 1) {
            parts << texts(a.result(rec));
        }
    }
    const char* last = a.finalResult(rec);
    if (qEnvironmentVariableIsSet("OKSASDR_VOSK_DEBUG")) {
        qCInfo(lcMobile).noquote() << "voice log: Vosk final:" << QByteArray(last).simplified().left(250)
                                   << "grammar" << QJsonDocument(grammar).toJson(QJsonDocument::Compact).left(250);
    }
    parts << texts(last);
    a.recognizerFree(rec);
    QStringList alternatives;
    for (int k = 0; k < 5; ++k) {
        QStringList joined;
        bool any = false;
        for (const QStringList& p : parts) {
            if (p.isEmpty()) {
                continue;
            }
            any = any || k < p.size();
            joined << p.at(std::min<int>(k, int(p.size()) - 1));
        }
        const QString text = joined.join(QLatin1Char(' ')).simplified();
        if (any && !text.isEmpty() && !alternatives.contains(text)) {
            alternatives << text;
        }
    }
    return alternatives;
}

void recognizeClip(const QByteArray& floatMono, int rate,
                   std::function<void(const QStringList&, const QString&)> done)
{
    QThreadPool::globalInstance()->start([floatMono, rate, done] {
        QString error;
        // 0.4 s of silence each side: the press often starts right on the
        // first word.
        const QByteArray pad(6400 * qsizetype(sizeof(qint16)), '\0');   // 6400 samples at 16 kHz
        const QStringList alternatives =
            recognize(pad + toPcm16k(floatMono, rate) + pad, CallsignSpeech::recognizerWords(), &error);
        QMetaObject::invokeMethod(qApp, [done, alternatives, error] {
            done(alternatives, error.isEmpty() && alternatives.isEmpty() ? QStringLiteral("Nothing was heard.") : error);
        }, Qt::QueuedConnection);
    });
}

QByteArray toPcm16k(const QByteArray& floatMono, int rate)
{
    const auto* in = reinterpret_cast<const float*>(floatMono.constData());
    const qsizetype n = floatMono.size() / qsizetype(sizeof(float));
    if (n == 0 || rate <= 0) {
        return {};
    }
    const double step = double(rate) / 16000.0;
    const qsizetype out = qsizetype(double(n) / step);
    QByteArray pcm(out * qsizetype(sizeof(qint16)), Qt::Uninitialized);
    auto* dst = reinterpret_cast<qint16*>(pcm.data());
    for (qsizetype i = 0; i < out; ++i) {
        double v;
        if (step >= 1.0) {
            // The mean over the input samples this output sample covers.
            const qsizetype a = qsizetype(double(i) * step);
            const qsizetype b = std::min(n, std::max(a + 1, qsizetype(double(i + 1) * step)));
            double sum = 0.0;
            for (qsizetype k = a; k < b; ++k) {
                sum += in[k];
            }
            v = sum / double(b - a);
        } else {
            const double x = double(i) * step;
            const qsizetype a = std::min(n - 1, qsizetype(x));
            const qsizetype b = std::min(n - 1, a + 1);
            v = in[a] + (in[b] - in[a]) * (x - double(a));
        }
        dst[i] = qint16(std::clamp(std::isfinite(v) ? v * 32767.0 : 0.0, -32768.0, 32767.0));
    }
    return pcm;
}

} // namespace AetherSDR::mobile::VoskSpeech
