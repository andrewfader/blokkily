import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// LEN: how many sixteenth steps the open pattern has. The arrows (or the
// wheel) make it a step shorter or longer; a pattern made in a 7/8 bar starts
// at fourteen. There is no longest: the editors scroll once the steps stop
// fitting. Shortening drops the steps past the new end, and undo brings them
// back. With Shift held, a click or a wheel notch moves it sixteen steps.
Rectangle {
    id: root
    objectName: "patternLength"
    implicitWidth: row.implicitWidth + 8; implicitHeight: 22
    radius: 3; color: "transparent"; border.color: Theme.line
    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: 2
        Label { text: "LEN"; color: Theme.muted; font.pixelSize: 8; font.letterSpacing: 0.6
            Layout.rightMargin: 2 }
        Rectangle {
            objectName: "patternLengthDown"
            implicitWidth: 18; implicitHeight: 18; radius: 2
            color: downMouse.containsMouse ? Theme.raised : "transparent"
            opacity: patternModel.stepCount > 1 ? 1 : 0.4
            Label { anchors.centerIn: parent; text: "−"; color: Theme.ink
                font.pixelSize: 11; font.bold: true }
            MouseArea { id: downMouse; anchors.fill: parent; hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: function(mouse) {
                    songModel.setPatternSteps(Math.max(1, patternModel.stepCount
                        - ((mouse.modifiers & Qt.ShiftModifier) ? 16 : 1)))
                } }
        }
        Label {
            objectName: "patternLengthReadout"
            Layout.preferredWidth: Math.max(38, implicitWidth)
            horizontalAlignment: Text.AlignHCenter
            text: patternModel.stepCount + " ST"
            color: patternModel.stepCount === 16 ? Theme.muted : Theme.amber
            font.pixelSize: 9; font.bold: true; font.family: Theme.mono
        }
        Rectangle {
            objectName: "patternLengthUp"
            implicitWidth: 18; implicitHeight: 18; radius: 2
            color: upMouse.containsMouse ? Theme.raised : "transparent"
            Label { anchors.centerIn: parent; text: "+"; color: Theme.ink
                font.pixelSize: 11; font.bold: true }
            MouseArea { id: upMouse; anchors.fill: parent; hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: function(mouse) {
                    songModel.setPatternSteps(patternModel.stepCount
                        + ((mouse.modifiers & Qt.ShiftModifier) ? 16 : 1))
                } }
        }
    }
    WheelHandler {
        onWheel: function(event) {
            var notches = event.angleDelta.y / 120
            var per = (event.modifiers & Qt.ShiftModifier) ? 16 : 1
            if (notches !== 0)
                songModel.setPatternSteps(Math.max(1,
                    patternModel.stepCount + Math.round(notches) * per))
        }
    }
}
