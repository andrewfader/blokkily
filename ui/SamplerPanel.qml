import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import "Format.js" as Format

// The built-in sampler on the selected track: which file a zone plays, the
// key it sounds at its own pitch, the keys it answers, its loop and envelope,
// and chopping it into pads. Keyed and kit are the same instrument in two
// modes, so one panel edits both. Every control writes the program through
// appController, which hands it to the running sampler without a rebuild; a
// control stepped or dragged is one step of history.
Rectangle {
    id: panel
    objectName: "samplerPanel"
    readonly property var sampler: appController.sampler
    readonly property bool hasZone: sampler.zone !== undefined && sampler.zone >= 0
    readonly property bool kit: sampler.mode === "kit"
    implicitHeight: body.implicitHeight + 16
    Layout.minimumHeight: 220
    radius: 6
    color: Theme.panel
    border.color: Theme.amber

    // A compact stepper: the rail is narrow, and a key is set one step at a
    // time or typed, so a slider would be the wrong control.
    component Stepper: SpinBox {
        id: spin
        property bool keyNames: true
        implicitWidth: 60; implicitHeight: 24
        from: 0; to: 127
        editable: false
        focusPolicy: Qt.NoFocus
        leftPadding: 16; rightPadding: 16
        textFromValue: function(value) { return keyNames ? Format.keyName(value) : String(value) }
        contentItem: Label {
            text: spin.textFromValue(spin.value, spin.locale)
            color: spin.enabled ? Theme.ink : Theme.muted
            font.pixelSize: 11; font.family: "monospace"; font.bold: true
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        }
        down.indicator: Rectangle {
            x: 0; width: 16; height: spin.height; radius: 3
            color: spin.down.pressed ? Theme.amber : Theme.raised
            border.color: Theme.line
            Label { anchors.centerIn: parent; text: "−"; color: Theme.ink; font.pixelSize: 11 }
        }
        up.indicator: Rectangle {
            x: spin.width - width; width: 16; height: spin.height; radius: 3
            color: spin.up.pressed ? Theme.amber : Theme.raised
            border.color: Theme.line
            Label { anchors.centerIn: parent; text: "+"; color: Theme.ink; font.pixelSize: 11 }
        }
        background: Rectangle { radius: 3; color: Theme.bg; border.color: Theme.line }
    }

    component Caption: Label {
        color: Theme.muted; font.pixelSize: 9; font.letterSpacing: 1
    }

    FileDialog {
        id: sampleDialog
        title: "Load a sample"
        nameFilters: ["Audio (*.wav *.flac *.aif *.aiff *.ogg *.mp3)", "All files (*)"]
        fileMode: FileDialog.OpenFile
        onAccepted: appController.loadSamplerSample(selectedFile.toString())
    }

    ColumnLayout {
        id: body
        anchors.fill: parent; anchors.margins: 8
        spacing: 6

        // The track the sampler plays on, and which of its two modes it is in.
        RowLayout {
            Layout.fillWidth: true; spacing: 4
            Rectangle {
                implicitWidth: 30; implicitHeight: 20; radius: 3; color: Theme.amber
                Label { anchors.centerIn: parent; text: "SMP"; color: Theme.bg
                    font.pixelSize: 9; font.bold: true; font.letterSpacing: 1 }
            }
            Label {
                objectName: "samplerTrackName"
                Layout.fillWidth: true
                text: songModel.tracks[songModel.selectedTrack] !== undefined
                      ? songModel.tracks[songModel.selectedTrack].name : ""
                color: Theme.ink; font.bold: true; font.pixelSize: 10
                font.letterSpacing: 0.8; elide: Text.ElideRight
            }
            Chip {
                objectName: "samplerModeKeyed"
                text: "KEYS"; implicitHeight: 20; accent: Theme.amber
                on: !panel.kit
                onClicked: appController.setSamplerMode("keyed")
            }
            Chip {
                objectName: "samplerModeKit"
                text: "KIT"; implicitHeight: 20; accent: Theme.amber
                on: panel.kit
                onClicked: appController.setSamplerMode("kit")
            }
        }

        // Which zone is being edited, and the file it plays.
        RowLayout {
            Layout.fillWidth: true; spacing: 4
            Chip {
                objectName: "samplerZonePrevious"
                text: "◂"; implicitWidth: 20; implicitHeight: 20
                enabled: panel.hasZone && panel.sampler.zone > 0
                onClicked: appController.selectSamplerZone(panel.sampler.zone - 1)
            }
            Label {
                objectName: "samplerZone"
                text: panel.hasZone ? (panel.sampler.zone + 1) + "/" + panel.sampler.zones : "0/0"
                color: Theme.muted; font.pixelSize: 10; font.family: "monospace"
            }
            Chip {
                objectName: "samplerZoneNext"
                text: "▸"; implicitWidth: 20; implicitHeight: 20
                enabled: panel.hasZone && panel.sampler.zone + 1 < panel.sampler.zones
                onClicked: appController.selectSamplerZone(panel.sampler.zone + 1)
            }
            Label {
                objectName: "samplerSample"
                Layout.fillWidth: true
                text: panel.hasZone ? panel.sampler.sample : "no sample"
                color: panel.sampler.missing ? Theme.record
                     : panel.hasZone ? Theme.ink : Theme.muted
                font.pixelSize: 10; font.bold: true; elide: Text.ElideMiddle
            }
            Chip {
                objectName: "samplerLoad"
                text: "LOAD"; implicitHeight: 20
                onClicked: sampleDialog.open()
            }
        }
        Label {
            objectName: "samplerMissing"
            Layout.fillWidth: true
            visible: panel.sampler.missing === true
            text: "File missing — load it again"
            color: Theme.record; font.pixelSize: 9
        }

        // The key the file sounds at its own pitch, and the keys it answers.
        RowLayout {
            Layout.fillWidth: true; spacing: 4
            ColumnLayout {
                spacing: 1; Layout.fillWidth: true
                Caption { text: "ROOT" }
                Stepper {
                    objectName: "samplerRootKey"
                    Layout.fillWidth: true
                    enabled: panel.hasZone && panel.sampler.trackPitch === true
                    value: panel.hasZone ? panel.sampler.rootKey : 60
                    onValueModified: appController.setSamplerRootKey(value)
                }
            }
            ColumnLayout {
                spacing: 1; Layout.fillWidth: true
                Caption { text: "LOW" }
                Stepper {
                    objectName: "samplerLowKey"
                    Layout.fillWidth: true
                    enabled: panel.hasZone
                    value: panel.hasZone ? panel.sampler.lowKey : 0
                    onValueModified: appController.setSamplerKeyRange(
                        value, Math.max(value, panel.sampler.highKey))
                }
            }
            ColumnLayout {
                spacing: 1; Layout.fillWidth: true
                Caption { text: "HIGH" }
                Stepper {
                    objectName: "samplerHighKey"
                    Layout.fillWidth: true
                    enabled: panel.hasZone
                    value: panel.hasZone ? panel.sampler.highKey : 127
                    onValueModified: appController.setSamplerKeyRange(
                        Math.min(value, panel.sampler.lowKey), value)
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true; spacing: 4
            Caption { text: "LOOP"; Layout.preferredWidth: 30 }
            Repeater {
                model: [["OFF", "off"], ["FWD", "forward"], ["P-P", "ping_pong"]]
                delegate: Chip {
                    required property var modelData
                    objectName: "samplerLoop_" + modelData[1]
                    Layout.fillWidth: true
                    text: modelData[0]; implicitHeight: 20; accent: Theme.amber
                    enabled: panel.hasZone
                    on: panel.sampler.loop === modelData[1]
                    onClicked: appController.setSamplerLoop(modelData[1])
                }
            }
        }

        // The envelope, in seconds, with the sustain as a level.
        GridLayout {
            Layout.fillWidth: true
            columns: 2; columnSpacing: 10; rowSpacing: 2
            ValueDial {
                objectName: "samplerAttack"
                Layout.fillWidth: true
                label: "A"; from: 0; to: 2; accent: Theme.amber
                enabled: panel.hasZone
                value: panel.hasZone ? panel.sampler.attack : 0
                readout: (value * 1000).toFixed(0) + " ms"
                onMoved: v => appController.setSamplerEnvelope("attack", v)
            }
            ValueDial {
                objectName: "samplerDecay"
                Layout.fillWidth: true
                label: "D"; from: 0; to: 2; accent: Theme.amber
                enabled: panel.hasZone
                value: panel.hasZone ? panel.sampler.decay : 0
                readout: (value * 1000).toFixed(0) + " ms"
                onMoved: v => appController.setSamplerEnvelope("decay", v)
            }
            ValueDial {
                objectName: "samplerSustain"
                Layout.fillWidth: true
                label: "S"; from: 0; to: 1; accent: Theme.amber
                enabled: panel.hasZone
                value: panel.hasZone ? panel.sampler.sustain : 1
                readout: Math.round(value * 100) + "%"
                onMoved: v => appController.setSamplerEnvelope("sustain", v)
            }
            ValueDial {
                objectName: "samplerRelease"
                Layout.fillWidth: true
                label: "R"; from: 0; to: 4; accent: Theme.amber
                enabled: panel.hasZone
                value: panel.hasZone ? panel.sampler.release : 0
                readout: (value * 1000).toFixed(0) + " ms"
                onMoved: v => appController.setSamplerEnvelope("release", v)
            }
        }

        // Chops the zone's whole file into equal pads on keys from C2.
        RowLayout {
            Layout.fillWidth: true; spacing: 4
            Caption { text: "SLICES"; Layout.preferredWidth: 38 }
            Stepper {
                id: sliceCount
                objectName: "samplerSliceCount"
                Layout.fillWidth: true
                keyNames: false
                from: 1; to: 64; value: 8
                enabled: panel.hasZone
            }
            Chip {
                objectName: "samplerChop"
                text: "CHOP"; implicitHeight: 22; accent: Theme.amber
                enabled: panel.hasZone
                onClicked: appController.sliceSampler(sliceCount.value)
            }
        }
    }
}
