import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A labelled horizontal slider with a numeric readout — the inspector's
// workhorse for velocity, probability, micro-timing and lock value. Named
// ValueDial because QtQuick.Controls already exports a Dial, and its Dial
// would win over a local file of the same name.
ColumnLayout {
    property string label: ""
    property string readout: ""
    property real value: 0
    property real from: 0
    property real to: 1
    property color accent: Theme.acid
    property bool enabled: true
    // A height for the slider itself. Left at 0 the slider keeps its implicit
    // height; a dial that must take a press along its travel gives it one.
    property real grip: 0
    signal moved(real value)
    // The slider was taken hold of (true) or let go (false), after the
    // gesture that makes a drag one step of history was opened or closed.
    signal touched(bool held)
    spacing: 3
    RowLayout {
        Layout.fillWidth: true; spacing: 6
        Label { text: label; color: Theme.muted; font.pixelSize: 10; font.letterSpacing: 1 }
        Item { Layout.fillWidth: true }
        Label { text: readout; color: enabled ? Theme.ink : Theme.muted
            font.pixelSize: 11; font.family: Theme.mono; font.bold: true }
    }
    Slider {
        Layout.fillWidth: true
        Layout.preferredHeight: parent.grip > 0 ? parent.grip : -1
        Layout.minimumHeight: parent.grip
        enabled: parent.enabled
        // A slider keeps no keyboard focus, so the arrow keys and Space
        // stay with the editors after a fader has been dragged.
        focusPolicy: Qt.NoFocus
        from: parent.from; to: parent.to; value: parent.value
        onMoved: parent.moved(value)
        // One drag is one step of history, however far it travels.
        onPressedChanged: {
            if (pressed) songModel.beginGesture()
            else songModel.endGesture()
            parent.touched(pressed)
        }
        background: Rectangle {
            x: parent.leftPadding; y: parent.topPadding + parent.availableHeight / 2 - height / 2
            width: parent.availableWidth; height: 4; radius: 2
            color: Theme.line
            Rectangle {
                width: parent.width * parent.parent.visualPosition; height: parent.height
                radius: 2; color: parent.parent.enabled ? accent : Theme.muted
            }
        }
        handle: Rectangle {
            x: parent.leftPadding + parent.visualPosition * (parent.availableWidth - width)
            y: parent.topPadding + parent.availableHeight / 2 - height / 2
            width: 12; height: 12; radius: 6
            color: parent.enabled ? accent : Theme.muted
            border.color: "#0e0f12"; border.width: 2
        }
    }
}
