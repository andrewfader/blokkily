import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// LOOP, IN and OUT for the arrangement loop: playback wraps from OUT back
// to IN. An export still renders the whole song.
RowLayout {
    id: root
    spacing: 4
    Chip {
        objectName: "loopButton"
        text: "LOOP"
        implicitWidth: 52; implicitHeight: 22
        accessibleLabel: "Loop the arrangement"
        on: songModel.loopEnabled
        onClicked: songModel.toggleLoop()
    }
    Chip {
        objectName: "loopInButton"
        text: "IN"
        implicitWidth: 36; implicitHeight: 22
        accessibleLabel: "Set loop start to this bar"
        on: songModel.loopEnabled
        onClicked: songModel.setLoopStartBar(transport.bar)
    }
    Chip {
        objectName: "loopOutButton"
        text: "OUT"
        implicitWidth: 40; implicitHeight: 22
        accessibleLabel: "Set loop end to this bar"
        on: songModel.loopEnabled
        onClicked: songModel.setLoopEndBar(transport.bar)
    }
    Label {
        objectName: "loopRangeReadout"
        visible: songModel.loopEnabled
        text: (songModel.loopStartBar + 1) + "–" + (songModel.loopEndBar + 1)
        color: Theme.acid
        font.family: Theme.mono; font.pixelSize: 9; font.bold: true
    }
}
