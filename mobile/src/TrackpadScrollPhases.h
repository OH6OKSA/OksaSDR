#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QPointer>
#include <QTimer>

class QQuickWindow;
class QWheelEvent;

namespace AetherSDR::mobile {

// Gives trackpad scrolling on iOS the phases Qt's Flickable needs.
//
// Qt's iOS plugin turns a trackpad scroll (an iPad's Magic Keyboard, or an
// iPad app on a Mac) into wheel events with a pixel delta but no scroll
// phase and no "synthesized" source (quiview.mm handleScroll). Flickable
// follows a pixel delta only with a phase and such a source, so it took each
// event as a mouse-wheel notch and started a short flick: a ScrollView moved
// about 44 % as far as the fingers and stopped when they did ("the panel
// stops halfway"). Installed on the window, this filter re-sends them as a
// Mac trackpad does: ScrollBegin, ScrollUpdate with the deltas, then after a
// pause ScrollMomentum events that coast to a stop, and ScrollEnd. Momentum
// carries pixel deltas only, so a handler counting wheel notches (the
// panadapter tunes one step per notch) does not keep going after the fingers
// lift. Events that already have a phase pass untouched.
class TrackpadScrollPhases : public QObject {
public:
    explicit TrackpadScrollPhases(QQuickWindow* window);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void send(Qt::ScrollPhase phase, QPoint pixelDelta, QPoint angleDelta);
    void endGesture();
    void momentumStep();

    QPointer<QQuickWindow> m_window;
    bool m_resending = false;
    bool m_inGesture = false;
    QPointF m_position;        // where the gesture began, as iOS reports it
    QPointF m_globalPosition;
    Qt::KeyboardModifiers m_modifiers;
    QTimer m_idle;             // no event for a while: the fingers lifted
    QTimer m_momentum;
    QElapsedTimer m_clock;
    qint64 m_lastEventMs = 0;
    QPointF m_velocity;        // px/s, smoothed over the gesture's last events
};

} // namespace AetherSDR::mobile
