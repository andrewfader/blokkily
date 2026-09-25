import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import "Format.js" as Format

ApplicationWindow {
    id: root
    objectName: "mainWindow"
    width: 1280; height: 800; visible: true
    // The session's name, and a dot while it holds changes that are not on disk.
    title: (songModel.dirty ? "\u2022 " : "") + appController.projectName + " \u2014 Blokkily"
    color: Theme.bg

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

    function fmt2(n) { return Format.fmt2(n) }
    function keyName(key) { return Format.keyName(key) }

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
            color: Theme.ink
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
        background: Rectangle { color: Theme.panel; border.color: Theme.acid; radius: 6 }
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
                color: Theme.ink; font.pixelSize: 13; font.bold: true
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

    function showTrackMenu(index, name) {
        trackMenu.target = index
        trackMenu.name = name
        trackMenu.popup()
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
               onActivated: instrumentPanel.findInstrument() }
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

    header: TransportBar {
        view: root.view
        focusHome: noteEntry
        onViewPicked: name => root.view = name
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
            var semitone = semitoneFor(event.text)
            if (semitone < 0) return
            // Recording against the running transport, a key is played into
            // the take - on every armed track, when and for as long as it is
            // held - and the cursor stays where it is.
            if (appController.performKey(root.entryBase + semitone, true)) {
                event.accepted = true
                return
            }
            var step = patternModel.selectedStep
            if (step < 0) return
            root.writeNote(step, root.entryBase + semitone)
            // A tracker advances after a note is typed, which is what makes it
            // faster to write a phrase in than to click one.
            patternModel.selectStep((step + 1) % 16)
            event.accepted = true
        }
        // A performed key is let go when it comes up, so the take holds it
        // exactly as long as it was held.
        Keys.onReleased: function(event) {
            if (event.isAutoRepeat || semitoneFor(event.text) < 0) return
            appController.releasePerformed()
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

                InstrumentPanel {
                    id: instrumentPanel
                    Layout.fillWidth: true; Layout.fillHeight: true
                    focusHome: noteEntry
                }

                ProjectPanel {
                    Layout.fillWidth: true
                    exportDepth: root.exportDepth
                    onExportDepthPicked: depth => root.exportDepth = depth
                    onNewRequested: root.whenDiscarded(function() { appController.newProject() })
                    onOpenRequested: root.whenDiscarded(function() { openProjectDialog.open() })
                    onSaveRequested: root.save()
                    onSaveAsRequested: saveProjectDialog.open()
                    onExportRequested: exportDialog.open()
                }
            }
        }

        // -------------------------------------------------------------- editors
        Rectangle {
            SplitView.fillWidth: true; color: Theme.bg
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
                            color: Theme.ink; font.pixelSize: 16; font.bold: true
                            font.letterSpacing: 0.5
                        }
                        Rectangle {
                            implicitWidth: 44; implicitHeight: 18; radius: 3
                            color: "transparent"; border.color: Theme.line
                            Label { anchors.centerIn: parent; text: "16 ST"; color: Theme.muted
                                font.pixelSize: 9; font.letterSpacing: 0.5 }
                        }
                        Rectangle {
                            implicitWidth: 52; implicitHeight: 18; radius: 3
                            color: "transparent"; border.color: Theme.line
                            Label { anchors.centerIn: parent; text: songModel.bars + " BARS"
                                color: Theme.muted; font.pixelSize: 9; font.letterSpacing: 0.5 }
                        }
                        Item { Layout.fillWidth: true }
                        Label { text: "SPACE play  |  HOME rewind  |  INSERT push  |  ALT nudge  |  CTRL+D duplicate  |  CTRL+M mute  |  CTRL+L solo"
                            color: Theme.muted; font.pixelSize: 10 }
                    }

                    ArrangementView {
                        focusHome: noteEntry
                        onRenameRequested: (kind, index, name) => renamePopup.ask(kind, index, name)
                        onPatternMenuRequested: (index, name) => {
                            patternMenu.target = index
                            patternMenu.name = name
                            patternMenu.popup()
                        }
                        onTrackMenuRequested: (index, name) => root.showTrackMenu(index, name)
                    }

                    StepEditors {
                        view: root.view
                        entryOctave: root.entryOctave
                        focusHome: noteEntry
                        onNoteWritten: (step, key) => root.writeNote(step, key)
                        onOctavePicked: octave => root.entryOctave = octave
                    }

                    KeyboardPanel {
                        view: root.view
                        focusHome: noteEntry
                    }

                    Inspector {}
                }
            }
        }

        MixerPanel {
            onTrackRenameRequested: (index, name) => renamePopup.ask("TRACK", index, name)
            onTrackMenuRequested: (index, name) => root.showTrackMenu(index, name)
        }
    }
}
