import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The metronome in the transport (item 3.7): the click on or off and the bars
// of count-in a recording starts with, over the click's level. The settings
// are the song's; the click follows its tempo and meter maps. The count-in
// chip lights while a count-in is playing. Stacked in two rows, so the
// transport keeps the room it had.
ColumnLayout {
    id: root
    spacing: 2

    RowLayout {
        spacing: 4
        Chip {
            objectName: "clickButton"
            text: "CLICK"
            implicitWidth: 44; implicitHeight: 22
            on: songModel.metronomeOn
            onClicked: songModel.toggleMetronome()
        }
        // A click steps the count-in up, a right click steps it down; it
        // wraps round from 4 bars to none.
        Chip {
            objectName: "countInButton"
            implicitWidth: 44; implicitHeight: 22
            text: songModel.countInBars === 0 ? "NO CI" : "CI " + songModel.countInBars
            accent: Theme.record
            on: appController.countingIn
            onClicked: songModel.cycleCountIn(1)
            onRightClicked: songModel.cycleCountIn(-1)
        }
    }
    RowLayout {
        spacing: 2
        Slider {
            objectName: "clickLevel"
            Layout.preferredWidth: 64; implicitHeight: 14
            padding: 0
            // A slider keeps no keyboard focus, so the editors keep the keys.
            focusPolicy: Qt.NoFocus
            from: -60; to: 6; stepSize: 0.5
            value: songModel.metronomeLevelDb
            onMoved: songModel.setMetronomeLevelDb(value)
            background: Rectangle {
                x: parent.leftPadding; y: parent.topPadding + parent.availableHeight / 2 - height / 2
                width: parent.availableWidth; height: 3; radius: 1.5
                color: Theme.line
                Rectangle {
                    width: parent.width * parent.parent.visualPosition; height: parent.height
                    radius: 1.5; color: songModel.metronomeOn ? Theme.acid : Theme.muted
                }
            }
            handle: Rectangle {
                x: parent.leftPadding + parent.visualPosition * (parent.availableWidth - width)
                y: parent.topPadding + parent.availableHeight / 2 - height / 2
                width: 10; height: 10; radius: 5
                color: songModel.metronomeOn ? Theme.acid : Theme.muted
                border.color: "#0e0f12"; border.width: 2
            }
        }
        Label {
            objectName: "clickLevelReadout"
            Layout.preferredWidth: 26
            horizontalAlignment: Text.AlignRight
            text: (songModel.metronomeLevelDb > 0 ? "+" : "") + songModel.metronomeLevelDb.toFixed(1)
            color: Theme.muted; font.family: "monospace"; font.pixelSize: 9; font.bold: true
        }
    }
}
