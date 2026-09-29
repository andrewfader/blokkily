import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One mixer track: its name and instrument, mute and solo, its arm and
// input channel, a peak meter, gain and pan. Every control writes to the
// song, which reaches the running engine without rebuilding it.
Rectangle {
    id: root
    // Naming and the track menu belong to the window, which owns them.
    signal renameRequested(int index, string name)
    signal menuRequested(int index, string name)
    required property int index
    readonly property var modelData: songModel.tracks[index] !== undefined
        ? songModel.tracks[index]
        : ({index: index, name: "", instrument: "", hasInstrument: false, gainDb: 0,
            gainText: "", pan: 0, panText: "", mute: false, solo: false, audible: true,
            selected: false, armed: false, routable: true, channelText: "",
            automationMode: "READ", inserts: 0, inputText: "MIDI", monitorText: "MON AUTO",
            takesAudio: false, monitoring: false, fed: false, outputs: []})
    readonly property int trackIndex: index
    objectName: "mixerStrip" + index
    Layout.fillWidth: true
    // The arm, input and automation rows are fixed, and so are the faders'
    // grips, so no row is squeezed to nothing; each send adds a row of its
    // own, sized so its dial is never squeezed.
    implicitHeight: 243 + songModel.returns.length * 48
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
                          + (modelData.inserts > 0 ? " · " + modelData.inserts + " FX" : "")
                    color: modelData.fed ? Theme.blue
                         : modelData.hasInstrument ? Theme.muted : "#5c626e"
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

        // Arm, channel and audio input: where what is played goes.
        TrackInputControls {
            Layout.fillWidth: true
            track: root.modelData
        }

        // Automation (decision 3): whether the track's lanes play (READ),
        // are ignored (OFF), or record the strip and its plugins while the
        // song plays (TOUCH while a control is held, LATCH until the stop,
        // WRITE the whole pass). A click steps forward, a right click back.
        RowLayout {
            Layout.fillWidth: true; spacing: 4
            Label {
                text: "AUTO"; color: Theme.muted
                font.pixelSize: 9; font.bold: true; font.letterSpacing: 0.8
            }
            Chip {
                objectName: "automationMode" + root.trackIndex
                implicitWidth: 64; implicitHeight: 24
                text: root.modelData.automationMode
                accent: root.modelData.automationMode === "READ" ? Theme.acid : Theme.record
                on: root.modelData.automationMode !== "OFF"
                onClicked: songModel.cycleAutomationMode(root.trackIndex, 1)
                onRightClicked: songModel.cycleAutomationMode(root.trackIndex, -1)
            }
            Item { Layout.fillWidth: true }
            // A multi-output instrument (wave 5.2): each aux output it
            // declares can be broken out to a mixer channel of its own.
            Chip {
                objectName: "addOutput" + root.trackIndex
                visible: root.modelData.outputs !== undefined && root.modelData.outputs.length > 0
                text: "+ OUT"; implicitHeight: 24
                accent: Theme.blue
                accessibleLabel: "Break out an instrument output"
                onClicked: outputMenu.open()
                Menu {
                    id: outputMenu
                    objectName: "outputMenu" + root.trackIndex
                    Instantiator {
                        model: root.modelData.outputs !== undefined ? root.modelData.outputs : []
                        delegate: MenuItem {
                            required property var modelData
                            objectName: "addOutput" + root.trackIndex + "_" + modelData.output
                            text: "AUX " + modelData.output
                                  + (modelData.routed ? " · on its channel" : " → new channel")
                            onTriggered: songModel.addInstrumentOutput(root.trackIndex,
                                                                       modelData.output)
                        }
                        onObjectAdded: function(i, item) { outputMenu.insertItem(i, item) }
                        onObjectRemoved: function(i, item) { outputMenu.removeItem(item) }
                    }
                }
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

        // Held, a fader overrides its automation lane in touch mode, and
        // letting go hands the strip back to the lane.
        ValueDial {
            objectName: "gainDial" + root.trackIndex
            Layout.fillWidth: true
            label: "GAIN"; from: -60; to: 6; grip: 18
            value: modelData.gainDb
            readout: modelData.gainText + " dB"
            accent: modelData.audible ? Theme.acid : Theme.muted
            onMoved: function(v) { songModel.setTrackGain(trackIndex, v) }
            onTouched: function(held) { appController.touchStrip(trackIndex, "gain", held) }
        }
        ValueDial {
            objectName: "panDial" + root.trackIndex
            Layout.fillWidth: true
            label: "PAN"; from: -1; to: 1; grip: 18
            accent: Theme.blue
            value: modelData.pan
            readout: modelData.panText
            onMoved: function(v) { songModel.setTrackPan(trackIndex, v) }
            onTouched: function(held) { appController.touchStrip(trackIndex, "pan", held) }
        }
        SendDials {
            id: sendDials
            Layout.fillWidth: true
            visible: songModel.returns.length > 0
            trackIndex: root.trackIndex
        }
    }
}
