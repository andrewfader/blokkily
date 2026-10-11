import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Format.js" as Format

// The three projections of the canonical pattern that are edited step by
// step: the step grid, the tracker and the piano roll. Each one writes
// through the same pattern model, and a note typed or drawn in any of them
// updates that shared pattern; the roll addresses individual chord voices.
ColumnLayout {
    id: root
    // Which editors the window is showing: ALL, STEP, TRACKER, PIANO, KEYS or
    // LAUNCH (the scene launcher, which shows none of them).
    property string view: "ALL"
    readonly property real playStep: appController.patternPlayStep
    readonly property int playRow: playStep < 0 ? -1 : Math.floor(playStep)

    // The octave new notes land in, owned by the window.
    property int entryOctave: 3
    readonly property int entryBase: (entryOctave + 1) * 12
    readonly property bool showTracker: view === "ALL" || view === "TRACKER"
    readonly property bool showRoll: view === "ALL" || view === "PIANO"
    // Where the keyboard goes back to after a click in an editor.
    property Item focusHome: null
    // A pitch written onto a step: the window writes and sounds it.
    signal noteWritten(int step, int key)
    signal octavePicked(int octave)
    // Laid out as the grid and the tracker/roll split always were: the one
    // that fills is the one the view focuses, and in KEYS neither is shown.
    Layout.fillWidth: true; Layout.fillHeight: true
    visible: view !== "KEYS" && view !== "LAUNCH"
    spacing: 10

    // ------------------------------------------------------ step grid
    ColumnLayout {
        id: gridSection
        Layout.fillWidth: true
        Layout.fillHeight: root.view === "STEP"
        // The detailed grid's 104, plus a row of the stem rack per track.
        Layout.preferredHeight: 104 + stemRack.implicitHeight + spacing
        Layout.maximumHeight: root.view === "STEP" ? 100000
                                                   : 104 + stemRack.implicitHeight + spacing
        spacing: 5
        visible: root.view === "ALL" || root.view === "STEP"
        RowLayout {
            Layout.fillWidth: true
            SectionLabel { text: "STEP GRID" }
            Item { Layout.fillWidth: true }
            Label { text: "click toggles  |  shift-click selects  |  CTRL-click seeks  |  CTRL+1..0 toggles"; color: Theme.muted
                font.pixelSize: 9 }
        }
        // The stem rack: every stem's part of the open pattern, one row each,
        // as a channel rack shows it. The name picks the stem the editors
        // below show; a step toggles on that stem's own part.
        ColumnLayout {
            id: stemRack
            objectName: "stemRack"
            Layout.fillWidth: true
            // Its rows fill their own height, which would otherwise make the
            // rack compete with the detailed grid for the section's room.
            Layout.fillHeight: false
            spacing: 3
            Repeater {
                model: patternModel.stemRows
                RowLayout {
                    id: rackRow
                    required property var modelData
                    required property int index
                    objectName: "stemRow" + index
                    Layout.fillWidth: true
                    Layout.fillHeight: false
                    Layout.preferredHeight: 16
                    Layout.maximumHeight: 16
                    spacing: 3
                    Rectangle {
                        objectName: "stemName" + rackRow.index
                        Layout.preferredWidth: 72; Layout.fillHeight: true
                        radius: 3
                        color: rackRow.modelData.selected ? Theme.raised : "transparent"
                        border.color: rackRow.modelData.selected ? Theme.acid : Theme.line
                        Label {
                            anchors.fill: parent; anchors.leftMargin: 5; anchors.rightMargin: 3
                            verticalAlignment: Text.AlignVCenter
                            text: (rackRow.modelData.selected ? "▸ " : "") + rackRow.modelData.name
                            color: rackRow.modelData.selected ? Theme.acid : Theme.ink
                            font.pixelSize: 9; font.bold: true; elide: Text.ElideRight
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                // Read before selecting: the rows are rebuilt.
                                const track = rackRow.modelData.track
                                const home = root.focusHome
                                songModel.selectTrack(track)
                                if (home) home.forceActiveFocus()
                            }
                        }
                    }
                    Repeater {
                        model: rackRow.modelData.steps
                        Rectangle {
                            required property var modelData
                            required property int index
                            objectName: "stemStep" + rackRow.index + "-" + index
                            Layout.fillWidth: true; Layout.fillHeight: true
                            radius: 2
                            color: modelData ? (rackRow.modelData.selected ? Theme.acid : Theme.blue)
                                             : (index % 4 === 0 ? "#262a33" : Theme.panel)
                            border.color: root.playRow === index ? Theme.ink : Theme.line
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    const track = rackRow.modelData.track
                                    const step = parent.index
                                    const home = root.focusHome
                                    patternModel.toggleStemStep(track, step, root.entryBase)
                                    if (home) home.forceActiveFocus()
                                }
                            }
                        }
                    }
                }
            }
        }
        RowLayout {
            objectName: "stepGrid"
            Layout.fillWidth: true; Layout.fillHeight: true
            spacing: 5
            // One column per step of the open pattern: sixteen in 4/4,
            // fourteen for a 7/8 bar, or whatever its LEN says.
            Repeater {
                model: patternModel.stepCount
                Rectangle {
                    id: cell
                    required property int index
                    readonly property var row: patternModel.steps[index]
                    readonly property bool active: row !== undefined && row.active === true
                    readonly property bool selected: row !== undefined && row.selected === true
                    readonly property bool onBeat: index % 4 === 0
                    readonly property bool playhead: root.playRow === index
                    objectName: "step" + index
                    Layout.fillWidth: true; Layout.fillHeight: true
                    radius: 5
                    color: active ? Theme.acid : (onBeat ? "#262a33" : Theme.panel)
                    border.width: selected ? 2 : 1
                    border.color: selected ? Theme.amber
                                  : (playhead ? Theme.ink : (onBeat ? "#4d5462" : Theme.line))

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
                        text: Format.fmt2(cell.index + 1)
                        color: cell.active ? (cell.tall && cell.velocity < 0.92
                                              ? Theme.ink : "#0e0f12")
                                           : (cell.onBeat ? Theme.ink : Theme.muted)
                        font.family: Theme.mono; font.pixelSize: 11
                        font.bold: cell.onBeat
                    }
                    Label {
                        visible: cell.active
                        anchors.bottom: parent.bottom; anchors.bottomMargin: 11
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: cell.row ? cell.row.noteName : ""
                        color: "#0e0f12"; font.family: Theme.mono
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
                        height: 3; radius: 2; color: Theme.ink
                    }
                    MouseArea {
                        anchors.fill: parent; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: function(mouse) {
                            root.focusHome.forceActiveFocus()
                            if (mouse.modifiers & Qt.ControlModifier) {
                                // Jump the song to this column of the
                                // pass of the pattern it is already in,
                                // engine and all, in whatever meter.
                                appController.seekToPatternStep(cell.index)
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
        // Enough height that every tracker row is readable rather than a
        // sliver.
        Layout.fillWidth: true; Layout.fillHeight: true
        Layout.minimumHeight: 250
        orientation: Qt.Horizontal
        visible: root.showTracker || root.showRoll

        Rectangle {
            id: trackerPanel
            objectName: "trackerView"
            visible: root.showTracker
            SplitView.fillWidth: root.view === "TRACKER"
            SplitView.preferredWidth: 340
            SplitView.minimumWidth: 260
            color: Theme.panel; radius: 6; border.color: Theme.line; clip: true
            // Every stem of the open pattern is a channel, side by side, the
            // way a tracker has always been read: across for what plays
            // together, down for time. The selected channel is the one the
            // keys write into; Tab and Shift+Tab move between channels.
            readonly property int channelCount: patternModel.trackerChannels.length
            readonly property real rowNumberWidth: 30
            readonly property real channelWidth:
                Math.max(128, (trackerScroll.width - rowNumberWidth)
                              / Math.max(1, channelCount))
            readonly property real headerHeight: 20
            // Rows keep a readable height and the view scrolls once they
            // stop fitting, rather than shrinking a long pattern to specks.
            readonly property int fitHeight:
                Math.floor((trackerScroll.height - headerHeight)
                           / Math.max(1, patternModel.stepCount))
            readonly property int rowHeight: fitHeight >= 11 ? fitHeight : 14
            readonly property int rowFont: Math.max(9, Math.min(13, rowHeight - 3))
            function selectedChannel() {
                var channels = patternModel.trackerChannels
                for (var c = 0; c < channels.length; ++c)
                    if (channels[c].selected) return c
                return 0
            }
            // Keeps a row (and the selected channel) on screen.
            function reveal(step) {
                if (step < 0) return
                var top = headerHeight + step * rowHeight
                var view = trackerScroll.height
                if (top - headerHeight < trackerScroll.contentY)
                    trackerScroll.contentY = Math.max(0, top - headerHeight)
                else if (top + rowHeight > trackerScroll.contentY + view)
                    trackerScroll.contentY = Math.min(
                        Math.max(0, trackerScroll.contentHeight - view),
                        top + rowHeight - view)
                var left = rowNumberWidth + selectedChannel() * channelWidth
                if (left - rowNumberWidth < trackerScroll.contentX)
                    trackerScroll.contentX = Math.max(0, left - rowNumberWidth)
                else if (left + channelWidth > trackerScroll.contentX + trackerScroll.width)
                    trackerScroll.contentX = left + channelWidth - trackerScroll.width
            }
            Connections {
                target: patternModel
                function onSelectionChanged() { trackerPanel.reveal(patternModel.selectedStep) }
            }
            Connections {
                target: appController
                function onPatternPlayheadChanged() {
                    if (transport.playing)
                        trackerPanel.reveal(Math.floor(appController.patternPlayStep))
                }
            }
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 10; spacing: 6
                RowLayout {
                    Layout.fillWidth: true; spacing: 6
                    Label { text: "TRACKER"; color: Theme.ink; font.bold: true
                        font.pixelSize: 12; font.letterSpacing: 1 }
                    Item { Layout.fillWidth: true }
                    // The letter keys write in this octave. Without
                    // it a typed note is a guess about where it lands.
                    Chip {
                        objectName: "octaveDown"; text: "OCT-"
                        onClicked: root.octavePicked(Math.max(0, root.entryOctave - 1))
                    }
                    Label {
                        objectName: "octaveReadout"
                        text: "O" + root.entryOctave; color: Theme.amber
                        font.family: Theme.mono; font.pixelSize: 11; font.bold: true
                    }
                    Chip {
                        objectName: "octaveUp"; text: "OCT+"
                        onClicked: root.octavePicked(Math.min(8, root.entryOctave + 1))
                    }
                    Label { text: "TYPE ZSXDCVGBHNJM  ·  TAB CHANNEL  ·  DRAG VEL/FX"
                        color: Theme.muted; font.pixelSize: 9; font.family: Theme.mono
                        Layout.minimumWidth: 0; Layout.fillWidth: true; elide: Text.ElideRight }
                }
                Flickable {
                    id: trackerScroll
                    objectName: "trackerScroll"
                    Layout.fillWidth: true; Layout.fillHeight: true
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    contentWidth: trackerPanel.rowNumberWidth
                                  + trackerPanel.channelCount * trackerPanel.channelWidth
                    contentHeight: trackerPanel.headerHeight
                                   + patternModel.stepCount * trackerPanel.rowHeight
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

                    // Every row is shown, empty ones included — a tracker
                    // that hides its empty rows is not a tracker.
                    Column {
                        id: trackerRows
                        objectName: "trackerRows"
                        y: trackerPanel.headerHeight
                        width: trackerScroll.contentWidth
                        readonly property int rowFont: trackerPanel.rowFont
                        Repeater {
                            model: patternModel.stepCount
                            Rectangle {
                                id: trackRow
                                required property int index
                                readonly property var row: patternModel.steps[index]
                                readonly property bool playhead: root.playRow === index
                                readonly property int rowFont: trackerPanel.rowFont
                                objectName: "trackerRow" + index
                                width: trackerRows.width
                                height: trackerPanel.rowHeight
                                color: playhead ? "#3c4930"
                                       : (row && row.selected ? "#2b2f38"
                                          : (index % 4 === 0 ? "#1e2128" : "transparent"))
                                // A press anywhere on the row that no
                                // column takes moves the cursor there.
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        root.focusHome.forceActiveFocus()
                                        patternModel.selectStep(trackRow.index)
                                    }
                                }
                                Repeater {
                                    model: trackerPanel.channelCount
                                    Item {
                                        id: channelCell
                                        required property int index
                                        readonly property var channel:
                                            patternModel.trackerChannels[index]
                                        readonly property bool editing:
                                            channel !== undefined && channel.selected
                                        // The selected channel shows the live
                                        // row, so its edits read back at once.
                                        readonly property var cell: editing ? trackRow.row
                                            : (channel !== undefined
                                               ? channel.rows[trackRow.index] : undefined)
                                        readonly property bool active:
                                            cell !== undefined && cell.active
                                        // Moves the cursor onto this channel.
                                        // True when it was elsewhere, so the
                                        // press is only a move, not an edit.
                                        function claim() {
                                            root.focusHome.forceActiveFocus()
                                            if (editing) {
                                                patternModel.selectStep(trackRow.index)
                                                return false
                                            }
                                            patternModel.selectChannelStep(index, trackRow.index)
                                            return true
                                        }
                                        objectName: "trackerChannel" + index + "-" + trackRow.index
                                        x: trackerPanel.rowNumberWidth
                                           + index * trackerPanel.channelWidth
                                        width: trackerPanel.channelWidth; height: parent.height
                                        Rectangle {
                                            anchors.left: parent.left; anchors.top: parent.top
                                            anchors.bottom: parent.bottom
                                            width: 1; color: Theme.line
                                        }
                                        Rectangle {
                                            visible: channelCell.editing && trackRow.row
                                                     && trackRow.row.selected
                                            anchors.left: parent.left; anchors.top: parent.top
                                            anchors.bottom: parent.bottom
                                            width: 2; color: Theme.amber
                                        }
                                        // The note column is where a tracker
                                        // is written, so it takes input: a
                                        // click puts the entry octave's root
                                        // on the row, a drag moves the pitch
                                        // by semitones, and a right-click
                                        // empties the row.
                                        Item {
                                            objectName: channelCell.editing
                                                        ? "trackerNote" + trackRow.index
                                                        : "trackerNote" + channelCell.index
                                                          + "-" + trackRow.index
                                            x: 8; width: 54; height: parent.height
                                            Label {
                                                anchors.verticalCenter: parent.verticalCenter
                                                text: channelCell.cell ? channelCell.cell.noteName
                                                                       : "---"
                                                color: channelCell.active ? Theme.acid : "#3d434e"
                                                font.family: Theme.mono
                                                font.pixelSize: trackRow.rowFont
                                                font.bold: channelCell.active
                                            }
                                            MouseArea {
                                                anchors.fill: parent
                                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                                cursorShape: Qt.PointingHandCursor
                                                // A drag here edits; it never
                                                // scrolls the rows.
                                                preventStealing: true
                                                property real anchorY: 0
                                                property int anchorKey: 60
                                                property bool live: false
                                                onPressed: function(mouse) {
                                                    live = !channelCell.claim()
                                                    if (!live) return
                                                    songModel.beginGesture()
                                                    if (mouse.button === Qt.RightButton) {
                                                        patternModel.clearStep(trackRow.index)
                                                        return
                                                    }
                                                    anchorY = mouse.y
                                                    if (trackRow.row && trackRow.row.active) {
                                                        // A filled row is selected and
                                                        // heard; its pitch (a chord's
                                                        // voices included) is only
                                                        // touched by a drag.
                                                        anchorKey = trackRow.row.key
                                                        patternModel.selectStep(trackRow.index)
                                                        appController.auditionStep(trackRow.index)
                                                        return
                                                    }
                                                    anchorKey = root.entryBase
                                                    root.noteWritten(trackRow.index, anchorKey)
                                                }
                                                onPositionChanged: function(mouse) {
                                                    if (!live || !pressed
                                                        || mouse.buttons !== Qt.LeftButton)
                                                        return
                                                    var moved = Math.round((anchorY - mouse.y) / 6)
                                                    if (moved === 0) return
                                                    patternModel.setStepKey(trackRow.index,
                                                                            anchorKey + moved)
                                                }
                                                onReleased: if (live) songModel.endGesture()
                                                onCanceled: if (live) songModel.endGesture()
                                            }
                                        }
                                        Item {
                                            objectName: channelCell.editing
                                                        ? "trackerVel" + trackRow.index
                                                        : "trackerVel" + channelCell.index
                                                          + "-" + trackRow.index
                                            x: 64; width: 28; height: parent.height
                                            Label {
                                                anchors.verticalCenter: parent.verticalCenter
                                                text: channelCell.cell
                                                      ? channelCell.cell.velocityHex : "--"
                                                color: channelCell.active ? Theme.ink : "#3d434e"
                                                font.family: Theme.mono
                                                font.pixelSize: trackRow.rowFont
                                            }
                                            MouseArea {
                                                anchors.fill: parent
                                                cursorShape: Qt.SizeVerCursor
                                                preventStealing: true
                                                property real anchorY: 0
                                                property real anchorVel: 0.9
                                                property bool live: false
                                                onPressed: function(mouse) {
                                                    live = !channelCell.claim()
                                                        && trackRow.row && trackRow.row.active
                                                    if (!live) return
                                                    songModel.beginGesture()
                                                    anchorY = mouse.y
                                                    anchorVel = trackRow.row.velocity
                                                }
                                                onPositionChanged: function(mouse) {
                                                    if (!live || !pressed) return
                                                    var next = anchorVel + (anchorY - mouse.y) / 40
                                                    patternModel.setSelectedVelocity(
                                                        Math.max(0, Math.min(1, next)))
                                                }
                                                onReleased: if (live) songModel.endGesture()
                                                onCanceled: if (live) songModel.endGesture()
                                            }
                                        }
                                        Item {
                                            objectName: channelCell.editing
                                                        ? "trackerFx" + trackRow.index
                                                        : "trackerFx" + channelCell.index
                                                          + "-" + trackRow.index
                                            x: 96; width: parent.width - 100
                                            height: parent.height
                                            Label {
                                                anchors.verticalCenter: parent.verticalCenter
                                                text: channelCell.cell
                                                      ? channelCell.cell.lockText : "---"
                                                color: channelCell.cell && channelCell.cell.hasLock
                                                       ? Theme.amber : "#3d434e"
                                                font.family: Theme.mono
                                                font.pixelSize: trackRow.rowFont
                                                font.bold: channelCell.cell
                                                           && channelCell.cell.hasLock
                                            }
                                            MouseArea {
                                                anchors.fill: parent
                                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                                cursorShape: Qt.SizeVerCursor
                                                preventStealing: true
                                                property real anchorY: 0
                                                property real anchorValue: 0.75
                                                property bool live: false
                                                onPressed: function(mouse) {
                                                    live = !channelCell.claim()
                                                        && trackRow.row && trackRow.row.active
                                                    if (!live) return
                                                    if (mouse.button === Qt.RightButton) {
                                                        live = false
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
                                                    if (!live || !pressed) return
                                                    if (!(mouse.buttons & Qt.LeftButton)) return
                                                    var next = anchorValue
                                                               + (anchorY - mouse.y) / 40
                                                    patternModel.setSelectedLock(
                                                        0, Math.max(0, Math.min(1, next)),
                                                        false)
                                                }
                                                onReleased: if (live) songModel.endGesture()
                                                onCanceled: if (live) songModel.endGesture()
                                            }
                                        }
                                    }
                                }
                                // The row number stays put while the
                                // channels scroll sideways under it.
                                Rectangle {
                                    x: trackerScroll.contentX
                                    width: trackerPanel.rowNumberWidth; height: parent.height
                                    color: trackRow.playhead ? Theme.acid : Theme.panel
                                    Label {
                                        anchors.verticalCenter: parent.verticalCenter
                                        x: 2
                                        text: trackRow.index.toString(16).toUpperCase()
                                                  .padStart(2, "0")
                                        color: trackRow.playhead ? Theme.bg
                                               : (trackRow.index % 4 === 0 ? Theme.ink : Theme.muted)
                                        font.family: Theme.mono
                                        font.pixelSize: trackRow.rowFont
                                    }
                                }
                            }
                        }
                    }
                    // The channel names stay on top while the rows scroll
                    // under them; a name picks that channel.
                    Rectangle {
                        objectName: "trackerHeader"
                        y: trackerScroll.contentY; z: 2
                        width: trackerScroll.contentWidth
                        height: trackerPanel.headerHeight
                        color: "#22262e"
                        Repeater {
                            model: trackerPanel.channelCount
                            Rectangle {
                                id: channelHead
                                required property int index
                                readonly property var channel:
                                    patternModel.trackerChannels[index]
                                objectName: "trackerChannelName" + index
                                x: trackerPanel.rowNumberWidth + index * trackerPanel.channelWidth
                                width: trackerPanel.channelWidth; height: parent.height
                                color: channel !== undefined && channel.selected
                                       ? "#3a3f2a" : "transparent"
                                border.color: Theme.line
                                Label {
                                    anchors.verticalCenter: parent.verticalCenter
                                    x: 8; width: parent.width - 12
                                    text: channelHead.channel !== undefined
                                          ? channelHead.channel.name : ""
                                    elide: Text.ElideRight
                                    color: channelHead.channel !== undefined
                                           && channelHead.channel.selected
                                           ? Theme.acid : Theme.muted
                                    font.family: Theme.mono; font.pixelSize: 10; font.bold: true
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        root.focusHome.forceActiveFocus()
                                        patternModel.selectChannelStep(
                                            channelHead.index,
                                            Math.max(0, patternModel.selectedStep))
                                    }
                                }
                            }
                        }
                        Rectangle {
                            x: trackerScroll.contentX
                            width: trackerPanel.rowNumberWidth; height: parent.height
                            color: "#22262e"
                            Label { anchors.centerIn: parent; text: "ROW"; color: Theme.muted
                                font.family: Theme.mono; font.pixelSize: 9 }
                        }
                    }
                }
            }
        }

        Rectangle {
            objectName: "pianoRollView"
            visible: root.showRoll
            SplitView.fillWidth: true
            color: Theme.panel; radius: 6; border.color: Theme.line; clip: true
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 10; spacing: 6
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "PIANO ROLL"; color: Theme.ink; font.bold: true
                        font.pixelSize: 12; font.letterSpacing: 1 }
                    // The hint gives way to the roll: at its full length it
                    // made the header wider than the panel, which pushed the
                    // last columns of the roll out of sight and out of reach.
                    Label { text: "drag draws  |  drag a note to move  |  drag its end to lengthen  |  right-click erases"; color: Theme.muted
                        font.pixelSize: 9
                        Layout.fillWidth: true; Layout.minimumWidth: 0
                        horizontalAlignment: Text.AlignRight; elide: Text.ElideRight }
                    Label {
                        objectName: "rollRange"
                        text: patternModel.lowKey + "-" + patternModel.highKey + " KEYS"
                        color: Theme.muted; font.pixelSize: 9; font.family: Theme.mono
                    }
                    // The roll's pitch window moves an octave a press (or a
                    // semitone a wheel notch over the roll, an octave with
                    // Shift), so any of the 128 keys can be drawn on.
                    Chip {
                        objectName: "rollDown"; text: "▼"
                        implicitHeight: 20; implicitWidth: 26
                        accessibleLabel: "Pitch window down an octave"
                        enabled: patternModel.lowKey > 0
                        onClicked: patternModel.scrollRoll(-12)
                    }
                    Chip {
                        objectName: "rollUp"; text: "▲"
                        implicitHeight: 20; implicitWidth: 26
                        accessibleLabel: "Pitch window up an octave"
                        enabled: patternModel.highKey < 127
                        onClicked: patternModel.scrollRoll(12)
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
                    readonly property int columns: Math.max(1, patternModel.stepCount)
                    readonly property real laneWidth: (width - gutter) / columns
                    function isBlack(key) {
                        var pc = key % 12
                        return pc === 1 || pc === 3 || pc === 6 || pc === 8 || pc === 10
                    }
                    // Highest pitch on top, so the roll reads like a keyboard.
                    function laneY(key) {
                        return (patternModel.highKey - key) * laneHeight
                    }

                    // A wheel over the roll moves its pitch window.
                    WheelHandler {
                        objectName: "rollWheel"
                        property real pending: 0
                        onWheel: function(event) {
                            pending += event.angleDelta.y
                            var notches = pending > 0 ? Math.floor(pending / 120)
                                                      : Math.ceil(pending / 120)
                            if (notches === 0) return
                            pending -= notches * 120
                            var per = (event.modifiers & Qt.ShiftModifier) ? 12 : 1
                            patternModel.scrollRoll(notches * per)
                        }
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
                                color: Theme.ink; font.pixelSize: 9; font.family: Theme.mono
                                font.bold: true
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onPressed: {
                                    root.focusHome.forceActiveFocus()
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
                        model: rollArea.columns + 1
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
                            // Each voice of a chord keeps its own length and
                            // velocity: a bar as long as that voice lasts,
                            // as strong as it was struck.
                            required property var voiceLengths
                            required property var voiceVelocities
                            objectName: "rollNote" + step
                            x: rollArea.gutter + step * rollArea.laneWidth + 1
                            width: Math.max(6, duration / 120 * rollArea.laneWidth - 2)
                            height: rollArea.height
                            Repeater {
                                model: rollNote.voiceKeys
                                Rectangle {
                                    required property var modelData
                                    required property int index
                                    readonly property real voiceLength:
                                        rollNote.voiceLengths && index < rollNote.voiceLengths.length
                                        ? rollNote.voiceLengths[index] : rollNote.duration
                                    readonly property real voiceVelocity:
                                        rollNote.voiceVelocities
                                        && index < rollNote.voiceVelocities.length
                                        ? rollNote.voiceVelocities[index] : 1
                                    y: rollArea.laneY(modelData)
                                    width: Math.max(6, voiceLength / 120 * rollArea.laneWidth - 2)
                                    opacity: 0.45 + 0.55 * voiceVelocity
                                    height: Math.max(3, rollArea.laneHeight - 1)
                                    radius: 2
                                    color: patternModel.selectedStep === rollNote.step
                                           ? Theme.amber : Theme.acid
                                }
                            }
                        }
                    }
                    // Playhead across the roll.
                    Rectangle {
                        objectName: "rollPlayhead"
                        visible: root.playStep >= 0
                        x: rollArea.gutter + Math.min(rollArea.columns,
                               root.playStep) * rollArea.laneWidth
                        y: 0; width: 2; height: rollArea.height
                        color: transport.playing ? Theme.acid : Theme.ink; opacity: 0.95
                    }

                    // The roll is an editor, not a picture of one.
                    // Drawing adds one pitch, dragging edits one voice, and
                    // the right button erases only the voice under the pointer.
                    // The grid, tracker and inspector follow the same edit to
                    // the canonical pattern.
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
                        property int dragKey: 0
                        property int originStep: -1
                        property real pressX: 0
                        property real pressY: 0
                        property bool moved: false

                        function stepAt(px) {
                            return Math.max(0, Math.min(rollArea.columns - 1,
                                Math.floor(px / rollArea.laneWidth)))
                        }
                        function keyAt(py) {
                            return Math.max(0, Math.min(127, patternModel.highKey -
                                Math.floor(py / rollArea.laneHeight)))
                        }
                        function coveringNote(px, py) {
                            var key = keyAt(py)
                            // Match each voice's length, including its minimum
                            // visible size and the one-pixel drawing inset so
                            // the right edge remains easy to grab.
                            for (var i = rollArea.columns - 1; i >= 0; --i) {
                                var duration = patternModel.rollNoteDuration(i, key)
                                if (duration <= 0) continue
                                var start = i * rollArea.laneWidth
                                var width = Math.max(8, duration / 120 * rollArea.laneWidth)
                                if (px >= start - 1 && px <= start + width + 1) return i
                            }
                            return -1
                        }
                        function paint(mouse) {
                            var step = stepAt(mouse.x)
                            var key = keyAt(mouse.y)
                            if (step === lastStep && key === lastKey) return
                            lastStep = step
                            lastKey = key
                            if (mouse.buttons & Qt.RightButton) {
                                var hit = coveringNote(mouse.x, mouse.y)
                                if (hit >= 0) patternModel.eraseRollNote(hit, key)
                            } else {
                                patternModel.addRollNote(step, key)
                                appController.auditionStep(step)
                            }
                        }
                        onPressed: function(mouse) {
                            root.focusHome.forceActiveFocus()
                            // Keep pitch lanes fixed throughout an edit.
                            patternModel.scrollRoll(0)
                            songModel.beginGesture()
                            lastStep = -1
                            lastKey = -1
                            dragMode = 0
                            dragStep = -1
                            moved = false
                            pressX = mouse.x
                            pressY = mouse.y
                            var key = keyAt(mouse.y)
                            var hit = coveringNote(mouse.x, mouse.y)
                            if (mouse.button === Qt.RightButton) {
                                paint(mouse)
                                return
                            }
                            if (hit >= 0) {
                                var dur = patternModel.rollNoteDuration(hit, key)
                                var end = hit + dur / 120
                                var xStep = mouse.x / rollArea.laneWidth
                                patternModel.selectStep(hit)
                                grabKey = key
                                dragKey = key
                                dragStep = hit
                                originStep = hit
                                var handle = Math.max(end - 0.25, hit + 0.55)
                                dragMode = xStep >= handle ? 2 : 1
                                return
                            }
                            paint(mouse)
                        }
                        onPositionChanged: function(mouse) {
                            if (!pressed) return
                            if (dragMode !== 0 && !moved) {
                                if (Math.abs(mouse.x - pressX) + Math.abs(mouse.y - pressY)
                                        < Qt.styleHints.startDragDistance) return
                                moved = true
                            }
                            if (dragMode === 2 && dragStep >= 0) {
                                var ticks = Math.round(
                                    (mouse.x / rollArea.laneWidth - dragStep) * 120)
                                patternModel.resizeRollNote(dragStep, dragKey, ticks)
                                return
                            }
                            if (dragMode === 1 && dragStep >= 0) {
                                var step = Math.max(0, Math.min(rollArea.columns - 1,
                                    originStep + Math.round((mouse.x - pressX) / rollArea.laneWidth)))
                                var key = Math.max(0, Math.min(127,
                                    grabKey + keyAt(mouse.y) - keyAt(pressY)))
                                if (step === dragStep && key === dragKey) return
                                if (patternModel.moveRollNote(dragStep, dragKey, step, key)) {
                                    dragStep = step
                                    dragKey = key
                                }
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
                        color: Theme.amber; opacity: 0.10
                    }
                }

                // The controller lane (wave 4.1): one controller of the open
                // pattern at a time - the wheel, the mod wheel, expression,
                // any other CC, or channel pressure - drawn under the roll on
                // the same columns. A drag draws a stroke, a drag that starts
                // on a point moves it, and the right button erases. Every
                // edit goes into the canonical pattern as one step of
                // history, and the engine plays it on its next block.
                ColumnLayout {
                    id: rollControls
                    objectName: "rollControls"
                    Layout.fillWidth: true
                    spacing: 3
                    // Every movement the pattern holds, whichever lane shows.
                    readonly property var marks: patternModel.controls
                    readonly property int count: marks.length
                    property string laneKind: "bend"
                    property int laneController: 1
                    readonly property bool bend: laneKind === "bend"
                    readonly property string laneName: laneKind === "bend" ? "BEND"
                        : laneKind === "pressure" ? "PRESSURE"
                        : laneController === 1 ? "MOD"
                        : laneController === 11 ? "EXPR" : "CC"
                    readonly property var lanePoints: marks.filter(function(mark) {
                        return mark.kind === laneKind &&
                               (laneKind !== "cc" || mark.controller === laneController)
                    })
                    function pick(name) {
                        if (name === "BEND") laneKind = "bend"
                        else if (name === "PRESSURE") laneKind = "pressure"
                        else {
                            laneKind = "cc"
                            laneController = name === "MOD" ? 1 : name === "EXPR" ? 11
                                : (laneController === 1 || laneController === 11 ? 7 : laneController)
                        }
                    }
                    // The next controller a lane may show: 0..119, never
                    // the sustain pedal, which the input keeps.
                    function stepController(delta) {
                        var next = laneController + delta
                        if (next === 64) next += delta
                        laneController = Math.max(0, Math.min(119, next))
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 5
                        Label { text: "CTRL"; color: Theme.muted; font.pixelSize: 9
                            font.bold: true; font.letterSpacing: 1 }
                        Picker {
                            objectName: "controllerLanePicker"
                            implicitHeight: 20
                            choices: ["BEND", "MOD", "EXPR", "CC", "PRESSURE"]
                            value: rollControls.laneName
                            onPicked: function(name) { rollControls.pick(name) }
                        }
                        Chip {
                            objectName: "controllerNumberDown"
                            visible: rollControls.laneKind === "cc"
                            text: "−"; implicitHeight: 20; implicitWidth: 22
                            accessibleLabel: "Lower controller number"
                            onClicked: rollControls.stepController(-1)
                        }
                        Label {
                            objectName: "controllerNumber"
                            visible: rollControls.laneKind === "cc"
                            text: "CC " + rollControls.laneController
                            color: Theme.ink; font.pixelSize: 10; font.family: Theme.mono
                        }
                        Chip {
                            objectName: "controllerNumberUp"
                            visible: rollControls.laneKind === "cc"
                            text: "+"; implicitHeight: 20; implicitWidth: 22
                            accessibleLabel: "Raise controller number"
                            onClicked: rollControls.stepController(1)
                        }
                        Label {
                            Layout.fillWidth: true; Layout.minimumWidth: 0
                            horizontalAlignment: Text.AlignRight; elide: Text.ElideRight
                            text: "drag draws  |  drag a point to move  |  right-click erases"
                            color: Theme.muted; font.pixelSize: 9
                        }
                        Label {
                            objectName: "controllerPointCount"
                            text: rollControls.lanePoints.length + " PTS"
                            color: Theme.muted; font.pixelSize: 9; font.family: Theme.mono
                        }
                    }
                    Rectangle {
                        id: laneArea
                        objectName: "controllerLane"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 56; Layout.minimumHeight: 40
                        color: "#0c0e11"; radius: 3
                        border.color: Theme.line
                        readonly property real plotX: rollArea.gutter
                        readonly property real plotWidth: width - rollArea.gutter
                        readonly property real ticks: Math.max(1, patternModel.patternTicks)
                        function xOf(tick) { return plotX + tick / 120 * rollArea.laneWidth }
                        // Where a level sits: the wheel from the middle, the
                        // rest from the floor.
                        function yOf(level) {
                            return rollControls.bend ? (1 - level) / 2 * (height - 4) + 2
                                                     : (1 - level) * (height - 4) + 2
                        }
                        // The wheel's rest line, or the floor.
                        Rectangle {
                            x: laneArea.plotX; width: laneArea.plotWidth; height: 1
                            y: rollControls.bend ? laneArea.height / 2 : laneArea.height - 2
                            color: Theme.line
                        }
                        Label {
                            x: 4; anchors.verticalCenter: parent.verticalCenter
                            text: rollControls.laneName
                            color: Theme.muted; font.pixelSize: 8; font.bold: true
                        }
                        Repeater {
                            model: rollControls.lanePoints
                            Rectangle {
                                required property var modelData
                                required property int index
                                objectName: "controllerPoint" + index
                                readonly property real levelY: laneArea.yOf(modelData.level)
                                readonly property real base: rollControls.bend ? laneArea.height / 2
                                                                               : laneArea.height - 2
                                x: laneArea.xOf(modelData.tick) - 1
                                width: 3
                                y: Math.min(levelY, base)
                                height: Math.max(2, Math.abs(base - levelY))
                                color: rollControls.bend ? Theme.amber
                                     : rollControls.laneKind === "cc" ? Theme.blue : Theme.acid
                            }
                        }
                        MouseArea {
                            id: laneInput
                            objectName: "controllerLaneInput"
                            x: laneArea.plotX; width: laneArea.plotWidth; height: parent.height
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            cursorShape: Qt.CrossCursor
                            property int lastTick: -1
                            property real lastLevel: 0
                            property int grabbed: -1
                            property bool erasing: false
                            function tickAt(px) {
                                return Math.max(0, Math.min(laneArea.ticks - 1,
                                    Math.round(px / rollArea.laneWidth * 120)))
                            }
                            function levelAt(py) {
                                var h = laneArea.height - 4
                                var unit = Math.max(0, Math.min(1, 1 - (py - 2) / h))
                                return rollControls.bend ? unit * 2 - 1 : unit
                            }
                            // The lane's point within a few pixels, or -1.
                            function pointNear(px) {
                                var best = -1, distance = 6
                                var points = rollControls.lanePoints
                                for (var i = 0; i < points.length; ++i) {
                                    var d = Math.abs(points[i].tick / 120 * rollArea.laneWidth - px)
                                    if (d <= distance) { distance = d; best = points[i].tick }
                                }
                                return best
                            }
                            function erase(fromPx, toPx) {
                                var from = tickAt(Math.min(fromPx, toPx) - 5)
                                var to = tickAt(Math.max(fromPx, toPx) + 5)
                                patternModel.eraseControls(rollControls.laneKind,
                                                           rollControls.laneController, from, to)
                            }
                            onPressed: function(mouse) {
                                if (root.focusHome) root.focusHome.forceActiveFocus()
                                songModel.beginGesture()
                                grabbed = -1
                                erasing = mouse.button === Qt.RightButton
                                lastTick = tickAt(mouse.x)
                                lastLevel = levelAt(mouse.y)
                                if (erasing) { erase(mouse.x, mouse.x); return }
                                var hit = pointNear(mouse.x)
                                if (hit >= 0) {
                                    grabbed = hit
                                    patternModel.moveControl(rollControls.laneKind,
                                        rollControls.laneController, hit, hit, lastLevel)
                                    return
                                }
                                patternModel.drawControl(rollControls.laneKind, rollControls.laneController,
                                                         lastTick, lastLevel, lastTick, lastLevel)
                            }
                            onPositionChanged: function(mouse) {
                                if (!pressed) return
                                var tick = tickAt(mouse.x)
                                var level = levelAt(mouse.y)
                                if (erasing) {
                                    erase(lastTick / 120 * rollArea.laneWidth, mouse.x)
                                } else if (grabbed >= 0) {
                                    if (patternModel.moveControl(rollControls.laneKind,
                                            rollControls.laneController, grabbed, tick, level))
                                        grabbed = tick
                                } else if (tick !== lastTick || level !== lastLevel) {
                                    patternModel.drawControl(rollControls.laneKind,
                                        rollControls.laneController, lastTick, lastLevel, tick, level)
                                }
                                lastTick = tick
                                lastLevel = level
                            }
                            onReleased: { grabbed = -1; erasing = false; songModel.endGesture() }
                            onCanceled: { grabbed = -1; erasing = false; songModel.endGesture() }
                        }
                    }
                }
            }
        }
    }
}
