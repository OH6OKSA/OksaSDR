// Digital max (roadmap 37, Sloan Davis, build 156 on a FLEX Aurora AU-520:
// his power, set for SSB at 65-70 %, gave 140 W on FT8). The highest RF
// power while the TX slice is in a digital mode: DIGU, DIGL or RTTY, so FT8 /
// FT4, PSK, RTTY and RADE (which runs in DIGU / DIGL). Off (0) by default.
//
// In the RF power control's own unit (W on a 100 W radio, % on an Aurora).
// When the TX slice goes to a digital mode with the power above the limit,
// the power comes down to it, and goes back to what it was when the slice
// leaves the digital modes (unless the power was changed meanwhile: then
// that choice stays). A power raised above the limit in a digital mode, here
// or from another client, comes back down: it is a limit. The limit changed
// while it holds the power follows it (up to the power before), so setting
// it with + in a digital mode does not leave the power at its first step. The radio's own
// TX profiles still keep a power per band; this is the app's, so another
// client's transmissions do not see it.

#include "MobileEngine.h"

#include "core/AppSettings.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(lcMobile)

namespace AetherSDR::mobile {

namespace {
const QString kDigitalMaxKey = QStringLiteral("MobileDigitalMax");
}

int MobileEngine::digitalMax() const
{
    return std::clamp(AppSettings::instance().value(kDigitalMaxKey, 0).toInt(), 0, 100);
}

void MobileEngine::setDigitalMax(int power)
{
    power = std::clamp(power, 0, 100);
    if (power == digitalMax()) {
        return;
    }
    AppSettings::instance().setValue(kDigitalMaxKey, power);
    AppSettings::instance().save();
    qCInfo(lcMobile) << "Digital max:" << (power > 0 ? QString::number(power) : QStringLiteral("off"));
    if (power == 0) {
        // Off: a power the limit lowered stays where it is now.
        m_digitalMaxRestore = -1;
        m_digitalMaxSetPower = -1;
        m_digitalMaxNote.clear();
    } else if (m_connected && m_digitalMaxRestore >= 0 && txSliceDigital() && rfPower() == m_digitalMaxSetPower) {
        // The limit holds the power: it follows the limit, never above the
        // power it had before.
        const int target = std::min(power, m_digitalMaxRestore);
        if (target != rfPower()) {
            const QString unit = rfPowerIsPercent() ? QStringLiteral("%") : QStringLiteral("W");
            m_digitalMaxNote = tr("RF power %1 → %2 %3 for the digital mode.").arg(m_digitalMaxRestore).arg(target).arg(unit);
            m_digitalMaxSetPower = target;
            setRfPower(target);
        }
    }
    emit digitalMaxChanged();
    checkDigitalMax();
}

bool MobileEngine::txSliceDigital() const
{
    for (SliceModel* s : m_session.radioModel().slices()) {
        if (s && s->isTxSlice()) {
            const QString mode = s->mode();
            return mode == QLatin1String("DIGU") || mode == QLatin1String("DIGL") || mode == QLatin1String("RTTY");
        }
    }
    return false;
}

void MobileEngine::checkDigitalMax()
{
    if (!m_connected || !m_caps.canTransmit) {
        m_digitalMaxRestore = -1;
        m_digitalMaxSetPower = -1;
        return;
    }
    const int max = digitalMax();
    if (max <= 0) {
        return;
    }
    const QString unit = rfPowerIsPercent() ? QStringLiteral("%") : QStringLiteral("W");
    const int now = rfPower();
    if (txSliceDigital()) {
        if (now > max) {
            if (m_digitalMaxRestore < 0) {
                m_digitalMaxRestore = now;
            }
            m_digitalMaxNote = tr("RF power %1 → %2 %3 for the digital mode.").arg(m_digitalMaxRestore).arg(max).arg(unit);
            qCInfo(lcMobile).noquote() << "Digital max: RF power" << now << "->" << max << unit;
            m_digitalMaxSetPower = max;
            setRfPower(max);
            emit digitalMaxChanged();
        }
        return;
    }
    if (m_digitalMaxRestore >= 0) {
        const int before = std::exchange(m_digitalMaxRestore, -1);
        const int set = std::exchange(m_digitalMaxSetPower, -1);
        // Only the power the limit set: one changed meanwhile is the operator's.
        if (now == set) {
            m_digitalMaxNote = tr("RF power back to %1 %2.").arg(before).arg(unit);
            qCInfo(lcMobile).noquote() << "Digital max: out of the digital modes, RF power back to" << before << unit;
            setRfPower(before);
        } else {
            m_digitalMaxNote.clear();
        }
        emit digitalMaxChanged();
    }
}

} // namespace AetherSDR::mobile
