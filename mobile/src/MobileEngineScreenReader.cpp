// Where a screen reader goes when a button opens something: into it. A tab
// of Radio Setup or Controls takes VoiceOver or TalkBack to that tab's
// settings, a chooser (the antenna, the step, the filter) to its current
// choice, Radio Setup, the Log or any other dialog to its content. Before,
// the cursor stayed on the button pressed, and on iOS a dialog that showed
// sent VoiceOver to the start of the screen (Qt's ScreenChanged), the page
// under the dialog first: the settings had to be swiped to, past every
// other button (Justin Mann, with VoiceOver on a Mac).
//
// Only the screen reader's cursor moves. The keyboard focus stays where the
// page keeps it (OperateKeys: Space PTT, the arrows tune), so nothing typed
// on a keyboard changes. The move is Qt's own focus event for the element,
// sent without giving it the keyboard: on iOS (the iPhone, the iPad and the
// iPad app on a Mac) Qt posts it as UIAccessibilityLayoutChangedNotification
// with the element, which is VoiceOver's "go here"; on Android as the view
// focused. The desktop build on a Mac is the exception (below): VoiceOver
// there follows only the keyboard focus.

#include "MobileEngine.h"

#include <QAccessible>
#include <QLoggingCategory>
#include <QQuickWindow>
#include <QTimer>

#include <functional>

Q_DECLARE_LOGGING_CATEGORY(lcMobile)

namespace AetherSDR::mobile {

namespace {

// Long enough for a tab's settings or a popup's content to be laid out and
// for iOS to have re-read the screen after the popup showed (its own
// ScreenChanged comes first, then this): 300 ms.
constexpr int kMoveDelayMs = 300;

// Containers whose name is a title for what is inside, not a place to stop.
bool isContainer(QAccessible::Role role)
{
    return role == QAccessible::Dialog || role == QAccessible::PopupMenu || role == QAccessible::PageTabList
        || role == QAccessible::Window || role == QAccessible::Pane;
}

// The element to go to inside ROOT (ROOT itself not counted): the first in
// the items' order with a name that shows, or with preferSelected the
// first selected one (a chooser's current choice) when there is one.
QQuickItem* screenReaderTarget(QQuickItem* root, bool preferSelected)
{
    QQuickItem* firstNamed = nullptr;
    QQuickItem* firstSelected = nullptr;
    const std::function<bool(QQuickItem*)> walk = [&](QQuickItem* item) {
        if (!item->isVisible() || item->opacity() <= 0.0) {
            return false;
        }
        if (item != root) {
            // Null for an item that is not accessible (no Accessible
            // property set by it or its type).
            if (QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(item)) {
                if (iface->isValid() && !iface->state().invisible && !isContainer(iface->role())
                    && !iface->text(QAccessible::Name).trimmed().isEmpty()) {
                    if (!firstNamed) {
                        firstNamed = item;
                    }
                    if (preferSelected && !firstSelected && iface->state().selected) {
                        firstSelected = item;
                    }
                }
            }
        }
        if (firstNamed && (!preferSelected || firstSelected)) {
            return true;
        }
        for (QQuickItem* child : item->childItems()) {
            if (walk(child)) {
                return true;
            }
        }
        return false;
    };
    walk(root);
    return firstSelected ? firstSelected : firstNamed;
}

bool isWithin(const QQuickItem* item, const QQuickItem* root)
{
    for (const QQuickItem* it = item; it; it = it->parentItem()) {
        if (it == root) {
            return true;
        }
    }
    return false;
}

} // namespace

void MobileEngine::screenReaderFocus(QQuickItem* item, bool preferSelected)
{
    if (!item) {
        return;
    }
    QTimer::singleShot(kMoveDelayMs, this, [target = QPointer<QQuickItem>(item), preferSelected] {
        if (!target || !target->isVisible() || !QAccessible::isActive()) {
            return;
        }
        // A text field the popup gave the keyboard to (frequency entry, a
        // name): the screen reader is there already, with the keyboard.
        if (QQuickWindow* window = target->window()) {
            QQuickItem* focused = window->activeFocusItem();
            if (focused && focused != target && isWithin(focused, target)
                && focused->property("cursorPosition").isValid()) {
                return;
            }
        }
        QQuickItem* to = screenReaderTarget(target, preferSelected);
        if (!to) {
            return;
        }
        QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(to);
        qCInfo(lcMobile).noquote() << "screen reader: to" << (iface ? iface->text(QAccessible::Name) : QString());
#if defined(Q_OS_MACOS)
        // The desktop build on a Mac (development, the render tool):
        // VoiceOver passed over Qt's focus event for an element without the
        // keyboard, and over AppKit's layout change naming it (tried with
        // VoiceOver: the cursor stayed on Setup); it follows the keyboard
        // focus. So the element takes the keyboard there, with a screen
        // reader on only, which Qt reports as the focus event; the page
        // takes it back as from any control (OperateKeys) once no popup is
        // open. While it has it, Space is the control's, not PTT.
        to->forceActiveFocus(Qt::OtherFocusReason);
        return;
#endif
        QAccessibleEvent event(to, QAccessible::Focus);
        QAccessible::updateAccessibility(&event);
    });
}

void MobileEngine::followPopups(QQuickItem* overlay)
{
    if (!overlay || overlay->property("screenReaderFollowed").toBool()) {
        return;
    }
    overlay->setProperty("screenReaderFollowed", true);
    // Each popup's item (its QObject parent is the Popup) once: its showing
    // takes the screen reader in.
    const auto watchChildren = [this, overlay] {
        for (QQuickItem* child : overlay->childItems()) {
            if (!child->inherits("QQuickPopupItem") || child->property("screenReaderWatched").toBool()) {
                continue;
            }
            child->setProperty("screenReaderWatched", true);
            connect(child, &QQuickItem::visibleChanged, this, [this, item = QPointer<QQuickItem>(child)] {
                QObject* popup = item ? item->parent() : nullptr;
                if (!popup) {
                    return;
                }
                // The order they opened in, for Esc (closeTopPopup).
                m_shownPopups.removeAll(popup);
                if (!item->isVisible()) {
                    return;
                }
                m_shownPopups.append(popup);
                const QVariant follows = popup->property("screenReaderFollows");
                if (!(follows.isValid() ? follows.toBool() : popup->property("modal").toBool())) {
                    return;
                }
                if (auto* start = qvariant_cast<QQuickItem*>(popup->property("screenReaderStart"))) {
                    screenReaderFocus(start, false);
                } else {
                    screenReaderFocus(item, true);
                }
            });
        }
    };
    watchChildren();
    connect(overlay, &QQuickItem::childrenChanged, this, watchChildren);
}

// Esc closes what was opened last. The page keeps the keyboard while a
// popup is open (their buttons never take it), and a Popup closes on Esc
// by itself only when it has the keyboard: Esc reached the page, stopped
// the keyers and left Radio Setup, a chooser or the Controls drawer open.
// One whose closePolicy leaves out CloseOnEscape (the side PTT copy, a spot
// alert, a notice, the macros panel) is passed over, as Qt would.
bool MobileEngine::closeTopPopup()
{
    constexpr int kCloseOnEscape = 0x10; // QQuickPopup::CloseOnEscape
    for (int i = m_shownPopups.size() - 1; i >= 0; --i) {
        QObject* popup = m_shownPopups.at(i);
        if (!popup) {
            m_shownPopups.removeAt(i);
            continue;
        }
        if (!popup->property("visible").toBool()
            || !(popup->property("closePolicy").toInt() & kCloseOnEscape)) {
            continue;
        }
        qCInfo(lcMobile).noquote() << "Esc closes" << popup->metaObject()->className() << popup->objectName();
        QMetaObject::invokeMethod(popup, "close");
        return true;
    }
    return false;
}

} // namespace AetherSDR::mobile
