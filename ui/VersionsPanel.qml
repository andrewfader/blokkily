import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// VERSIONS: the song saved as named versions along the way. SAVE VERSION
// keeps the song as it is now, every processor's sound included; RESTORE
// puts one back in its place as one step of history, so UNDO brings back
// the song it replaced. Versions are saved with the project, not undone with
// the song. Restoring also keeps the song it replaces as a version (unless it
// already is one), so nothing is lost when the session's undo is gone. Each
// row says how its version differs from the song now. RENAME (or a
// double-click on the name) edits the name in place. DELETE asks twice, because a deleted version cannot be
// undone.
Rectangle {
    id: root
    objectName: "versionsPanel"
    property string view: "ALL"
    property Item focusHome
    visible: view === "VERSIONS"
    Layout.fillWidth: true
    Layout.fillHeight: visible
    Layout.minimumHeight: visible ? 320 : 0
    implicitHeight: visible ? column.implicitHeight + 24 : 0
    radius: 6
    color: Theme.panel
    border.color: Theme.line
    // The row whose DELETE was pressed once, waiting for the second press.
    property int confirming: -1
    // The version whose name is being edited, or -1.
    property int renaming: -1

    function bpmText(bpm) { return bpm === Math.round(bpm) ? bpm.toFixed(0) : bpm.toFixed(2) }
    function plural(count, word) { return count + " " + word + (count === 1 ? "" : "s") }
    // What a version holds, said briefly.
    function contents(row) {
        if (!row.readable) return "cannot be read"
        return plural(row.patterns, "pattern") + " · " + plural(row.tracks, "track") + " · "
               + plural(row.notes, "note") + " · " + bpmText(row.bpm) + " BPM"
    }
    // How a version differs from the song now, in the figures both have.
    function difference(row) {
        if (!row.readable) return ""
        const now = songModel.songFigures
        var parts = []
        if (Math.abs(row.bpm - now.bpm) > 1e-6)
            parts.push(bpmText(row.bpm) + " BPM, now " + bpmText(now.bpm))
        if (row.tracks !== now.tracks) parts.push(plural(row.tracks, "track") + ", now " + now.tracks)
        if (row.patterns !== now.patterns)
            parts.push(plural(row.patterns, "pattern") + ", now " + now.patterns)
        if (row.notes !== now.notes) parts.push(plural(row.notes, "note") + ", now " + now.notes)
        return parts.length === 0 ? "same tempo, tracks, patterns and note count as now"
                                  : "differs: " + parts.join(" · ")
    }

    ColumnLayout {
        id: column
        anchors.fill: parent; anchors.margins: 12
        spacing: 10

        RowLayout {
            Layout.fillWidth: true; spacing: 8
            SectionLabel { text: "VERSIONS" }
            Label {
                text: songModel.versions.length === 0 ? "none saved yet"
                      : songModel.versions.length + " saved with this project"
                color: Theme.muted; font.pixelSize: 10
            }
            Item { Layout.fillWidth: true }
        }

        RowLayout {
            Layout.fillWidth: true; spacing: 8
            TextField {
                id: versionName
                objectName: "versionName"
                Layout.fillWidth: true
                placeholderText: "Name this version — “before the new chorus”, “mix 2”…"
                color: Theme.ink; font.pixelSize: 12
                selectByMouse: true
                Accessible.name: "Version name"
                onAccepted: saveButton.save()
                Keys.onEscapePressed: if (root.focusHome) root.focusHome.forceActiveFocus()
                background: Rectangle {
                    color: versionName.activeFocus ? Theme.bg : Theme.raised; radius: 4
                    border.color: versionName.activeFocus ? Theme.acid : "transparent"
                }
            }
            Chip {
                id: saveButton
                objectName: "saveVersion"
                text: "SAVE VERSION"
                accessibleLabel: "Save this version of the song"
                function save() {
                    if (appController.saveVersion(versionName.text)) versionName.text = ""
                    if (root.focusHome) root.focusHome.forceActiveFocus()
                }
                onClicked: save()
            }
        }

        Label {
            visible: songModel.versions.length === 0
            Layout.fillWidth: true
            text: "Save a version before a big change. Restoring one later is a single step UNDO can take back, and the song it replaces is kept as a version too."
            color: Theme.muted; font.pixelSize: 11; wrapMode: Text.WordWrap
        }

        ListView {
            id: list
            objectName: "versionList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 120
            clip: true
            spacing: 4
            model: songModel.versions
            delegate: Rectangle {
                id: row
                required property var modelData
                required property int index
                objectName: "versionRow" + index
                function startRenaming() {
                    root.confirming = -1
                    root.renaming = modelData.index
                    renameField.text = modelData.name
                    renameField.forceActiveFocus()
                    renameField.selectAll()
                }
                width: list.width
                height: 58
                radius: 4
                color: Theme.raised
                border.color: Theme.line
                RowLayout {
                    anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 8
                    spacing: 8
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 1
                        Label {
                            objectName: "versionTitle" + row.index
                            Layout.fillWidth: true
                            visible: root.renaming !== row.modelData.index
                            text: row.modelData.name
                            color: Theme.ink; font.pixelSize: 12; font.bold: true
                            elide: Text.ElideRight
                            ToolTip.visible: titleHover.hovered
                            ToolTip.delay: 600
                            ToolTip.text: "Double-click to rename"
                            HoverHandler { id: titleHover }
                            MouseArea {
                                anchors.fill: parent
                                onDoubleClicked: row.startRenaming()
                            }
                        }
                        TextField {
                            id: renameField
                            objectName: "versionRenameField" + row.index
                            Layout.fillWidth: true
                            visible: root.renaming === row.modelData.index
                            color: Theme.ink; font.pixelSize: 12; font.bold: true
                            padding: 2
                            selectByMouse: true
                            onAccepted: {
                                // The rename rebuilds this row, so everything
                                // the row holds is read and settled first.
                                const index = row.modelData.index
                                const name = text
                                root.renaming = -1
                                if (root.focusHome) root.focusHome.forceActiveFocus()
                                appController.renameVersion(index, name)
                            }
                            Keys.onEscapePressed: {
                                root.renaming = -1
                                if (root.focusHome) root.focusHome.forceActiveFocus()
                            }
                            onActiveFocusChanged: if (!activeFocus && root.renaming === row.modelData.index)
                                                      root.renaming = -1
                            background: Rectangle { color: Theme.bg; radius: 3; border.color: Theme.acid }
                        }
                        Label {
                            objectName: "versionContents" + row.index
                            Layout.fillWidth: true
                            text: row.modelData.when + "  ·  " + root.contents(row.modelData)
                            color: Theme.muted; font.pixelSize: 10; font.family: Theme.mono
                            elide: Text.ElideRight
                        }
                        Label {
                            objectName: "versionDifference" + row.index
                            Layout.fillWidth: true
                            text: root.difference(row.modelData)
                            color: text.indexOf("differs") === 0 ? Theme.amber : Theme.muted
                            font.pixelSize: 10
                            elide: Text.ElideRight
                        }
                    }
                    Chip {
                        objectName: "versionRename" + row.index
                        text: "RENAME"; implicitHeight: 22
                        accessibleLabel: "Rename " + row.modelData.name
                        onClicked: row.startRenaming()
                    }
                    Chip {
                        objectName: "restoreVersion" + row.index
                        text: "RESTORE"; accent: Theme.blue; implicitHeight: 22
                        accessibleLabel: "Restore " + row.modelData.name
                        onClicked: {
                            const index = row.modelData.index
                            root.confirming = -1
                            root.renaming = -1
                            appController.restoreVersion(index)
                        }
                    }
                    Chip {
                        objectName: "deleteVersion" + row.index
                        text: root.confirming === row.modelData.index ? "SURE?" : "DELETE"
                        accent: Theme.record; implicitHeight: 22
                        on: root.confirming === row.modelData.index
                        accessibleLabel: "Delete " + row.modelData.name
                        onClicked: {
                            if (root.confirming === row.modelData.index) {
                                const index = row.modelData.index
                                root.confirming = -1
                                appController.deleteVersion(index)
                            } else {
                                root.confirming = row.modelData.index
                            }
                        }
                    }
                }
            }
        }
    }
}
