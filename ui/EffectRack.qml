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
    // Inserts that take a sidechain key carry a second line for it.
    readonly property int keyedRows: rack.inserts !== undefined
                                     ? rack.inserts.filter(slot => slot.keyable).length : 0
    Layout.fillWidth: true
    Layout.minimumHeight: 92
    implicitHeight: 78 + Math.max(1, rows) * 50 + keyedRows * 24
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
                implicitHeight: modelData.keyable ? 70 : 46; radius: 4
                color: modelData.bypass ? "#15161a" : Theme.raised
                border.color: Theme.line
                ColumnLayout {
                    anchors.fill: parent; anchors.leftMargin: 6; anchors.rightMargin: 4
                    anchors.topMargin: 4; anchors.bottomMargin: 4
                    spacing: 3
                    // The effect's name has a line of its own, so the chips
                    // below it never squeeze it to "Blokkil...".
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 5
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
                            font.pixelSize: 11; font.bold: true; elide: Text.ElideRight
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        Chip {
                            objectName: "insertEditor" + index
                            text: "E"; implicitHeight: 20; implicitWidth: 26
                            accessibleLabel: "Open the plugin window"
                            on: { appController.openEditors; return appController.insertEditorOpen(root.rack.kind, root.rack.bus, index) }
                            onClicked: appController.toggleInsertEditor(root.rack.kind, root.rack.bus, index)
                        }
                        Chip {
                            objectName: "insertAutomation" + index
                            text: "A"; implicitHeight: 20; implicitWidth: 26
                            accessibleLabel: "Automate a parameter"
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
                        Item { Layout.fillWidth: true }
                        Chip {
                            objectName: "bypass" + index
                            text: "BYP"; implicitHeight: 20; implicitWidth: 40; accent: Theme.amber
                            on: modelData.bypass
                            onClicked: songModel.setInsertBypass(root.rack.kind, root.rack.bus,
                                                                 index, !modelData.bypass)
                        }
                        Chip {
                            objectName: "removeInsert" + index
                            text: "×"; implicitHeight: 20; implicitWidth: 22
                            accessibleLabel: "Remove the insert"
                            onClicked: songModel.removeInsert(root.rack.kind, root.rack.bus, index)
                        }
                    }
                    // The sidechain key, on a line of its own so the row keeps
                    // room for the effect's name.
                    RowLayout {
                        Layout.fillWidth: true
                        visible: modelData.keyable === true
                        spacing: 5
                        Label {
                            text: "SIDECHAIN"; color: Theme.muted
                            font.pixelSize: 9; font.bold: true; font.letterSpacing: 1
                        }
                        // The sidechain key (wave 5.2): which track the compressor
                        // listens to, taken after that track's inserts and before
                        // its fader.
                        Chip {
                            objectName: "insertSidechain" + index
                            Layout.fillWidth: true
                            text: modelData.sidechain >= 0 ? modelData.sidechainName : "NONE"
                            implicitHeight: 20
                            accent: Theme.blue
                            accessibleLabel: "Sidechain key"
                            on: modelData.sidechain >= 0
                            onClicked: keyMenu.open()
                            Menu {
                                id: keyMenu
                                objectName: "sidechainMenu" + insertRow.index
                                MenuItem {
                                    objectName: "sidechainNone" + insertRow.index
                                    text: "No key"
                                    onTriggered: songModel.setInsertSidechain(root.rack.kind, root.rack.bus,
                                                                              insertRow.index, -1)
                                }
                                Instantiator {
                                    model: songModel.tracks
                                    delegate: MenuItem {
                                        required property var modelData
                                        required property int index
                                        objectName: "sidechainKey" + insertRow.index + "_" + index
                                        text: "Key from " + modelData.name
                                        // A track's insert cannot key from its own track.
                                        enabled: !(root.rack.kind === "track" && index === root.rack.bus)
                                        onTriggered: songModel.setInsertSidechain(root.rack.kind, root.rack.bus,
                                                                                  insertRow.index, index)
                                    }
                                    onObjectAdded: function(i, item) { keyMenu.insertItem(i + 1, item) }
                                    onObjectRemoved: function(i, item) { keyMenu.removeItem(item) }
                                }
                            }
                        }
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
