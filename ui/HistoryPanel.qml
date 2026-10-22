import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Every step of history (features/undo_history.feature): opened by Ctrl+H
// or a right-click on UNDO. The steps are listed in the order they were made,
// each named by what it changed; an edit made after an undo starts a branch,
// indented under the step it went on from, and the steps undone stay listed
// beside it. A click on any of them puts the song as it was there. The step
// the song is at is lit; steps on another branch are dimmed.
Popup {
    id: panel
    objectName: "historyPanel"
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    x: parent.width - width - 24
    y: 64
    width: Math.min(360, parent.width - 48)
    height: Math.min(520, parent.height - 120)
    padding: 12
    background: Rectangle { color: Theme.panel; radius: 8; border.color: Theme.line }

    // The window's own Ctrl+H stands aside while a popup is open.
    Shortcut { sequence: "Ctrl+H"; enabled: panel.opened; onActivated: panel.close() }

    contentItem: ColumnLayout {
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            SectionLabel { text: qsTr("HISTORY"); color: Theme.acid }
            Item { Layout.fillWidth: true }
            Chip {
                objectName: "closeHistory"
                text: "×"; implicitWidth: 24; implicitHeight: 22
                accessibleLabel: qsTr("Close")
                onClicked: panel.close()
            }
        }
        ListView {
            id: steps
            objectName: "historyList"
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true
            spacing: 1
            // Read only while open: each listing names every step again.
            model: panel.opened ? songModel.history : []
            onCountChanged: positionViewAtEnd()
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                id: entry
                required property var modelData
                required property int index
                objectName: "historyStep" + index
                width: ListView.view.width
                height: 24
                radius: 3
                color: modelData.current ? Theme.acid
                     : stepHover.hovered ? Theme.raised : "transparent"
                opacity: modelData.onPath || modelData.current ? 1 : 0.6
                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    x: 6 + entry.modelData.depth * 14
                    spacing: 6
                    // A branch's first step is marked where it leaves the line.
                    Label {
                        visible: entry.modelData.depth > 0
                        text: "└"
                        color: entry.modelData.current ? Theme.onAccent : Theme.muted
                        font.pixelSize: 11
                    }
                    Label {
                        text: entry.modelData.label
                        color: entry.modelData.current ? Theme.onAccent : Theme.ink
                        font.pixelSize: 11; font.bold: entry.modelData.current
                    }
                }
                Label {
                    anchors.right: parent.right; anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    text: entry.modelData.time
                    color: entry.modelData.current ? Theme.onAccent : Theme.muted
                    font.pixelSize: 9; font.family: Theme.mono
                }
                HoverHandler { id: stepHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: songModel.jumpToHistory(entry.modelData.id) }
            }
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("Click a step to go back or forward to it. An edit after an undo starts a branch; the steps undone stay here.")
            color: Theme.muted; font.pixelSize: 10; wrapMode: Text.Wrap
        }
    }
}
