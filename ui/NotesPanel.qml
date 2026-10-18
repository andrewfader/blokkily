import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// NOTES: what the producer wants to remember about the song - lyrics, a
// to-do list, what the next session is for. Saved with the project; each
// burst of typing is one step of history.
Rectangle {
    id: root
    objectName: "notesPanel"
    property string view: "ALL"
    property Item focusHome
    visible: view === "NOTES"
    Layout.fillWidth: true
    Layout.fillHeight: visible
    Layout.minimumHeight: visible ? 320 : 0
    implicitHeight: visible ? 420 : 0
    radius: 6
    color: Theme.panel
    border.color: Theme.line

    ColumnLayout {
        anchors.fill: parent; anchors.margins: 12
        spacing: 8
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            SectionLabel { text: qsTr("NOTES") }
            Label {
                text: qsTr("saved with the song · Escape returns to the editors")
                color: Theme.muted; font.pixelSize: 10
            }
            Item { Layout.fillWidth: true }
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            TextArea {
                id: notes
                objectName: "notesText"
                placeholderText: qsTr("Lyrics, ideas, what to try next…")
                wrapMode: TextEdit.Wrap
                color: Theme.ink; font.pixelSize: 13
                selectByMouse: true
                Accessible.name: "Song notes"
                // What they type goes straight into the song; what the song
                // holds (an undo, a redo, a restored version, another
                // project) is put here by the handler below, which keeps the
                // caret where it was. A binding would put it back at the start.
                Component.onCompleted: text = songModel.notes
                onTextChanged: if (text !== songModel.notes) songModel.setNotes(text)
                Connections {
                    target: songModel
                    function onNotesChanged() {
                        if (notes.text === songModel.notes) return
                        // The caret stays where it was, not at the start: at
                        // the end if it was at the end.
                        const atEnd = notes.cursorPosition >= notes.length
                        const caret = notes.cursorPosition
                        notes.text = songModel.notes
                        notes.cursorPosition = atEnd ? notes.length : Math.min(caret, notes.length)
                    }
                }
                Keys.onEscapePressed: if (root.focusHome) root.focusHome.forceActiveFocus()
                background: Rectangle {
                    color: notes.activeFocus ? Theme.bg : Theme.raised; radius: 4
                    border.color: notes.activeFocus ? Theme.acid : "transparent"
                }
            }
        }
    }
}
