import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A strip's input: R arms the track, so what is played - a MIDI keyboard, the
// on-screen keyboard, the tracker's note keys - is heard on it and recorded
// into it. With no track armed, the selected track takes what is played. The
// channel says which of a keyboard's sixteen channels the track hears; a click
// steps it forward, a right click back. Arming is saved with the song but is
// never a step of history, so undo leaves it alone.
RowLayout {
    id: root
    required property var track
    spacing: 4

    Chip {
        objectName: "arm" + root.track.index
        text: "R"; accent: Theme.record
        on: root.track.armed
        enabled: root.track.routable
        onClicked: songModel.toggleArm(root.track.index)
    }
    Chip {
        objectName: "inputChannel" + root.track.index
        // Wide enough for "CH 16", so the chip does not jump as it steps.
        implicitWidth: 58
        text: root.track.channelText
        accent: Theme.record
        onClicked: songModel.cycleInputChannel(root.track.index, 1)
        onRightClicked: songModel.cycleInputChannel(root.track.index, -1)
    }
    Label {
        objectName: "inputState" + root.track.index
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignRight
        // Says where played notes go, so an unarmed track that is still
        // taking them (the selected one, with nothing armed) is not a surprise.
        text: root.track.armed ? "ARMED"
              : (songModel.armedCount === 0 && root.track.selected ? "PLAYS INPUT" : "")
        color: root.track.armed ? Theme.record : Theme.muted
        font.pixelSize: 9; font.bold: true; font.letterSpacing: 0.8
        elide: Text.ElideRight
    }
}
