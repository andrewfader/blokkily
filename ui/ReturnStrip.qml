import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One return bus: the sum of the tracks' sends, through its own inserts, onto
// the master. Returns are solo-safe: soloing a track never silences one.
Rectangle {
    id: root
    required property var modelData
    readonly property int busIndex: modelData.index
    // The strip's live values, read apart from the song's structure so a
    // fader being dragged is not rebuilt under the pointer.
    readonly property var mix: songModel.returnMix[busIndex] !== undefined
                               ? songModel.returnMix[busIndex]
                               : ({gainDb: 0, gainText: "0.0", pan: 0, mute: false})
    objectName: "returnStrip" + busIndex
    Layout.fillWidth: true
    implicitHeight: 100
    radius: 6
    color: modelData.racked ? Theme.raised : Theme.panel
    border.color: modelData.racked ? Theme.amber : Theme.line

    MouseArea {
        anchors.fill: parent
        onClicked: songModel.selectRack("return", root.busIndex)
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
                    color: root.mix.mute ? Theme.muted : Theme.amber
                    font.pixelSize: 11; font.bold: true
                    font.letterSpacing: 0.8; elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    text: modelData.inserts + (modelData.inserts === 1 ? " insert" : " inserts")
                    color: Theme.muted; font.pixelSize: 9; elide: Text.ElideRight
                }
            }
            Chip {
                objectName: "returnRack" + root.busIndex
                text: "FX"; accent: Theme.amber
                on: modelData.racked
                onClicked: songModel.selectRack("return", root.busIndex)
            }
            Chip {
                objectName: "returnMute" + root.busIndex
                text: "M"; accent: Theme.amber
                on: root.mix.mute
                onClicked: songModel.toggleReturnMute(root.busIndex)
            }
        }

        Rectangle {
            objectName: "returnMeter" + root.busIndex
            Layout.fillWidth: true
            implicitHeight: 5; radius: 2
            color: "#0d0e11"
            Rectangle {
                readonly property real level:
                    songModel.returnMeters[root.busIndex] !== undefined
                    ? songModel.returnMeters[root.busIndex] : 0
                width: parent.width * level
                height: parent.height; radius: 2
                color: level > 0.998 ? "#ff4d4d" : Theme.amber
            }
        }

        ValueDial {
            objectName: "returnGain" + root.busIndex
            Layout.fillWidth: true
            label: "RETURN"; from: -60; to: 6; grip: 18
            value: root.mix.gainDb
            readout: root.mix.gainText + " dB"
            accent: Theme.amber
            onMoved: function(v) { songModel.setReturnGain(root.busIndex, v) }
        }
    }
}
