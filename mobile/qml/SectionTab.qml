import QtQuick
import QtQuick.Controls

// One button of a row of tabs (Radio Setup's Radio, Transmit, …; the
// Controls' DSP, Audio, TX, …): it looks as any AppButton, lit when its tab
// shows, and tells a screen reader it is a tab, which one of how many, and
// whether it is the one shown. Without that it was "Transmit, button", and
// nothing said that the settings under it had changed.
// The row around it is a PageTabList, named only off iOS (an iOS element
// with a name would be one more stop for VoiceOver, with nothing to do).
AppButton {
    id: tab
    // This tab's place in the row, from 0, and how many there are.
    property int tabIndex: 0
    property int tabCount: 1

    // A tab where the platform has one: TalkBack says "Tab", VoiceOver on a
    // Mac (the desktop build) a tab button, AT-SPI a page tab. Qt's iOS
    // bridge (the iPhone, the iPad and the iPad app on a Mac) has no tab
    // trait: a page tab there would lose even "button", so it stays a
    // button, with the Selected trait (AppButton) and the place in the hint.
    readonly property bool iosBridge: Qt.platform.os === "ios"
    Accessible.role: iosBridge ? Accessible.Button : Accessible.PageTab
    // A page tab has no press action of its own in Qt (only a button,
    // a link and a menu item do), so without this a screen reader's double
    // tap or VO-Space would do nothing on Android and the desktop.
    Accessible.onPressAction: tab.clicked()
    // iOS reads the description as the hint, after "selected, button";
    // Android adds it to the name, before its own "Tab"; neither of them,
    // nor VoiceOver on the desktop, says a tab is the selected one by itself.
    Accessible.description: iosBridge
        ? qsTr("Tab %1 of %2").arg(tabIndex + 1).arg(tabCount)
        : checked ? qsTr("selected, %1 of %2").arg(tabIndex + 1).arg(tabCount)
                  : qsTr("%1 of %2").arg(tabIndex + 1).arg(tabCount)
}
