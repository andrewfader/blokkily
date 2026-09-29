import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The song's modulators (phase 2, wave 5.1): LFOs, macros and envelope
// followers, each aimed at parameters of the selected track's instrument or
// inserts. Adding, removing or aiming one is recompiled into the running
// engine; a rate, a shape, a macro or a depth reaches it live, like a fader.
// A modulation is an offset on top of the parameter's own value, a share of
// its range: it never writes automation.
Rectangle {
    id: root
    objectName: "modulationPanel"
    Layout.fillWidth: true
    implicitHeight: panelColumn.implicitHeight + 18
    radius: 6
    color: Theme.panel
    border.color: Theme.line

    readonly property var kindColour: ({ "lfo": Theme.acid, "macro": Theme.amber,
                                         "follower": Theme.blue })

    ColumnLayout {
        id: panelColumn
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        anchors.margins: 9
        spacing: 6

        RowLayout {
            Layout.fillWidth: true; spacing: 6
            SectionLabel { text: "MODULATION" }
            Item { Layout.fillWidth: true }
            Chip { objectName: "addLfo"; text: "+ LFO"; implicitHeight: 20
                onClicked: songModel.addModulator("lfo") }
            Chip { objectName: "addMacro"; text: "+ MACRO"; implicitHeight: 20; accent: Theme.amber
                onClicked: songModel.addModulator("macro") }
            Chip { objectName: "addFollower"; text: "+ FOLLOW"; implicitHeight: 20; accent: Theme.blue
                onClicked: songModel.addModulator("follower") }
        }

        Label {
            objectName: "modulationEmpty"
            visible: songModel.modulatorCount === 0
            Layout.fillWidth: true
            text: "No modulators. Add an LFO, a macro or a follower, then aim it at a parameter."
            color: Theme.muted; font.pixelSize: 10; elide: Text.ElideRight
        }

        // Counted rather than given the rows, so a dial being dragged is not
        // rebuilt under the pointer by the move it just made.
        Repeater {
            model: songModel.modulatorCount
            Rectangle {
                id: card
                required property int index
                readonly property var row: songModel.modulators[index] !== undefined
                                           ? songModel.modulators[index] : ({})
                readonly property int targetCount: row.targets !== undefined ? row.targets.length : 0
                readonly property color accent: root.kindColour[row.kind] !== undefined
                                                ? root.kindColour[row.kind] : Theme.acid
                // What the selected track offers to aim at, read when the
                // menu opens so it lists the inserts the track has now.
                property var choices: []
                objectName: "modulator" + index
                Layout.fillWidth: true
                implicitHeight: cardColumn.implicitHeight + 14
                radius: 4
                color: Theme.raised
                border.color: Theme.line

                ColumnLayout {
                    id: cardColumn
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                    anchors.margins: 7
                    spacing: 5

                    RowLayout {
                        Layout.fillWidth: true; spacing: 5
                        Rectangle {
                            Layout.preferredWidth: 6; Layout.preferredHeight: 18; radius: 2
                            color: card.accent
                        }
                        Label {
                            objectName: "modulatorName" + card.index
                            text: card.row.name !== undefined ? card.row.name : ""
                            color: Theme.ink; font.pixelSize: 11; font.bold: true
                            font.letterSpacing: 0.8
                        }
                        Picker {
                            objectName: "modulatorShape" + card.index
                            visible: card.row.kind === "lfo"
                            implicitHeight: 22
                            choices: songModel.lfoShapes
                            value: card.row.shape !== undefined ? card.row.shape : ""
                            onPicked: name => songModel.setModulatorShape(card.index, name)
                        }
                        Picker {
                            objectName: "followerSource" + card.index
                            visible: card.row.kind === "follower"
                            implicitHeight: 22
                            choices: songModel.tracks.map(track => track.name)
                            value: card.row.sourceName !== undefined ? card.row.sourceName : ""
                            onPicked: function(name) {
                                const index = choices.indexOf(name)
                                if (index >= 0) songModel.setFollowerSource(card.index, index)
                            }
                        }
                        Item { Layout.fillWidth: true }
                        Chip {
                            objectName: "addTarget" + card.index
                            text: "+ TARGET"; implicitHeight: 20; accent: card.accent
                            enabled: card.targetCount < 8
                            onClicked: {
                                card.choices = appController.modulationTargets()
                                targetMenu.open()
                            }
                            Menu {
                                id: targetMenu
                                objectName: "targetMenu" + card.index
                                Instantiator {
                                    model: card.choices
                                    delegate: MenuItem {
                                        required property var modelData
                                        required property int index
                                        objectName: "targetChoice" + card.index + "_" + index
                                        text: modelData.label
                                        onTriggered: songModel.addModulationTarget(
                                            card.index, modelData.kind, modelData.bus,
                                            modelData.slot, modelData.parameter)
                                    }
                                    onObjectAdded: function(i, item) { targetMenu.insertItem(i, item) }
                                    onObjectRemoved: function(i, item) { targetMenu.removeItem(item) }
                                }
                            }
                        }
                        Chip {
                            objectName: "removeModulator" + card.index
                            text: "×"; implicitHeight: 20; implicitWidth: 24
                            accessibleLabel: "Remove modulator"
                            onClicked: songModel.removeModulator(card.index)
                        }
                    }

                    ValueDial {
                        objectName: "modulatorRate" + card.index
                        visible: card.row.kind === "lfo"
                        Layout.fillWidth: true
                        label: "RATE"; from: 0.05; to: 20; grip: 18
                        accent: card.accent
                        value: card.row.rateHz !== undefined ? card.row.rateHz : 1
                        readout: card.row.rateText !== undefined ? card.row.rateText : ""
                        onMoved: v => songModel.setModulatorRate(card.index, v)
                    }
                    ValueDial {
                        objectName: "macroValue" + card.index
                        visible: card.row.kind === "macro"
                        Layout.fillWidth: true
                        label: "VALUE"; from: 0; to: 1; grip: 18
                        accent: card.accent
                        value: card.row.value !== undefined ? card.row.value : 0
                        readout: card.row.valueText !== undefined ? card.row.valueText : ""
                        onMoved: v => songModel.setModulatorValue(card.index, v)
                    }

                    Label {
                        visible: card.targetCount === 0
                        Layout.fillWidth: true
                        text: "Not aimed yet: + TARGET picks a parameter of this track."
                        color: Theme.muted; font.pixelSize: 10; elide: Text.ElideRight
                    }

                    // One row per target: where it is, what it is, how much.
                    Repeater {
                        model: card.targetCount
                        RowLayout {
                            id: targetRow
                            required property int index
                            readonly property var target: card.row.targets !== undefined
                                                          && card.row.targets[index] !== undefined
                                                          ? card.row.targets[index] : ({})
                            objectName: "modTarget" + card.index + "_" + index
                            Layout.fillWidth: true
                            spacing: 5
                            ValueDial {
                                objectName: "modDepth" + card.index + "_" + targetRow.index
                                Layout.fillWidth: true
                                label: targetRow.target.place === undefined ? ""
                                       : targetRow.target.place + " · "
                                         + appController.parameterName(targetRow.target.kind,
                                                                       targetRow.target.bus,
                                                                       targetRow.target.slot,
                                                                       targetRow.target.parameter)
                                from: -1; to: 1; grip: 16
                                accent: card.accent
                                value: targetRow.target.depth !== undefined ? targetRow.target.depth : 0
                                readout: targetRow.target.depthText !== undefined
                                         ? targetRow.target.depthText : ""
                                onMoved: v => songModel.setModulationDepth(card.index, targetRow.index, v)
                            }
                            Chip {
                                objectName: "removeTarget" + card.index + "_" + targetRow.index
                                Layout.alignment: Qt.AlignBottom
                                text: "×"; implicitHeight: 20; implicitWidth: 24
                                accessibleLabel: "Remove target"
                                onClicked: songModel.removeModulationTarget(card.index, targetRow.index)
                            }
                        }
                    }
                }
            }
        }
    }
}
