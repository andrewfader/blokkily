import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One strip per track, reading the same song the arrangement does. Gain,
// pan, mute and solo reach the running engine without rebuilding it, so
// a fader can be moved while the song plays.
Rectangle {
    id: root
    signal trackRenameRequested(int index, string name)
    signal trackMenuRequested(int index, string name)
    objectName: "mixerPanel"
    SplitView.preferredWidth: 250; SplitView.minimumWidth: 210
    SplitView.maximumWidth: 320
    color: "#121317"

    ColumnLayout {
        anchors.fill: parent; anchors.margins: 12; spacing: 8

        RowLayout {
            Layout.fillWidth: true
            SectionLabel { text: "MIXER" }
            Item { Layout.fillWidth: true }
            Label {
                objectName: "mixerTrackCount"
                text: songModel.trackCount + (songModel.trackCount === 1 ? " TRACK" : " TRACKS")
                color: Theme.muted; font.pixelSize: 9; font.letterSpacing: 0.8
            }
        }

        // However many tracks the song has, the strips scroll and the
        // master stays in reach below them.
        Flickable {
            id: mixerScroll
            objectName: "mixerScroll"
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true
            contentWidth: width
            contentHeight: mixerStripColumn.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        ColumnLayout {
            id: mixerStripColumn
            objectName: "mixerStrips"
            width: mixerScroll.width; spacing: 6

            Repeater {
                model: songModel.tracks
                MixerStrip {
                    onRenameRequested: (index, name) => root.trackRenameRequested(index, name)
                    onMenuRequested: (index, name) => root.trackMenuRequested(index, name)
                }
            }
        }
        }

        Rectangle {
            objectName: "masterStrip"
            Layout.fillWidth: true; implicitHeight: 78; radius: 6
            color: Theme.panel; border.color: Theme.acid

            ColumnLayout {
                anchors.fill: parent; anchors.margins: 9; spacing: 5
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "MASTER"; color: Theme.acid; font.pixelSize: 11
                        font.bold: true; font.letterSpacing: 1 }
                    Item { Layout.fillWidth: true }
                    Label {
                        text: songModel.masterPeak > 0.99 ? "CLIP" : "OK"
                        color: songModel.masterPeak > 0.99 ? "#ff4d4d" : Theme.muted
                        font.pixelSize: 9; font.bold: true
                    }
                }
                Rectangle {
                    Layout.fillWidth: true; implicitHeight: 5; radius: 2
                    color: "#0d0e11"
                    Rectangle {
                        width: parent.width * Math.min(1, songModel.masterPeak)
                        height: parent.height; radius: 2
                        color: songModel.masterPeak > 0.99 ? "#ff4d4d"
                             : songModel.masterPeak > 0.7 ? Theme.amber : Theme.acid
                    }
                }
                ValueDial {
                    Layout.fillWidth: true
                    label: "LEVEL"; from: -60; to: 6
                    value: songModel.masterGainDb
                    readout: (songModel.masterGainDb <= -59.95 ? "-inf"
                              : (songModel.masterGainDb > 0 ? "+" : "")
                                + songModel.masterGainDb.toFixed(1)) + " dB"
                    onMoved: function(v) { songModel.setMasterGain(v) }
                }
            }
        }
    }
}
