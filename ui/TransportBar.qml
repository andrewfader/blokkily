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
    // Short of room, the bar gives up what is least needed first: the event
    // count, then the long labels, then the wordmark and the readouts'
    // captions. The controls themselves never run off the window's edge,
    // however narrow it is allowed to get (bdd_metronome sweeps the widths).
    // Each step is taken a little before the wider layout stops fitting.
    readonly property bool tight: width < 1430
    readonly property bool cramped: width < 1330
    readonly property bool narrow: width < 1230
    height: 52
    background: Rectangle {
        color: "#141519"
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
    }
    RowLayout {
        anchors.fill: parent; anchors.leftMargin: root.narrow ? 10 : 16
        anchors.rightMargin: root.narrow ? 10 : 16; spacing: root.narrow ? 4 : 7

        Label { text: "BLOKKILY"; color: Theme.acid; font.bold: true; font.letterSpacing: 2.5
            font.pixelSize: 15; visible: !root.narrow }

        Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 22; color: Theme.line
            visible: !root.narrow }

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
                    font.family: Theme.mono; font.pixelSize: 14; font.bold: true }
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
                    font.family: Theme.mono; font.pixelSize: 12; font.bold: true }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                    onClicked: appController.rewindPlayback() }
            }
        }

        // The click, the count-in and the click level (item 3.7).
        MetronomeControls { objectName: "metronomeControls" }

        Rectangle {
            Layout.preferredWidth: root.narrow ? 76 : 96; Layout.preferredHeight: 30; radius: 4
            color: Theme.raised; border.color: Theme.line
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 0
                Label { objectName: "positionReadout"; text: transport.position
                    color: transport.playing ? Theme.acid : Theme.ink
                    font.family: Theme.mono; font.pixelSize: 13; font.bold: true }
                Item { Layout.fillWidth: true }
                Label { text: "BAR"; color: Theme.muted; font.pixelSize: 8; font.letterSpacing: 1
                    visible: !root.narrow }
            }
        }

        // Tempo: dragged, scrolled, or typed. Dragging and the wheel move it
        // by whole beats per minute (a tenth with Shift held), and a
        // double-click takes a number from the keyboard.
        Rectangle {
            objectName: "tempoBox"
            Layout.preferredWidth: root.narrow ? 70 : 88; Layout.preferredHeight: 30; radius: 4
            color: Theme.raised; border.color: tempoField.visible ? Theme.acid : Theme.line
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 0
                Label { objectName: "tempoReadout"; visible: !tempoField.visible
                    text: transport.bpm.toFixed(2); color: Theme.ink
                    font.family: Theme.mono; font.pixelSize: 13; font.bold: true }
                TextField {
                    id: tempoField
                    objectName: "tempoField"
                    visible: false
                    Layout.fillWidth: true
                    padding: 0; background: Item {}
                    color: Theme.acid; font.family: Theme.mono; font.pixelSize: 13; font.bold: true
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
                Label { text: "BPM"; color: Theme.muted; font.pixelSize: 8; font.letterSpacing: 1
                    visible: !root.narrow }
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

        // The meter of the bar the playhead is in, read from the song's
        // meter map; a click offers that bar the other meters.
        Rectangle {
            objectName: "meterBox"
            Layout.preferredWidth: 58; Layout.preferredHeight: 30; radius: 4
            color: meterMouse.containsMouse ? Theme.raised : "transparent"
            border.color: Theme.line
            Label {
                objectName: "meterReadout"
                anchors.centerIn: parent
                text: transport.meter.replace("/", " / ")
                color: Theme.ink; font.family: Theme.mono; font.pixelSize: 12; font.bold: true
            }
            MouseArea {
                id: meterMouse
                anchors.fill: parent; hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    transportMeterMenu.bar = transport.bar
                    transportMeterMenu.popup(parent, 0, parent.height)
                }
            }
            MeterMenu { id: transportMeterMenu; objectName: "transportMeterMenu" }
        }

        Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 22; color: Theme.line }
        // The history chips sit as tight as the view chips do, so CLEAR
        // takes no room the bar's other controls need.
        RowLayout {
            objectName: "historyChips"
            spacing: 2
            TextMetrics { id: historyName; text: "CLEAR"
                font.pixelSize: 11; font.bold: true; font.letterSpacing: 1 }
            Chip { objectName: "undoButton"; text: "UNDO"; enabled: songModel.canUndo
                   labelPadding: 10; implicitWidth: Math.ceil(historyName.advanceWidth) + 12
                   onClicked: songModel.undo() }
            Chip { objectName: "redoButton"; text: "REDO"; enabled: songModel.canRedo
                   labelPadding: 10; implicitWidth: Math.ceil(historyName.advanceWidth) + 12
                   onClicked: songModel.redo() }
            // Empties the song so the producer can start from nothing; it is one
            // step of history, so the UNDO beside it brings everything back.
            Chip { objectName: "clearButton"; text: "CLEAR"; accent: Theme.record
                   labelPadding: 10; implicitWidth: Math.ceil(historyName.advanceWidth) + 12
                   accessibleLabel: "Clear everything"
                   onClicked: appController.clearEverything() }
        }

        Item { Layout.fillWidth: true }

        // View switcher — these actually switch the editors. Six views share
        // the bar with everything else, so the chips sit tighter than a
        // chip's usual padding.
        RowLayout {
            objectName: "viewSwitcher"
            spacing: 2
            Repeater {
                model: ["ALL", "STEP", "TRACKER", "PIANO", "KEYS", "LAUNCH", "VERSIONS", "NOTES"]
                Chip {
                    required property var modelData
                    objectName: "view" + modelData
                    readonly property var shortNames: ({
                        "ALL": "ALL", "STEP": "STEP", "TRACKER": "TRK", "PIANO": "ROLL",
                        "KEYS": "KEYS", "LAUNCH": "LNCH", "VERSIONS": "VERS", "NOTES": "NOTE"
                    })
                    readonly property var shortestNames: ({
                        "ALL": "ALL", "STEP": "STP", "TRACKER": "TRK", "PIANO": "ROL",
                        "KEYS": "KEY", "LAUNCH": "LNC", "VERSIONS": "VER", "NOTES": "NTS"
                    })
                    readonly property string shown: root.narrow ? shortestNames[modelData]
                                                  : root.cramped ? shortNames[modelData] : modelData
                    text: shown
                    accessibleLabel: modelData
                    labelPadding: root.narrow ? 4 : root.tight ? 8 : 12
                    implicitWidth: Math.ceil(viewName.advanceWidth) + labelPadding + 1
                    TextMetrics {
                        id: viewName
                        text: shown
                        font.pixelSize: 11; font.bold: true; font.letterSpacing: 1
                    }
                    on: root.view === modelData
                    onClicked: root.viewPicked(modelData)
                }
            }
        }

        Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 22; color: Theme.line }

        // The least needed readout on the bar: it elides first when the bar is
        // short of room, so the controls after it never run off the window.
        Label {
            id: eventCount
            objectName: "eventCount"
            readonly property string full: patternModel.eventCount + " EVENTS"
            TextMetrics { id: eventCountFull; font: eventCount.font; text: eventCount.full }
            // Short of room it keeps the number and drops the word.
            text: width + 0.5 >= eventCountFull.advanceWidth ? full : patternModel.eventCount
            color: Theme.muted; font.pixelSize: 11
            elide: Text.ElideRight
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.maximumWidth: eventCountFull.advanceWidth
            visible: !root.tight
        }
        Chip { objectName: "rescanButton"
               text: appController.scanning ? (root.narrow ? "…" : "SCANNING…")
                     : root.narrow ? "SCAN" : root.tight ? "RESCAN" : "RESCAN PLUGINS"
               accessibleLabel: "Rescan plugins"
               onClicked: appController.rescanPlugins() }
    }
}
