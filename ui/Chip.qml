import QtQuick
import QtQuick.Controls

// A toggle or a push button: lit in its accent colour while it is on.
Rectangle {
    property alias text: chipText.text
    property bool on: false
    property color accent: Theme.acid
    property string accessibleLabel: ""
    // The room left either side of the label. A chip sized to its text with
    // less padding than the usual says so, or its label is elided.
    property real labelPadding: 12
    // The width the label wants, for a chip that sets its own width but
    // must still fit a longer label (a translation).
    readonly property real labelWidth: chipText.implicitWidth
    signal clicked()
    signal rightClicked()
    signal doubleClicked()
    implicitWidth: Math.max(28, chipText.implicitWidth + 22)
    implicitHeight: 26
    opacity: enabled ? 1.0 : 0.4
    radius: 4
    color: on ? accent : (chipMouse.containsMouse ? Theme.raised : "transparent")
    border.color: on ? accent : Theme.line
    Label {
        id: chipText; anchors.centerIn: parent
        // The chip sets its own size; the label has the same bounds so an
        // overflowing name (a producer has not renamed) trims to "WRPED...".
        width: parent.width - labelPadding
        color: parent.on ? Theme.onAccent : Theme.ink
        font.pixelSize: 11; font.bold: true; font.letterSpacing: 1
        elide: Text.ElideRight
        Accessible.name: parent.accessibleLabel !== "" ? parent.accessibleLabel : text
    }
    MouseArea {
        id: chipMouse; anchors.fill: parent; hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: function(mouse) {
            if (mouse.button === Qt.RightButton) parent.rightClicked()
            else parent.clicked()
        }
        onDoubleClicked: function(mouse) {
            if (mouse.button === Qt.LeftButton) parent.doubleClicked()
        }
    }
}
