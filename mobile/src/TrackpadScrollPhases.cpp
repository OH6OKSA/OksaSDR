#include "TrackpadScrollPhases.h"

#include <QCoreApplication>
#include <QQuickWindow>
#include <QWheelEvent>

#include <cmath>

namespace AetherSDR::mobile {

namespace {
constexpr int kIdleMs = 60;           // a gap this long ends the gesture
constexpr int kMomentumTickMs = 16;
constexpr double kMomentumDecay = 0.95;   // per tick
constexpr double kMomentumMinSpeed = 40.0;   // px/s: below this it stops
constexpr double kMomentumStartSpeed = 150.0;
} // namespace

TrackpadScrollPhases::TrackpadScrollPhases(QQuickWindow* window)
    : QObject(window)
    , m_window(window)
{
    m_idle.setSingleShot(true);
    m_idle.setInterval(kIdleMs);
    connect(&m_idle, &QTimer::timeout, this, &TrackpadScrollPhases::endGesture);
    m_momentum.setInterval(kMomentumTickMs);
    connect(&m_momentum, &QTimer::timeout, this, &TrackpadScrollPhases::momentumStep);
    m_clock.start();
    window->installEventFilter(this);
}

bool TrackpadScrollPhases::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() != QEvent::Wheel || m_resending || watched != m_window) {
        return false;
    }
    auto* wheel = static_cast<QWheelEvent*>(event);
    if (wheel->phase() != Qt::NoScrollPhase || wheel->pixelDelta().isNull()
        || wheel->source() != Qt::MouseEventNotSynthesized) {
        return false;   // a mouse wheel, or already phased: Qt handles it
    }
    const qint64 now = m_clock.elapsed();
    if (m_momentum.isActive()) {
        // New fingers on the pad stop the coast.
        m_momentum.stop();
        send(Qt::ScrollEnd, {}, {});
    }
    if (!m_inGesture) {
        m_inGesture = true;
        m_position = wheel->position();
        m_globalPosition = wheel->globalPosition();
        m_velocity = {};
        m_lastEventMs = now;
        m_modifiers = wheel->modifiers();
        send(Qt::ScrollBegin, {}, {});
    }
    const double dt = std::max<qint64>(1, now - m_lastEventMs) / 1000.0;
    const QPointF instant = QPointF(wheel->pixelDelta()) / dt;
    m_velocity = m_velocity.isNull() ? instant : m_velocity * 0.6 + instant * 0.4;
    m_lastEventMs = now;
    m_modifiers = wheel->modifiers();
    send(Qt::ScrollUpdate, wheel->pixelDelta(), wheel->angleDelta());
    m_idle.start();
    return true;
}

void TrackpadScrollPhases::send(Qt::ScrollPhase phase, QPoint pixelDelta, QPoint angleDelta)
{
    if (!m_window) {
        return;
    }
    // Synthesized by the system, as macOS marks trackpad scrolling: with a
    // phase, Flickable follows the pixel delta instead of flicking.
    QWheelEvent resent(m_position, m_globalPosition, pixelDelta, angleDelta, Qt::NoButton, m_modifiers, phase,
                       false, Qt::MouseEventSynthesizedBySystem);
    resent.setTimestamp(static_cast<ulong>(m_clock.elapsed()));
    m_resending = true;
    QCoreApplication::sendEvent(m_window, &resent);
    m_resending = false;
}

void TrackpadScrollPhases::endGesture()
{
    if (!m_inGesture) {
        return;
    }
    m_inGesture = false;
    // A gesture that was still moving at the end coasts on, slowing down;
    // the gesture's ScrollEnd comes after the coast, as on a Mac (Flickable
    // moves momentum by the delta accumulated since ScrollBegin).
    const double speed = std::hypot(m_velocity.x(), m_velocity.y());
    if (speed >= kMomentumStartSpeed && m_clock.elapsed() - m_lastEventMs <= kIdleMs * 2) {
        m_momentum.start();
    } else {
        send(Qt::ScrollEnd, {}, {});
    }
}

void TrackpadScrollPhases::momentumStep()
{
    m_velocity *= kMomentumDecay;
    const QPointF step = m_velocity * (kMomentumTickMs / 1000.0);
    const QPoint pixels(static_cast<int>(std::lround(step.x())), static_cast<int>(std::lround(step.y())));
    if (std::hypot(m_velocity.x(), m_velocity.y()) < kMomentumMinSpeed) {
        m_momentum.stop();
        send(Qt::ScrollEnd, {}, {});
        return;
    }
    if (!pixels.isNull()) {
        send(Qt::ScrollMomentum, pixels, {});
    }
}

} // namespace AetherSDR::mobile
