import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Format.js" as Format

// The selected step, shaped: pitch and length, velocity, micro-timing and a
// chord's strum, probability, ratchets and a chord's inversion, and its
// parameter lock.
Rectangle {
    id: inspector
    objectName: "stepInspector"
    // The selected step, read once per change: every field below reads
    // this, not the model (whose map is rebuilt on each read).
    readonly property var selection: patternModel.selected
    Layout.fillWidth: true
    Layout.preferredHeight: 264
    Layout.minimumHeight: 264
    radius: 6; color: Theme.panel; border.color: Theme.line

    // Nothing selected, or an empty step: say so and say what to do.
    ColumnLayout {
        anchors.centerIn: parent
        visible: !inspector.selection.exists
        spacing: 4
        Label {
            text: "STEP " + Format.fmt2(patternModel.selectedStep + 1) + " IS EMPTY"
            color: Theme.muted; font.pixelSize: 12; font.bold: true
            font.letterSpacing: 1
            Layout.alignment: Qt.AlignHCenter
        }
        Label {
            text: "Click the step to place a note, then shape it here"
            color: Theme.faint; font.pixelSize: 11
            Layout.alignment: Qt.AlignHCenter
        }
    }

    GridLayout {
        anchors.fill: parent; anchors.margins: 12
        columns: 2; columnSpacing: 18; rowSpacing: 10
        visible: inspector.selection.exists

        // Identity + pitch
        RowLayout {
            Layout.fillWidth: true; spacing: 14
        ColumnLayout {
            Layout.preferredWidth: 116; spacing: 4
            SectionLabel { text: "STEP " + Format.fmt2(patternModel.selectedStep + 1) }
            Label {
                objectName: "inspectorNote"
                text: inspector.selection.noteName !== undefined
                      ? inspector.selection.noteName : ""
                color: Theme.amber; font.pixelSize: 26; font.bold: true
                font.family: Theme.mono
            }
            RowLayout {
                spacing: 4
                Chip { text: "−12"; accent: Theme.blue
                    onClicked: patternModel.transposeSelected(-12) }
                Chip { text: "−"; accent: Theme.blue
                    onClicked: patternModel.transposeSelected(-1) }
                Chip { text: "+"; accent: Theme.blue
                    onClicked: patternModel.transposeSelected(1) }
                Chip { text: "+12"; accent: Theme.blue
                    onClicked: patternModel.transposeSelected(12) }
            }
            RowLayout {
                spacing: 3
                SectionLabel { text: "LEN" }
                Repeater {
                    model: [1, 2, 4, 8]
                    Chip {
                        required property var modelData
                        objectName: "duration" + modelData
                        text: modelData + "ST"
                        on: Math.round(inspector.selection.duration / 120)
                            === modelData
                        onClicked: patternModel.setSelectedDuration(modelData * 120)
                    }
                }
            }
        }

        Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: Theme.line }

        // Feel
        ColumnLayout {
            Layout.fillWidth: true; spacing: 10
            ValueDial {
                Layout.fillWidth: true
                label: "VELOCITY"; from: 0; to: 1
                value: inspector.selection.velocity !== undefined
                       ? inspector.selection.velocity : 0
                readout: inspector.selection.velocityUnits !== undefined
                         ? inspector.selection.velocityUnits : 0
                onMoved: function(v) { patternModel.setSelectedVelocity(v) }
            }
            ValueDial {
                Layout.fillWidth: true
                label: "MICRO"; from: -59; to: 59
                accent: Theme.blue
                value: inspector.selection.micro !== undefined
                       ? inspector.selection.micro : 0
                readout: (inspector.selection.micro > 0 ? "+" : "") +
                         (inspector.selection.micro !== undefined
                          ? inspector.selection.micro : 0)
                onMoved: function(v) { patternModel.setSelectedMicroOffset(Math.round(v)) }
            }
            // Ticks from one voice of a chord to the next, lowest voice first.
            ValueDial {
                objectName: "strumDial"
                Layout.fillWidth: true
                label: "STRUM"; from: 0; to: 60; grip: 16
                accent: Theme.blue
                enabled: inspector.selection.voices > 1
                value: inspector.selection.strum !== undefined
                       ? inspector.selection.strum : 0
                readout: inspector.selection.voices > 1
                         ? inspector.selection.strum + " TK" : "chords"
                onMoved: function(v) { patternModel.setSelectedStrum(Math.round(v)) }
            }
        }
        }

        // Chance
        ColumnLayout {
            Layout.fillWidth: true; spacing: 10
            ValueDial {
                Layout.fillWidth: true
                label: "PROBABILITY"; from: 0; to: 1
                accent: Theme.amber
                value: inspector.selection.probability !== undefined
                       ? inspector.selection.probability : 1
                readout: Math.round((inspector.selection.probability !== undefined
                         ? inspector.selection.probability : 1) * 100) + "%"
                onMoved: function(v) { patternModel.setSelectedProbability(v) }
            }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 3
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "RATCHET"; color: Theme.muted; font.pixelSize: 10
                        font.letterSpacing: 1 }
                    Item { Layout.fillWidth: true }
                }
                RowLayout {
                    spacing: 3
                    Repeater {
                        model: [1, 2, 3, 4, 6, 8]
                        Chip {
                            required property var modelData
                            text: "×" + modelData
                            on: inspector.selection.ratchets === modelData
                            onClicked: patternModel.setSelectedRatchets(modelData)
                        }
                    }
                }
            }
            // Turns a chord's voices: down drops its highest voice an octave,
            // up lifts its lowest, and every editor shows the voices it turns
            // into.
            RowLayout {
                Layout.fillWidth: true; spacing: 3
                Label { text: "INVERSION"; color: Theme.muted; font.pixelSize: 10
                    font.letterSpacing: 1 }
                Item { Layout.fillWidth: true }
                Chip {
                    objectName: "invertDown"
                    text: "▼ INV"; accent: Theme.blue
                    enabled: inspector.selection.voices > 1
                    onClicked: patternModel.invertSelected(-1)
                }
                Chip {
                    objectName: "invertUp"
                    text: "INV ▲"; accent: Theme.blue
                    enabled: inspector.selection.voices > 1
                    onClicked: patternModel.invertSelected(1)
                }
            }
        }

        // Parameter lock — the Elektron-style p-lock, now editable.
        ColumnLayout {
            Layout.fillWidth: true; spacing: 8
            RowLayout {
                Layout.fillWidth: true; spacing: 6
                SectionLabel { text: "PARAMETER LOCK" }
                Item { Layout.fillWidth: true }
                Chip {
                    objectName: "lockAutomation"
                    text: "AUTO"
                    on: inspector.selection.hasLock === true &&
                        inspector.selection.lockModulation === false
                    onClicked: patternModel.setSelectedLock(
                        inspector.selection.lockIndex,
                        inspector.selection.lockValue, false)
                }
                Chip {
                    objectName: "lockModulation"
                    text: "MOD"; accent: Theme.blue
                    on: inspector.selection.lockModulation === true
                    onClicked: patternModel.setSelectedLock(
                        inspector.selection.lockIndex,
                        inspector.selection.lockValue, true)
                }
                Chip {
                    text: "CLEAR"
                    enabled: inspector.selection.hasLock === true
                    onClicked: patternModel.clearSelectedLock()
                }
            }
            // What the lock moves: the track's instrument or one of its
            // insert effects, a return's or the master's, and which of its
            // parameters.
            RowLayout {
                id: lockTargetRow
                Layout.fillWidth: true; spacing: 6
                readonly property int slot: inspector.selection.lockSlot !== undefined
                                            ? inspector.selection.lockSlot : -1
                readonly property string kind: inspector.selection.lockKind !== undefined
                                               ? inspector.selection.lockKind : "track"
                readonly property int bus: kind === "track" ? songModel.selectedTrack
                                           : (inspector.selection.lockBus !== undefined
                                              ? inspector.selection.lockBus : 0)
                readonly property var choices: songModel.lockTargetChoices(songModel.selectedTrack)
                readonly property var targets: choices.map(choice => choice.name)
                readonly property var parameters: appController.insertParameters(kind, bus, slot)
                function chosen() {
                    for (var i = 0; i < choices.length; ++i)
                        if (choices[i].kind === kind && choices[i].slot === slot &&
                                (kind === "track" || choices[i].bus === bus))
                            return choices[i]
                    return null
                }
                function parameterNames() {
                    var names = []
                    for (var i = 0; i < parameters.length; ++i) names.push(parameters[i].name)
                    return names
                }
                Label { text: "ON"; color: Theme.muted; font.pixelSize: 10
                    font.letterSpacing: 1 }
                Picker {
                    objectName: "lockTarget"
                    implicitHeight: 22
                    choices: lockTargetRow.targets
                    value: lockTargetRow.chosen() ? lockTargetRow.chosen().name : ""
                    onPicked: name => {
                        const choice = lockTargetRow.choices[lockTargetRow.targets.indexOf(name)]
                        if (!choice) return
                        const parameters =
                            appController.insertParameters(choice.kind, choice.bus, choice.slot)
                        patternModel.setSelectedLockTargetOn(
                            choice.kind, choice.bus, choice.slot,
                            parameters.length > 0 ? parameters[0].id : 0,
                            parameters.length > 0 ? parameters[0].name : "")
                    }
                }
                Picker {
                    objectName: "lockParameter"
                    Layout.fillWidth: true
                    implicitHeight: 22
                    choices: lockTargetRow.parameterNames()
                    value: inspector.selection.hasLock === true
                           ? inspector.selection.lockName : "—"
                    onPicked: name => {
                        for (var i = 0; i < lockTargetRow.parameters.length; ++i)
                            if (lockTargetRow.parameters[i].name === name)
                                patternModel.setSelectedLockTargetOn(
                                    lockTargetRow.kind, lockTargetRow.bus, lockTargetRow.slot,
                                    lockTargetRow.parameters[i].id, name)
                    }
                }
            }
            ValueDial {
                Layout.fillWidth: true
                label: "VALUE"; from: 0; to: 1
                enabled: inspector.selection.hasLock === true
                accent: inspector.selection.lockModulation ? Theme.blue : Theme.acid
                value: inspector.selection.lockValue !== undefined
                       ? inspector.selection.lockValue : 0
                readout: inspector.selection.hasLock
                         ? inspector.selection.lockText : "none"
                onMoved: function(v) {
                    patternModel.setSelectedLock(inspector.selection.lockIndex, v,
                                                 inspector.selection.lockModulation)
                }
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 6
                Label { text: "EVERY"; color: Theme.muted; font.pixelSize: 10
                    font.letterSpacing: 1 }
                Repeater {
                    model: [0, 2, 3, 4]
                    Chip {
                        required property var modelData
                        text: modelData === 0 ? "LOOP" : modelData + "×"
                        accent: Theme.amber
                        on: inspector.selection.loop === modelData
                        onClicked: patternModel.setSelectedPlayOnLoop(modelData)
                    }
                }
            }
        }

        // Voices — a chord's voices each sound as hard as they were struck.
        // One bar per voice, in interval order: its fill is its velocity,
        // dragging it up or down sets that voice alone, and the figure under
        // it is how long it lasts, in steps.
        ColumnLayout {
            objectName: "chordVoices"
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            spacing: 4
            visible: inspector.selection.voices !== undefined
                     && inspector.selection.voices > 1
            RowLayout {
                Layout.fillWidth: true; spacing: 6
                SectionLabel { text: "VOICE VELOCITY" }
                Item { Layout.fillWidth: true }
                Label {
                    text: "DRAG ↕"
                    color: Theme.muted; font.pixelSize: 9; font.letterSpacing: 1
                }
            }
            RowLayout {
                spacing: 6
                Repeater {
                    // A count rather than the list of velocities: every edit
                    // hands out a new list, and a Repeater given a new list
                    // rebuilds its bars, dropping the one being dragged.
                    model: inspector.selection.voices !== undefined
                           && inspector.selection.voices > 1
                           ? inspector.selection.voices : 0
                    ColumnLayout {
                        id: voiceColumn
                        required property int index
                        readonly property real modelData:
                            inspector.selection.voiceVelocities !== undefined
                            && index < inspector.selection.voiceVelocities.length
                            ? inspector.selection.voiceVelocities[index] : 0
                        spacing: 2
                        Rectangle {
                            objectName: "voiceVel" + voiceColumn.index
                            Layout.preferredWidth: 26
                            Layout.minimumWidth: 20
                            Layout.preferredHeight: 50
                            Layout.minimumHeight: 48
                            radius: 3
                            color: Theme.raised; border.color: Theme.line
                            Rectangle {
                                anchors.left: parent.left; anchors.right: parent.right
                                anchors.bottom: parent.bottom; anchors.margins: 2
                                height: Math.max(0, (parent.height - 4) * voiceColumn.modelData)
                                radius: 2
                                color: Theme.acid
                            }
                            Label {
                                anchors.top: parent.top; anchors.topMargin: 2
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: inspector.selection.voiceUnits
                                      ? inspector.selection.voiceUnits[voiceColumn.index] : ""
                                color: voiceColumn.modelData > 0.8 ? Theme.bg : Theme.ink
                                font.pixelSize: 10; font.family: Theme.mono; font.bold: true
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.SizeVerCursor
                                // The editors scroll vertically; a vertical
                                // drag on a bar is an edit, not a scroll.
                                preventStealing: true
                                function write(y) {
                                    patternModel.setSelectedVoiceVelocity(
                                        voiceColumn.index,
                                        Math.max(0, Math.min(1, 1 - y / height)))
                                }
                                onPressed: function(mouse) {
                                    songModel.beginGesture()
                                    write(mouse.y)
                                }
                                onPositionChanged: function(mouse) {
                                    if (pressed) write(mouse.y)
                                }
                                onReleased: songModel.endGesture()
                                onCanceled: songModel.endGesture()
                            }
                        }
                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            text: inspector.selection.voiceNames
                                  ? inspector.selection.voiceNames[voiceColumn.index] : ""
                            color: Theme.muted; font.pixelSize: 9; font.family: Theme.mono
                        }
                        Label {
                            objectName: "voiceLen" + voiceColumn.index
                            Layout.alignment: Qt.AlignHCenter
                            text: inspector.selection.voiceLengths
                                  ? (inspector.selection.voiceLengths[voiceColumn.index] / 120)
                                        .toFixed(1)
                                  : ""
                            color: Theme.blue; font.pixelSize: 9; font.family: Theme.mono
                        }
                    }
                }
            }
        }
    }
}
