import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The insert chain being edited: the selected track's, a return's, or the
// master's (songModel.rack). Effects run top to bottom. Bypass is a live
// mixer move; adding or removing one rebuilds the graph, keeping every
// plugin already running as it was.
Rectangle {
    id: root
    objectName: "effectRack"
    readonly property var rack: songModel.rack
    readonly property int rows: rack.inserts !== undefined ? rack.inserts.length : 0
    Layout.fillWidth: true
    Layout.minimumHeight: 92
    implicitHeight: 78 + Math.max(1, rows) * 30
    radius: 6
    color: Theme.panel
    border.color: Theme.line

    ColumnLayout {
        anchors.fill: parent; anchors.margins: 9; spacing: 4

        RowLayout {
            Layout.fillWidth: true; spacing: 6
            SectionLabel { text: "INSERTS" }
            Label {
                objectName: "effectRackTitle"
                Layout.fillWidth: true
                text: root.rack.title !== undefined ? root.rack.title : ""
                color: Theme.acid; font.pixelSize: 10; font.bold: true
                font.letterSpacing: 0.8; elide: Text.ElideRight
            }
            // Turns the browser to its effects, where a click inserts here.
            Chip {
                objectName: "addEffectButton"
                text: "+ FX"; implicitHeight: 20
                on: appController.browserKind === "effect"
                onClicked: appController.setBrowserKind("effect")
            }
        }

        RowLayout {
            Layout.fillWidth: true; spacing: 4
            Chip { objectName: "recordRackOutput"; text: "RESAMPLE"; implicitHeight: 20
                on: appController.outputRecordingArmed
                onClicked: appController.toggleRackOutputRecording() }
            Chip { objectName: "bounceRackOutput"; text: "STEM"; implicitHeight: 20
                onClicked: stemMenu.open()
                Menu { id: stemMenu
                    MenuItem { text: "Bounce to new track"; onTriggered: appController.bounceRackInPlace(false) }
                    MenuItem { text: "Bounce and mute source"; onTriggered: appController.bounceRackInPlace(true) }
                }
            }
        }
        Repeater {
            model: root.rack.inserts
            Rectangle {
                id: insertRow
                required property var modelData
                required property int index
                objectName: "insertRow" + index
                Layout.fillWidth: true
                implicitHeight: 26; radius: 4
                color: modelData.bypass ? "#15161a" : Theme.raised
                border.color: Theme.line
                RowLayout {
                    anchors.fill: parent; anchors.leftMargin: 6; anchors.rightMargin: 4
                    spacing: 3
                    Rectangle {
                        Layout.preferredWidth: 32; Layout.preferredHeight: 15; radius: 3
                        color: modelData.format === "CLAP" ? Theme.acid
                             : modelData.format === "VST3" ? Theme.blue : Theme.line
                        Label { anchors.centerIn: parent; text: modelData.format
                            color: modelData.format === "CLAP" ? "#0e0f12" : Theme.ink
                            font.pixelSize: 8; font.bold: true; font.letterSpacing: 0.8 }
                    }
                    Label {
                        objectName: "insertName" + index
                        Layout.fillWidth: true
                        text: modelData.name
                        color: modelData.bypass ? Theme.muted : Theme.ink
                        font.pixelSize: 10; font.bold: true; elide: Text.ElideRight
                    }
                    Chip {
                        objectName: "insertEditor" + index
                        text: "E"; implicitHeight: 20; implicitWidth: 22
                        on: { appController.openEditors; return appController.insertEditorOpen(root.rack.kind, root.rack.bus, index) }
                        onClicked: appController.toggleInsertEditor(root.rack.kind, root.rack.bus, index)
                    }
                    Chip {
                        objectName: "insertAutomation" + index
                        text: "A"; implicitHeight: 20; implicitWidth: 22
                        onClicked: parameters.open()
                        Menu {
                            id: parameters
                            Instantiator {
                                model: appController.insertParameters(root.rack.kind, root.rack.bus, index)
                                delegate: MenuItem {
                                    required property var modelData
                                    objectName: "insertParameter" + insertRow.index + "_" + modelData.id
                                    text: modelData.name
                                    onTriggered: appController.automateInsert(root.rack.kind, root.rack.bus, insertRow.index, modelData.id)
                                }
                                onObjectAdded: function(i, item) { parameters.insertItem(i, item) }
                                onObjectRemoved: function(i, item) { parameters.removeItem(item) }
                            }
                        }
                    }
                    Chip {
                        objectName: "bypass" + index
                        text: "BYP"; implicitHeight: 20; accent: Theme.amber
                        on: modelData.bypass
                        onClicked: songModel.setInsertBypass(root.rack.kind, root.rack.bus,
                                                             index, !modelData.bypass)
                    }
                    Chip {
                        objectName: "removeInsert" + index
                        text: "×"; implicitHeight: 20
                        onClicked: songModel.removeInsert(root.rack.kind, root.rack.bus, index)
                    }
                }
            }
        }

        Label {
            objectName: "effectRackEmpty"
            visible: root.rows === 0
            Layout.fillWidth: true
            text: "No inserts. Choose + FX, then an effect."
            color: Theme.muted; font.pixelSize: 10; elide: Text.ElideRight
        }
        Item { Layout.fillHeight: true }
    }
}
