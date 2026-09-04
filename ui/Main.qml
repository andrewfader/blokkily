import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: root
    objectName: "mainWindow"
    width: 1280; height: 800; visible: true
    title: "Blokkily"
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

    // STEP / TRACKER / PIANO focus one editor; ALL keeps every projection on
    // screen at once, which is the point of the instrument.
    property string view: "ALL"
    // Bit depth the next bounce is written at.
    property string exportDepth: "PCM24"
    readonly property bool showTracker: view === "ALL" || view === "TRACKER"
    readonly property bool showRoll: view === "ALL" || view === "PIANO"

    function fmt2(n) { return n.toString().padStart(2, "0") }

    FileDialog {
        id: openProjectDialog
        title: "Open Blokkily project"
        nameFilters: ["Blokkily projects (*.blok)", "All files (*)"]
        fileMode: FileDialog.OpenFile
        onAccepted: appController.loadProjectFile(selectedFile.toString())
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

    // Space plays, arrows move the step cursor, and the number row toggles the
    // step under it, so the grid can be driven without leaving the keyboard.
    Shortcut { sequence: "Space"; onActivated: appController.togglePlayback() }
    Shortcut { sequence: StandardKey.Open; onActivated: openProjectDialog.open() }
    Shortcut { sequence: StandardKey.Save; onActivated: saveProjectDialog.open() }
    Shortcut { sequence: "Ctrl+E"; onActivated: exportDialog.open() }
    Shortcut { sequence: "Return"; onActivated: transport.rewind() }
    Shortcut { sequence: "Left"
        onActivated: patternModel.selectStep(Math.max(0, patternModel.selectedStep - 1)) }
    Shortcut { sequence: "Right"
        onActivated: patternModel.selectStep(Math.min(15, patternModel.selectedStep + 1)) }
    Shortcut { sequence: "Up"; onActivated: patternModel.transposeSelected(1) }
    Shortcut { sequence: "Down"; onActivated: patternModel.transposeSelected(-1) }
    Shortcut { sequence: "Ctrl+Up"; onActivated: patternModel.transposeSelected(12) }
    Shortcut { sequence: "Ctrl+Down"; onActivated: patternModel.transposeSelected(-12) }
    Shortcut { sequence: "Delete"
        onActivated: { if (patternModel.selected.exists)
                           patternModel.toggleStep(patternModel.selectedStep, 60) } }

    component SectionLabel: Label {
        color: muted; font.pixelSize: 10; font.bold: true; font.letterSpacing: 1.4
    }

    component Chip: Rectangle {
        property alias text: chipText.text
        property bool on: false
        property color accent: acid
        signal clicked()
        implicitWidth: chipText.implicitWidth + 22; implicitHeight: 26
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
            onClicked: parent.clicked()
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
            from: parent.from; to: parent.to; value: parent.value
            onMoved: parent.moved(value)
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
                Rectangle {
                    implicitWidth: 30; implicitHeight: 28; radius: 4
                    color: raised; border.color: line
                    Label { anchors.centerIn: parent; text: "|<"; color: ink
                        font.family: "monospace"; font.pixelSize: 12; font.bold: true }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                        onClicked: transport.rewind() }
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

            Rectangle {
                Layout.preferredWidth: 88; Layout.preferredHeight: 30; radius: 4
                color: raised; border.color: line
                RowLayout {
                    anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 0
                    Label { text: transport.bpm.toFixed(2); color: ink
                        font.family: "monospace"; font.pixelSize: 13; font.bold: true }
                    Item { Layout.fillWidth: true }
                    Label { text: "BPM"; color: muted; font.pixelSize: 8; font.letterSpacing: 1 }
                }
                MouseArea {
                    anchors.fill: parent; cursorShape: Qt.SizeVerCursor
                    property real anchorY: 0
                    property real anchorBpm: 120
                    onPressed: function(mouse) { anchorY = mouse.y; anchorBpm = transport.bpm }
                    onPositionChanged: function(mouse) {
                        appController.setTempo(anchorBpm + (anchorY - mouse.y) * 0.5)
                    }
                }
            }

            Label { text: "4 / 4"; color: muted; font.pixelSize: 12 }

            Item { Layout.fillWidth: true }

            // View switcher — these actually switch the editors.
            RowLayout {
                objectName: "viewSwitcher"
                spacing: 4
                Repeater {
                    model: ["ALL", "STEP", "TRACKER", "PIANO"]
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
            Chip { text: "RESCAN PLUGINS"; onClicked: appController.scanPlugins() }
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

                SectionLabel { text: "PLUGINS" }
                ListView {
                    objectName: "pluginBrowser"
                    Layout.fillWidth: true; Layout.fillHeight: true
                    clip: true; spacing: 4
                    model: appController.plugins
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        width: ListView.view.width; height: 40; radius: 5
                        color: panel; border.color: line
                        MouseArea {
                            anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: appController.selectInstrument(index)
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
                                    font.pixelSize: 10; elide: Text.ElideRight }
                            }
                        }
                    }
                }

                SectionLabel { text: "PROJECT" }
                RowLayout {
                    Layout.fillWidth: true; spacing: 4
                    Chip { objectName: "openProjectButton"; text: "OPEN"
                        onClicked: openProjectDialog.open() }
                    Chip { objectName: "saveProjectButton"; text: "SAVE"
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
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 14; spacing: 10

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
                    Label { text: "SPACE play  |  ARROWS select  |  UP/DOWN transpose  |  DEL clear"
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
                        Repeater {
                            model: songModel.patterns
                            Chip {
                                required property var modelData
                                objectName: "patternChip" + modelData.index
                                text: modelData.name
                                on: modelData.current
                                onClicked: songModel.selectPattern(modelData.index)
                            }
                        }
                        Chip { objectName: "addPatternButton"; text: "+PAT"; accent: blue
                            onClicked: songModel.addPattern() }
                        Chip { objectName: "addTrackButton"; text: "+TRK"; accent: blue
                            onClicked: songModel.addTrack() }
                    }

                    Rectangle {
                        objectName: "arrangement"
                        Layout.fillWidth: true
                        // One lane per track plus the frame, so the timeline
                        // takes only the room it needs and the editors keep
                        // the rest of the window.
                        Layout.preferredHeight: 12 + Math.max(1, songModel.trackCount) * 24
                        radius: 6; color: panel; border.color: line; clip: true

                        ColumnLayout {
                            id: arrangementRows
                            objectName: "arrangementRows"
                            anchors.fill: parent; anchors.margins: 5; spacing: 2

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
                                        Label {
                                            anchors.centerIn: parent
                                            text: track.name
                                            color: track.audible ? ink : muted
                                            font.pixelSize: 9; font.bold: true
                                            font.letterSpacing: 0.6
                                        }
                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: songModel.selectTrack(trackIndex)
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
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: {
                                                    songModel.selectTrack(trackIndex)
                                                    songModel.toggleClip(trackIndex, index)
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
                        Label { text: "click toggles  |  shift-click selects"; color: muted
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
                                        if (mouse.modifiers & Qt.ShiftModifier)
                                            patternModel.selectStep(cell.index)
                                        else
                                            patternModel.toggleStep(cell.index, 60)
                                    }
                                }
                            }
                        }
                    }
                }

                // -------------------------------------------- tracker + piano roll
                SplitView {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    Layout.minimumHeight: 190
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
                                Layout.fillWidth: true
                                Label { text: "TRACKER"; color: ink; font.bold: true
                                    font.pixelSize: 12; font.letterSpacing: 1 }
                                Item { Layout.fillWidth: true }
                                Label { text: "16 ROWS"; color: muted; font.pixelSize: 9 }
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
                                objectName: "trackerRows"
                                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
                                Repeater {
                                    model: 16
                                    Rectangle {
                                        id: trackRow
                                        required property int index
                                        readonly property var row: patternModel.steps[index]
                                        readonly property bool playhead: transport.step === index
                                        objectName: "trackerRow" + index
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
                                                font.family: "monospace"; font.pixelSize: 12
                                            }
                                            Label {
                                                Layout.preferredWidth: 62
                                                text: trackRow.row ? trackRow.row.noteName : "---"
                                                color: trackRow.row && trackRow.row.active ? acid : "#3d434e"
                                                font.family: "monospace"; font.pixelSize: 12
                                                font.bold: trackRow.row && trackRow.row.active
                                            }
                                            Label {
                                                Layout.preferredWidth: 44
                                                text: trackRow.row ? trackRow.row.velocityHex : "--"
                                                color: trackRow.row && trackRow.row.active ? ink : "#3d434e"
                                                font.family: "monospace"; font.pixelSize: 12
                                            }
                                            Label {
                                                Layout.fillWidth: true
                                                text: trackRow.row ? trackRow.row.lockText : "---"
                                                color: trackRow.row && trackRow.row.hasLock ? amber : "#3d434e"
                                                font.family: "monospace"; font.pixelSize: 12
                                                font.bold: trackRow.row && trackRow.row.hasLock
                                            }
                                        }
                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: patternModel.selectStep(trackRow.index)
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
                                Repeater {
                                    model: patternModel
                                    Rectangle {
                                        required property int step
                                        required property int key
                                        required property int duration
                                        required property string velocityHex
                                        x: rollArea.gutter + step * rollArea.laneWidth + 1
                                        y: rollArea.laneY(key)
                                        width: Math.max(6, duration / 120 * rollArea.laneWidth - 2)
                                        height: Math.max(3, rollArea.laneHeight - 1)
                                        radius: 2
                                        color: patternModel.selectedStep === step ? amber : acid
                                    }
                                }
                                // Playhead across the roll.
                                Rectangle {
                                    objectName: "rollPlayhead"
                                    x: rollArea.gutter + transport.stepFraction * rollArea.laneWidth
                                    y: 0; width: 2; height: rollArea.height
                                    color: ink; opacity: 0.85
                                }
                            }
                        }
                    }
                }

                // ------------------------------------------------- step inspector
                Rectangle {
                    objectName: "stepInspector"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 206
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

                ColumnLayout {
                    objectName: "mixerStrips"
                    Layout.fillWidth: true; spacing: 6

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
                                onClicked: songModel.selectTrack(trackIndex)
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
                                    Rectangle {
                                        width: parent.width * modelData.peakFraction
                                        height: parent.height; radius: 2
                                        color: modelData.peak > 0.99 ? "#ff4d4d"
                                             : modelData.peak > 0.7 ? amber : acid
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

                Item { Layout.fillHeight: true }

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
                            readout: songModel.masterGainDb.toFixed(1) + " dB"
                            onMoved: function(v) { songModel.setMasterGain(v) }
                        }
                    }
                }
            }
        }
    }
}
