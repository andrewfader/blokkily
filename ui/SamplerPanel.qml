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
    // Whether the zone map, velocities and waveform are open.
    property bool editing: false
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
            font.pixelSize: 11; font.family: Theme.mono; font.bold: true
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
        title: qsTr("Load a sample")
        nameFilters: ["Audio (*.wav *.flac *.aif *.aiff *.ogg *.mp3)", "All files (*)"]
        fileMode: FileDialog.OpenFile
        onAccepted: appController.loadSamplerSample(selectedFile.toString())
    }
    FileDialog {
        id: multiDialog
        title: qsTr("Import a multi-sampled instrument (key and layer from each file's name)")
        nameFilters: ["Audio (*.wav *.flac *.aif *.aiff *.ogg *.mp3)", "All files (*)"]
        fileMode: FileDialog.OpenFiles
        onAccepted: {
            var paths = []
            for (var i = 0; i < selectedFiles.length; ++i) paths.push(selectedFiles[i].toString())
            appController.importMultisample(paths)
        }
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
                Label { anchors.centerIn: parent; text: qsTr("SMP"); color: Theme.bg
                    font.pixelSize: 9; font.bold: true; font.letterSpacing: 1 }
            }
            Label {
                objectName: "samplerTrackName"
                Layout.fillWidth: true
                text: songModel.trackNames[songModel.selectedTrack] !== undefined
                      ? songModel.trackNames[songModel.selectedTrack] : ""
                color: Theme.ink; font.bold: true; font.pixelSize: 10
                font.letterSpacing: 0.8; elide: Text.ElideRight
            }
            Chip {
                objectName: "samplerModeKeyed"
                text: qsTr("KEYS"); implicitHeight: 20; accent: Theme.amber
                on: !panel.kit
                onClicked: appController.setSamplerMode("keyed")
            }
            Chip {
                objectName: "samplerModeKit"
                text: qsTr("KIT"); implicitHeight: 20; accent: Theme.amber
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
                color: Theme.muted; font.pixelSize: 10; font.family: Theme.mono
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
                text: panel.hasZone ? panel.sampler.sample : qsTr("no sample")
                color: panel.sampler.missing ? Theme.record
                     : panel.hasZone ? Theme.ink : Theme.muted
                font.pixelSize: 10; font.bold: true; elide: Text.ElideMiddle
            }
        }
        // One file for the zone, or a multi-sampled instrument from many.
        RowLayout {
            Layout.fillWidth: true; spacing: 4
            Chip {
                objectName: "samplerLoad"
                text: qsTr("LOAD"); implicitHeight: 20
                Layout.fillWidth: true
                onClicked: sampleDialog.open()
            }
            Chip {
                objectName: "samplerImportMulti"
                text: qsTr("MULTI"); implicitHeight: 20; accent: Theme.amber
                Layout.fillWidth: true
                accessibleLabel: "Import a multi-sampled instrument"
                onClicked: multiDialog.open()
            }
            // The zone's velocities, the map of every zone and its waveform,
            // opened when wanted so the browser below keeps its room.
            Chip {
                objectName: "samplerEditToggle"
                text: qsTr("EDIT"); implicitHeight: 20
                Layout.fillWidth: true
                accessibleLabel: "Show the zone map, velocities and waveform"
                on: panel.editing
                onClicked: panel.editing = !panel.editing
            }
        }
        Label {
            objectName: "samplerMissing"
            Layout.fillWidth: true
            visible: panel.sampler.missing === true
            text: qsTr("File missing — load it again")
            color: Theme.record; font.pixelSize: 9
        }

        // The key the file sounds at its own pitch, and the keys it answers.
        RowLayout {
            Layout.fillWidth: true; spacing: 4
            ColumnLayout {
                spacing: 1; Layout.fillWidth: true
                Caption { text: qsTr("ROOT") }
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
                Caption { text: qsTr("LOW") }
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
                Caption { text: qsTr("HIGH") }
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

        ColumnLayout {
            objectName: "samplerEditing"
            Layout.fillWidth: true
            spacing: 6
            visible: panel.editing
            // The velocities the zone answers.
            RowLayout {
                Layout.fillWidth: true; spacing: 4
                Caption { text: qsTr("VEL"); Layout.preferredWidth: 30 }
                Stepper {
                    objectName: "samplerLowVelocity"
                    Layout.fillWidth: true
                    keyNames: false; from: 1; to: 127
                    enabled: panel.hasZone
                    value: panel.hasZone ? panel.sampler.lowVelocity : 1
                    onValueModified: appController.setSamplerVelocityRange(
                        value, Math.max(value, panel.sampler.highVelocity))
                }
                Stepper {
                    objectName: "samplerHighVelocity"
                    Layout.fillWidth: true
                    keyNames: false; from: 1; to: 127
                    enabled: panel.hasZone
                    value: panel.hasZone ? panel.sampler.highVelocity : 127
                    onValueModified: appController.setSamplerVelocityRange(
                        Math.min(value, panel.sampler.lowVelocity), value)
                }
            }

            // Every zone as a box of the keys (across) and velocities (up) it
            // answers: velocity layers stack. A click picks the zone to edit.
            Rectangle {
                objectName: "samplerZoneMap"
                Layout.fillWidth: true
                implicitHeight: 54
                visible: panel.sampler.map !== undefined && panel.sampler.map.length > 0
                radius: 3; color: Theme.bg; border.color: Theme.line
                Repeater {
                    model: panel.sampler.map !== undefined ? panel.sampler.map : []
                    Rectangle {
                        required property var modelData
                        objectName: "samplerZoneBox" + modelData.zone
                        readonly property bool chosen: modelData.zone === panel.sampler.zone
                        x: 1 + modelData.lowKey / 128 * (parent.width - 2)
                        width: Math.max(2, (modelData.highKey - modelData.lowKey + 1) / 128 * (parent.width - 2))
                        y: 1 + (127 - modelData.highVelocity) / 127 * (parent.height - 2)
                        height: Math.max(2, (modelData.highVelocity - modelData.lowVelocity + 1) / 127 * (parent.height - 2))
                        color: chosen ? Theme.amber : Theme.unchosen
                        border.color: Theme.bg; border.width: 1
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: appController.selectSamplerZone(parent.modelData.zone)
                        }
                    }
                }
            }

            // The zone's file: what plays (START..END) and its loop, each marker
            // dragged to move it. A double click zooms in on where it lands, a
            // right double click out; Ctrl and the wheel zoom around the
            // pointer, Shift and the wheel move along the file. The wheel
            // alone still scrolls the rail, and the panel is no taller.
            Rectangle {
                id: wave
                objectName: "samplerWaveform"
                Layout.fillWidth: true
                implicitHeight: 52
                visible: panel.hasZone && panel.sampler.frames > 0
                radius: 3; color: Theme.bg; border.color: Theme.line
                clip: true
                readonly property real frames: Math.max(1, panel.sampler.frames || 1)
                readonly property real startFrame: panel.sampler.start || 0
                readonly property real endFrame: panel.sampler.end > 0 ? panel.sampler.end : frames
                readonly property bool looped: panel.sampler.loop !== undefined && panel.sampler.loop !== "off"
                // The frames in view: the whole file until zoomed.
                property real viewStart: 0
                property real viewFrames: frames
                readonly property real zoom: frames / viewFrames
                onFramesChanged: { viewStart = 0; viewFrames = frames }
                function xOf(frame) { return 1 + (frame - viewStart) / viewFrames * (width - 2) }
                function frameOf(x) {
                    return Math.max(0, Math.min(frames, viewStart + (x - 1) / (width - 2) * viewFrames))
                }
                // Zooms by `factor` keeping `frame` where it is on screen, no
                // closer than 64 frames across the waveform.
                function zoomBy(factor, frame) {
                    const at = (frame - viewStart) / viewFrames
                    const shown = Math.max(Math.min(frames, 64), Math.min(frames, viewFrames / factor))
                    viewFrames = shown
                    viewStart = Math.max(0, Math.min(frames - shown, frame - at * shown))
                }
                function scrollBy(fraction) {
                    viewStart = Math.max(0, Math.min(frames - viewFrames, viewStart + fraction * viewFrames))
                }
                WaveformItem {
                    objectName: "samplerWaveformPeaks"
                    anchors.fill: parent; anchors.margins: 1
                    color: Theme.amber
                    peaks: wave.visible ? (panel.sampler, appController.samplerPeaks(Math.max(1, Math.round(width / 2)),
                                                                                    wave.viewStart, wave.viewStart + wave.viewFrames)) : []
                }
                // Under the markers, so a marker is still dragged where it is.
                MouseArea {
                    objectName: "samplerWaveformZoom"
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onDoubleClicked: mouse => wave.zoomBy(mouse.button === Qt.RightButton ? 0.5 : 2,
                                                          wave.frameOf(mouse.x))
                }
                Label {
                    objectName: "samplerZoomReadout"
                    anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 3
                    visible: wave.zoom > 1.001
                    text: "×" + (wave.zoom >= 10 ? Math.round(wave.zoom) : wave.zoom.toFixed(1))
                    color: Theme.ink; font.pixelSize: 9; font.bold: true; font.family: Theme.mono
                }
                WheelHandler {
                    acceptedModifiers: Qt.ControlModifier
                    onWheel: event => wave.zoomBy(Math.pow(2, event.angleDelta.y / 120), wave.frameOf(point.position.x))
                }
                WheelHandler {
                    acceptedModifiers: Qt.ShiftModifier
                    onWheel: event => wave.scrollBy(-(event.angleDelta.y + event.angleDelta.x) / 120 * 0.25)
                }
                // What does not play is shaded.
                Rectangle { x: 1; width: Math.max(0, Math.min(wave.width - 2, wave.xOf(wave.startFrame) - 1)); height: parent.height
                    color: Theme.scrim }
                Rectangle { x: Math.max(1, wave.xOf(wave.endFrame)); width: Math.max(0, wave.width - 1 - x); height: parent.height
                    color: Theme.scrim }
                Rectangle {
                    visible: wave.looped
                    x: wave.xOf(panel.sampler.loopStart || 0)
                    width: Math.max(1, wave.xOf(panel.sampler.loopEnd || 0) - x); height: 4
                    color: Theme.acid
                }
                component Marker: Rectangle {
                    id: marker
                    property real frame: 0
                    property color tint: Theme.ink
                    signal dropped(real frame)
                    x: wave.xOf(frame) - width / 2
                    width: 9; height: wave.height; color: "transparent"
                    Rectangle { anchors.horizontalCenter: parent.horizontalCenter; width: 2; height: parent.height
                        color: marker.tint }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.SizeHorCursor
                        drag.target: marker; drag.axis: Drag.XAxis
                        drag.minimumX: -marker.width / 2; drag.maximumX: wave.width - marker.width / 2
                        onReleased: {
                            marker.dropped(wave.frameOf(marker.x + marker.width / 2))
                            // Follows the zone again, wherever the edit put it.
                            marker.x = Qt.binding(function() { return wave.xOf(marker.frame) - marker.width / 2 })
                        }
                    }
                }
                Marker { objectName: "samplerStartMarker"; frame: wave.startFrame; tint: Theme.ink
                    onDropped: f => appController.setSamplerRegion(f, wave.endFrame) }
                Marker { objectName: "samplerEndMarker"; frame: wave.endFrame; tint: Theme.ink
                    onDropped: f => appController.setSamplerRegion(wave.startFrame, f) }
                Marker { objectName: "samplerLoopStartMarker"; visible: wave.looped
                    frame: panel.sampler.loopStart || 0; tint: Theme.acid
                    onDropped: f => appController.setSamplerLoopPoints(f, panel.sampler.loopEnd) }
                Marker { objectName: "samplerLoopEndMarker"; visible: wave.looped
                    frame: panel.sampler.loopEnd || 0; tint: Theme.acid
                    onDropped: f => appController.setSamplerLoopPoints(panel.sampler.loopStart, f) }
            }
        }

        RowLayout {
            Layout.fillWidth: true; spacing: 4
            Caption { text: qsTr("LOOP"); Layout.preferredWidth: 30 }
            Repeater {
                model: [[QT_TR_NOOP("OFF"), "off"], [QT_TR_NOOP("FWD"), "forward"], [QT_TR_NOOP("P-P"), "ping_pong"]]
                delegate: Chip {
                    required property var modelData
                    objectName: "samplerLoop_" + modelData[1]
                    Layout.fillWidth: true
                    text: qsTr(modelData[0]); implicitHeight: 20; accent: Theme.amber
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
            Caption { text: qsTr("SLICES"); Layout.preferredWidth: 38 }
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
                text: qsTr("CHOP"); implicitHeight: 22; accent: Theme.amber
                enabled: panel.hasZone
                onClicked: appController.sliceSampler(sliceCount.value)
            }
        }
    }
}
