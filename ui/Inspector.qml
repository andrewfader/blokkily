import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Format.js" as Format

// The selected step, shaped: pitch and length, velocity and micro-timing,
// probability and ratchets, and its parameter lock.
Rectangle {
    objectName: "stepInspector"
    Layout.fillWidth: true
    Layout.preferredHeight: 228
    Layout.minimumHeight: 228
    radius: 6; color: Theme.panel; border.color: Theme.line

    // Nothing selected, or an empty step: say so and say what to do.
    ColumnLayout {
        anchors.centerIn: parent
        visible: !patternModel.selected.exists
        spacing: 4
        Label {
            text: "STEP " + Format.fmt2(patternModel.selectedStep + 1) + " IS EMPTY"
            color: Theme.muted; font.pixelSize: 12; font.bold: true
            font.letterSpacing: 1
            Layout.alignment: Qt.AlignHCenter
        }
        Label {
            text: "Click the step to place a note, then shape it here"
            color: "#5c626e"; font.pixelSize: 11
            Layout.alignment: Qt.AlignHCenter
        }
    }

    GridLayout {
        anchors.fill: parent; anchors.margins: 12
        columns: 2; columnSpacing: 18; rowSpacing: 10
        visible: patternModel.selected.exists

        // Identity + pitch
        RowLayout {
            Layout.fillWidth: true; spacing: 14
        ColumnLayout {
            Layout.preferredWidth: 116; spacing: 4
            SectionLabel { text: "STEP " + Format.fmt2(patternModel.selectedStep + 1) }
            Label {
                objectName: "inspectorNote"
                text: patternModel.selected.noteName !== undefined
                      ? patternModel.selected.noteName : ""
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
                        on: Math.round(patternModel.selected.duration / 120)
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
                value: patternModel.selected.velocity !== undefined
                       ? patternModel.selected.velocity : 0
                readout: patternModel.selected.velocityUnits !== undefined
                         ? patternModel.selected.velocityUnits : 0
                onMoved: function(v) { patternModel.setSelectedVelocity(v) }
            }
            ValueDial {
                Layout.fillWidth: true
                label: "MICRO"; from: -59; to: 59
                accent: Theme.blue
                value: patternModel.selected.micro !== undefined
                       ? patternModel.selected.micro : 0
                readout: (patternModel.selected.micro > 0 ? "+" : "") +
                         (patternModel.selected.micro !== undefined
                          ? patternModel.selected.micro : 0)
                onMoved: function(v) { patternModel.setSelectedMicroOffset(Math.round(v)) }
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
                value: patternModel.selected.probability !== undefined
                       ? patternModel.selected.probability : 1
                readout: Math.round((patternModel.selected.probability !== undefined
                         ? patternModel.selected.probability : 1) * 100) + "%"
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
                            on: patternModel.selected.ratchets === modelData
                            onClicked: patternModel.setSelectedRatchets(modelData)
                        }
                    }
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
                    on: patternModel.selected.hasLock === true &&
                        patternModel.selected.lockModulation === false
                    onClicked: patternModel.setSelectedLock(
                        patternModel.selected.lockIndex,
                        patternModel.selected.lockValue, false)
                }
                Chip {
                    objectName: "lockModulation"
                    text: "MOD"; accent: Theme.blue
                    on: patternModel.selected.lockModulation === true
                    onClicked: patternModel.setSelectedLock(
                        patternModel.selected.lockIndex,
                        patternModel.selected.lockValue, true)
                }
                Chip {
                    text: "CLEAR"
                    enabled: patternModel.selected.hasLock === true
                    onClicked: patternModel.clearSelectedLock()
                }
            }
            ValueDial {
                Layout.fillWidth: true
                label: "VALUE"; from: 0; to: 1
                enabled: patternModel.selected.hasLock === true
                accent: patternModel.selected.lockModulation ? Theme.blue : Theme.acid
                value: patternModel.selected.lockValue !== undefined
                       ? patternModel.selected.lockValue : 0
                readout: patternModel.selected.hasLock
                         ? patternModel.selected.lockText : "none"
                onMoved: function(v) {
                    patternModel.setSelectedLock(patternModel.selected.lockIndex, v,
                                                 patternModel.selected.lockModulation)
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
                        on: patternModel.selected.loop === modelData
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
            visible: patternModel.selected.voices !== undefined
                     && patternModel.selected.voices > 1
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
                    model: patternModel.selected.voices !== undefined
                           && patternModel.selected.voices > 1
                           ? patternModel.selected.voices : 0
                    ColumnLayout {
                        id: voiceColumn
                        required property int index
                        readonly property real modelData:
                            patternModel.selected.voiceVelocities !== undefined
                            && index < patternModel.selected.voiceVelocities.length
                            ? patternModel.selected.voiceVelocities[index] : 0
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
                                text: patternModel.selected.voiceUnits
                                      ? patternModel.selected.voiceUnits[voiceColumn.index] : ""
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
                            text: patternModel.selected.voiceNames
                                  ? patternModel.selected.voiceNames[voiceColumn.index] : ""
                            color: Theme.muted; font.pixelSize: 9; font.family: Theme.mono
                        }
                        Label {
                            objectName: "voiceLen" + voiceColumn.index
                            Layout.alignment: Qt.AlignHCenter
                            text: patternModel.selected.voiceLengths
                                  ? (patternModel.selected.voiceLengths[voiceColumn.index] / 120)
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
