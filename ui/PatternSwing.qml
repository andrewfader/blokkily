import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// SWING: how far even sixteenths of the open pattern are delayed when they
// play. 0 is straight; 100 delays them by half a sixteenth. The arrows (or
// the wheel) move it by one percent; Shift moves it by twenty-five. The
// editors keep the straight grid.
Rectangle {
    id: root
    objectName: "patternSwing"
    implicitWidth: row.implicitWidth + 8; implicitHeight: 22
    radius: 3; color: "transparent"; border.color: Theme.line
    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: 2
        Label { text: "SWING"; color: Theme.muted; font.pixelSize: 8; font.letterSpacing: 0.6
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
                    songModel.setPatternSwing(Math.max(0, songModel.patternSwing
                        - ((mouse.modifiers & Qt.ShiftModifier) ? 25 : 1)))
                } }
        }
        Label {
            objectName: "patternSwingReadout"
            Layout.preferredWidth: Math.max(38, implicitWidth)
            horizontalAlignment: Text.AlignHCenter
            text: songModel.patternSwing + "%"
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
                    songModel.setPatternSwing(Math.min(100, songModel.patternSwing
                        + ((mouse.modifiers & Qt.ShiftModifier) ? 25 : 1)))
                } }
        }
    }
    WheelHandler {
        onWheel: function(event) {
            var notches = event.angleDelta.y / 120
            var per = (event.modifiers & Qt.ShiftModifier) ? 25 : 1
            if (notches !== 0)
                songModel.setPatternSwing(Math.max(0, Math.min(100,
                    songModel.patternSwing + Math.round(notches) * per)))
        }
    }
}
