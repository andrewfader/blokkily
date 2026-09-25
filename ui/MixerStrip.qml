import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One mixer track: its name and instrument, mute and solo, a peak meter,
// gain and pan. Every control writes to the song, which reaches the
// running engine without rebuilding it.
Rectangle {
    id: root
    // Naming and the track menu belong to the window, which owns them.
    signal renameRequested(int index, string name)
    signal menuRequested(int index, string name)
    required property var modelData
    readonly property int trackIndex: modelData.index
    objectName: "mixerStrip" + modelData.index
    Layout.fillWidth: true
    implicitHeight: 126
    radius: 6
    color: modelData.selected ? Theme.raised : Theme.panel
    border.color: modelData.selected ? Theme.acid : Theme.line

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: function(mouse) {
            songModel.selectTrack(trackIndex)
            if (mouse.button !== Qt.RightButton) return
            root.menuRequested(trackIndex, modelData.name)
        }
        onDoubleClicked: root.renameRequested(trackIndex, modelData.name)
    }

    ColumnLayout {
        anchors.fill: parent; anchors.margins: 9; spacing: 5

        RowLayout {
            Layout.fillWidth: true; spacing: 4
            ColumnLayout {
                Layout.fillWidth: true; spacing: 0
                Label {
                    Layout.fillWidth: true
                    text: modelData.name
                    color: modelData.audible ? Theme.ink : Theme.muted
                    font.pixelSize: 11; font.bold: true
                    font.letterSpacing: 0.8; elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    text: modelData.instrument
                    color: modelData.hasInstrument ? Theme.muted : "#5c626e"
                    font.pixelSize: 9; elide: Text.ElideRight
                }
            }
            PluginEditorButton { track: trackIndex; available: modelData.hasInstrument }
            Chip {
                objectName: "mute" + modelData.index
                text: "M"; accent: Theme.amber
                on: modelData.mute
                onClicked: songModel.toggleMute(trackIndex)
            }
            Chip {
                objectName: "solo" + modelData.index
                text: "S"; accent: Theme.blue
                on: modelData.solo
                onClicked: songModel.toggleSolo(trackIndex)
            }
        }

        // Peak meter: the last block's loudest sample,
        // laid out over the top 60 dB.
        Rectangle {
            objectName: "meter" + modelData.index
            Layout.fillWidth: true
            implicitHeight: 5; radius: 2
            color: "#0d0e11"
            // Read from the meters alone: they move
            // thirty times a second, and the rest of
            // the strip has no reason to redraw.
            Rectangle {
                objectName: "meterFill" + trackIndex
                readonly property real level:
                    songModel.meters[trackIndex] !== undefined
                    ? songModel.meters[trackIndex] : 0
                width: parent.width * level
                height: parent.height; radius: 2
                color: level > 0.998 ? "#ff4d4d"
                     : level > 0.948 ? Theme.amber : Theme.acid
            }
        }

        ValueDial {
            Layout.fillWidth: true
            label: "GAIN"; from: -60; to: 6
            value: modelData.gainDb
            readout: modelData.gainText + " dB"
            accent: modelData.audible ? Theme.acid : Theme.muted
            onMoved: function(v) { songModel.setTrackGain(trackIndex, v) }
        }
        ValueDial {
            Layout.fillWidth: true
            label: "PAN"; from: -1; to: 1
            accent: Theme.blue
            value: modelData.pan
            readout: modelData.panText
            onMoved: function(v) { songModel.setTrackPan(trackIndex, v) }
        }
    }
}
