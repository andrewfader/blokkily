import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// SWING: how far even sixteenths of the open pattern are delayed when they
// play. 0 is straight; 100 delays them by half a sixteenth. The arrows (or
// the wheel) move it by one percent; Shift moves it by twenty-five. It is
// every track's, unless Ctrl is held: then only the selected track's part
// swings, and the readout is marked while the tracks differ. The editors
// keep the straight grid.
Rectangle {
    id: root
    objectName: "patternSwing"
    implicitWidth: row.implicitWidth + 8; implicitHeight: 22
    radius: 3; color: "transparent"; border.color: Theme.line
    // Ctrl sets the selected track's part alone; without it, every track's.
    function set(percent, modifiers) {
        const amount = Math.max(0, Math.min(100, percent))
        if (modifiers & Qt.ControlModifier) songModel.setPartSwing(amount)
        else songModel.setPatternSwing(amount)
    }
    HoverHandler { id: swingHover }
    ToolTip.visible: swingHover.hovered; ToolTip.delay: 700
    ToolTip.text: qsTr("Swing for every track of the pattern. Ctrl: this track only. Shift: steps of 25%")
    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: 2
        Label { text: qsTr("SWING"); color: Theme.muted; font.pixelSize: 8; font.letterSpacing: 0.6
            Layout.rightMargin: 2 }
        Rectangle {
            objectName: "patternSwingDown"
            implicitWidth: 18; implicitHeight: 18; radius: 2
            color: downMouse.containsMouse ? Theme.raised : "transparent"
            opacity: songModel.patternSwing > 0 ? 1 : 0.4
            Label { anchors.centerIn: parent; text: "−"; color: Theme.ink
                font.pixelSize: 11; font.bold: true }
            MouseArea { id: downMouse; anchors.fill: parent; hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: function(mouse) {
                    root.set(songModel.patternSwing - ((mouse.modifiers & Qt.ShiftModifier) ? 25 : 1),
                             mouse.modifiers)
                } }
        }
        Label {
            objectName: "patternSwingReadout"
            Layout.preferredWidth: Math.max(38, implicitWidth)
            horizontalAlignment: Text.AlignHCenter
            // Marked while the tracks swing by different amounts.
            text: songModel.patternSwing + "%" + (songModel.swingVaries ? "*" : "")
            color: songModel.patternSwing === 0 ? Theme.muted : Theme.acid
            font.pixelSize: 9; font.bold: true; font.family: Theme.mono
        }
        Rectangle {
            objectName: "patternSwingUp"
            implicitWidth: 18; implicitHeight: 18; radius: 2
            color: upMouse.containsMouse ? Theme.raised : "transparent"
            Label { anchors.centerIn: parent; text: "+"; color: Theme.ink
                font.pixelSize: 11; font.bold: true }
            MouseArea { id: upMouse; anchors.fill: parent; hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: function(mouse) {
                    root.set(songModel.patternSwing + ((mouse.modifiers & Qt.ShiftModifier) ? 25 : 1),
                             mouse.modifiers)
                } }
        }
    }
    WheelHandler {
        onWheel: function(event) {
            var notches = event.angleDelta.y / 120
            var per = (event.modifiers & Qt.ShiftModifier) ? 25 : 1
            if (notches !== 0)
                root.set(songModel.patternSwing + Math.round(notches) * per, event.modifiers)
        }
    }
}
