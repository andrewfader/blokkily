import QtQuick
import QtQuick.Controls
import QtQuick.Shapes
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: root
    objectName: "mainWindow"
    width: 1280; height: 800; visible: true
    // The session's name, and a dot while it holds changes that are not on disk.
    title: (songModel.dirty ? "\u2022 " : "") + appController.projectName + " \u2014 Blokkily"
    color: bg

    readonly property color bg: "#0e0f12"
    readonly property color ink: "#e8e9ed"
    readonly property color muted: "#868b98"
    readonly property color panel: "#181a1f"
    readonly property color raised: "#1f222a"
    readonly property color line: "#2a2e37"
    readonly property color acid: "#c8ff3d"
    readonly property color blue: "#4d7cff"
    readonly property color amber: "#ffb340"
    // Recording, and nothing else: a red that means the song is being written.
    readonly property color record: "#ff4d5e"

    // STEP / TRACKER / PIANO focus one editor; ALL keeps every projection on
    // screen at once, which is the point of the instrument.
    property string view: "ALL"
    // Bit depth the next bounce is written at.
    property string exportDepth: "PCM24"
    readonly property bool showTracker: view === "ALL" || view === "TRACKER"
    readonly property bool showRoll: view === "ALL" || view === "PIANO"

    // Which octave the tracker's note keys write in. A tracker is played from
    // the letter keys, and the letter keys only span two octaves, so the octave
    // they land in has to be something the producer can move.
    property int entryOctave: 3
    readonly property int entryBase: (entryOctave + 1) * 12

    function fmt2(n) { return n.toString().padStart(2, "0") }
    function keyName(key) {
        var names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
        return names[((key % 12) + 12) % 12] + (Math.floor(key / 12) - 1)
    }

    // Writes a pitch onto a step of the canonical pattern and sounds it. Every
    // editor that takes note input calls this, so the tracker, the piano roll
    // and the step grid cannot disagree about what an entered note does.
    function writeNote(step, key) {
        patternModel.setStepKey(step, key)
        appController.auditionStep(step)
    }

    // True while a text field has the keyboard. The transport and editing
    // keys stand aside then, so Return accepts what was typed, the arrows move
    // the caret or the list, and Space types a space.
    readonly property bool typing: activeFocusItem !== null
                                   && activeFocusItem.cursorPosition !== undefined

    // Save writes back to the file the session came from; a session that has
    // never been saved asks where to put it.
    function save() {
        if (!appController.saveProjectInPlace()) saveProjectDialog.open()
    }

    // Opening or starting a session throws the current one away, and there is
    // no undo across that, so unsaved work is asked about first.
    property var pendingDiscard: null
    property bool closeConfirmed: false
    function whenDiscarded(action) {
        if (!songModel.dirty) { action(); return }
        pendingDiscard = action
        discardDialog.open()
    }

    onClosing: function(close) {
        if (!songModel.dirty || closeConfirmed) return
        close.accepted = false
        whenDiscarded(function() { root.closeConfirmed = true; root.close() })
    }

    Dialog {
        id: discardDialog
        objectName: "discardDialog"
        anchors.centerIn: parent
        modal: true
        title: "Discard unsaved changes?"
        standardButtons: Dialog.Discard | Dialog.Cancel
        Label {
            text: "“" + appController.projectName + "” has changes that are not saved."
            color: ink
        }
        onDiscarded: {
            close()
            var action = root.pendingDiscard
            root.pendingDiscard = null
            if (action) action()
        }
        onRejected: root.pendingDiscard = null
    }

    // Names a pattern or a track. One field serves both, because renaming is
    // the same act whichever lane it happens in.
    Popup {
        id: renamePopup
        objectName: "renamePopup"
        property string kind: ""
        property int target: -1
        anchors.centerIn: parent
        modal: true; focus: true
        padding: 14
        background: Rectangle { color: panel; border.color: acid; radius: 6 }
        function ask(kind, target, current) {
            renamePopup.kind = kind
            renamePopup.target = target
            renameField.text = current
            open()
            renameField.forceActiveFocus()
            renameField.selectAll()
        }
        onClosed: noteEntry.forceActiveFocus()
        ColumnLayout {
            spacing: 8
            SectionLabel { text: "RENAME " + renamePopup.kind }
            TextField {
                id: renameField
                objectName: "renameField"
                Layout.preferredWidth: 220
                color: ink; font.pixelSize: 13; font.bold: true
                selectByMouse: true
                maximumLength: 24
                onAccepted: {
                    if (renamePopup.kind === "PATTERN")
                        songModel.renamePattern(renamePopup.target, text)
                    else
                        songModel.renameTrack(renamePopup.target, text)
                    renamePopup.close()
                }
                Keys.onEscapePressed: renamePopup.close()
            }
        }
    }

    Menu {
        id: patternMenu
        objectName: "patternMenu"
        property int target: -1
        property string name: ""
        MenuItem { text: "Rename…"
            onTriggered: renamePopup.ask("PATTERN", patternMenu.target, patternMenu.name) }
        MenuItem { text: "Duplicate"
            onTriggered: { songModel.selectPattern(patternMenu.target); songModel.duplicatePattern() } }
        MenuItem { text: "Clear steps"
            onTriggered: { songModel.selectPattern(patternMenu.target); songModel.clearPattern() } }
        MenuItem { text: "Delete"; enabled: songModel.patterns.length > 1
            onTriggered: songModel.deletePattern(patternMenu.target) }
    }

    Menu {
        id: trackMenu
        objectName: "trackMenu"
        property int target: -1
        property string name: ""
        MenuItem { text: "Rename…"
            onTriggered: renamePopup.ask("TRACK", trackMenu.target, trackMenu.name) }
        MenuItem { text: "Mute"; onTriggered: songModel.toggleMute(trackMenu.target) }
        MenuItem { text: "Solo"; onTriggered: songModel.toggleSolo(trackMenu.target) }
        MenuItem { text: "Delete"; enabled: songModel.trackCount > 1
            onTriggered: songModel.deleteTrack(trackMenu.target) }
    }

    FileDialog {
        id: openProjectDialog
        title: "Open Blokkily project"
        nameFilters: ["Blokkily projects (*.blok)", "All files (*)"]
        fileMode: FileDialog.OpenFile
        onAccepted: appController.loadProjectFile(selectedFile.toString())
        onRejected: noteEntry.forceActiveFocus()
    }
    FileDialog {
        id: saveProjectDialog
        title: "Save Blokkily project"
        nameFilters: ["Blokkily projects (*.blok)"]
        fileMode: FileDialog.SaveFile
        defaultSuffix: "blok"
        onAccepted: appController.saveProjectFile(selectedFile.toString())
    }

    FileDialog {
        id: exportDialog
        title: "Bounce the arrangement"
        nameFilters: ["Wave audio (*.wav)"]
        fileMode: FileDialog.SaveFile
        defaultSuffix: "wav"
        onAccepted: appController.exportAudioFile(selectedFile.toString(), root.exportDepth)
    }

    // Space plays, arrows move the step cursor, and Ctrl with the number row
    // toggles a step, so the grid can be driven without leaving the keyboard.
    // Bare digits are reserved for the tracker's upper-octave note keys.
    Shortcut { sequence: "Space"; enabled: !root.typing
               onActivated: appController.togglePlayback() }
    Shortcut { sequence: StandardKey.New
               onActivated: root.whenDiscarded(function() { appController.newProject() }) }
    Shortcut { sequence: StandardKey.Open
               onActivated: root.whenDiscarded(function() { openProjectDialog.open() }) }
    Shortcut { sequence: StandardKey.Save; onActivated: root.save() }
    Shortcut { sequence: "Ctrl+R"; enabled: !root.typing
               onActivated: appController.toggleRecord() }
    Shortcut { sequence: "Ctrl+Shift+S"; onActivated: saveProjectDialog.open() }
    Shortcut { sequence: "Ctrl+E"; onActivated: exportDialog.open() }
    // A text field keeps its own undo; everywhere else undo is the song's.
    Shortcut { sequence: StandardKey.Undo; enabled: !root.typing
               onActivated: songModel.undo() }
    Shortcut { sequences: [StandardKey.Redo, "Ctrl+Y"]; enabled: !root.typing
               onActivated: songModel.redo() }
    // Finding an instrument is a search, so it answers to the search key.
    Shortcut { sequence: StandardKey.Find
               onActivated: { pluginFilter.forceActiveFocus(); pluginFilter.selectAll() } }
    // Return and Home go back to the top of the song: the audio engine's
    // playhead and the one drawn over the editors, which are the same playhead.
    Shortcut { sequence: "Return"; enabled: !root.typing
               onActivated: appController.rewindPlayback() }
    Shortcut { sequence: "Home"; enabled: !root.typing
               onActivated: appController.rewindPlayback() }
    Shortcut { sequence: "Left"; enabled: !root.typing
        onActivated: patternModel.selectStep(Math.max(0, patternModel.selectedStep - 1)) }
    Shortcut { sequence: "Right"; enabled: !root.typing
        onActivated: patternModel.selectStep(Math.min(15, patternModel.selectedStep + 1)) }
    Shortcut { sequence: "Up"; enabled: !root.typing
               onActivated: patternModel.transposeSelected(1) }
    Shortcut { sequence: "Down"; enabled: !root.typing
               onActivated: patternModel.transposeSelected(-1) }
    Shortcut { sequence: "Ctrl+Up"; enabled: !root.typing
               onActivated: patternModel.transposeSelected(12) }
    Shortcut { sequence: "Ctrl+Down"; enabled: !root.typing
               onActivated: patternModel.transposeSelected(-12) }
    // The tracker's own octave, moved without leaving the letter keys.
    Shortcut { sequence: "Ctrl+Left"; enabled: !root.typing
        onActivated: root.entryOctave = Math.max(0, root.entryOctave - 1) }
    Shortcut { sequence: "Ctrl+Right"; enabled: !root.typing
        onActivated: root.entryOctave = Math.min(8, root.entryOctave + 1) }
    Shortcut { sequences: ["Backspace", "Delete"]; enabled: !root.typing
        onActivated: patternModel.clearStep(patternModel.selectedStep) }
    // Insert pushes later rows down; Shift+Backspace pulls them up.
    Shortcut { sequence: "Insert"; enabled: !root.typing
        onActivated: patternModel.insertStep(patternModel.selectedStep) }
    Shortcut { sequence: "Shift+Backspace"; enabled: !root.typing
        onActivated: patternModel.deleteAndShift(patternModel.selectedStep) }
    Shortcut { sequence: StandardKey.Copy; enabled: !root.typing
               onActivated: patternModel.copySelected() }
    Shortcut { sequence: StandardKey.Paste; enabled: !root.typing
               onActivated: patternModel.pasteSelected() }
    Shortcut { sequence: "Ctrl+D"; enabled: !root.typing
               onActivated: patternModel.duplicateSelected() }
    // Mute and solo the selected mixer track. Ctrl+S is Save, so solo is Ctrl+L
    // ("listen alone") rather than fighting the file shortcut.
    Shortcut { sequence: "Ctrl+M"; enabled: !root.typing
               onActivated: songModel.toggleMute(songModel.selectedTrack) }
    Shortcut { sequence: "Ctrl+L"; enabled: !root.typing
               onActivated: songModel.toggleSolo(songModel.selectedTrack) }
    // Alt nudges feel without leaving the letter keys: micro-timing sideways,
    // note length up and down by one step.
    Shortcut { sequence: "Alt+Left"; enabled: !root.typing
        onActivated: patternModel.setSelectedMicroOffset(
            (patternModel.selected.micro !== undefined ? patternModel.selected.micro : 0) - 1) }
    Shortcut { sequence: "Alt+Right"; enabled: !root.typing
        onActivated: patternModel.setSelectedMicroOffset(
            (patternModel.selected.micro !== undefined ? patternModel.selected.micro : 0) + 1) }
    Shortcut { sequence: "Alt+Down"; enabled: !root.typing
        onActivated: patternModel.setSelectedDuration(
            Math.max(30, (patternModel.selected.duration !== undefined
                          ? patternModel.selected.duration : 120) - 120)) }
    Shortcut { sequence: "Alt+Up"; enabled: !root.typing
        onActivated: patternModel.setSelectedDuration(
            Math.min(1920, (patternModel.selected.duration !== undefined
                            ? patternModel.selected.duration : 120) + 120)) }
    // Ctrl+1..0 toggles steps 0..9; Ctrl+Shift+1..6 toggles steps 10..15.
    // Digits alone stay with the tracker's note layout. Declared outright
    // rather than through a Repeater so the shortcuts actually register.
    Shortcut { sequence: "Ctrl+1"; enabled: !root.typing
        onActivated: { patternModel.toggleStep(0, root.entryBase); if (patternModel.hasStep(0)) appController.auditionStep(0) } }
    Shortcut { sequence: "Ctrl+2"; enabled: !root.typing
        onActivated: { patternModel.toggleStep(1, root.entryBase); if (patternModel.hasStep(1)) appController.auditionStep(1) } }
    Shortcut { sequence: "Ctrl+3"; enabled: !root.typing
        onActivated: { patternModel.toggleStep(2, root.entryBase); if (patternModel.hasStep(2)) appController.auditionStep(2) } }
    Shortcut { sequence: "Ctrl+4"; enabled: !root.typing
        onActivated: { patternModel.toggleStep(3, root.entryBase); if (patternModel.hasStep(3)) appController.auditionStep(3) } }
    Shortcut { sequence: "Ctrl+5"; enabled: !root.typing
        onActivated: { patternModel.toggleStep(4, root.entryBase); if (patternModel.hasStep(4)) appController.auditionStep(4) } }
    Shortcut { sequence: "Ctrl+6"; enabled: !root.typing
        onActivated: { patternModel.toggleStep(5, root.entryBase); if (patternModel.hasStep(5)) appController.auditionStep(5) } }
    Shortcut { sequence: "Ctrl+7"; enabled: !root.typing
        onActivated: { patternModel.toggleStep(6, root.entryBase); if (patternModel.hasStep(6)) appController.auditionStep(6) } }
    Shortcut { sequence: "Ctrl+8"; enabled: !root.typing
        onActivated: { patternModel.toggleStep(7, root.entryBase); if (patternModel.hasStep(7)) appController.auditionStep(7) } }
    Shortcut { sequence: "Ctrl+9"; enabled: !root.typing
        onActivated: { patternModel.toggleStep(8, root.entryBase); if (patternModel.hasStep(8)) appController.auditionStep(8) } }
    Shortcut { sequence: "Ctrl+0"; enabled: !root.typing
        onActivated: { patternModel.toggleStep(9, root.entryBase); if (patternModel.hasStep(9)) appController.auditionStep(9) } }
    Shortcut { sequence: "Ctrl+Shift+1"; enabled: !root.typing
        onActivated: { patternModel.toggleStep(10, root.entryBase); if (patternModel.hasStep(10)) appController.auditionStep(10) } }
    Shortcut { sequence: "Ctrl+Shift+2"; enabled: !root.typing
        onActivated: { patternModel.toggleStep(11, root.entryBase); if (patternModel.hasStep(11)) appController.auditionStep(11) } }
    Shortcut { sequence: "Ctrl+Shift+3"; enabled: !root.typing
        onActivated: { patternModel.toggleStep(12, root.entryBase); if (patternModel.hasStep(12)) appController.auditionStep(12) } }
    Shortcut { sequence: "Ctrl+Shift+4"; enabled: !root.typing
        onActivated: { patternModel.toggleStep(13, root.entryBase); if (patternModel.hasStep(13)) appController.auditionStep(13) } }
    Shortcut { sequence: "Ctrl+Shift+5"; enabled: !root.typing
        onActivated: { patternModel.toggleStep(14, root.entryBase); if (patternModel.hasStep(14)) appController.auditionStep(14) } }
    Shortcut { sequence: "Ctrl+Shift+6"; enabled: !root.typing
        onActivated: { patternModel.toggleStep(15, root.entryBase); if (patternModel.hasStep(15)) appController.auditionStep(15) } }

    component SectionLabel: Label {
        color: muted; font.pixelSize: 10; font.bold: true; font.letterSpacing: 1.4
    }

    component Chip: Rectangle {
        property alias text: chipText.text
        property bool on: false
        property color accent: acid
        signal clicked()
        signal rightClicked()
        signal doubleClicked()
        implicitWidth: chipText.implicitWidth + 22; implicitHeight: 26
        opacity: enabled ? 1.0 : 0.4
        radius: 4
        color: on ? accent : (chipMouse.containsMouse ? raised : "transparent")
        border.color: on ? accent : line
        Label {
            id: chipText; anchors.centerIn: parent
            color: parent.on ? "#0e0f12" : ink
            font.pixelSize: 11; font.bold: true; font.letterSpacing: 1
        }
        MouseArea {
            id: chipMouse; anchors.fill: parent; hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onClicked: function(mouse) {
                if (mouse.button === Qt.RightButton) parent.rightClicked()
                else parent.clicked()
            }
            onDoubleClicked: function(mouse) {
                if (mouse.button === Qt.LeftButton) parent.doubleClicked()
            }
        }
    }

    // One value out of a named list. The lists behind the tuning, the scale and
    // the keyboard layouts are long enough that cycling through them would be a
    // chore, so the current value opens the rest.
    component Picker: Rectangle {
        id: pickerRoot
        property var choices: []
        property string value: ""
        signal picked(string name)
        implicitWidth: pickerText.implicitWidth + 30; implicitHeight: 26
        radius: 4
        color: pickerMouse.containsMouse ? raised : "transparent"
        border.color: line
        Label {
            id: pickerText
            anchors.left: parent.left; anchors.leftMargin: 9
            anchors.verticalCenter: parent.verticalCenter
            text: pickerRoot.value; color: ink; font.pixelSize: 11; font.bold: true
        }
        Label {
            anchors.right: parent.right; anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: "\u25be"; color: muted; font.pixelSize: 10
        }
        MouseArea {
            id: pickerMouse; anchors.fill: parent; hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: pickerMenu.popup()
        }
        Menu {
            id: pickerMenu
            // Named after its picker, so a gate can prove the list is really
            // populated rather than an empty dropdown.
            objectName: pickerRoot.objectName + "Menu"
            Instantiator {
                model: pickerRoot.choices
                delegate: MenuItem {
                    required property var modelData
                    text: modelData
                    onTriggered: pickerRoot.picked(modelData)
                }
                onObjectAdded: (index, object) => pickerMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => pickerMenu.removeItem(object)
            }
        }
    }

    // A labelled horizontal slider with a numeric readout — the inspector's
    // workhorse for velocity, probability, micro-timing and lock value.
    component Dial: ColumnLayout {
        property string label: ""
        property string readout: ""
        property real value: 0
        property real from: 0
        property real to: 1
        property color accent: acid
        property bool enabled: true
        signal moved(real value)
        spacing: 3
        RowLayout {
            Layout.fillWidth: true; spacing: 6
            Label { text: label; color: muted; font.pixelSize: 10; font.letterSpacing: 1 }
            Item { Layout.fillWidth: true }
            Label { text: readout; color: enabled ? ink : muted
                font.pixelSize: 11; font.family: "monospace"; font.bold: true }
        }
        Slider {
            Layout.fillWidth: true
            enabled: parent.enabled
            // A slider keeps no keyboard focus, so the arrow keys and Space
            // stay with the editors after a fader has been dragged.
            focusPolicy: Qt.NoFocus
            from: parent.from; to: parent.to; value: parent.value
            onMoved: parent.moved(value)
            // One drag is one step of history, however far it travels.
            onPressedChanged: pressed ? songModel.beginGesture() : songModel.endGesture()
            background: Rectangle {
                x: parent.leftPadding; y: parent.topPadding + parent.availableHeight / 2 - height / 2
                width: parent.availableWidth; height: 4; radius: 2
                color: line
                Rectangle {
                    width: parent.width * parent.parent.visualPosition; height: parent.height
                    radius: 2; color: parent.parent.enabled ? accent : muted
                }
            }
            handle: Rectangle {
                x: parent.leftPadding + parent.visualPosition * (parent.availableWidth - width)
                y: parent.topPadding + parent.availableHeight / 2 - height / 2
                width: 12; height: 12; radius: 6
                color: parent.enabled ? accent : muted
                border.color: "#0e0f12"; border.width: 2
            }
        }
    }

    header: ToolBar {
        height: 52
        background: Rectangle {
            color: "#141519"
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: line }
        }
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16; spacing: 14

            Label { text: "BLOKKILY"; color: acid; font.bold: true; font.letterSpacing: 2.5
                font.pixelSize: 15 }

            Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 22; color: line }

            // Transport
            RowLayout {
                spacing: 4
                Rectangle {
                    objectName: "playButton"
                    implicitWidth: 34; implicitHeight: 28; radius: 4
                    color: transport.playing ? acid : raised
                    border.color: transport.playing ? acid : line
                    Label { anchors.centerIn: parent; text: transport.playing ? "||" : ">"
                        color: transport.playing ? "#0e0f12" : ink
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
                    color: armed ? record : raised
                    border.color: armed ? record : line
                    Rectangle { anchors.centerIn: parent; width: 10; height: 10; radius: 5
                        color: parent.armed ? "#0e0f12" : record }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                        onClicked: appController.toggleRecord() }
                }
                Rectangle {
                    objectName: "rewindButton"
                    implicitWidth: 30; implicitHeight: 28; radius: 4
                    color: raised; border.color: line
                    Label { anchors.centerIn: parent; text: "|<"; color: ink
                        font.family: "monospace"; font.pixelSize: 12; font.bold: true }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                        onClicked: appController.rewindPlayback() }
                }
            }

            Rectangle {
                Layout.preferredWidth: 96; Layout.preferredHeight: 30; radius: 4
                color: raised; border.color: line
                RowLayout {
                    anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 0
                    Label { objectName: "positionReadout"; text: transport.position
                        color: transport.playing ? acid : ink
                        font.family: "monospace"; font.pixelSize: 13; font.bold: true }
                    Item { Layout.fillWidth: true }
                    Label { text: "BAR"; color: muted; font.pixelSize: 8; font.letterSpacing: 1 }
                }
            }

            // Tempo: dragged, scrolled, or typed. Dragging and the wheel move it
            // by whole beats per minute (a tenth with Shift held), and a
            // double-click takes a number from the keyboard.
            Rectangle {
                objectName: "tempoBox"
                Layout.preferredWidth: 88; Layout.preferredHeight: 30; radius: 4
                color: raised; border.color: tempoField.visible ? acid : line
                RowLayout {
                    anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 0
                    Label { objectName: "tempoReadout"; visible: !tempoField.visible
                        text: transport.bpm.toFixed(2); color: ink
                        font.family: "monospace"; font.pixelSize: 13; font.bold: true }
                    TextField {
                        id: tempoField
                        objectName: "tempoField"
                        visible: false
                        Layout.fillWidth: true
                        padding: 0; background: Item {}
                        color: acid; font.family: "monospace"; font.pixelSize: 13; font.bold: true
                        validator: DoubleValidator { bottom: 20; top: 300; decimals: 2 }
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        function finish() { visible = false; noteEntry.forceActiveFocus() }
                        onAccepted: {
                            var typed = parseFloat(text)
                            if (!isNaN(typed)) appController.setTempo(typed)
                            finish()
                        }
                        onActiveFocusChanged: if (!activeFocus && visible) finish()
                        Keys.onEscapePressed: finish()
                    }
                    Item { Layout.fillWidth: true; visible: !tempoField.visible }
                    Label { text: "BPM"; color: muted; font.pixelSize: 8; font.letterSpacing: 1 }
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

            Label { text: "4 / 4"; color: muted; font.pixelSize: 12 }

            Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 22; color: line }
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
                        onClicked: root.view = modelData
                    }
                }
            }

            Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 22; color: line }

            Label { text: patternModel.eventCount + " EVENTS"; color: muted; font.pixelSize: 11 }
            Chip { text: appController.scanning ? "SCANNING…" : "RESCAN PLUGINS"
                   onClicked: appController.rescanPlugins() }
        }
    }

    // Note entry from the computer keyboard, laid out the way a tracker has
    // always laid it out: the bottom two rows are one octave, the top two are
    // the octave above it. A key writes onto the row the cursor is on and steps
    // the cursor down, so a phrase is typed rather than clicked. It writes to
    // the same canonical pattern every editor projects.
    Item {
        id: noteEntry
        objectName: "noteEntry"
        anchors.fill: parent
        z: -1
        focus: true

        // Semitone above the entry octave for each key of the two rows.
        readonly property var lowerRow: ({
            "Z": 0, "S": 1, "X": 2, "D": 3, "C": 4, "V": 5, "G": 6,
            "B": 7, "H": 8, "N": 9, "J": 10, "M": 11
        })
        readonly property var upperRow: ({
            "Q": 12, "2": 13, "W": 14, "3": 15, "E": 16, "R": 17, "5": 18,
            "T": 19, "6": 20, "Y": 21, "7": 22, "U": 23, "I": 24
        })

        function semitoneFor(text) {
            var glyph = text.toUpperCase()
            if (lowerRow[glyph] !== undefined) return lowerRow[glyph]
            if (upperRow[glyph] !== undefined) return upperRow[glyph]
            return -1
        }

        Keys.onPressed: function(event) {
            if (event.isAutoRepeat || (event.modifiers & (Qt.ControlModifier | Qt.AltModifier)))
                return
            var step = patternModel.selectedStep
            if (step < 0) return
            var semitone = semitoneFor(event.text)
            if (semitone < 0) return
            root.writeNote(step, root.entryBase + semitone)
            // A tracker advances after a note is typed, which is what makes it
            // faster to write a phrase in than to click one.
            patternModel.selectStep((step + 1) % 16)
            event.accepted = true
        }
    }

    SplitView {
        anchors.fill: parent

        // ---------------------------------------------------------------- rail
        Rectangle {
            SplitView.preferredWidth: 232; SplitView.minimumWidth: 200
            color: "#121317"
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 14; spacing: 10

                SectionLabel { text: "DEVICES" }
                Rectangle {
                    Layout.fillWidth: true; implicitHeight: 56; radius: 6
                    color: panel; border.color: acid
                    RowLayout {
                        anchors.fill: parent; anchors.margins: 10; spacing: 8
                        Rectangle {
                            Layout.preferredWidth: 38; Layout.preferredHeight: 30; radius: 4
                            color: appController.activeInstrument.startsWith("VST3") ? blue
                                 : appController.activeInstrument.startsWith("SF") ? amber : acid
                            Label { anchors.centerIn: parent
                                text: appController.activeInstrument === "Choose an instrument" ? "—"
                                      : appController.activeInstrument.split(" ")[0]
                                color: "#0e0f12"
                                font.pixelSize: 11; font.bold: true }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 1
                            // The selected mixer track, and what is loaded on it.
                            Label {
                                objectName: "selectedTrackName"
                                Layout.fillWidth: true
                                text: songModel.tracks[songModel.selectedTrack] !== undefined
                                      ? songModel.tracks[songModel.selectedTrack].name
                                      : "NO TRACK"
                                color: ink; font.bold: true; font.pixelSize: 10
                                font.letterSpacing: 0.8; elide: Text.ElideRight
                            }
                            Label { Layout.fillWidth: true; text: appController.soundfontStatus
                                color: muted; font.pixelSize: 10; elide: Text.ElideMiddle
                                visible: appController.activeInstrument === "Choose an instrument" }
                            Label { Layout.fillWidth: true; text: appController.activeInstrument
                                color: acid; font.pixelSize: 10; elide: Text.ElideMiddle
                                visible: appController.activeInstrument !== "Choose an instrument" }
                        }
                    }
                }

                SectionLabel { text: "MIDI IN" }
                // The keyboard being played, and what it last sent. The light
                // flashes on every key the port delivers, so a controller that
                // is not reaching the application is seen not to be.
                Rectangle {
                    objectName: "midiPanel"
                    Layout.fillWidth: true; implicitHeight: 30; radius: 4
                    color: midiMouse.containsMouse ? raised : panel
                    border.color: appController.midiPort !== "" ? acid : line
                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 9; anchors.rightMargin: 8
                        spacing: 7
                        Rectangle {
                            objectName: "midiLight"
                            implicitWidth: 8; implicitHeight: 8; radius: 4
                            color: midiFlash.running ? acid
                                 : appController.midiPort !== "" ? "#4a5a22" : line
                        }
                        Label {
                            objectName: "midiPortName"
                            Layout.fillWidth: true
                            text: appController.midiPort !== "" ? appController.midiPort
                                                                : "No MIDI input"
                            color: appController.midiPort !== "" ? ink : muted
                            font.pixelSize: 10; font.bold: true; elide: Text.ElideRight
                        }
                        Label {
                            objectName: "midiActivity"
                            visible: appController.midiPort !== ""
                            text: appController.midiActivity
                            color: muted; font.pixelSize: 10; font.family: "monospace"
                        }
                        Label { text: "\u25be"; color: muted; font.pixelSize: 10 }
                    }
                    Timer { id: midiFlash; interval: 120 }
                    Connections {
                        target: appController
                        function onMidiActivityChanged() { midiFlash.restart() }
                    }
                    MouseArea {
                        id: midiMouse; anchors.fill: parent; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        // A keyboard plugged in after launch is listed the next
                        // time the list is opened.
                        onClicked: { appController.refreshMidiPorts(); midiMenu.popup() }
                    }
                    Menu {
                        id: midiMenu
                        objectName: "midiMenu"
                        MenuItem { text: "None"; onTriggered: appController.selectMidiPort(-1) }
                        Instantiator {
                            model: appController.midiPorts
                            delegate: MenuItem {
                                required property var modelData
                                required property int index
                                text: modelData
                                checkable: true
                                checked: modelData === appController.midiPort
                                onTriggered: appController.selectMidiPort(index)
                            }
                            onObjectAdded: (index, object) => midiMenu.insertItem(index + 1, object)
                            onObjectRemoved: (index, object) => midiMenu.removeItem(object)
                        }
                    }
                }

                SectionLabel { text: "PLUGINS" }

                // An installation holds hundreds of instruments, so the browser
                // is searched by typing a few letters of one rather than by
                // scrolling past the rest. The letters need only appear in
                // order — "fbs" reaches "Fat Bass" — and the arrow keys and
                // Return move and load without leaving the field.
                Rectangle {
                    Layout.fillWidth: true; implicitHeight: 28; radius: 4
                    color: raised
                    border.color: pluginFilter.activeFocus ? acid : line
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 6
                        Label {
                            text: "⌕"; color: pluginFilter.activeFocus ? acid : muted
                            font.pixelSize: 12
                        }
                        TextField {
                            id: pluginFilter
                            objectName: "pluginFilter"
                            Layout.fillWidth: true
                            padding: 0
                            placeholderText: "Find instrument"
                            placeholderTextColor: muted
                            color: ink; font.pixelSize: 11
                            selectByMouse: true
                            background: Item {}
                            // The list is the filter's own answer, so it is
                            // rebuilt as the letters arrive and the cursor goes
                            // back to the closest match.
                            onTextChanged: {
                                appController.browserFilter = text
                                pluginBrowser.currentIndex = pluginBrowser.count > 0 ? 0 : -1
                            }
                            Keys.onDownPressed: pluginBrowser.step(1)
                            Keys.onUpPressed: pluginBrowser.step(-1)
                            Keys.onPressed: function(event) {
                                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                                    pluginBrowser.load(pluginBrowser.currentIndex)
                                    event.accepted = true
                                } else if (event.key === Qt.Key_Escape) {
                                    // A cleared field hands the keyboard back to
                                    // the tracker, so note entry resumes without
                                    // a click.
                                    if (text === "") noteEntry.forceActiveFocus()
                                    text = ""
                                    event.accepted = true
                                }
                            }
                        }
                        // How much of the installation is on screen, so a filter
                        // that hides everything says so rather than looking like
                        // an empty scan.
                        Label {
                            objectName: "pluginBrowserCount"
                            text: pluginFilter.text === ""
                                  ? appController.plugins.length
                                  : appController.browserPlugins.length + "/"
                                    + appController.plugins.length
                            color: muted; font.pixelSize: 10; font.family: "monospace"
                        }
                    }
                }

                ListView {
                    id: pluginBrowser
                    objectName: "pluginBrowser"
                    Layout.fillWidth: true; Layout.fillHeight: true
                    clip: true; spacing: 4
                    model: appController.browserPlugins
                    currentIndex: 0
                    highlightMoveDuration: 90
                    // Keeps the row the arrow keys landed on in view, which is
                    // what makes a long list navigable from the keyboard.
                    highlightRangeMode: ListView.ApplyRange
                    preferredHighlightBegin: 0
                    preferredHighlightEnd: Math.max(0, height - 44)
                    // Loads the instrument on a row of the *filtered* list: the
                    // entry carries where it sits in the full list, so what was
                    // pointed at is what is loaded.
                    function load(row) {
                        if (row < 0 || row >= count) return
                        appController.selectInstrument(model[row].source)
                    }
                    function step(delta) {
                        if (count === 0) return
                        currentIndex = Math.max(0, Math.min(count - 1, currentIndex + delta))
                    }

                    ScrollBar.vertical: ScrollBar {
                        objectName: "pluginScrollBar"
                        policy: ScrollBar.AsNeeded
                        // Drawn only when there is somewhere to scroll to, so a
                        // short list does not carry a stray bar down its side.
                        visible: size < 1.0
                        contentItem: Rectangle {
                            implicitWidth: 5; radius: 2
                            color: parent.pressed ? acid : line
                        }
                    }

                    // A filter nothing answers is a fact about the query, not
                    // about the installation.
                    Label {
                        anchors.centerIn: parent
                        width: parent.width - 16
                        visible: pluginBrowser.count === 0 && appController.plugins.length > 0
                        text: "No instrument matches “" + pluginFilter.text + "”"
                        color: muted; font.pixelSize: 11
                        horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap
                    }

                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        width: ListView.view.width; height: 40; radius: 5
                        color: ListView.isCurrentItem || rowMouse.containsMouse ? raised : panel
                        border.color: ListView.isCurrentItem ? acid : line
                        MouseArea {
                            id: rowMouse
                            anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                pluginBrowser.currentIndex = index
                                appController.selectInstrument(modelData.source)
                            }
                        }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 8
                            Rectangle {
                                Layout.preferredWidth: 38; Layout.preferredHeight: 16; radius: 3
                                color: modelData.format === "CLAP" ? acid
                                     : modelData.format === "VST3" ? blue : amber
                                Label { anchors.centerIn: parent; text: modelData.format
                                    color: modelData.format === "CLAP" ? "#0e0f12" : ink
                                    font.pixelSize: 9; font.bold: true; font.letterSpacing: 1 }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 0
                                Label { Layout.fillWidth: true; text: modelData.name; color: ink
                                    font.pixelSize: 12; elide: Text.ElideRight }
                                Label { Layout.fillWidth: true; text: modelData.vendor; color: muted
                                    font.pixelSize: 10; elide: Text.ElideMiddle }
                            }
                        }
                    }
                }

                SectionLabel { text: "PROJECT" }
                // Two rows, because four actions side by side are wider than
                // the rail, and a control row that cannot shrink widens every
                // other row of the rail with it.
                GridLayout {
                    objectName: "projectActions"
                    Layout.fillWidth: true; Layout.minimumWidth: 0
                    columns: 2; columnSpacing: 4; rowSpacing: 4
                    Chip { objectName: "newProjectButton"; text: "NEW"
                        Layout.fillWidth: true
                        onClicked: root.whenDiscarded(function() { appController.newProject() }) }
                    Chip { objectName: "openProjectButton"; text: "OPEN"
                        Layout.fillWidth: true
                        onClicked: root.whenDiscarded(function() { openProjectDialog.open() }) }
                    Chip { objectName: "saveProjectButton"; text: "SAVE"
                        Layout.fillWidth: true
                        // Lit while there is something on screen that is not
                        // on disk.
                        on: songModel.dirty
                        onClicked: root.save() }
                    Chip { objectName: "saveAsProjectButton"; text: "SAVE AS"
                        Layout.fillWidth: true
                        onClicked: saveProjectDialog.open() }
                }
                Rectangle {
                    objectName: "projectCard"
                    Layout.fillWidth: true; implicitHeight: 42; radius: 5
                    color: panel; border.color: line
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 8; spacing: 0
                        Label { Layout.fillWidth: true; text: appController.projectStatus
                            color: ink; font.pixelSize: 12; font.bold: true; elide: Text.ElideRight }
                        Label { Layout.fillWidth: true; text: appController.projectDetail
                            color: muted; font.pixelSize: 10; elide: Text.ElideRight }
                    }
                }
                SectionLabel { text: "EXPORT" }
                RowLayout {
                    Layout.fillWidth: true; spacing: 4
                    Chip {
                        objectName: "exportButton"
                        text: "BOUNCE"; accent: amber
                        onClicked: exportDialog.open()
                    }
                    Repeater {
                        model: ["PCM16", "PCM24", "FLOAT32"]
                        Chip {
                            required property var modelData
                            objectName: "exportDepth" + modelData
                            text: modelData === "FLOAT32" ? "F32" : modelData.substring(3)
                            accent: amber
                            on: root.exportDepth === modelData
                            onClicked: root.exportDepth = modelData
                        }
                    }
                }
                Label {
                    objectName: "exportStatus"
                    Layout.fillWidth: true; text: appController.exportStatus
                    color: muted; font.pixelSize: 10; wrapMode: Text.Wrap
                }

                Label { Layout.fillWidth: true; text: appController.status; color: muted
                    wrapMode: Text.Wrap; font.pixelSize: 11 }
            }
        }

        // -------------------------------------------------------------- editors
        Rectangle {
            SplitView.fillWidth: true; color: bg
            Flickable {
                id: editorScroll
                objectName: "editorScroll"
                anchors.fill: parent; anchors.margins: 14
                clip: true
                contentWidth: width
                contentHeight: Math.max(height, editorColumn.implicitHeight)
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            ColumnLayout {
                id: editorColumn
                width: editorScroll.width
                height: editorScroll.contentHeight
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true; spacing: 10
                    Label {
                        objectName: "patternTitle"
                        text: songModel.patterns[songModel.currentPattern] !== undefined
                              ? songModel.patterns[songModel.currentPattern].name : ""
                        color: ink; font.pixelSize: 16; font.bold: true
                        font.letterSpacing: 0.5
                    }
                    Rectangle {
                        implicitWidth: 44; implicitHeight: 18; radius: 3
                        color: "transparent"; border.color: line
                        Label { anchors.centerIn: parent; text: "16 ST"; color: muted
                            font.pixelSize: 9; font.letterSpacing: 0.5 }
                    }
                    Rectangle {
                        implicitWidth: 52; implicitHeight: 18; radius: 3
                        color: "transparent"; border.color: line
                        Label { anchors.centerIn: parent; text: songModel.bars + " BARS"
                            color: muted; font.pixelSize: 9; font.letterSpacing: 0.5 }
                    }
                    Item { Layout.fillWidth: true }
                    Label { text: "SPACE play  |  HOME rewind  |  INSERT push  |  ALT nudge  |  CTRL+D duplicate  |  CTRL+M mute  |  CTRL+L solo"
                        color: muted; font.pixelSize: 10 }
                }

                // ---------------------------------------------------- arrangement
                // The song timeline: one lane per mixer track, one cell per bar.
                // Clicking a cell places the pattern that is currently open, so
                // arranging and editing are the same two clicks.
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true; spacing: 6
                        SectionLabel { text: "ARRANGEMENT" }
                        Item { Layout.fillWidth: true }
                        Label { text: "shift-click lengthens  |  alt-click moves  |  right-click removes"
                            color: muted; font.pixelSize: 9 }
                        Repeater {
                            model: songModel.patterns
                            Chip {
                                required property var modelData
                                objectName: "patternChip" + modelData.index
                                text: modelData.name
                                on: modelData.current
                                onClicked: songModel.selectPattern(modelData.index)
                                onDoubleClicked: renamePopup.ask("PATTERN", modelData.index,
                                                                 modelData.name)
                                onRightClicked: {
                                    patternMenu.target = modelData.index
                                    patternMenu.name = modelData.name
                                    patternMenu.popup()
                                }
                            }
                        }
                        Chip { objectName: "addPatternButton"; text: "+PAT"; accent: blue
                            onClicked: songModel.addPattern() }
                        // A variation starts as a copy of what is open.
                        Chip { objectName: "duplicatePatternButton"; text: "DUP"; accent: blue
                            onClicked: songModel.duplicatePattern() }
                        Chip { objectName: "addTrackButton"; text: "+TRK"; accent: blue
                            onClicked: appController.addTrack() }
                    }

                    Rectangle {
                        objectName: "arrangement"
                        Layout.fillWidth: true
                        // One lane per track plus the frame, so the timeline
                        // takes only the room it needs and the editors keep
                        // the rest of the window.
                        Layout.preferredHeight: 28 + Math.max(1, songModel.trackCount) * 24
                        radius: 6; color: panel; border.color: line; clip: true

                        ColumnLayout {
                            id: arrangementRows
                            objectName: "arrangementRows"
                            anchors.fill: parent; anchors.margins: 5; spacing: 2

                            // A ruler over the bars, so the playhead is something
                            // the producer can put rather than only watch.
                            RowLayout {
                                objectName: "arrangeRuler"
                                Layout.fillWidth: true
                                Layout.preferredHeight: 14
                                spacing: 2
                                Item { Layout.preferredWidth: 64; Layout.preferredHeight: 14 }
                                Repeater {
                                    model: songModel.bars
                                    Rectangle {
                                        id: rulerCell
                                        required property int index
                                        objectName: "rulerBar" + index
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 14
                                        radius: 2
                                        color: transport.bar === index ? acid : "transparent"
                                        Label {
                                            anchors.centerIn: parent
                                            text: (rulerCell.index + 1).toString()
                                            color: transport.bar === rulerCell.index ? "#0e0f12" : muted
                                            font.pixelSize: 9; font.bold: true
                                            font.family: "monospace"
                                        }
                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                noteEntry.forceActiveFocus()
                                                appController.seekToBar(rulerCell.index)
                                            }
                                        }
                                    }
                                }
                            }

                            Repeater {
                                model: songModel.tracks
                                RowLayout {
                                    required property var modelData
                                    // Named so the bar cells below do not have
                                    // to reach through their own delegate scope.
                                    readonly property var track: modelData
                                    readonly property int trackIndex: modelData.index
                                    objectName: "arrangeRow" + modelData.index
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 22
                                    spacing: 2

                                    Rectangle {
                                        Layout.preferredWidth: 64; Layout.preferredHeight: 22
                                        radius: 3
                                        color: track.selected ? raised : "transparent"
                                        border.color: track.selected ? acid : line
                                        objectName: "trackHeader" + trackIndex
                                        Label {
                                            anchors.centerIn: parent
                                            width: parent.width - 6
                                            horizontalAlignment: Text.AlignHCenter
                                            elide: Text.ElideRight
                                            text: track.name
                                            color: track.audible ? ink : muted
                                            font.pixelSize: 9; font.bold: true
                                            font.letterSpacing: 0.6
                                        }
                                        // Click selects, double-click names,
                                        // the right button offers the rest.
                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                                            onClicked: function(mouse) {
                                                songModel.selectTrack(trackIndex)
                                                if (mouse.button !== Qt.RightButton) return
                                                trackMenu.target = trackIndex
                                                trackMenu.name = track.name
                                                trackMenu.popup()
                                            }
                                            onDoubleClicked: renamePopup.ask("TRACK", trackIndex,
                                                                             track.name)
                                        }
                                    }

                                    Repeater {
                                        model: songModel.bars
                                        Rectangle {
                                            required property int index
                                            objectName: "clip" + trackIndex + "-" + index
                                            readonly property var cell:
                                                songModel.lanes[trackIndex] !== undefined
                                                ? songModel.lanes[trackIndex][index] : undefined
                                            property bool filled: cell !== undefined && cell.filled
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 22
                                            radius: 3
                                            readonly property bool atPlayhead: transport.bar === index
                                            color: filled ? (track.audible ? blue : line)
                                                          : (index % 4 === 0 ? "#191c22" : "#15171c")
                                            border.color: atPlayhead ? acid
                                                        : filled ? blue : "#212530"
                                            border.width: atPlayhead ? 2 : 1
                                            Label {
                                                anchors.centerIn: parent
                                                visible: parent.filled
                                                // The clip is named where it
                                                // starts; its repeats carry on.
                                                text: parent.cell.start ? parent.cell.name : "·"
                                                color: "#0e0f12"
                                                font.pixelSize: 9; font.bold: true
                                                elide: Text.ElideRight
                                                width: parent.width - 6
                                                horizontalAlignment: Text.AlignHCenter
                                            }
                                            MouseArea {
                                                anchors.fill: parent
                                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                                cursorShape: Qt.PointingHandCursor
                                                // onPressed rather than onClicked: a synthetic
                                                // right-button event reaches the press handler
                                                // even when the composed click does not.
                                                onPressed: function(mouse) {
                                                    noteEntry.forceActiveFocus()
                                                    songModel.selectTrack(trackIndex)
                                                    if (mouse.button === Qt.RightButton) {
                                                        // The right button takes a clip away;
                                                        // the left button never does.
                                                        songModel.removeClip(trackIndex, index)
                                                        mouse.accepted = true
                                                        return
                                                    }
                                                    // Shift+click sets how many bars the clip
                                                    // runs: on a filled cell it ends there; on
                                                    // an empty cell it extends the earlier clip.
                                                    if (mouse.modifiers & Qt.ShiftModifier) {
                                                        if (parent.filled && parent.cell
                                                            && parent.cell.startBar >= 0) {
                                                            songModel.setClipRepeats(
                                                                trackIndex, parent.cell.startBar,
                                                                Math.max(1, index
                                                                    - parent.cell.startBar + 1))
                                                        } else {
                                                            for (var b = index - 1; b >= 0; --b) {
                                                                var earlier =
                                                                    songModel.lanes[trackIndex][b]
                                                                if (earlier !== undefined
                                                                    && earlier.filled) {
                                                                    songModel.setClipRepeats(
                                                                        trackIndex,
                                                                        earlier.startBar,
                                                                        index - earlier.startBar
                                                                            + 1)
                                                                    break
                                                                }
                                                            }
                                                        }
                                                        mouse.accepted = true
                                                        return
                                                    }
                                                    // Alt+click on an empty bar moves the clip
                                                    // the playhead is sitting in on this track.
                                                    if ((mouse.modifiers & Qt.AltModifier)
                                                        && !parent.filled) {
                                                        if (songModel.moveClip(
                                                                trackIndex, transport.bar, index)) {
                                                            mouse.accepted = true
                                                            return
                                                        }
                                                    }
                                                    if (parent.filled) {
                                                        // Opening a clip puts its pattern in
                                                        // every editor and puts the playhead
                                                        // where that clip starts sounding.
                                                        songModel.openClip(trackIndex, index)
                                                        appController.seekToBar(index)
                                                    } else {
                                                        songModel.placeClip(trackIndex, index)
                                                    }
                                                    mouse.accepted = true
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // ------------------------------------------------------ step grid
                ColumnLayout {
                    id: gridSection
                    Layout.fillWidth: true
                    Layout.fillHeight: root.view === "STEP"
                    Layout.preferredHeight: 104
                    Layout.maximumHeight: root.view === "STEP" ? 100000 : 104
                    spacing: 5
                    visible: root.view === "ALL" || root.view === "STEP"
                    RowLayout {
                        Layout.fillWidth: true
                        SectionLabel { text: "STEP GRID" }
                        Item { Layout.fillWidth: true }
                        Label { text: "click toggles  |  shift-click selects  |  CTRL-click seeks  |  CTRL+1..0 toggles"; color: muted
                            font.pixelSize: 9 }
                    }
                    RowLayout {
                        objectName: "stepGrid"
                        Layout.fillWidth: true; Layout.fillHeight: true
                        spacing: 5
                        Repeater {
                            model: 16
                            Rectangle {
                                id: cell
                                required property int index
                                readonly property var row: patternModel.steps[index]
                                readonly property bool active: row !== undefined && row.active === true
                                readonly property bool selected: row !== undefined && row.selected === true
                                readonly property bool onBeat: index % 4 === 0
                                readonly property bool playhead: transport.step === index
                                objectName: "step" + index
                                Layout.fillWidth: true; Layout.fillHeight: true
                                radius: 5
                                color: active ? acid : (onBeat ? "#262a33" : panel)
                                border.width: selected ? 2 : 1
                                border.color: selected ? amber
                                              : (playhead ? ink : (onBeat ? "#4d5462" : line))

                                // Velocity meter. A fuller cell always means a louder
                                // step: tall cells dim the headroom above the level,
                                // compact ones carry a bar along the bottom.
                                readonly property real velocity: row ? row.velocity : 0
                                readonly property bool tall: height > 130
                                Rectangle {
                                    visible: cell.active && cell.tall
                                    anchors.top: parent.top; anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.margins: 2
                                    height: Math.max(0, (parent.height - 4) * (1 - cell.velocity))
                                    radius: 4
                                    color: "#0e0f12"; opacity: 0.55
                                }
                                Rectangle {
                                    visible: cell.active && !cell.tall
                                    anchors.bottom: parent.bottom; anchors.bottomMargin: 4
                                    anchors.left: parent.left; anchors.leftMargin: 5
                                    width: (parent.width - 10) * cell.velocity
                                    height: 3; radius: 2
                                    color: "#0e0f12"; opacity: 0.55
                                }

                                Label {
                                    anchors.top: parent.top; anchors.topMargin: 6
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: root.fmt2(cell.index + 1)
                                    color: cell.active ? (cell.tall && cell.velocity < 0.92
                                                          ? ink : "#0e0f12")
                                                       : (cell.onBeat ? ink : muted)
                                    font.family: "monospace"; font.pixelSize: 11
                                    font.bold: cell.onBeat
                                }
                                Label {
                                    visible: cell.active
                                    anchors.bottom: parent.bottom; anchors.bottomMargin: 11
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: cell.row ? cell.row.noteName : ""
                                    color: "#0e0f12"; font.family: "monospace"
                                    font.pixelSize: 10; font.bold: true
                                }
                                // Lock and ratchet badges: the two things a step can
                                // carry that the note name alone does not reveal.
                                Rectangle {
                                    visible: cell.row !== undefined && cell.row.hasLock === true
                                    anchors.top: parent.top; anchors.right: parent.right
                                    anchors.margins: 4
                                    width: 6; height: 6; radius: 3; color: "#0e0f12"
                                }
                                Label {
                                    visible: cell.row !== undefined && cell.row.ratchets > 1
                                    anchors.top: parent.top; anchors.left: parent.left
                                    anchors.margins: 4
                                    text: "×" + (cell.row ? cell.row.ratchets : "")
                                    color: "#0e0f12"; font.pixelSize: 9; font.bold: true
                                }
                                // Playhead marker across the top edge.
                                Rectangle {
                                    visible: cell.playhead
                                    anchors.top: parent.top; anchors.left: parent.left
                                    anchors.right: parent.right
                                    height: 3; radius: 2; color: ink
                                }
                                MouseArea {
                                    anchors.fill: parent; hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: function(mouse) {
                                        noteEntry.forceActiveFocus()
                                        if (mouse.modifiers & Qt.ControlModifier) {
                                            // Jump the song to this column of the
                                            // bar it is already in, engine and all.
                                            appController.seekToStep(
                                                transport.bar * 16 + cell.index)
                                            patternModel.selectStep(cell.index)
                                            return
                                        }
                                        if (mouse.modifiers & Qt.ShiftModifier) {
                                            patternModel.selectStep(cell.index)
                                            return
                                        }
                                        // A new step is written in the octave the
                                        // tracker types in, and heard as it lands.
                                        patternModel.toggleStep(cell.index, root.entryBase)
                                        if (patternModel.hasStep(cell.index))
                                            appController.auditionStep(cell.index)
                                    }
                                }
                            }
                        }
                    }
                }

                // -------------------------------------------- tracker + piano roll
                SplitView {
                    // Enough height that sixteen tracker rows are readable rather
                    // than sixteen slivers.
                    Layout.fillWidth: true; Layout.fillHeight: true
                    Layout.minimumHeight: 250
                    orientation: Qt.Horizontal
                    visible: root.showTracker || root.showRoll

                    Rectangle {
                        objectName: "trackerView"
                        visible: root.showTracker
                        SplitView.fillWidth: root.view === "TRACKER"
                        SplitView.preferredWidth: 340
                        SplitView.minimumWidth: 260
                        color: panel; radius: 6; border.color: line; clip: true
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 10; spacing: 6
                            RowLayout {
                                Layout.fillWidth: true; spacing: 6
                                Label { text: "TRACKER"; color: ink; font.bold: true
                                    font.pixelSize: 12; font.letterSpacing: 1 }
                                Item { Layout.fillWidth: true }
                                // The letter keys write in this octave. Without
                                // it a typed note is a guess about where it lands.
                                Chip {
                                    objectName: "octaveDown"; text: "OCT-"
                                    onClicked: root.entryOctave = Math.max(0, root.entryOctave - 1)
                                }
                                Label {
                                    objectName: "octaveReadout"
                                    text: "O" + root.entryOctave; color: amber
                                    font.family: "monospace"; font.pixelSize: 11; font.bold: true
                                }
                                Chip {
                                    objectName: "octaveUp"; text: "OCT+"
                                    onClicked: root.entryOctave = Math.min(8, root.entryOctave + 1)
                                }
                                Label { text: "TYPE ZSXDCVGBHNJM  ·  DRAG VEL/FX"; color: muted
                                    font.pixelSize: 9; font.family: "monospace" }
                            }
                            Rectangle {
                                Layout.fillWidth: true; implicitHeight: 22; radius: 3
                                color: "#22262e"
                                RowLayout {
                                    anchors.fill: parent; anchors.leftMargin: 10
                                    anchors.rightMargin: 10; spacing: 0
                                    Label { Layout.preferredWidth: 34; text: "ROW"; color: muted
                                        font.family: "monospace"; font.pixelSize: 10 }
                                    Label { Layout.preferredWidth: 62; text: "NOTE"; color: muted
                                        font.family: "monospace"; font.pixelSize: 10 }
                                    Label { Layout.preferredWidth: 44; text: "VEL"; color: muted
                                        font.family: "monospace"; font.pixelSize: 10 }
                                    Label { Layout.fillWidth: true; text: "FX"; color: muted
                                        font.family: "monospace"; font.pixelSize: 10 }
                                }
                            }
                            // Every row is shown, empty ones included — a tracker that
                            // hides its empty rows is not a tracker.
                            ColumnLayout {
                                id: trackerRows
                                objectName: "trackerRows"
                                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
                                // One size for every row, so a height that does
                                // not divide by sixteen does not print alternate
                                // rows in alternate sizes.
                                readonly property int rowFont:
                                    Math.max(7, Math.min(13, Math.floor(height / 16) - 2))
                                Repeater {
                                    model: 16
                                    Rectangle {
                                        id: trackRow
                                        required property int index
                                        readonly property var row: patternModel.steps[index]
                                        readonly property bool playhead: transport.step === index
                                        // Sixteen rows have to fit whatever height
                                        // the panel was given, and a row that spills
                                        // into the one below it is not a tracker
                                        // anybody can read. The text follows the row
                                        // rather than the row being assumed.
                                        readonly property int rowFont: trackerRows.rowFont
                                        objectName: "trackerRow" + index
                                        clip: true
                                        Layout.fillWidth: true; Layout.fillHeight: true
                                        color: playhead ? "#33383f"
                                               : (row && row.selected ? "#2b2f38"
                                                  : (index % 4 === 0 ? "#1e2128" : "transparent"))
                                        Rectangle {
                                            visible: trackRow.row && trackRow.row.selected
                                            anchors.left: parent.left; anchors.top: parent.top
                                            anchors.bottom: parent.bottom
                                            width: 2; color: amber
                                        }
                                        RowLayout {
                                            anchors.fill: parent; anchors.leftMargin: 10
                                            anchors.rightMargin: 10; spacing: 0
                                            Label {
                                                Layout.preferredWidth: 34
                                                text: trackRow.index.toString(16).toUpperCase().padStart(2, "0")
                                                color: trackRow.index % 4 === 0 ? ink : muted
                                                font.family: "monospace"
                                                font.pixelSize: trackRow.rowFont
                                            }
                                            // The note column is where a tracker
                                            // is written, so it takes input: a
                                            // click puts the entry octave's root
                                            // on the row, a drag moves the pitch
                                            // by semitones, and a right-click
                                            // empties the row.
                                            Item {
                                                objectName: "trackerNote" + trackRow.index
                                                Layout.preferredWidth: 62
                                                Layout.fillHeight: true
                                                Label {
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    text: trackRow.row ? trackRow.row.noteName : "---"
                                                    color: trackRow.row && trackRow.row.active ? acid : "#3d434e"
                                                    font.family: "monospace"
                                                    font.pixelSize: trackRow.rowFont
                                                    font.bold: trackRow.row && trackRow.row.active
                                                }
                                                MouseArea {
                                                    anchors.fill: parent
                                                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                                                    cursorShape: Qt.PointingHandCursor
                                                    property real anchorY: 0
                                                    property int anchorKey: 60
                                                    onPressed: function(mouse) {
                                                        noteEntry.forceActiveFocus()
                                                        patternModel.selectStep(trackRow.index)
                                                        songModel.beginGesture()
                                                        if (mouse.button === Qt.RightButton) {
                                                            patternModel.clearStep(trackRow.index)
                                                            return
                                                        }
                                                        anchorY = mouse.y
                                                        anchorKey = trackRow.row && trackRow.row.active
                                                                    ? trackRow.row.key : root.entryBase
                                                        root.writeNote(trackRow.index, anchorKey)
                                                    }
                                                    onPositionChanged: function(mouse) {
                                                        if (!pressed || mouse.buttons !== Qt.LeftButton)
                                                            return
                                                        var moved = Math.round((anchorY - mouse.y) / 6)
                                                        patternModel.setStepKey(trackRow.index,
                                                                                anchorKey + moved)
                                                    }
                                                    onReleased: songModel.endGesture()
                                                    onCanceled: songModel.endGesture()
                                                }
                                            }
                                            Item {
                                                objectName: "trackerVel" + trackRow.index
                                                Layout.preferredWidth: 44
                                                Layout.fillHeight: true
                                                Label {
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    text: trackRow.row ? trackRow.row.velocityHex : "--"
                                                    color: trackRow.row && trackRow.row.active ? ink : "#3d434e"
                                                    font.family: "monospace"
                                                    font.pixelSize: trackRow.rowFont
                                                }
                                                MouseArea {
                                                    anchors.fill: parent
                                                    cursorShape: Qt.SizeVerCursor
                                                    property real anchorY: 0
                                                    property real anchorVel: 0.9
                                                    onPressed: function(mouse) {
                                                        noteEntry.forceActiveFocus()
                                                        patternModel.selectStep(trackRow.index)
                                                        if (!trackRow.row || !trackRow.row.active) return
                                                        songModel.beginGesture()
                                                        anchorY = mouse.y
                                                        anchorVel = trackRow.row.velocity
                                                    }
                                                    onPositionChanged: function(mouse) {
                                                        if (!pressed || !trackRow.row || !trackRow.row.active)
                                                            return
                                                        var next = anchorVel + (anchorY - mouse.y) / 40
                                                        patternModel.setSelectedVelocity(
                                                            Math.max(0, Math.min(1, next)))
                                                    }
                                                    onReleased: songModel.endGesture()
                                                    onCanceled: songModel.endGesture()
                                                }
                                            }
                                            Item {
                                                objectName: "trackerFx" + trackRow.index
                                                Layout.fillWidth: true
                                                Layout.fillHeight: true
                                                Label {
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    text: trackRow.row ? trackRow.row.lockText : "---"
                                                    color: trackRow.row && trackRow.row.hasLock
                                                           ? amber : "#3d434e"
                                                    font.family: "monospace"
                                                    font.pixelSize: trackRow.rowFont
                                                    font.bold: trackRow.row && trackRow.row.hasLock
                                                }
                                                MouseArea {
                                                    anchors.fill: parent
                                                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                                                    cursorShape: Qt.SizeVerCursor
                                                    property real anchorY: 0
                                                    property real anchorValue: 0.75
                                                    onPressed: function(mouse) {
                                                        noteEntry.forceActiveFocus()
                                                        patternModel.selectStep(trackRow.index)
                                                        if (!trackRow.row || !trackRow.row.active)
                                                            return
                                                        if (mouse.button === Qt.RightButton) {
                                                            patternModel.clearSelectedLock()
                                                            return
                                                        }
                                                        songModel.beginGesture()
                                                        anchorY = mouse.y
                                                        if (!trackRow.row.hasLock)
                                                            patternModel.setSelectedLock(
                                                                0, 0.75, false)
                                                        anchorValue = patternModel.selected.lockValue
                                                                      !== undefined
                                                            ? patternModel.selected.lockValue
                                                            : 0.75
                                                    }
                                                    onPositionChanged: function(mouse) {
                                                        if (!pressed || !trackRow.row
                                                            || !trackRow.row.active) return
                                                        if (!(mouse.buttons & Qt.LeftButton)) return
                                                        var next = anchorValue
                                                                   + (anchorY - mouse.y) / 40
                                                        patternModel.setSelectedLock(
                                                            0, Math.max(0, Math.min(1, next)),
                                                            false)
                                                    }
                                                    onReleased: songModel.endGesture()
                                                    onCanceled: songModel.endGesture()
                                                }
                                            }
                                        }
                                        MouseArea {
                                            anchors.fill: parent
                                            z: -1
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                noteEntry.forceActiveFocus()
                                                patternModel.selectStep(trackRow.index)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Rectangle {
                        objectName: "pianoRollView"
                        visible: root.showRoll
                        SplitView.fillWidth: true
                        color: panel; radius: 6; border.color: line; clip: true
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 10; spacing: 6
                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: "PIANO ROLL"; color: ink; font.bold: true
                                    font.pixelSize: 12; font.letterSpacing: 1 }
                                Item { Layout.fillWidth: true }
                                Label { text: "drag draws  |  drag a note to move  |  drag its end to lengthen  |  right-click erases"; color: muted
                                    font.pixelSize: 9 }
                                Label {
                                    text: patternModel.lowKey + "-" + patternModel.highKey + " KEYS"
                                    color: muted; font.pixelSize: 9; font.family: "monospace"
                                }
                            }
                            Item {
                                id: rollArea
                                Layout.fillWidth: true; Layout.fillHeight: true
                                readonly property int lowKey: patternModel.lowKey
                                readonly property int keyCount:
                                    Math.max(1, patternModel.highKey - patternModel.lowKey + 1)
                                readonly property real laneHeight: height / keyCount
                                readonly property real gutter: 42
                                readonly property real laneWidth: (width - gutter) / 16
                                function isBlack(key) {
                                    var pc = key % 12
                                    return pc === 1 || pc === 3 || pc === 6 || pc === 8 || pc === 10
                                }
                                // Highest pitch on top, so the roll reads like a keyboard.
                                function laneY(key) {
                                    return (patternModel.highKey - key) * laneHeight
                                }

                                // Keyboard gutter: without it a block's pitch is a guess.
                                Repeater {
                                    model: rollArea.keyCount
                                    Rectangle {
                                        required property int index
                                        readonly property int key: patternModel.highKey - index
                                        objectName: "rollKey" + key
                                        x: 0; y: index * rollArea.laneHeight
                                        width: rollArea.gutter; height: rollArea.laneHeight
                                        color: rollArea.isBlack(key) ? "#111318" : "#39404b"
                                        Rectangle { anchors.bottom: parent.bottom
                                            width: parent.width; height: 1; color: "#0c0e11" }
                                        Label {
                                            visible: key % 12 === 0 && rollArea.laneHeight > 8
                                            anchors.right: parent.right; anchors.rightMargin: 5
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: "C" + (key / 12 - 1)
                                            color: ink; font.pixelSize: 9; font.family: "monospace"
                                            font.bold: true
                                        }
                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onPressed: {
                                                noteEntry.forceActiveFocus()
                                                appController.auditionKey(key, true)
                                            }
                                            onReleased: appController.releaseAudition()
                                            onCanceled: appController.releaseAudition()
                                        }
                                    }
                                }
                                // Pitch lanes, banded like keys so octaves are readable.
                                Repeater {
                                    model: rollArea.keyCount
                                    Rectangle {
                                        required property int index
                                        readonly property int key: patternModel.highKey - index
                                        x: rollArea.gutter; y: index * rollArea.laneHeight
                                        width: rollArea.width - rollArea.gutter
                                        height: rollArea.laneHeight
                                        color: rollArea.isBlack(key) ? "#15171c" : "transparent"
                                        Rectangle {
                                            visible: key % 12 === 0
                                            anchors.bottom: parent.bottom
                                            width: parent.width; height: 1; color: "#31363f"
                                        }
                                    }
                                }
                                Rectangle {
                                    x: rollArea.gutter - 1; y: 0
                                    width: 1; height: rollArea.height; color: "#0c0e11"
                                }
                                // Bar lines every four steps.
                                Repeater {
                                    model: 17
                                    Rectangle {
                                        required property int index
                                        x: rollArea.gutter + index * rollArea.laneWidth
                                        y: 0; width: 1; height: rollArea.height
                                        color: index % 4 === 0 ? "#3b414c" : "#252932"
                                    }
                                }
                                // Notes, drawn on the lane the gutter labels.
                                // A chord is drawn as every voice it sounds.
                                Repeater {
                                    model: patternModel
                                    Item {
                                        id: rollNote
                                        required property int step
                                        required property int duration
                                        required property var voiceKeys
                                        objectName: "rollNote" + step
                                        x: rollArea.gutter + step * rollArea.laneWidth + 1
                                        width: Math.max(6, duration / 120 * rollArea.laneWidth - 2)
                                        height: rollArea.height
                                        Repeater {
                                            model: rollNote.voiceKeys
                                            Rectangle {
                                                required property var modelData
                                                y: rollArea.laneY(modelData)
                                                width: rollNote.width
                                                height: Math.max(3, rollArea.laneHeight - 1)
                                                radius: 2
                                                color: patternModel.selectedStep === rollNote.step
                                                       ? amber : acid
                                            }
                                        }
                                    }
                                }
                                // Playhead across the roll.
                                Rectangle {
                                    objectName: "rollPlayhead"
                                    // The roll shows one bar, so the song's
                                    // playhead is drawn where it falls in it.
                                    x: rollArea.gutter + (transport.stepFraction % 16) * rollArea.laneWidth
                                    y: 0; width: 2; height: rollArea.height
                                    color: ink; opacity: 0.85
                                }

                                // The roll is an editor, not a picture of one.
                                // A press writes the lane it landed on onto the
                                // step it landed on, dragging keeps writing as
                                // the pointer moves, and the right button
                                // erases. Everything goes through the canonical
                                // pattern, so the grid, the tracker and the
                                // inspector follow the same stroke.
                                MouseArea {
                                    id: rollInput
                                    objectName: "rollInput"
                                    x: rollArea.gutter; y: 0
                                    width: rollArea.width - rollArea.gutter
                                    height: rollArea.height
                                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                                    hoverEnabled: true
                                    cursorShape: Qt.CrossCursor
                                    property int lastStep: -1
                                    property int lastKey: -1
                                    // The lane the pointer is over, so the roll
                                    // can show which pitch a press would write.
                                    property int hoverKey: keyAt(mouseY)
                                    // 0 draws a phrase, 1 moves a note, 2 lengthens it.
                                    property int dragMode: 0
                                    property int dragStep: -1
                                    property int grabKey: 0

                                    function stepAt(px) {
                                        return Math.max(0, Math.min(15,
                                            Math.floor(px / rollArea.laneWidth)))
                                    }
                                    function keyAt(py) {
                                        return Math.max(0, Math.min(127, patternModel.highKey -
                                            Math.floor(py / rollArea.laneHeight)))
                                    }
                                    function coveringNote(px, py) {
                                        var key = keyAt(py)
                                        var xStep = px / rollArea.laneWidth
                                        for (var i = 0; i < 16; ++i) {
                                            if (!patternModel.hasStep(i)) continue
                                            var dur = patternModel.stepDuration(i)
                                            if (dur <= 0) dur = 96
                                            var end = i + dur / 120
                                            if (xStep < i || xStep >= Math.max(end, i + 1)) continue
                                            if (patternModel.stepKey(i) === key) return i
                                            var row = patternModel.steps[i]
                                            var voices = row && row.voiceKeys ? row.voiceKeys : []
                                            for (var v = 0; v < voices.length; ++v)
                                                if (Number(voices[v]) === key) return i
                                        }
                                        return -1
                                    }
                                    function paint(mouse) {
                                        var step = stepAt(mouse.x)
                                        var key = keyAt(mouse.y)
                                        if (step === lastStep && key === lastKey) return
                                        lastStep = step
                                        lastKey = key
                                        if (mouse.buttons & Qt.RightButton)
                                            patternModel.clearStep(step)
                                        else
                                            root.writeNote(step, key)
                                    }
                                    onPressed: function(mouse) {
                                        noteEntry.forceActiveFocus()
                                        // A stroke is one edit, so one undo
                                        // takes the whole phrase back.
                                        songModel.beginGesture()
                                        lastStep = -1
                                        lastKey = -1
                                        dragMode = 0
                                        dragStep = -1
                                        var key = keyAt(mouse.y)
                                        var hit = coveringNote(mouse.x, mouse.y)
                                        if (hit < 0) {
                                            var under = stepAt(mouse.x)
                                            if (patternModel.hasStep(under) &&
                                                patternModel.stepKey(under) === key)
                                                hit = under
                                        }
                                        if (mouse.button === Qt.RightButton) {
                                            if (hit >= 0) patternModel.clearStep(hit)
                                            else patternModel.clearStep(stepAt(mouse.x))
                                            return
                                        }
                                        if (hit >= 0) {
                                            var dur = patternModel.stepDuration(hit)
                                            if (dur <= 0) dur = 96
                                            var end = hit + dur / 120
                                            var xStep = mouse.x / rollArea.laneWidth
                                            patternModel.selectStep(hit)
                                            grabKey = key
                                            dragStep = hit
                                            var handle = Math.max(end - 0.25, hit + 0.55)
                                            dragMode = xStep >= handle ? 2 : 1
                                            return
                                        }
                                        paint(mouse)
                                    }
                                    onPositionChanged: function(mouse) {
                                        if (!pressed) return
                                        if (dragMode === 2 && dragStep >= 0) {
                                            var ticks = Math.round(
                                                (mouse.x / rollArea.laneWidth - dragStep) * 120)
                                            patternModel.setStepDuration(dragStep, ticks)
                                            return
                                        }
                                        if (dragMode === 1 && dragStep >= 0) {
                                            var step = stepAt(mouse.x)
                                            var key = keyAt(mouse.y)
                                            var delta = key - grabKey
                                            if (step === dragStep && delta === 0) return
                                            patternModel.relocateStep(dragStep, step, delta)
                                            dragStep = step
                                            grabKey = key
                                            return
                                        }
                                        paint(mouse)
                                    }
                                    onReleased: {
                                        lastStep = -1; lastKey = -1
                                        dragMode = 0; dragStep = -1
                                        songModel.endGesture()
                                    }
                                    onCanceled: {
                                        lastStep = -1; lastKey = -1
                                        dragMode = 0; dragStep = -1
                                        songModel.endGesture()
                                    }
                                }

                                // Where a press would land, so the pitch under
                                // the pointer is known before it is written.
                                Rectangle {
                                    visible: rollInput.containsMouse
                                    x: rollArea.gutter
                                    y: rollArea.laneY(rollInput.hoverKey)
                                    width: rollArea.width - rollArea.gutter
                                    height: rollArea.laneHeight
                                    color: amber; opacity: 0.10
                                }
                            }
                        }
                    }
                }

                // ---------------------------------------------------- keyboards
                // One playable surface over the song's own tuning and scale.
                // Every surface produces the same kind of cell, so the piano,
                // the isomorphic grid, the fretboard and the chord pads are one
                // renderer laid out four ways rather than four editors.
                Rectangle {
                    id: keyboardPanel
                    objectName: "keyboardPanel"
                    // A surface with many rows — a fretboard, or anything
                    // turned on its side — needs more of the window than a
                    // single strip of piano keys. The panel asks for what the
                    // laid-out surface needs, up to a share of the window; past
                    // that the surface scrolls rather than shrinking its keys
                    // below the size of a fingertip.
                    // Piano keys may be narrow; grid and chord cells need room
                    // for a readable pitch name and a usable pointer target.
                    // Both orientations scroll once those dimensions are met.
                    readonly property real keyExtent: keyboardModel.surface === "PIANO" ? 14 : 60
                    readonly property real wantedSurface:
                        Math.min(360, keyboardModel.spanY * keyExtent)
                    readonly property real wantedHeight: Math.max(168, wantedSurface + 104)
                    Layout.fillWidth: true
                    Layout.fillHeight: root.view === "KEYS"
                    Layout.preferredHeight: wantedHeight
                    Layout.maximumHeight: root.view === "KEYS" ? 100000 : wantedHeight
                    Layout.minimumHeight: 140
                    visible: root.view === "ALL" || root.view === "KEYS"
                    radius: 6; color: panel; border.color: line; clip: true

                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 10; spacing: 7

                        // The song's tuning, its scale, and the key it is in.
                        // These belong to the session, not to the keyboard, so
                        // changing one re-reads every editor at once.
                        RowLayout {
                            objectName: "tuningBar"
                            Layout.fillWidth: true; Layout.minimumWidth: 0
                            clip: true
                            spacing: 6
                            SectionLabel { text: "TUNING" }
                            Picker {
                                objectName: "tuningPicker"
                                choices: songModel.tuningNames
                                value: songModel.tuningName
                                onPicked: name => songModel.setTuning(name)
                            }
                            Label { objectName: "divisionReadout"
                                text: songModel.divisions + " STEPS"; color: muted
                                font.pixelSize: 10; font.family: "monospace" }
                            SectionLabel { text: "SCALE" }
                            Picker {
                                objectName: "scalePicker"
                                choices: songModel.scaleNames
                                value: songModel.scaleName
                                onPicked: name => songModel.setScale(name)
                            }
                            SectionLabel { text: "ROOT" }
                            Chip {
                                objectName: "rootDown"; text: "<"
                                onClicked: songModel.setRootDegree(songModel.rootDegree - 1)
                            }
                            Label { objectName: "rootReadout"; text: songModel.rootName
                                color: acid; font.pixelSize: 11; font.bold: true
                                font.family: "monospace" }
                            Chip {
                                objectName: "rootUp"; text: ">"
                                onClicked: songModel.setRootDegree(songModel.rootDegree + 1)
                            }
                            Chip {
                                objectName: "autoScaleChip"; text: "AUTO-SCALE"; accent: blue
                                on: songModel.autoScale
                                onClicked: songModel.toggleAutoScale()
                            }
                            Item { Layout.fillWidth: true }
                            Label { objectName: "lastPlayed"
                                text: "PLAYED " + keyboardModel.lastPlayed
                                color: ink; font.pixelSize: 11; font.family: "monospace" }
                        }

                        // Which surface is under the hands, and the one setting
                        // that surface has of its own.
                        RowLayout {
                            objectName: "surfaceBar"
                            // Its own contents may not set the panel's width:
                            // a long layout name would otherwise push the
                            // surface beside it out past the panel's edge.
                            Layout.fillWidth: true; Layout.minimumWidth: 0
                            clip: true
                            spacing: 6
                            SectionLabel { text: "SURFACE" }
                            Repeater {
                                model: keyboardModel.surfaces
                                Chip {
                                    required property var modelData
                                    objectName: "surface" + modelData
                                    text: modelData
                                    on: keyboardModel.surface === modelData
                                    onClicked: keyboardModel.setSurface(modelData)
                                }
                            }
                            Picker {
                                objectName: "layoutPicker"
                                visible: keyboardModel.surface === "GRID"
                                choices: keyboardModel.layoutNames
                                value: keyboardModel.layoutName
                                onPicked: name => keyboardModel.setLayout(name)
                            }
                            Picker {
                                objectName: "stringPicker"
                                visible: keyboardModel.surface === "FRETS"
                                choices: keyboardModel.stringTuningNames
                                value: keyboardModel.stringTuningName
                                onPicked: name => keyboardModel.setStringTuning(name)
                            }
                            Picker {
                                objectName: "registerPicker"
                                visible: keyboardModel.surface === "PIANO"
                                           || keyboardModel.surface === "GRID"
                                choices: keyboardModel.registerNames
                                value: keyboardModel.registerName
                                onPicked: name => keyboardModel.setRegister(name)
                            }
                            // One chip rather than two: the bar has to fit
                            // beside the surface's own setting, and a control
                            // row that cannot shrink drags the keyboard out
                            // past the panel that is meant to hold it.
                            Chip {
                                objectName: "orientToggle"
                                text: "RUNS " + keyboardModel.orientation
                                accent: blue
                                on: keyboardModel.orientation === "DOWN"
                                onClicked: keyboardModel.toggleOrientation()
                            }
                            Item { Layout.fillWidth: true }
                            Chip {
                                objectName: "keyboardRecord"
                                text: keyboardModel.recording ? "WRITING STEP " +
                                          root.fmt2(patternModel.selectedStep + 1) : "AUDITION ONLY"
                                accent: amber
                                on: keyboardModel.recording
                                onClicked: keyboardModel.toggleRecording()
                            }
                        }

                        // The cells themselves. Row and column come from the
                        // model, so nothing here has to know which surface it
                        // is drawing.
                        Flickable {
                            id: keyboardScroll
                            objectName: "keyboardScroll"
                            Layout.fillWidth: true; Layout.fillHeight: true
                            clip: true
                            // Composite the viewport before placing it in the
                            // window. This also bounds Shape painting on Qt's
                            // software renderer when the surface scrolls.
                            layer.enabled: true
                            boundsBehavior: Flickable.StopAtBounds
                            // A surface that fits is laid out to fill. One that
                            // does not scrolls, because a key too small to hit
                            // is not a key.
                            contentWidth: Math.max(width, keyboardModel.spanX *
                                keyboardPanel.keyExtent * keyboardSurface.cellAspect)
                            contentHeight: Math.max(height,
                                keyboardModel.spanY * keyboardPanel.keyExtent)
                            ScrollBar.vertical: ScrollBar {
                                objectName: "keyboardScrollBar"
                                policy: ScrollBar.AsNeeded
                            }
                            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

                        Item {
                            id: keyboardSurface
                            objectName: "keyboardSurface"
                            width: keyboardScroll.contentWidth
                            height: keyboardScroll.contentHeight
                            // The model says how much room the layout needs, in
                            // cell widths and heights. Staggered rows are half a
                            // cell wider than their contents and hexagonal rows
                            // overlap, so this is measured rather than counted.
                            readonly property real spanX: Math.max(0.001, keyboardModel.spanX)
                            readonly property real spanY: Math.max(0.001, keyboardModel.spanY)
                            readonly property bool hex: keyboardModel.cellShape === "HEX"
                            // A honeycomb turns with the surface: rows of
                            // pointy-top cells become columns of flat-top ones.
                            readonly property bool flatTop:
                                keyboardModel.orientation === "DOWN"
                            // A hexagon only reads as one, and only tiles as
                            // one, while it stays close to regular. So a
                            // honeycomb keeps the width its height asks for and
                            // sits centred in whatever room is left, where a
                            // square grid still stretches to fill the way keys
                            // always have.
                            readonly property real cellAspect:
                                !hex ? 1.0 : (flatTop ? 2.0 / Math.sqrt(3.0)
                                                      : Math.sqrt(3.0) / 2.0)
                            readonly property real cellHeight:
                                hex ? Math.min(height / spanY, width / spanX / cellAspect)
                                    : height / spanY
                            readonly property real cellWidth:
                                hex ? cellHeight * cellAspect : width / spanX
                            readonly property real originX: (width - cellWidth * spanX) / 2
                            readonly property real originY: (height - cellHeight * spanY) / 2

                            // The six corners of a cell, in its own pixels.
                            function hexCorners(w, h) {
                                if (flatTop)
                                    return [Qt.point(0.25 * w, 0), Qt.point(0.75 * w, 0),
                                            Qt.point(w, 0.5 * h), Qt.point(0.75 * w, h),
                                            Qt.point(0.25 * w, h), Qt.point(0, 0.5 * h),
                                            Qt.point(0.25 * w, 0)]
                                return [Qt.point(0.5 * w, 0), Qt.point(w, 0.25 * h),
                                        Qt.point(w, 0.75 * h), Qt.point(0.5 * w, h),
                                        Qt.point(0, 0.75 * h), Qt.point(0, 0.25 * h),
                                        Qt.point(0.5 * w, 0)]
                            }

                            // Hexagons interlock, so their rectangles overlap at
                            // the corners. A press there belongs to the
                            // neighbour it is drawn inside, not to whichever
                            // cell happens to be painted on top.
                            function insideCell(nx, ny) {
                                if (!hex) return true
                                var dx = Math.abs(nx - 0.5), dy = Math.abs(ny - 0.5)
                                return flatTop ? (2 * dx + dy <= 1) : (2 * dy + dx <= 1)
                            }

                            Repeater {
                                model: keyboardModel.cells
                                Item {
                                    id: cellItem
                                    required property var modelData
                                    objectName: "keyboardCell" + modelData.index
                                    x: keyboardSurface.originX
                                       + modelData.x * keyboardSurface.cellWidth
                                    y: keyboardSurface.originY
                                       + modelData.y * keyboardSurface.cellHeight
                                    width: keyboardSurface.cellWidth - 2
                                    height: keyboardSurface.cellHeight - 2
                                    // A key says three things at once: whether
                                    // it is raised, whether the scale holds it,
                                    // and whether it is the root of that scale.
                                    readonly property color fill: keyInput.pressed ? amber
                                           : modelData.root ? acid
                                           : (modelData.accidental ? "#101218"
                                              : (modelData.inScale ? "#2d323d" : raised))
                                    readonly property color edge:
                                        modelData.root ? acid
                                        : (modelData.inScale ? "#3a4150" : "#333947")
                                    opacity: modelData.inScale || modelData.accidental
                                             ? 1.0 : (keyboardSurface.hex ? 0.8 : 0.55)

                                    Rectangle {
                                        objectName: "rectTile"
                                        anchors.fill: parent
                                        visible: !keyboardSurface.hex
                                        radius: 3
                                        color: cellItem.fill
                                        border.width: modelData.inScale ? 1 : 0
                                        border.color: cellItem.edge
                                    }

                                    Shape {
                                        objectName: "hexTile"
                                        anchors.fill: parent
                                        visible: keyboardSurface.hex
                                        ShapePath {
                                            fillColor: cellItem.fill
                                            // Every cell of a honeycomb is
                                            // outlined. One that is not is not
                                            // a dim key, it is a hole in the
                                            // tiling, and a player cannot count
                                            // steps across a hole.
                                            strokeColor: cellItem.edge
                                            strokeWidth: 1
                                            PathPolyline {
                                                path: keyboardSurface.hexCorners(
                                                    cellItem.width, cellItem.height)
                                            }
                                        }
                                    }

                                    ColumnLayout {
                                        anchors.centerIn: parent
                                        spacing: 0
                                        Label {
                                            id: cellName
                                            Layout.alignment: Qt.AlignHCenter
                                            // A name that does not fit its key
                                            // is not a name, it is a smear
                                            // across its neighbours. A tuning
                                            // of thirty-one degrees has longer
                                            // names than one of twelve, so the
                                            // label measures itself rather than
                                            // guessing at a cell size.
                                            visible: implicitWidth
                                                     <= cellItem.width * 0.9
                                                     && implicitHeight
                                                        <= cellItem.height * 0.55
                                            text: modelData.label
                                            color: modelData.root ? "#0e0f12"
                                                   : (modelData.accidental ? muted : ink)
                                            font.pixelSize: 9; font.family: "monospace"
                                            font.bold: modelData.root
                                        }
                                        // The retune is the whole point in a
                                        // tuning that is not twelve tones: it
                                        // is what the instrument is told.
                                        Label {
                                            Layout.alignment: Qt.AlignHCenter
                                            visible: modelData.retune !== ""
                                                     && cellName.visible
                                                     && implicitWidth
                                                        <= cellItem.width * 0.9
                                            text: modelData.retune
                                            color: modelData.root ? "#0e0f12" : blue
                                            font.pixelSize: 8; font.family: "monospace"
                                        }
                                    }

                                    // A key sounds when it goes down and
                                    // rings until it comes back up.
                                    MouseArea {
                                        id: keyInput
                                        objectName: "keyInput"
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onPressed: mouse => {
                                            // Outside the drawn cell the press
                                            // belongs to the neighbour beneath.
                                            mouse.accepted = keyboardSurface.insideCell(
                                                mouse.x / Math.max(1, width),
                                                mouse.y / Math.max(1, height))
                                            if (!mouse.accepted) return
                                            noteEntry.forceActiveFocus()
                                            keyboardModel.hold(modelData.index)
                                        }
                                        onReleased: keyboardModel.release()
                                        onCanceled: keyboardModel.release()
                                    }
                                }
                            }
                        }
                        }
                    }
                }

                // ------------------------------------------------- step inspector
                Rectangle {
                    objectName: "stepInspector"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 228
                    Layout.minimumHeight: 228
                    radius: 6; color: panel; border.color: line

                    // Nothing selected, or an empty step: say so and say what to do.
                    ColumnLayout {
                        anchors.centerIn: parent
                        visible: !patternModel.selected.exists
                        spacing: 4
                        Label {
                            text: "STEP " + root.fmt2(patternModel.selectedStep + 1) + " IS EMPTY"
                            color: muted; font.pixelSize: 12; font.bold: true
                            font.letterSpacing: 1
                            Layout.alignment: Qt.AlignHCenter
                        }
                        Label {
                            text: "Click the step to place a note, then shape it here"
                            color: "#5c626e"; font.pixelSize: 11
                            Layout.alignment: Qt.AlignHCenter
                        }
                    }

                    GridLayout {
                        anchors.fill: parent; anchors.margins: 12
                        columns: 2; columnSpacing: 18; rowSpacing: 10
                        visible: patternModel.selected.exists

                        // Identity + pitch
                        RowLayout {
                            Layout.fillWidth: true; spacing: 14
                        ColumnLayout {
                            Layout.preferredWidth: 116; spacing: 4
                            SectionLabel { text: "STEP " + root.fmt2(patternModel.selectedStep + 1) }
                            Label {
                                objectName: "inspectorNote"
                                text: patternModel.selected.noteName !== undefined
                                      ? patternModel.selected.noteName : ""
                                color: amber; font.pixelSize: 26; font.bold: true
                                font.family: "monospace"
                            }
                            RowLayout {
                                spacing: 4
                                Chip { text: "−12"; accent: blue
                                    onClicked: patternModel.transposeSelected(-12) }
                                Chip { text: "−"; accent: blue
                                    onClicked: patternModel.transposeSelected(-1) }
                                Chip { text: "+"; accent: blue
                                    onClicked: patternModel.transposeSelected(1) }
                                Chip { text: "+12"; accent: blue
                                    onClicked: patternModel.transposeSelected(12) }
                            }
                            RowLayout {
                                spacing: 3
                                SectionLabel { text: "LEN" }
                                Repeater {
                                    model: [1, 2, 4, 8]
                                    Chip {
                                        required property var modelData
                                        objectName: "duration" + modelData
                                        text: modelData + "ST"
                                        on: Math.round(patternModel.selected.duration / 120)
                                            === modelData
                                        onClicked: patternModel.setSelectedDuration(modelData * 120)
                                    }
                                }
                            }
                        }

                        Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: line }

                        // Feel
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 10
                            Dial {
                                Layout.fillWidth: true
                                label: "VELOCITY"; from: 0; to: 1
                                value: patternModel.selected.velocity !== undefined
                                       ? patternModel.selected.velocity : 0
                                readout: patternModel.selected.velocityUnits !== undefined
                                         ? patternModel.selected.velocityUnits : 0
                                onMoved: function(v) { patternModel.setSelectedVelocity(v) }
                            }
                            Dial {
                                Layout.fillWidth: true
                                label: "MICRO"; from: -59; to: 59
                                accent: blue
                                value: patternModel.selected.micro !== undefined
                                       ? patternModel.selected.micro : 0
                                readout: (patternModel.selected.micro > 0 ? "+" : "") +
                                         (patternModel.selected.micro !== undefined
                                          ? patternModel.selected.micro : 0)
                                onMoved: function(v) { patternModel.setSelectedMicroOffset(Math.round(v)) }
                            }
                        }
                        }

                        // Chance
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 10
                            Dial {
                                Layout.fillWidth: true
                                label: "PROBABILITY"; from: 0; to: 1
                                accent: amber
                                value: patternModel.selected.probability !== undefined
                                       ? patternModel.selected.probability : 1
                                readout: Math.round((patternModel.selected.probability !== undefined
                                         ? patternModel.selected.probability : 1) * 100) + "%"
                                onMoved: function(v) { patternModel.setSelectedProbability(v) }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 3
                                RowLayout {
                                    Layout.fillWidth: true
                                    Label { text: "RATCHET"; color: muted; font.pixelSize: 10
                                        font.letterSpacing: 1 }
                                    Item { Layout.fillWidth: true }
                                }
                                RowLayout {
                                    spacing: 3
                                    Repeater {
                                        model: [1, 2, 3, 4, 6, 8]
                                        Chip {
                                            required property var modelData
                                            text: "×" + modelData
                                            on: patternModel.selected.ratchets === modelData
                                            onClicked: patternModel.setSelectedRatchets(modelData)
                                        }
                                    }
                                }
                            }
                        }

                        // Parameter lock — the Elektron-style p-lock, now editable.
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 8
                            RowLayout {
                                Layout.fillWidth: true; spacing: 6
                                SectionLabel { text: "PARAMETER LOCK" }
                                Item { Layout.fillWidth: true }
                                Chip {
                                    objectName: "lockAutomation"
                                    text: "AUTO"
                                    on: patternModel.selected.hasLock === true &&
                                        patternModel.selected.lockModulation === false
                                    onClicked: patternModel.setSelectedLock(
                                        patternModel.selected.lockIndex,
                                        patternModel.selected.lockValue, false)
                                }
                                Chip {
                                    objectName: "lockModulation"
                                    text: "MOD"; accent: blue
                                    on: patternModel.selected.lockModulation === true
                                    onClicked: patternModel.setSelectedLock(
                                        patternModel.selected.lockIndex,
                                        patternModel.selected.lockValue, true)
                                }
                                Chip {
                                    text: "CLEAR"
                                    enabled: patternModel.selected.hasLock === true
                                    onClicked: patternModel.clearSelectedLock()
                                }
                            }
                            Dial {
                                Layout.fillWidth: true
                                label: "VALUE"; from: 0; to: 1
                                enabled: patternModel.selected.hasLock === true
                                accent: patternModel.selected.lockModulation ? blue : acid
                                value: patternModel.selected.lockValue !== undefined
                                       ? patternModel.selected.lockValue : 0
                                readout: patternModel.selected.hasLock
                                         ? patternModel.selected.lockText : "none"
                                onMoved: function(v) {
                                    patternModel.setSelectedLock(patternModel.selected.lockIndex, v,
                                                                 patternModel.selected.lockModulation)
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true; spacing: 6
                                Label { text: "EVERY"; color: muted; font.pixelSize: 10
                                    font.letterSpacing: 1 }
                                Repeater {
                                    model: [0, 2, 3, 4]
                                    Chip {
                                        required property var modelData
                                        text: modelData === 0 ? "LOOP" : modelData + "×"
                                        accent: amber
                                        on: patternModel.selected.loop === modelData
                                        onClicked: patternModel.setSelectedPlayOnLoop(modelData)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        }

        // ---------------------------------------------------------------- mixer
        // One strip per track, reading the same song the arrangement does. Gain,
        // pan, mute and solo reach the running engine without rebuilding it, so
        // a fader can be moved while the song plays.
        Rectangle {
            objectName: "mixerPanel"
            SplitView.preferredWidth: 250; SplitView.minimumWidth: 210
            SplitView.maximumWidth: 320
            color: "#121317"

            ColumnLayout {
                anchors.fill: parent; anchors.margins: 12; spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    SectionLabel { text: "MIXER" }
                    Item { Layout.fillWidth: true }
                    Label {
                        objectName: "mixerTrackCount"
                        text: songModel.trackCount + (songModel.trackCount === 1 ? " TRACK" : " TRACKS")
                        color: muted; font.pixelSize: 9; font.letterSpacing: 0.8
                    }
                }

                // However many tracks the song has, the strips scroll and the
                // master stays in reach below them.
                Flickable {
                    id: mixerScroll
                    objectName: "mixerScroll"
                    Layout.fillWidth: true; Layout.fillHeight: true
                    clip: true
                    contentWidth: width
                    contentHeight: mixerStripColumn.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                ColumnLayout {
                    id: mixerStripColumn
                    objectName: "mixerStrips"
                    width: mixerScroll.width; spacing: 6

                    Repeater {
                        model: songModel.tracks
                        Rectangle {
                            required property var modelData
                            readonly property int trackIndex: modelData.index
                            objectName: "mixerStrip" + modelData.index
                            Layout.fillWidth: true
                            implicitHeight: 126
                            radius: 6
                            color: modelData.selected ? raised : panel
                            border.color: modelData.selected ? acid : line

                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                onClicked: function(mouse) {
                                    songModel.selectTrack(trackIndex)
                                    if (mouse.button !== Qt.RightButton) return
                                    trackMenu.target = trackIndex
                                    trackMenu.name = modelData.name
                                    trackMenu.popup()
                                }
                                onDoubleClicked: renamePopup.ask("TRACK", trackIndex, modelData.name)
                            }

                            ColumnLayout {
                                anchors.fill: parent; anchors.margins: 9; spacing: 5

                                RowLayout {
                                    Layout.fillWidth: true; spacing: 4
                                    ColumnLayout {
                                        Layout.fillWidth: true; spacing: 0
                                        Label {
                                            Layout.fillWidth: true
                                            text: modelData.name
                                            color: modelData.audible ? ink : muted
                                            font.pixelSize: 11; font.bold: true
                                            font.letterSpacing: 0.8; elide: Text.ElideRight
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            text: modelData.instrument
                                            color: modelData.hasInstrument ? muted : "#5c626e"
                                            font.pixelSize: 9; elide: Text.ElideRight
                                        }
                                    }
                                    Chip {
                                        objectName: "mute" + modelData.index
                                        text: "M"; accent: amber
                                        on: modelData.mute
                                        onClicked: songModel.toggleMute(trackIndex)
                                    }
                                    Chip {
                                        objectName: "solo" + modelData.index
                                        text: "S"; accent: blue
                                        on: modelData.solo
                                        onClicked: songModel.toggleSolo(trackIndex)
                                    }
                                }

                                // Peak meter: the last block's loudest sample,
                                // laid out over the top 60 dB.
                                Rectangle {
                                    objectName: "meter" + modelData.index
                                    Layout.fillWidth: true
                                    implicitHeight: 5; radius: 2
                                    color: "#0d0e11"
                                    // Read from the meters alone: they move
                                    // thirty times a second, and the rest of
                                    // the strip has no reason to redraw.
                                    Rectangle {
                                        objectName: "meterFill" + trackIndex
                                        readonly property real level:
                                            songModel.meters[trackIndex] !== undefined
                                            ? songModel.meters[trackIndex] : 0
                                        width: parent.width * level
                                        height: parent.height; radius: 2
                                        color: level > 0.998 ? "#ff4d4d"
                                             : level > 0.948 ? amber : acid
                                    }
                                }

                                Dial {
                                    Layout.fillWidth: true
                                    label: "GAIN"; from: -60; to: 6
                                    value: modelData.gainDb
                                    readout: modelData.gainText + " dB"
                                    accent: modelData.audible ? acid : muted
                                    onMoved: function(v) { songModel.setTrackGain(trackIndex, v) }
                                }
                                Dial {
                                    Layout.fillWidth: true
                                    label: "PAN"; from: -1; to: 1
                                    accent: blue
                                    value: modelData.pan
                                    readout: modelData.panText
                                    onMoved: function(v) { songModel.setTrackPan(trackIndex, v) }
                                }
                            }
                        }
                    }
                }
                }

                Rectangle {
                    objectName: "masterStrip"
                    Layout.fillWidth: true; implicitHeight: 78; radius: 6
                    color: panel; border.color: acid

                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 9; spacing: 5
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: "MASTER"; color: acid; font.pixelSize: 11
                                font.bold: true; font.letterSpacing: 1 }
                            Item { Layout.fillWidth: true }
                            Label {
                                text: songModel.masterPeak > 0.99 ? "CLIP" : "OK"
                                color: songModel.masterPeak > 0.99 ? "#ff4d4d" : muted
                                font.pixelSize: 9; font.bold: true
                            }
                        }
                        Rectangle {
                            Layout.fillWidth: true; implicitHeight: 5; radius: 2
                            color: "#0d0e11"
                            Rectangle {
                                width: parent.width * Math.min(1, songModel.masterPeak)
                                height: parent.height; radius: 2
                                color: songModel.masterPeak > 0.99 ? "#ff4d4d"
                                     : songModel.masterPeak > 0.7 ? amber : acid
                            }
                        }
                        Dial {
                            Layout.fillWidth: true
                            label: "LEVEL"; from: -60; to: 6
                            value: songModel.masterGainDb
                            readout: (songModel.masterGainDb <= -59.95 ? "-inf"
                                      : (songModel.masterGainDb > 0 ? "+" : "")
                                        + songModel.masterGainDb.toFixed(1)) + " dB"
                            onMoved: function(v) { songModel.setMasterGain(v) }
                        }
                    }
                }
            }
        }
    }
}
