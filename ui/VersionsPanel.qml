import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// VERSIONS: the song saved as named versions along the way. SAVE VERSION
// keeps the song as it is now, every processor's sound included; RESTORE
// puts one back in its place as one step of history, so UNDO brings back
// the song it replaced. Versions are saved with the project, not undone with
// the song. DELETE asks twice, because a deleted version cannot be undone.
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
            text: "Save a version before a big change. Restoring one later is a single step UNDO can take back."
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
                width: list.width
                height: 44
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
                            text: row.modelData.name
                            color: Theme.ink; font.pixelSize: 12; font.bold: true
                            elide: Text.ElideRight
                        }
                        Label {
                            Layout.fillWidth: true
                            text: row.modelData.when + "  ·  " + row.modelData.summary
                            color: Theme.muted; font.pixelSize: 10; font.family: Theme.mono
                            elide: Text.ElideRight
                        }
                    }
                    Chip {
                        objectName: "restoreVersion" + row.index
                        text: "RESTORE"; accent: Theme.blue; implicitHeight: 22
                        accessibleLabel: "Restore " + row.modelData.name
                        onClicked: {
                            root.confirming = -1
                            appController.restoreVersion(row.modelData.index)
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
                                root.confirming = -1
                                appController.deleteVersion(row.modelData.index)
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
