import QtQuick
import QtQuick.Controls

// One value out of a named list. The lists behind the tuning, the scale and
// the keyboard layouts are long enough that cycling through them would be a
// chore, so the current value opens the rest.
Rectangle {
    id: pickerRoot
    property var choices: []
    property string value: ""
    signal picked(string name)
    implicitWidth: pickerText.implicitWidth + 30; implicitHeight: 26
    radius: 4
    color: pickerMouse.containsMouse ? Theme.raised : "transparent"
    border.color: Theme.line
    Label {
        id: pickerText
        anchors.left: parent.left; anchors.leftMargin: 9
        anchors.verticalCenter: parent.verticalCenter
        text: pickerRoot.value; color: Theme.ink; font.pixelSize: 11; font.bold: true
    }
    Label {
        anchors.right: parent.right; anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        text: "\u25be"; color: Theme.muted; font.pixelSize: 10
    }
    MouseArea {
        id: pickerMouse; anchors.fill: parent; hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: pickerMenu.popup()
    }
    Menu {
        id: pickerMenu
        // Named after its picker, so a gate can prove the list is really
        // populated rather than an empty dropdown.
        objectName: pickerRoot.objectName + "Menu"
        Instantiator {
            model: pickerRoot.choices
            delegate: MenuItem {
                required property var modelData
                text: modelData
                onTriggered: pickerRoot.picked(modelData)
            }
            onObjectAdded: (index, object) => pickerMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => pickerMenu.removeItem(object)
        }
    }
}
