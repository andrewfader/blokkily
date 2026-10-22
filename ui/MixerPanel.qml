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
    color: Theme.rail
    // Every track's meter, read once for all the strips.
    readonly property var meterLevels: songModel.meters

    ColumnLayout {
        anchors.fill: parent; anchors.margins: 12; spacing: 8

        RowLayout {
            Layout.fillWidth: true
            SectionLabel { text: qsTr("MIXER") }
            Item { Layout.fillWidth: true }
            // How hard the audio device is pushed: amber past 70%, red at a
            // dropout or past 90%.
            Label {
                objectName: "dspLoad"
                readonly property int percent: Math.round(appController.dspLoad * 100)
                text: qsTr("DSP %1%").arg(percent)
                      + (appController.dropouts > 0 ? qsTr(" · %1 DROP").arg(appController.dropouts) : "")
                color: appController.dropouts > 0 || percent > 90 ? Theme.record
                     : percent > 70 ? Theme.amber : Theme.muted
                font.pixelSize: 9; font.letterSpacing: 0.8; font.family: Theme.mono
                HoverHandler { id: dspHover }
                ToolTip.visible: dspHover.hovered; ToolTip.delay: 500
                ToolTip.text: qsTr("The share of each audio block's time the mix takes to render, at its recent worst. Past 100% the device runs dry and drops out; a larger buffer in SETTINGS gives it more time.")
            }
            Label {
                objectName: "mixerTrackCount"
                text: songModel.trackCount + (songModel.trackCount === 1 ? qsTr(" TRACK") : qsTr(" TRACKS"))
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

            // Counted rather than given the rows, so a fader being dragged is
            // not rebuilt under the pointer by the move it just made: each
            // strip reads its own row of songModel.tracks.
            Repeater {
                model: songModel.trackCount
                MixerStrip {
                    // The meters are read once per update for every strip,
                    // not once per strip.
                    meterLevel: root.meterLevels[index] !== undefined ? root.meterLevels[index] : 0
                    onRenameRequested: (index, name) => root.trackRenameRequested(index, name)
                    onMenuRequested: (index, name) => root.trackMenuRequested(index, name)
                }
            }

            // Return buses, fed by the tracks' sends.
            Repeater {
                model: songModel.returns
                ReturnStrip {}
            }
            Chip {
                objectName: "addReturnButton"
                Layout.fillWidth: true
                text: qsTr("+ RETURN"); accent: Theme.amber
                onClicked: songModel.addReturn()
            }
        }
        }

        EffectRack {}

        Rectangle {
            objectName: "masterStrip"
            Layout.fillWidth: true; implicitHeight: 78; radius: 6
            color: songModel.masterRacked ? Theme.raised : Theme.panel
            border.color: Theme.acid

            // A click on the strip points the effect rack at the master.
            MouseArea {
                anchors.fill: parent
                onClicked: songModel.selectRack("master", 0)
            }

            ColumnLayout {
                anchors.fill: parent; anchors.margins: 9; spacing: 5
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("MASTER"); color: Theme.acid; font.pixelSize: 11
                        font.bold: true; font.letterSpacing: 1 }
                    Item { Layout.fillWidth: true }
                    Chip {
                        objectName: "masterRack"
                        text: qsTr("FX"); implicitHeight: 18
                        on: songModel.masterRacked
                        onClicked: songModel.selectRack("master", 0)
                    }
                    // CLIP stays lit once the master has gone over, until
                    // it is clicked, so an overload between glances is seen.
                    Label {
                        id: clipLight
                        objectName: "masterClip"
                        property bool held: false
                        Connections {
                            target: songModel
                            function onMetersChanged() { if (songModel.masterPeak > 0.99) clipLight.held = true }
                        }
                        text: held ? qsTr("CLIP") : qsTr("OK")
                        color: held ? Theme.record : Theme.muted
                        font.pixelSize: 9; font.bold: true
                        TapHandler { onTapped: clipLight.held = false }
                        HoverHandler { id: clipHover; cursorShape: clipLight.held ? Qt.PointingHandCursor : Qt.ArrowCursor }
                        ToolTip.visible: clipLight.held && clipHover.hovered; ToolTip.delay: 400
                        ToolTip.text: qsTr("The master went over 0 dB. Click to clear")
                    }
                }
                // On the tracks' 60 dB scale, so a level reads the same here
                // as on a strip.
                Rectangle {
                    objectName: "masterMeter"
                    Layout.fillWidth: true; implicitHeight: 5; radius: 2
                    color: Theme.well
                    Rectangle {
                        readonly property real level: songModel.masterMeter
                        width: parent.width * level
                        height: parent.height; radius: 2
                        color: level > 0.998 ? Theme.record
                             : level > 0.948 ? Theme.amber : Theme.acid
                    }
                }
                ValueDial {
                    Layout.fillWidth: true
                    label: qsTr("LEVEL"); from: -60; to: 6
                    defaultValue: 0
                    value: songModel.masterGainDb
                    readout: (songModel.masterGainDb <= -95.95 ? "-inf"
                              : (songModel.masterGainDb > 0 ? "+" : "")
                                + songModel.masterGainDb.toFixed(1)) + " dB"
                    // The bottom of the dial is silence, as on every strip.
                    onMoved: function(v) { songModel.setMasterGain(v <= -59.95 ? -96 : v) }
                }
            }
        }
    }
}
