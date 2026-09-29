import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The assistant's prompt bar: say what you want in musical words, see what
// the model proposes, and only then write it into the pattern. Nothing the
// model says reaches the song until Apply; everything it writes is one undo
// step. Escape hands the keyboard back to the editors.
//
// Layout states:
//   Idle        — empty input, status reads "Ready — backend".
//   Busy        — input is locked, spinner shows, status reads "Asking …".
//   Proposal    — input is locked, Apply/Discard are visible, the status
//                 line names the proposed triggers (e.g. "Proposed: 5
//                 triggers to replace the pattern") and lists every field
//                 the reply had to repair.
//   Wrote       — input is open again, status names what was written and
//                 reminds the producer one undo takes it back.
//
// The Ctrl+K shortcut in Main.qml focuses the prompt; Escape inside the
// prompt hands focus back to whatever was editing before.
Rectangle {
    id: bar
    objectName: "promptBar"
    property Item focusHome: null

    implicitHeight: column.implicitHeight + 16
    color: Theme.panel
    border.color: llmModel.hasProposal ? Theme.acid
                  : promptField.activeFocus ? Theme.acid
                  : Theme.line
    border.width: llmModel.hasProposal || promptField.activeFocus ? 2 : 1
    radius: 6

    ColumnLayout {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.margins: 8
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            // The section label sits to the left so the row reads the way
            // every other toolbar in the app reads — name, then controls.
            Label {
                text: "ASSISTANT"
                color: llmModel.hasProposal ? Theme.acid : Theme.muted
                font.pixelSize: 10
                font.letterSpacing: 1.2
                font.bold: llmModel.hasProposal
                Accessible.name: "Composition assistant"
            }

            Picker {
                id: backendPicker
                objectName: "backendPicker"
                choices: llmModel.backendNames
                value: llmModel.backendKey
                onPicked: name => llmModel.setBackend(name)
                Accessible.name: "Assistant backend"
                ToolTip.visible: pickerHover.hovered
                ToolTip.text: "Where the prompt is answered: Ollama on this machine, or a cloud model you have keyed"
                HoverHandler { id: pickerHover }
            }

            TextField {
                id: promptField
                objectName: "promptField"
                Layout.fillWidth: true
                enabled: !llmModel.busy && !llmModel.hasProposal
                placeholderText: llmModel.hasProposal
                    ? "Apply or discard the proposal below."
                    : "Ask for a pattern — “a minor pentatonic ascending”, “offbeat hats”, “make this jazzier”…"
                color: Theme.ink
                font.pixelSize: 13
                selectByMouse: true
                Accessible.name: "Assistant prompt"
                onAccepted: {
                    if (text.trim() !== "" && !llmModel.busy && !llmModel.hasProposal) {
                        llmModel.ask(text)
                        text = ""
                    }
                }
                Keys.onEscapePressed: {
                    if (llmModel.hasProposal) llmModel.discardProposal()
                    else if (bar.focusHome) bar.focusHome.forceActiveFocus()
                }
                background: Rectangle {
                    color: llmModel.hasProposal ? Theme.raised
                                                  : (promptField.activeFocus ? Theme.bg : Theme.raised)
                    radius: 4
                    border.color: promptField.activeFocus ? Theme.acid : "transparent"
                    border.width: promptField.activeFocus ? 1 : 0
                }
            }

            BusyIndicator {
                visible: llmModel.busy
                running: llmModel.busy
                Layout.preferredWidth: 22
                Layout.preferredHeight: 22
                Accessible.name: "Waiting for the model"
            }

            Chip {
                id: askButton
                objectName: "askButton"
                text: llmModel.busy ? "\u2026" : "ASK"
                implicitWidth: 56
                enabled: !llmModel.busy && !llmModel.hasProposal
                          && promptField.text.trim() !== ""
                onClicked: {
                    llmModel.ask(promptField.text)
                    promptField.text = ""
                }
                accessibleLabel: "Send prompt"
            }
        }

        // The status row is always present so the bar's height is stable;
        // its text and accent change with the assistant's state.
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            visible: llmModel.statusText !== "" || llmModel.hasProposal

            Rectangle {
                // A 6-pixel chip on the left edge colour-codes the state:
                // muted = idle, acid = proposal, amber = writing back, blue
                // = busy. Producers can see at a glance whether the bar is
                // idle or holding a model answer.
                Layout.preferredWidth: 6
                Layout.preferredHeight: 14
                Layout.alignment: Qt.AlignVCenter
                radius: 3
                color: llmModel.busy ? Theme.blue
                       : llmModel.hasProposal ? Theme.acid
                       : Theme.line
            }

            Label {
                id: statusLabel
                objectName: "promptStatus"
                Layout.fillWidth: true
                text: llmModel.hasProposal ? llmModel.proposalSummary : llmModel.statusText
                color: llmModel.busy ? Theme.blue
                       : llmModel.hasProposal ? Theme.ink
                       : Theme.muted
                font.pixelSize: 11
                wrapMode: Text.Wrap
                Accessible.name: "Assistant status: " + text
            }

            // Apply is the lit chip: the one action that writes the song.
            Chip {
                id: applyButton
                objectName: "promptApply"
                visible: llmModel.hasProposal
                text: "APPLY"
                implicitWidth: 72
                on: true
                onClicked: llmModel.applyProposal()
                accessibleLabel: "Apply proposed triggers to the pattern (one undo step)"
            }

            Chip {
                id: discardButton
                objectName: "promptDiscard"
                visible: llmModel.hasProposal
                text: "DISCARD"
                implicitWidth: 72
                onClicked: llmModel.discardProposal()
                accessibleLabel: "Discard the proposal without touching the pattern"
            }
        }

        // A tiny hint line that names the keyboard shortcut the producer can
        // reach the bar with. Shown only when nothing else needs the row,
        // so it never competes with a status or proposal.
        Label {
            Layout.fillWidth: true
            visible: !llmModel.busy && !llmModel.hasProposal
                      && llmModel.statusText === ""
            text: "Ctrl+K to focus · Return to send · Escape to leave"
            color: Theme.muted
            font.pixelSize: 10
            font.letterSpacing: 0.5
        }
    }

    function focusPrompt() {
        promptField.forceActiveFocus()
        promptField.selectAll()
    }
}