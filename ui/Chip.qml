import QtQuick
import QtQuick.Controls

// A toggle or a push button: lit in its accent colour while it is on.
Rectangle {
    property alias text: chipText.text
    property bool on: false
    property color accent: Theme.acid
    signal clicked()
    signal rightClicked()
    signal doubleClicked()
    implicitWidth: chipText.implicitWidth + 22; implicitHeight: 26
    opacity: enabled ? 1.0 : 0.4
    radius: 4
    color: on ? accent : (chipMouse.containsMouse ? Theme.raised : "transparent")
    border.color: on ? accent : Theme.line
    Label {
        id: chipText; anchors.centerIn: parent
        color: parent.on ? "#0e0f12" : Theme.ink
        font.pixelSize: 11; font.bold: true; font.letterSpacing: 1
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
