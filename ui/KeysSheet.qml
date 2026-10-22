import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Every key the window answers, in one place (features/keys_sheet.feature):
// opened by Ctrl+/ or the ? beside HELP, closed by Escape, a click outside
// it, or the same key again. The same list as docs/usage.md's.
Popup {
    id: sheet
    objectName: "keysSheet"
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    anchors.centerIn: parent
    width: Math.min(760, parent.width - 48)
    height: Math.min(sheetColumn.implicitHeight + 40, parent.height - 80)
    padding: 20
    background: Rectangle {
        color: Theme.panel; radius: 8
        border.color: Theme.line
    }

    // The window's own Ctrl+/ stands aside while a popup is open, so the
    // sheet answers it itself.
    Shortcut { sequence: "Ctrl+/"; enabled: sheet.opened; onActivated: sheet.close() }

    readonly property var groups: [
        { title: QT_TR_NOOP("TRANSPORT"), keys: [
            ["Space", QT_TR_NOOP("Play or stop")],
            [QT_TR_NOOP("Return or Home"), QT_TR_NOOP("Go to the start of the song")],
            ["Ctrl+R", QT_TR_NOOP("Arm recording")],
            ["Ctrl+M", QT_TR_NOOP("Mute the selected track")],
            ["Ctrl+L", QT_TR_NOOP("Solo the selected track")] ] },
        { title: QT_TR_NOOP("STEPS AND TRACKER"), keys: [
            [QT_TR_NOOP("Arrow keys"), QT_TR_NOOP("Move the step cursor")],
            ["Shift+Up / Down", QT_TR_NOOP("Transpose the step by a semitone")],
            ["Ctrl+Up / Down", QT_TR_NOOP("Transpose the step by an octave")],
            ["Ctrl+Left / Right", QT_TR_NOOP("Change the tracker entry octave")],
            ["Tab / Shift+Tab", QT_TR_NOOP("Next / previous tracker channel")],
            [QT_TR_NOOP("Backspace or Delete"), QT_TR_NOOP("Clear a tracker row")],
            ["Insert / Shift+Backspace", QT_TR_NOOP("Push later rows down / pull them up")],
            [QT_TR_NOOP("Alt+arrows"), QT_TR_NOOP("Nudge micro-timing and note length")],
            ["Ctrl+C / Ctrl+V", QT_TR_NOOP("Copy / paste the selected step")],
            ["Ctrl+D", QT_TR_NOOP("Duplicate the step onto the next row")],
            ["Ctrl+1..0, Ctrl+Shift+1..6", QT_TR_NOOP("Toggle a step")] ] },
        { title: QT_TR_NOOP("FILE"), keys: [
            ["Ctrl+S / Ctrl+Shift+S", QT_TR_NOOP("Save / save to a new file")],
            ["Ctrl+N / Ctrl+O", QT_TR_NOOP("New session / open a project")],
            ["Ctrl+Shift+N / Ctrl+Shift+O", QT_TR_NOOP("New window / open in a new window")],
            ["Ctrl+E", QT_TR_NOOP("Bounce the arrangement to disk")],
            ["Ctrl+Z / Ctrl+Shift+Z", QT_TR_NOOP("Undo / redo")],
            ["Ctrl+H", QT_TR_NOOP("Every step of history, branches too")] ] },
        { title: QT_TR_NOOP("WINDOW"), keys: [
            ["Ctrl+[ / Ctrl+]", QT_TR_NOOP("Bring the left / right bar out, or send it back")],
            ["Ctrl+F", QT_TR_NOOP("Search the plugin browser")],
            ["Ctrl+K", QT_TR_NOOP("Ask the assistant")],
            ["F1", QT_TR_NOOP("Start or leave the guided tour")],
            ["Ctrl+/", QT_TR_NOOP("Show or hide these keys")] ] }
    ]

    contentItem: Flickable {
        clip: true
        contentWidth: width
        contentHeight: sheetColumn.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        ColumnLayout {
            id: sheetColumn
            width: parent.width
            spacing: 14
            RowLayout {
                Layout.fillWidth: true
                SectionLabel { text: qsTr("KEYS"); color: Theme.acid }
                Item { Layout.fillWidth: true }
                Chip {
                    objectName: "closeKeysSheet"
                    text: "×"; implicitWidth: 24; implicitHeight: 22
                    accessibleLabel: qsTr("Close")
                    onClicked: sheet.close()
                }
            }
            GridLayout {
                Layout.fillWidth: true
                columns: sheet.width > 600 ? 2 : 1
                columnSpacing: 28; rowSpacing: 14
                Repeater {
                    model: sheet.groups
                    ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        spacing: 4
                        SectionLabel { text: qsTr(modelData.title) }
                        Repeater {
                            model: modelData.keys
                            RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 10
                                Label {
                                    Layout.preferredWidth: 150
                                    text: qsTr(modelData[0])
                                    color: Theme.ink; font.family: Theme.mono
                                    font.pixelSize: 11; font.bold: true
                                    wrapMode: Text.Wrap
                                }
                                Label {
                                    Layout.fillWidth: true; Layout.minimumWidth: 0
                                    text: qsTr(modelData[1])
                                    color: Theme.muted; font.pixelSize: 11
                                    wrapMode: Text.Wrap
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
