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
        text: qsTr("LOOP")
        implicitWidth: Math.max(52, labelWidth + labelPadding + 2); implicitHeight: 22
        accessibleLabel: "Loop the arrangement"
        on: songModel.loopEnabled
        onClicked: songModel.toggleLoop()
    }
    Chip {
        objectName: "loopInButton"
        text: qsTr("IN")
        implicitWidth: Math.max(36, labelWidth + labelPadding + 2); implicitHeight: 22
        accessibleLabel: "Set loop start to this bar"
        on: songModel.loopEnabled
        onClicked: songModel.setLoopStartBar(transport.bar)
    }
    Chip {
        objectName: "loopOutButton"
        text: qsTr("OUT")
        implicitWidth: Math.max(40, labelWidth + labelPadding + 2); implicitHeight: 22
        accessibleLabel: "Set loop end to this bar"
        on: songModel.loopEnabled
        onClicked: songModel.setLoopEndBar(transport.bar)
    }
    // PUNCH records only between IN and OUT; looped, each pass is a take.
    Chip {
        objectName: "punchButton"
        text: qsTr("PUNCH")
        implicitWidth: Math.max(56, labelWidth + labelPadding + 2); implicitHeight: 22
        accent: Theme.record
        accessibleLabel: "Record only between IN and OUT"
        on: songModel.punch
        onClicked: songModel.setPunch(!songModel.punch)
    }
    // Looped MIDI recording: each pass a take of its own, stacked over the
    // loop's bars (lit), or merged into the pattern there as an overdub.
    Chip {
        objectName: "midiTakesButton"
        text: qsTr("TAKES")
        implicitWidth: Math.max(62, labelWidth + labelPadding + 2); implicitHeight: 22
        accent: Theme.record
        accessibleLabel: "Looped MIDI passes become takes"
        on: songModel.midiTakes
        onClicked: songModel.setMidiTakes(!songModel.midiTakes)
    }
    // Cuts the selected track's takes at the playhead, so each side can
    // play a different take.
    Chip {
        objectName: "splitTakesButton"
        text: qsTr("SPLIT")
        implicitWidth: Math.max(48, labelWidth + labelPadding + 2); implicitHeight: 22
        accent: Theme.amber
        accessibleLabel: "Split the selected track's takes at the playhead"
        onClicked: songModel.splitTakes(songModel.selectedTrack, transport.tick)
    }
    Label {
        objectName: "loopRangeReadout"
        visible: songModel.loopEnabled || songModel.punch
        text: (songModel.loopStartBar + 1) + "–" + (songModel.loopEndBar + 1)
        color: Theme.acid
        font.family: Theme.mono; font.pixelSize: 9; font.bold: true
    }
}
