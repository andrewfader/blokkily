import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import "Format.js" as Format

ApplicationWindow {
    id: root
    objectName: "mainWindow"
    // The window opens at the size the layout is designed for — every editor,
    // the keyboard and the inspector without scrolling — unless the screen's
    // free area is smaller, in which case it opens filling that area and the
    // editor column scrolls. A window taller than the screen would hide its
    // own bottom edge behind the panel.
    width: Math.min(1280, Screen.desktopAvailableWidth)
    height: Math.min(1080, Screen.desktopAvailableHeight)
    minimumWidth: Math.min(960, Screen.desktopAvailableWidth)
    minimumHeight: Math.min(600, Screen.desktopAvailableHeight)
    visible: true
    // The session's name, and a dot while it holds changes that are not on disk.
    title: (songModel.dirty ? "\u2022 " : "") + appController.projectName + qsTr(" \u2014 Blokkily")
    color: Theme.bg
    font.family: Theme.sans
    // SETTINGS · WINDOW · THEME: dark (as shipped), light, or the desktop's.
    readonly property string themeChoice: appController.uiSettings["ui/theme"] !== undefined
                                          ? String(appController.uiSettings["ui/theme"]) : "dark"
    Binding {
        target: Theme; property: "light"
        value: root.themeChoice === "light"
               || (root.themeChoice === "system" && Qt.styleHints.colorScheme === Qt.ColorScheme.Light)
    }
    // The controls' own palette (menus, fields, scroll bars) follows it.
    palette.window: Theme.panel
    palette.windowText: Theme.ink
    palette.base: Theme.raised
    palette.alternateBase: Theme.stripe
    palette.text: Theme.ink
    palette.button: Theme.raised
    palette.buttonText: Theme.ink
    palette.brightText: Theme.ink
    palette.highlight: Theme.acid
    palette.highlightedText: Theme.onAccent
    palette.placeholderText: Theme.muted
    palette.mid: Theme.line
    palette.dark: Theme.well
    palette.light: Theme.raised
    palette.shadow: Theme.well
    palette.toolTipBase: Theme.panel
    palette.toolTipText: Theme.ink
    font.pixelSize: 12

    // STEP / TRACKER / PIANO / SCORE focus one editor; ALL keeps the step
    // grid, tracker and piano roll on screen at once, which is the point of
    // the instrument (the score needs a view of its own to be readable).
    // LAUNCH shows the scene launcher in the editors' place, VERSIONS the
    // song's saved versions and NOTES its notes. SETTINGS (the gear beside
    // HELP) shows the machine's settings there.
    property string view: "ALL"
    // SETTINGS opens in the editors' place; this is where it goes back to.
    property string viewBeforeSettings: "ALL"
    function openSettings() {
        viewBeforeSettings = view
        return "SETTINGS"
    }
    // How the next export is written, remembered between launches (and
    // kept by LAYOUT · RESET, which is the layout's): the bit depth, the
    // file (WAV, MP3 or OGG), and whether a 16- or 24-bit WAV is dithered
    // (on, as mastering expects).
    readonly property string exportDepth: ui["ui/exportDepth"] !== undefined ? ui["ui/exportDepth"] : "PCM24"
    readonly property string exportFormat: ui["ui/exportFormat"] !== undefined ? ui["ui/exportFormat"] : "WAV"
    readonly property bool exportDither: uiFlag("ui/exportDither", true)
    // Whether the next bounce has the metronome in it (off: the click is a
    // guide, not part of the mix).
    property bool exportClick: false
    // Whether BOUNCE and STEMS write only the loop's bars (while there is a
    // loop); the whole song otherwise.
    property bool exportLoopOnly: false
    readonly property bool exportsLoop: exportLoopOnly && songModel.loopEnabled
    // What BOUNCE writes to `path`, as the rail has it set.
    function exportTo(path) {
        return exportsLoop
            ? appController.exportAudioRange(path, songModel.loopStartBar, songModel.loopEndBar,
                                             exportDepth, exportClick, exportDither)
            : appController.exportAudioFile(path, exportDepth, exportClick, exportDither)
    }
    function exportStemsTo(folder) {
        return exportsLoop
            ? appController.exportTrackFilesRange(folder, songModel.loopStartBar, songModel.loopEndBar,
                                                  exportFormat, exportDepth, exportDither)
            : appController.exportTrackFiles(folder, exportFormat, exportDepth, exportDither)
    }
    readonly property bool showTracker: view === "ALL" || view === "TRACKER"
    readonly property bool showRoll: view === "ALL" || view === "PIANO"
    readonly property bool showScore: view === "SCORE"

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
    // True while the score's staff has the keyboard: its arrows move the
    // selected note and Delete removes it, so the step cursor's keys stand aside.
    readonly property bool staffEditing: activeFocusItem !== null
                                         && activeFocusItem.objectName === "staffInput"

    // Save writes back to the file the session came from; a session that has
    // never been saved asks where to put it.
    // A save that fails says why in a notice; only a session with no file
    // yet asks where to put it.
    function save() {
        if (appController.projectPath === "") saveProjectDialog.open()
        else appController.saveProjectInPlace()
    }

    // Asks for a song to open, in this window (once its unsaved work is let
    // go) or in a new one beside it.
    function openProject(newWindow) {
        if (newWindow) {
            openProjectDialog.newWindow = true
            openProjectDialog.open()
            return
        }
        whenDiscarded(function() {
            openProjectDialog.newWindow = false
            openProjectDialog.open()
        })
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
        title: qsTr("Discard unsaved changes?")
        standardButtons: Dialog.Discard | Dialog.Cancel
        Label {
            text: qsTr("“%1” has changes that are not saved.").arg(appController.projectName)
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

    // A .blok opened from the desktop while this window runs comes here when
    // the window holds nothing yet (AppController::routeOpenRequest).
    Connections {
        target: appController
        function onOpenRequested(path) {
            root.whenDiscarded(function() { appController.loadProjectFile(path) })
        }
    }

    // A song a window left unsaved when it did not close normally (a crash,
    // a kill, a power cut) is offered back, newest first, one at a time.
    readonly property var recovery: appController.recoveries.length > 0
                                    ? appController.recoveries[0] : null
    Popup {
        id: recoveryPopup
        objectName: "recoveryPopup"
        anchors.centerIn: parent
        modal: true; focus: true
        closePolicy: Popup.NoAutoClose
        padding: 16
        visible: root.recovery !== null
        background: Rectangle { color: Theme.panel; border.color: Theme.amber; radius: 6 }
        onClosed: noteEntry.forceActiveFocus()
        ColumnLayout {
            spacing: 10
            SectionLabel { text: qsTr("RECOVER UNSAVED WORK") }
            Label {
                objectName: "recoveryMessage"
                Layout.preferredWidth: 320
                wrapMode: Text.Wrap
                color: Theme.ink; font.pixelSize: 13
                text: root.recovery === null ? "" :
                      qsTr("“%1” was not saved when Blokkily last closed. It was autosaved %2.")
                          .arg(root.recovery.title || qsTr("Untitled")).arg(root.recovery.saved)
            }
            Label {
                Layout.preferredWidth: 320
                visible: root.recovery !== null && root.recovery.project !== ""
                text: root.recovery === null ? "" : root.recovery.project
                color: Theme.muted; font.pixelSize: 10; elide: Text.ElideMiddle
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 6
                Item { Layout.fillWidth: true }
                Chip {
                    objectName: "discardRecoveryButton"
                    text: qsTr("DISCARD")
                    onClicked: appController.discardRecovery(root.recovery.id)
                }
                Chip {
                    objectName: "recoverButton"
                    text: qsTr("RECOVER"); accent: Theme.amber; on: true
                    onClicked: {
                        var id = root.recovery.id
                        root.whenDiscarded(function() { appController.recoverSession(id) })
                    }
                }
            }
        }
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
        function accept() {
            if (renamePopup.kind === "PATTERN")
                songModel.renamePattern(renamePopup.target, renameField.text)
            else
                songModel.renameTrack(renamePopup.target, renameField.text)
            renamePopup.close()
        }
        ColumnLayout {
            spacing: 8
            SectionLabel {
                objectName: "renameTitle"
                text: renamePopup.kind === "PATTERN" ? qsTr("RENAME PATTERN") : qsTr("RENAME TRACK")
            }
            TextField {
                id: renameField
                objectName: "renameField"
                Layout.preferredWidth: 240
                color: Theme.ink; font.pixelSize: 13; font.bold: true
                selectByMouse: true
                maximumLength: 24
                onAccepted: renamePopup.accept()
                Keys.onEscapePressed: renamePopup.close()
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 6
                // How much of a name fits: it says so as the limit nears.
                Label {
                    objectName: "renameLength"
                    text: renameField.length + " / " + renameField.maximumLength
                    color: renameField.length >= renameField.maximumLength ? Theme.amber : Theme.muted
                    font.pixelSize: 9; font.family: Theme.mono
                }
                Item { Layout.fillWidth: true }
                Chip { objectName: "renameCancel"; text: qsTr("CANCEL"); implicitHeight: 22
                       hint: qsTr("Keep the name (Escape)")
                       onClicked: renamePopup.close() }
                Chip { objectName: "renameAccept"; text: qsTr("RENAME"); implicitHeight: 22; on: true
                       hint: qsTr("Use the new name (Return)")
                       onClicked: renamePopup.accept() }
            }
        }
    }

    Menu {
        id: patternMenu
        objectName: "patternMenu"
        property int target: -1
        property string name: ""
        MenuItem { text: qsTr("Rename…")
            onTriggered: renamePopup.ask("PATTERN", patternMenu.target, patternMenu.name) }
        MenuItem { text: qsTr("Duplicate")
            onTriggered: { songModel.selectSection(patternMenu.target); songModel.duplicatePattern() } }
        MenuItem { text: qsTr("Clear steps")
            onTriggered: { songModel.selectSection(patternMenu.target); songModel.clearPattern() } }
        MenuItem { text: qsTr("Delete"); enabled: songModel.sections.length > 1
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
        MenuItem { text: qsTr("Rename…")
            onTriggered: renamePopup.ask("TRACK", trackMenu.target, trackMenu.name) }
        // Ticked when the track is muted or soloed, so the menu says how it is.
        MenuItem { objectName: "trackMenuMute"; text: qsTr("Mute"); checkable: true
            checked: songModel.tracks[trackMenu.target] !== undefined && songModel.tracks[trackMenu.target].mute === true
            onTriggered: songModel.toggleMute(trackMenu.target) }
        MenuItem { objectName: "trackMenuSolo"; text: qsTr("Solo"); checkable: true
            checked: songModel.tracks[trackMenu.target] !== undefined && songModel.tracks[trackMenu.target].solo === true
            onTriggered: songModel.toggleSolo(trackMenu.target) }
        MenuItem { text: qsTr("Delete"); enabled: songModel.trackCount > 1
            onTriggered: songModel.deleteTrack(trackMenu.target) }
        // The track's colour, on its clips, its header and its strip.
        Menu {
            id: colourMenu
            objectName: "trackColourMenu"
            title: qsTr("Colour")
            MenuItem { objectName: "trackColourAuto"; text: qsTr("Automatic")
                onTriggered: songModel.setTrackColour(trackMenu.target, "") }
            Instantiator {
                model: Theme.trackPalette
                delegate: MenuItem {
                    required property var modelData
                    required property int index
                    readonly property var names: [qsTr("Red"), qsTr("Orange"), qsTr("Amber"), qsTr("Green"),
                                                  qsTr("Teal"), qsTr("Blue"), qsTr("Violet"), qsTr("Pink")]
                    objectName: "trackColour" + index
                    text: names[index]
                    leftPadding: 30
                    indicator: Rectangle {
                        x: 10; anchors.verticalCenter: parent.verticalCenter
                        width: 12; height: 12; radius: 3
                        color: parent.modelData
                        border.color: Theme.line
                    }
                    onTriggered: songModel.setTrackColour(trackMenu.target, modelData)
                }
                onObjectAdded: function(i, item) { colourMenu.insertItem(i + 1, item) }
                onObjectRemoved: function(i, item) { colourMenu.removeItem(item) }
            }
        }
        MenuSeparator {}
        // The next hardware controller moved is bound to the control.
        MenuItem { objectName: "learnGain"; text: qsTr("MIDI learn gain")
            onTriggered: appController.learnMidi("gain", trackMenu.target) }
        MenuItem { objectName: "learnPan"; text: qsTr("MIDI learn pan")
            onTriggered: appController.learnMidi("pan", trackMenu.target) }
        MenuItem { objectName: "learnMute"; text: qsTr("MIDI learn mute")
            onTriggered: appController.learnMidi("mute", trackMenu.target) }
        MenuItem { objectName: "learnSolo"; text: qsTr("MIDI learn solo")
            onTriggered: appController.learnMidi("solo", trackMenu.target) }
        MenuItem { objectName: "forgetMidi"; text: qsTr("Forget MIDI controls")
            onTriggered: {
                for (const target of ["gain", "pan", "mute", "solo"])
                    appController.forgetMidiBindings(target, trackMenu.target)
            } }
    }

    FileDialog {
        id: openProjectDialog
        objectName: "openProjectDialog"
        // Whether the chosen song opens beside this one rather than in its place.
        property bool newWindow: false
        title: newWindow ? qsTr("Open Blokkily project in a new window") : qsTr("Open Blokkily project")
        nameFilters: ["Blokkily projects (*.blok)", "All files (*)"]
        fileMode: FileDialog.OpenFile
        onAccepted: newWindow ? appController.openWindow(selectedFile.toString())
                              : appController.loadProjectFile(selectedFile.toString())
        onRejected: noteEntry.forceActiveFocus()
    }
    FileDialog {
        id: saveProjectDialog
        title: qsTr("Save Blokkily project")
        nameFilters: ["Blokkily projects (*.blok)"]
        fileMode: FileDialog.SaveFile
        defaultSuffix: "blok"
        onAccepted: appController.saveProjectFile(selectedFile.toString())
    }

    FileDialog {
        id: exportDialog
        title: qsTr("Bounce the arrangement")
        // The extension chooses the container, so the dialog offers the
        // chosen format's.
        nameFilters: root.exportFormat === "MP3" ? ["MP3 audio (*.mp3)"]
                   : root.exportFormat === "OGG" ? ["Ogg Vorbis audio (*.ogg)"]
                   : [qsTr("Wave audio (*.wav)")]
        fileMode: FileDialog.SaveFile
        defaultSuffix: root.exportFormat.toLowerCase()
        onAccepted: root.exportTo(selectedFile.toString())
    }
    FolderDialog {
        id: exportStemsDialog
        title: qsTr("Export every track as a file of its own")
        onAccepted: root.exportStemsTo(selectedFolder.toString())
    }
    FileDialog {
        id: exportScopeDialog
        title: qsTr("Render an oscilloscope video of the tracks and the master")
        nameFilters: ["MPEG-4 video (*.mp4)"]
        fileMode: FileDialog.SaveFile
        defaultSuffix: "mp4"
        onAccepted: appController.exportScopeVideo(selectedFile.toString())
    }

    FileDialog {
        id: importMidiDialog
        title: qsTr("Import MIDI after the arrangement (keep song tempo)")
        nameFilters: ["MIDI files (*.mid *.midi)"]
        fileMode: FileDialog.OpenFile
        onAccepted: appController.importMidiFile(selectedFile.toString())
    }
    FileDialog {
        id: exportMidiDialog
        title: qsTr("Export arrangement notes and controllers")
        nameFilters: ["MIDI files (*.mid)"]
        fileMode: FileDialog.SaveFile
        defaultSuffix: "mid"
        onAccepted: appController.exportMidiFile(selectedFile.toString())
    }
    FileDialog {
        id: importDawProjectDialog
        objectName: "importDawProjectDialog"
        title: qsTr("Open a song from another DAW")
        nameFilters: ["DAWproject files (*.dawproject)"]
        fileMode: FileDialog.OpenFile
        onAccepted: appController.importDawProjectFile(selectedFile.toString())
    }
    FileDialog {
        id: exportDawProjectDialog
        title: qsTr("Export the song for another DAW")
        nameFilters: ["DAWproject files (*.dawproject)"]
        fileMode: FileDialog.SaveFile
        defaultSuffix: "dawproject"
        onAccepted: appController.exportDawProjectFile(selectedFile.toString())
    }

    // Ctrl+K goes to the assistant's prompt bar, the way a command palette
    // answers Ctrl+K elsewhere; Escape there hands the keyboard back.
    Shortcut { sequence: "Ctrl+K"; onActivated: promptBar.focusPrompt() }
    // Space plays, the arrows move the step cursor (Shift+Up/Down transposes),
    // and Ctrl with the number row
    // toggles a step, so the grid can be driven without leaving the keyboard.
    // Bare digits are reserved for the tracker's upper-octave note keys.
    Shortcut { sequence: "Space"; enabled: !root.typing
               onActivated: appController.togglePlayback() }
    Shortcut { sequence: StandardKey.New
               onActivated: root.whenDiscarded(function() { appController.newProject() }) }
    Shortcut { sequence: StandardKey.Open
               onActivated: root.openProject(false) }
    Shortcut { sequence: "Ctrl+Shift+N"; onActivated: appController.openWindow() }
    Shortcut { sequence: "Ctrl+Shift+O"; onActivated: root.openProject(true) }
    Shortcut { sequence: StandardKey.Save; onActivated: root.save() }
    Shortcut { sequence: "Ctrl+R"; enabled: !root.typing
               onActivated: appController.toggleRecord() }
    Shortcut { sequence: "Ctrl+Shift+S"; onActivated: saveProjectDialog.open() }
    Shortcut { sequence: "Ctrl+E"; onActivated: exportDialog.open() }
    Shortcut { sequence: "Escape"; enabled: appController.midiLearning
               onActivated: appController.cancelMidiLearn() }
    // A text field keeps its own undo; everywhere else undo is the song's.
    Shortcut { sequence: StandardKey.Undo; enabled: !root.typing
               onActivated: songModel.undo() }
    Shortcut { sequences: [StandardKey.Redo, "Ctrl+Y"]; enabled: !root.typing
               onActivated: songModel.redo() }
    // Finding an instrument is a search, so it answers to the search key.
    Shortcut { sequence: StandardKey.Find
               onActivated: { rail.reveal(false); instrumentPanel.findInstrument() } }
    // Return and Home go back to the top of the song: the audio engine's
    // playhead and the one drawn over the editors, which are the same playhead.
    Shortcut { sequence: "Return"; enabled: !root.typing
               onActivated: appController.rewindPlayback() }
    Shortcut { sequence: "Home"; enabled: !root.typing
               onActivated: appController.rewindPlayback() }
    Shortcut { sequence: "Left"; enabled: !root.typing
        onActivated: patternModel.selectStep(Math.max(0, patternModel.selectedStep - 1)) }
    Shortcut { sequence: "Right"; enabled: !root.typing
        onActivated: patternModel.selectStep(Math.min(patternModel.stepCount - 1, patternModel.selectedStep + 1)) }
    // The tracker runs down the screen, so Up and Down walk its rows; the
    // pitch of a row is moved with Shift held (Ctrl for an octave).
    Shortcut { sequence: "Up"; enabled: !root.typing && !root.staffEditing
        onActivated: patternModel.selectStep(Math.max(0, patternModel.selectedStep - 1)) }
    Shortcut { sequence: "Down"; enabled: !root.typing && !root.staffEditing
        onActivated: patternModel.selectStep(Math.min(patternModel.stepCount - 1, patternModel.selectedStep + 1)) }
    Shortcut { sequence: "Shift+Up"; enabled: !root.typing && !root.staffEditing
               onActivated: patternModel.transposeSelected(1) }
    Shortcut { sequence: "Shift+Down"; enabled: !root.typing && !root.staffEditing
               onActivated: patternModel.transposeSelected(-1) }
    Shortcut { sequence: "Ctrl+Up"; enabled: !root.typing && !root.staffEditing
               onActivated: patternModel.transposeSelected(12) }
    Shortcut { sequence: "Ctrl+Down"; enabled: !root.typing && !root.staffEditing
               onActivated: patternModel.transposeSelected(-12) }
    // The tracker's own octave, moved without leaving the letter keys.
    Shortcut { sequence: "Ctrl+Left"; enabled: !root.typing
        onActivated: root.entryOctave = Math.max(0, root.entryOctave - 1) }
    Shortcut { sequence: "Ctrl+Right"; enabled: !root.typing
        onActivated: root.entryOctave = Math.min(8, root.entryOctave + 1) }
    Shortcut { sequences: ["Backspace", "Delete"]; enabled: !root.typing && !root.staffEditing
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

    // F1 asks for help, as it does everywhere else: the tour, and pressed
    // again during it, straight to the guide's question box.
    Shortcut {
        sequence: StandardKey.HelpContents
        onActivated: tutorial.running ? tutorial.skipToAsk() : tutorial.start()
    }

    header: TransportBar {
        view: root.view
        focusHome: noteEntry
        onViewPicked: name => root.view = name
    }

    // The guided tour. It lives in the overlay, above the header, so it can
    // point at the transport as well as the panels.
    Tutorial {
        id: tutorial
        parent: root.Overlay.overlay
        focusHome: noteEntry
        onAskRequested: guide.ask()
    }

    // The always-on floating guide. Same overlay layer as the tour, above the
    // header, so it can point at the transport as well as the panels. It is
    // drawn nothing while disabled; the GUIDE chip in the transport bar
    // toggles it.
    GuideOverlay {
        id: guide
        parent: root.Overlay.overlay
    }

    // A song opening, its instruments loading, an export: the window shows
    // how far it has got, above everything else, tour included.
    LoadingOverlay { parent: root.Overlay.overlay }

    // Failures, and work that finished, said where they are seen: at the
    // foot of the window over the editors, whichever bars are hidden.
    NoticeStack {
        parent: root.Overlay.overlay
        z: 900
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom; anchors.bottomMargin: 16
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
            // Handle Tab before Qt's focus traversal, including platforms
            // that deliver Shift+Tab as a Backtab key with Shift still set.
            if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
                var backwards = event.key === Qt.Key_Backtab
                                || (event.modifiers & Qt.ShiftModifier)
                var count = Math.max(1, songModel.trackCount)
                patternModel.selectChannelStep(
                    (songModel.selectedTrack + (backwards ? count - 1 : 1)) % count,
                    Math.max(0, patternModel.selectedStep))
                event.accepted = true
                return
            }
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
            patternModel.selectStep((step + 1) % patternModel.stepCount)
            event.accepted = true
        }
        // A performed key is let go when it comes up, so the take holds it
        // exactly as long as it was held.
        Keys.onReleased: function(event) {
            if (event.isAutoRepeat || semitoneFor(event.text) < 0) return
            appController.releasePerformed()
        }
    }

    // The window's own preferences (SETTINGS · WINDOW, and what the bars
    // and panels were dragged to), read through the controller so a gate
    // never touches the user's file.
    readonly property var ui: appController.uiSettings
    function uiFlag(key, fallback) {
        var value = ui[key]
        return value === undefined ? fallback : value === true || value === "true"
    }
    function uiNumber(key, fallback) {
        var value = parseFloat(ui[key])
        return isNaN(value) ? fallback : value
    }

    // Ctrl+[ and Ctrl+] bring a hidden bar out from the keyboard, or send it
    // back.
    // Every key the window answers, listed.
    Shortcut { sequence: "Ctrl+/"; onActivated: keysSheet.opened ? keysSheet.close() : keysSheet.open() }
    KeysSheet { id: keysSheet; parent: root.Overlay.overlay }

    Shortcut { sequence: "Ctrl+["; onActivated: rail.toggle() }
    Shortcut { sequence: "Ctrl+]"; onActivated: mixer.toggle() }

    Item {
        id: workspace
        objectName: "workspace"
        anchors.fill: parent
        // The editors at the width they are designed for, beside both bars:
        // a window that wide docks the bars whether or not they auto-hide,
        // when WINDOW · DOCK · WHEN WIDE says so. As it ships they auto-hide
        // at any width.
        readonly property bool roomy: root.uiFlag("ui/dockWhenRoomy", false)
                                      && width >= rail.barWidth + mixer.barWidth + 1100

        // ---------------------------------------------------------------- rail
        SideBar {
            id: rail
            objectName: "rail"
            z: 2
            anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
            edge: "left"
            title: qsTr("DEVICES")
            autoHide: root.uiFlag("ui/autoHideRail", true)
            roomy: workspace.roomy
            // The tour points at the bars, so they are out while it runs.
            held: tutorial.running
            storedWidth: root.uiNumber("ui/railWidth", 232)
            minimumBarWidth: 200; maximumBarWidth: 420
            onWidthPicked: width => appController.setUiSetting("ui/railWidth", width)

        Rectangle {
            anchors.fill: parent
            color: Theme.rail
            // The rail scrolls when what it holds is taller than the window
            // (a sampler's editing tools open, a small screen), so nothing
            // at its foot is pushed out of reach.
            Flickable {
                id: railScroll
                objectName: "railScroll"
                anchors.fill: parent; anchors.margins: 14
                contentWidth: width
                contentHeight: Math.max(height, railColumn.implicitHeight)
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; width: 6 }
            ColumnLayout {
                id: railColumn
                width: railScroll.width; height: railScroll.contentHeight
                spacing: 10

                InstrumentPanel {
                    id: instrumentPanel
                    objectName: "instrumentPanel"
                    Layout.fillWidth: true; Layout.fillHeight: true
                    focusHome: noteEntry
                }

                ProjectPanel {
                    Layout.fillWidth: true
                    exportDepth: root.exportDepth
                    onExportDepthPicked: depth => appController.setUiSetting("ui/exportDepth", depth)
                    exportFormat: root.exportFormat
                    exportDither: root.exportDither
                    onExportDitherToggled: appController.setUiSetting("ui/exportDither", !root.exportDither)
                    onExportFormatPicked: format => appController.setUiSetting("ui/exportFormat", format)
                    onExportStemsRequested: exportStemsDialog.open()
                    onExportScopeRequested: exportScopeDialog.open()
                    exportClick: root.exportClick
                    exportLoopOnly: root.exportLoopOnly
                    onExportLoopOnlyToggled: root.exportLoopOnly = !root.exportLoopOnly
                    onExportClickToggled: root.exportClick = !root.exportClick
                    onNewRequested: root.whenDiscarded(function() { appController.newProject() })
                    onOpenRequested: root.openProject(false)
                    onNewWindowRequested: appController.openWindow()
                    onOpenWindowRequested: root.openProject(true)
                    onSaveRequested: root.save()
                    onSaveAsRequested: saveProjectDialog.open()
                    onExportRequested: exportDialog.open()
                    onImportMidiRequested: importMidiDialog.open()
                    onExportMidiRequested: exportMidiDialog.open()
                    onExportDawProjectRequested: exportDawProjectDialog.open()
                    onImportDawProjectRequested: root.whenDiscarded(function() { importDawProjectDialog.open() })
                }
            }
            }
        }
        }

        // -------------------------------------------------------------- editors
        Rectangle {
            objectName: "editorArea"
            anchors { left: rail.right; right: mixer.left; top: parent.top; bottom: parent.bottom }
            color: Theme.bg
            Flickable {
                id: editorScroll
                objectName: "editorScroll"
                // The left margin holds the panels' grips.
                anchors.fill: parent; anchors.margins: 14; anchors.leftMargin: 2
                clip: true
                contentWidth: width
                contentHeight: Math.max(height, editorColumn.implicitHeight)
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                // The editors' panels, in the order they were dragged into.
                // Each is placed on its own row of the grid; LAUNCH,
                // VERSIONS, NOTES and SETTINGS take the editors' row, which
                // their views hide.
                GridLayout {
                    id: editorColumn
                    objectName: "editorColumn"
                    x: 12
                    width: editorScroll.width - 12
                    height: editorScroll.contentHeight
                    columns: 1
                    rowSpacing: 10

                    // The pattern's name, its stem, its controls, the keys
                    // and HELP, GUIDE and SETTINGS. Short of room (both bars
                    // docked in a narrow window) the controls drop to a row
                    // of their own rather than squeezing the names.
                    ColumnLayout {
                        id: editorHeader
                        objectName: "editorHeader"
                        Layout.row: 0
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 6
                        readonly property real wanted: patternTitle.implicitWidth + patternStem.implicitWidth
                            + patternControls.implicitWidth + settingsChip.implicitWidth
                            + helpChip.implicitWidth + guideChip.implicitWidth + 6 * 10 + 60
                        readonly property bool wraps: width < wanted
                        // Not even the names and the chips fit: the names elide.
                        readonly property bool squeezed: width < patternTitle.implicitWidth + patternStem.implicitWidth
                            + settingsChip.implicitWidth + helpChip.implicitWidth + guideChip.implicitWidth + 5 * 10 + 20
                    RowLayout {
                        Layout.fillWidth: true; spacing: 10
                        // However little room the editors have, this row
                        // never widens the column under them: its names
                        // elide instead.
                        Layout.minimumWidth: 0
                        Label {
                            id: patternTitle
                            objectName: "patternTitle"
                            text: songModel.patterns[songModel.currentPattern] !== undefined
                                  ? songModel.patterns[songModel.currentPattern].name : ""
                            color: Theme.ink; font.pixelSize: 16; font.bold: true
                            font.letterSpacing: 0.5
                            // Only an item that fills can be squeezed, and
                            // only then; it never grows past its name.
                            Layout.fillWidth: editorHeader.squeezed
                            Layout.maximumWidth: implicitWidth
                            Layout.minimumWidth: Math.min(implicitWidth, 48)
                            elide: Text.ElideRight
                        }
                        // Whose part of the pattern the editors below show.
                        Label {
                            id: patternStem
                            objectName: "patternStem"
                            text: songModel.trackNames[songModel.selectedTrack] !== undefined
                                  ? "· " + songModel.trackNames[songModel.selectedTrack] : ""
                            color: Theme.acid; font.pixelSize: 11; font.bold: true
                            font.letterSpacing: 0.5
                            Layout.fillWidth: editorHeader.squeezed
                            Layout.maximumWidth: implicitWidth
                            Layout.minimumWidth: Math.min(implicitWidth, 40)
                            elide: Text.ElideRight
                        }
                        // Where the controls sit when the row has room.
                        Item {
                            id: controlsBeside
                            visible: !editorHeader.wraps
                            implicitWidth: patternControls.implicitWidth
                            implicitHeight: patternControls.implicitHeight
                            RowLayout {
                                id: patternControls
                                objectName: "patternControls"
                                parent: editorHeader.wraps ? controlsBelow : controlsBeside
                                spacing: 10
                                PatternLength {}
                                PatternSwing {}
                                PatternGroove {}
                                Rectangle {
                                    implicitWidth: 52; implicitHeight: 18; radius: 3
                                    color: "transparent"; border.color: Theme.line
                                    Label { anchors.centerIn: parent; text: songModel.bars + " BARS"
                                        color: Theme.muted; font.pixelSize: 9; font.letterSpacing: 0.5 }
                                }
                            }
                        }
                        // The hint gives way first: it elides before the
                        // title, the stem or the length is pushed off.
                        Label { text: qsTr("SPACE play  |  HOME rewind  |  ARROWS move  |  SHIFT+UP/DOWN transpose  |  INSERT push  |  ALT nudge  |  CTRL+D duplicate  |  CTRL+M mute  |  CTRL+L solo")
                            color: Theme.muted; font.pixelSize: 10
                            // It takes only the room left over, so the names
                            // never elide while it still shows a word.
                            Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredWidth: 0
                            horizontalAlignment: Text.AlignRight; elide: Text.ElideLeft }
                        // SETTINGS: the machine's audio, MIDI and window
                        // settings, in the editors' place. A second press goes
                        // back to the view it was opened from.
                        // A gear rather than a word: the row has no room
                        // for one beside docked bars.
                        Chip { id: settingsChip
                               objectName: "viewSETTINGS"; text: ""
                               implicitWidth: 28; implicitHeight: 20
                               accessibleLabel: "Settings: audio device, MIDI and window"
                               on: root.view === "SETTINGS"
                               onClicked: root.view = root.view === "SETTINGS" ? root.viewBeforeSettings
                                                                               : root.openSettings()
                               Item {
                                   anchors.centerIn: parent
                                   width: 13; height: 13
                                   readonly property color ink: settingsChip.on ? Theme.onAccent : Theme.ink
                                   Repeater {
                                       model: 4
                                       Rectangle {
                                           required property int index
                                           anchors.centerIn: parent
                                           width: 3; height: 13; radius: 0.5
                                           rotation: index * 45
                                           color: parent.ink
                                       }
                                   }
                                   Rectangle {
                                       anchors.centerIn: parent
                                       width: 9; height: 9; radius: 4.5
                                       color: parent.ink
                                   }
                                   Rectangle {
                                       anchors.centerIn: parent
                                       width: 4; height: 4; radius: 2
                                       color: settingsChip.on ? Theme.acid : Theme.bg
                                   }
                               } }
                        // The guided tour, at the end of the keys it explains.
                        // Lit while the tour runs; a press starts it or sends
                        // it away. The hints beside it elide to make room.
                        Chip { objectName: "keysButton"; text: "?"
                               implicitHeight: 20; implicitWidth: 26
                               hint: qsTr("Keyboard shortcuts (Ctrl+/)")
                               accessibleLabel: qsTr("Keyboard shortcuts")
                               on: keysSheet.opened
                               onClicked: keysSheet.opened ? keysSheet.close() : keysSheet.open() }
                        Chip { id: helpChip; objectName: "helpButton"; text: qsTr("HELP")
                               implicitHeight: 20
                               accessibleLabel: "Help: a guided tour"
                               on: tutorial.running
                               onClicked: tutorial.toggle() }
                        // The floating guide beside the tour: Blokkily
                        // narrates what happens, answers questions and
                        // teaches. Lit while it is on; a press opens its
                        // bubble (turning it on first), or closes it. It is
                        // turned off from the bubble.
                        Chip { id: guideChip; objectName: "guideButton"; text: qsTr("GUIDE")
                               implicitHeight: 20
                               accessibleLabel: "Guide: the floating assistant"
                               on: guideController.guideEnabled
                               onClicked: {
                                   if (!guideController.guideEnabled) guideController.guideEnabled = true
                                   if (guideController.bubbleVisible) guideController.dismiss()
                                   else guideController.open()
                               } }
                    }
                    // Where they go when it has not.
                    Item {
                        id: controlsBelow
                        visible: editorHeader.wraps
                        Layout.fillWidth: true
                        implicitHeight: patternControls.implicitHeight
                    }
                    }

                    // A plugin that crashed, hung or threw is silenced; this
                    // says which, and loads it again.
                    Rectangle {
                        objectName: "pluginFailureBanner"
                        Layout.row: 1
                        Layout.fillWidth: true
                        visible: appController.failedPlugins.length > 0
                        implicitHeight: failureRow.implicitHeight + 14
                        radius: 6
                        color: Qt.rgba(1, 0.3, 0.37, 0.12)
                        border.color: Theme.record
                        RowLayout {
                            id: failureRow
                            anchors.fill: parent; anchors.margins: 7; anchors.leftMargin: 12
                            spacing: 10
                            Rectangle {
                                implicitWidth: 18; implicitHeight: 18; radius: 9
                                color: Theme.record
                                Label { anchors.centerIn: parent; text: "!"; color: Theme.bg
                                        font.pixelSize: 12; font.bold: true }
                            }
                            Label {
                                objectName: "pluginFailureText"
                                Layout.fillWidth: true; Layout.minimumWidth: 0
                                text: (appController.failedPlugins.length === 1
                                       ? qsTr("%1 has stopped and is silenced. The song and everything else play on.")
                                       : qsTr("%1 have stopped and are silenced. The song and everything else play on."))
                                      .arg(appController.failedPlugins.join(", "))
                                color: Theme.ink; font.pixelSize: 11
                                wrapMode: Text.Wrap; maximumLineCount: 3
                                elide: Text.ElideRight
                            }
                            Chip {
                                objectName: "reloadPlugins"
                                text: qsTr("RELOAD"); implicitHeight: 22
                                accent: Theme.record; on: true
                                accessibleLabel: "Load the stopped plugins again"
                                onClicked: appController.reloadPlugins()
                            }
                        }
                    }

                    PromptBar {
                        id: promptBar
                        Layout.row: 2
                        Layout.fillWidth: true
                        focusHome: noteEntry
                    }

                    ArrangementView {
                        id: arrangementView
                        objectName: "arrangementView"
                        Layout.row: panelDock.rowOf("arrangement")
                        focusHome: noteEntry
                        onRenameRequested: (kind, index, name) => renamePopup.ask(kind, index, name)
                        onPatternMenuRequested: (index, name) => {
                            patternMenu.target = index
                            patternMenu.name = name
                            patternMenu.popup()
                        }
                        onTrackMenuRequested: (index, name) => root.showTrackMenu(index, name)
                    }

                    LauncherView {
                        Layout.row: panelDock.rowOf("editors")
                        view: root.view
                        focusHome: noteEntry
                    }

                    VersionsPanel {
                        Layout.row: panelDock.rowOf("editors")
                        view: root.view
                        focusHome: noteEntry
                    }

                    NotesPanel {
                        Layout.row: panelDock.rowOf("editors")
                        view: root.view
                        focusHome: noteEntry
                    }

                    SettingsPanel {
                        Layout.row: panelDock.rowOf("editors")
                        view: root.view
                        focusHome: noteEntry
                    }

                    StepEditors {
                        id: stepEditors
                        Layout.row: panelDock.rowOf("editors")
                        view: root.view
                        entryOctave: root.entryOctave
                        focusHome: noteEntry
                        onNoteWritten: (step, key) => root.writeNote(step, key)
                        onOctavePicked: octave => root.entryOctave = octave
                    }

                    KeyboardPanel {
                        id: keyboardPanel
                        Layout.row: panelDock.rowOf("keyboard")
                        view: root.view
                        focusHome: noteEntry
                        heightOverride: panelDock.heightOf("keyboard")
                    }

                    // The step's inspector and the modulators belong to the
                    // editors; VERSIONS, NOTES and SETTINGS take their place.
                    Inspector {
                        id: inspectorPanel
                        Layout.row: panelDock.rowOf("inspector")
                        visible: root.view !== "VERSIONS" && root.view !== "NOTES" && root.view !== "SETTINGS"
                    }

                    ModulationPanel {
                        id: modulationPanel
                        Layout.row: panelDock.rowOf("modulation")
                        visible: root.view !== "VERSIONS" && root.view !== "NOTES" && root.view !== "SETTINGS"
                    }
                }

                // Over the column: each panel's grip in the left margin, and
                // the gap under a panel that can be sized.
                PanelDock {
                    id: panelDock
                    objectName: "panelDock"
                    x: editorColumn.x; y: editorColumn.y
                    width: editorColumn.width; height: editorColumn.height
                    z: 5
                    rowBase: 3
                    spacing: editorColumn.rowSpacing
                    order: root.ui["ui/editorOrder"] !== undefined ? String(root.ui["ui/editorOrder"]).split(",") : []
                    heights: ({ "keyboard": root.uiNumber("ui/keyboardHeight", 0) })
                    panels: ({
                        "arrangement": arrangementView, "editors": stepEditors,
                        "keyboard": keyboardPanel, "inspector": inspectorPanel,
                        "modulation": modulationPanel
                    })
                    resizable: ["keyboard"]
                    onOrderPicked: order => appController.setUiSetting("ui/editorOrder", order.join(","))
                    onHeightPicked: (key, height) => appController.setUiSetting(
                        "ui/" + key + "Height", height > 0 ? Math.round(height) : undefined)
                }
            }
        }

        SideBar {
            id: mixer
            objectName: "mixer"
            z: 2
            anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
            edge: "right"
            title: qsTr("MIXER")
            autoHide: root.uiFlag("ui/autoHideMixer", true)
            roomy: workspace.roomy
            held: tutorial.running
            storedWidth: root.uiNumber("ui/mixerWidth", 250)
            minimumBarWidth: 210; maximumBarWidth: 420
            onWidthPicked: width => appController.setUiSetting("ui/mixerWidth", width)

            MixerPanel {
                anchors.fill: parent
                onTrackRenameRequested: (index, name) => renamePopup.ask("TRACK", index, name)
                onTrackMenuRequested: (index, name) => root.showTrackMenu(index, name)
            }
        }
    }
}
