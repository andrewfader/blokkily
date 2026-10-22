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
    // Where a double-click puts it back (0 dB, centre), as one step of
    // history; left undefined, a double-click does nothing.
    property var defaultValue: undefined
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
        // Named after its dial, so a gate can press the slider itself.
        objectName: parent.objectName.length > 0 ? parent.objectName + "Slider" : ""
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
        // A second press soon after the first is a double-click: the slider
        // takes every press itself, so it is told by the time between them.
        // Its release puts the default back, in the same step of history.
        property double pressedAt: 0
        property bool doubled: false
        onPressedChanged: {
            if (pressed) {
                const now = Date.now()
                doubled = parent.defaultValue !== undefined
                          && now - pressedAt < Qt.styleHints.mouseDoubleClickInterval
                pressedAt = doubled ? 0 : now
                songModel.beginGesture()
            } else {
                if (doubled) parent.moved(parent.defaultValue)
                doubled = false
                songModel.endGesture()
            }
            parent.touched(pressed)
        }
        HoverHandler { id: sliderHover }
        ToolTip.visible: sliderHover.hovered && parent.defaultValue !== undefined
        ToolTip.delay: 900
        ToolTip.text: qsTr("Double-click to reset")
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
            border.color: Theme.onAccent; border.width: 2
        }
    }
}
