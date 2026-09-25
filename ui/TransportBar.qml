import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The bar across the top of the window: transport, tempo, undo and the
// view switcher.
ToolBar {
    id: root
    // The editors on screen, owned by the window; a chip asks for a change.
    property string view: "ALL"
    // Where the keyboard goes back to once the tempo has been typed.
    property Item focusHome: null
    signal viewPicked(string name)
    height: 52
    background: Rectangle {
        color: "#141519"
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
    }
    RowLayout {
        anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16; spacing: 14

        Label { text: "BLOKKILY"; color: Theme.acid; font.bold: true; font.letterSpacing: 2.5
            font.pixelSize: 15 }

        Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 22; color: Theme.line }

        // Transport
        RowLayout {
            spacing: 4
            Rectangle {
                objectName: "playButton"
                implicitWidth: 34; implicitHeight: 28; radius: 4
                color: transport.playing ? Theme.acid : Theme.raised
                border.color: transport.playing ? Theme.acid : Theme.line
                Label { anchors.centerIn: parent; text: transport.playing ? "||" : ">"
                    color: transport.playing ? "#0e0f12" : Theme.ink
                    font.family: "monospace"; font.pixelSize: 14; font.bold: true }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                    onClicked: appController.togglePlayback() }
            }
            // Arms recording. While the song plays, what a MIDI keyboard
            // plays is written into the pattern under the playhead.
            Rectangle {
                objectName: "recordButton"
                property bool armed: appController.recordArmed
                implicitWidth: 30; implicitHeight: 28; radius: 4
                color: armed ? Theme.record : Theme.raised
                border.color: armed ? Theme.record : Theme.line
                Rectangle { anchors.centerIn: parent; width: 10; height: 10; radius: 5
                    color: parent.armed ? "#0e0f12" : Theme.record }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                    onClicked: appController.toggleRecord() }
            }
            Rectangle {
                objectName: "rewindButton"
                implicitWidth: 30; implicitHeight: 28; radius: 4
                color: Theme.raised; border.color: Theme.line
                Label { anchors.centerIn: parent; text: "|<"; color: Theme.ink
                    font.family: "monospace"; font.pixelSize: 12; font.bold: true }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                    onClicked: appController.rewindPlayback() }
            }
        }

        Rectangle {
            Layout.preferredWidth: 96; Layout.preferredHeight: 30; radius: 4
            color: Theme.raised; border.color: Theme.line
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 0
                Label { objectName: "positionReadout"; text: transport.position
                    color: transport.playing ? Theme.acid : Theme.ink
                    font.family: "monospace"; font.pixelSize: 13; font.bold: true }
                Item { Layout.fillWidth: true }
                Label { text: "BAR"; color: Theme.muted; font.pixelSize: 8; font.letterSpacing: 1 }
            }
        }

        // Tempo: dragged, scrolled, or typed. Dragging and the wheel move it
        // by whole beats per minute (a tenth with Shift held), and a
        // double-click takes a number from the keyboard.
        Rectangle {
            objectName: "tempoBox"
            Layout.preferredWidth: 88; Layout.preferredHeight: 30; radius: 4
            color: Theme.raised; border.color: tempoField.visible ? Theme.acid : Theme.line
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 0
                Label { objectName: "tempoReadout"; visible: !tempoField.visible
                    text: transport.bpm.toFixed(2); color: Theme.ink
                    font.family: "monospace"; font.pixelSize: 13; font.bold: true }
                TextField {
                    id: tempoField
                    objectName: "tempoField"
                    visible: false
                    Layout.fillWidth: true
                    padding: 0; background: Item {}
                    color: Theme.acid; font.family: "monospace"; font.pixelSize: 13; font.bold: true
                    validator: DoubleValidator { bottom: 20; top: 300; decimals: 2 }
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    function finish() { visible = false; root.focusHome.forceActiveFocus() }
                    onAccepted: {
                        var typed = parseFloat(text)
                        if (!isNaN(typed)) appController.setTempo(typed)
                        finish()
                    }
                    onActiveFocusChanged: if (!activeFocus && visible) finish()
                    Keys.onEscapePressed: finish()
                }
                Item { Layout.fillWidth: true; visible: !tempoField.visible }
                Label { text: "BPM"; color: Theme.muted; font.pixelSize: 8; font.letterSpacing: 1 }
            }
            MouseArea {
                anchors.fill: parent; cursorShape: Qt.SizeVerCursor
                visible: !tempoField.visible
                property real anchorY: 0
                property real anchorBpm: 120
                onPressed: function(mouse) { anchorY = mouse.y; anchorBpm = transport.bpm }
                onPositionChanged: function(mouse) {
                    var fine = mouse.modifiers & Qt.ShiftModifier
                    appController.setTempo(Math.round((anchorBpm + (anchorY - mouse.y)
                                            * (fine ? 0.1 : 0.5)) * 100) / 100)
                }
                onDoubleClicked: {
                    tempoField.text = transport.bpm.toFixed(2)
                    tempoField.visible = true
                    tempoField.forceActiveFocus()
                    tempoField.selectAll()
                }
                onWheel: function(wheel) {
                    var step = (wheel.modifiers & Qt.ShiftModifier) ? 0.1 : 1.0
                    var notches = wheel.angleDelta.y / 120
                    appController.setTempo(Math.round((transport.bpm + notches * step) * 100) / 100)
                }
            }
        }

        Label { text: "4 / 4"; color: Theme.muted; font.pixelSize: 12 }

        Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 22; color: Theme.line }
        Chip { objectName: "undoButton"; text: "UNDO"; enabled: songModel.canUndo
               onClicked: songModel.undo() }
        Chip { objectName: "redoButton"; text: "REDO"; enabled: songModel.canRedo
               onClicked: songModel.redo() }

        Item { Layout.fillWidth: true }

        // View switcher — these actually switch the editors.
        RowLayout {
            objectName: "viewSwitcher"
            spacing: 4
            Repeater {
                model: ["ALL", "STEP", "TRACKER", "PIANO", "KEYS"]
                Chip {
                    required property var modelData
                    objectName: "view" + modelData
                    text: modelData
                    on: root.view === modelData
                    onClicked: root.viewPicked(modelData)
                }
            }
        }

        Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 22; color: Theme.line }

        Label { text: patternModel.eventCount + " EVENTS"; color: Theme.muted; font.pixelSize: 11 }
        Chip { text: appController.scanning ? "SCANNING…" : "RESCAN PLUGINS"
               onClicked: appController.rescanPlugins() }
    }
}
