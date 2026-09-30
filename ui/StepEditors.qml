import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Format.js" as Format

// The three projections of the canonical pattern that are edited step by
// step: the step grid, the tracker and the piano roll. Each one writes
// through the same pattern model, and a note typed or drawn in any of them
// goes through the window's writeNote so no two can disagree about it.
ColumnLayout {
    id: root
    // Which editors the window is showing: ALL, STEP, TRACKER, PIANO, KEYS or
    // LAUNCH (the scene launcher, which shows none of them).
    property string view: "ALL"
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
        Layout.preferredHeight: 104
        Layout.maximumHeight: root.view === "STEP" ? 100000 : 104
        spacing: 5
        visible: root.view === "ALL" || root.view === "STEP"
        RowLayout {
            Layout.fillWidth: true
            SectionLabel { text: "STEP GRID" }
            Item { Layout.fillWidth: true }
            Label { text: "click toggles  |  shift-click selects  |  CTRL-click seeks  |  CTRL+1..0 toggles"; color: Theme.muted
                font.pixelSize: 9 }
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
                    readonly property bool playhead: transport.step === index
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
            objectName: "trackerView"
            visible: root.showTracker
            SplitView.fillWidth: root.view === "TRACKER"
            SplitView.preferredWidth: 340
            SplitView.minimumWidth: 260
            color: Theme.panel; radius: 6; border.color: Theme.line; clip: true
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
                    Label { text: "TYPE ZSXDCVGBHNJM  ·  DRAG VEL/FX"; color: Theme.muted
                        font.pixelSize: 9; font.family: Theme.mono }
                }
                Rectangle {
                    Layout.fillWidth: true; implicitHeight: 22; radius: 3
                    color: "#22262e"
                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 10
                        anchors.rightMargin: 10; spacing: 0
                        Label { Layout.preferredWidth: 34; text: "ROW"; color: Theme.muted
                            font.family: Theme.mono; font.pixelSize: 10 }
                        Label { Layout.preferredWidth: 62; text: "NOTE"; color: Theme.muted
                            font.family: Theme.mono; font.pixelSize: 10 }
                        Label { Layout.preferredWidth: 44; text: "VEL"; color: Theme.muted
                            font.family: Theme.mono; font.pixelSize: 10 }
                        Label { Layout.fillWidth: true; text: "FX"; color: Theme.muted
                            font.family: Theme.mono; font.pixelSize: 10 }
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
                        Math.max(7, Math.min(13, Math.floor(height / Math.max(1,
                            patternModel.stepCount)) - 2))
                    Repeater {
                        model: patternModel.stepCount
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
                                width: 2; color: Theme.amber
                            }
                            RowLayout {
                                anchors.fill: parent; anchors.leftMargin: 10
                                anchors.rightMargin: 10; spacing: 0
                                Label {
                                    Layout.preferredWidth: 34
                                    text: trackRow.index.toString(16).toUpperCase().padStart(2, "0")
                                    color: trackRow.index % 4 === 0 ? Theme.ink : Theme.muted
                                    font.family: Theme.mono
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
                                        color: trackRow.row && trackRow.row.active ? Theme.acid : "#3d434e"
                                        font.family: Theme.mono
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
                                            root.focusHome.forceActiveFocus()
                                            patternModel.selectStep(trackRow.index)
                                            songModel.beginGesture()
                                            if (mouse.button === Qt.RightButton) {
                                                patternModel.clearStep(trackRow.index)
                                                return
                                            }
                                            anchorY = mouse.y
                                            anchorKey = trackRow.row && trackRow.row.active
                                                        ? trackRow.row.key : root.entryBase
                                            root.noteWritten(trackRow.index, anchorKey)
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
                                        color: trackRow.row && trackRow.row.active ? Theme.ink : "#3d434e"
                                        font.family: Theme.mono
                                        font.pixelSize: trackRow.rowFont
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.SizeVerCursor
                                        property real anchorY: 0
                                        property real anchorVel: 0.9
                                        onPressed: function(mouse) {
                                            root.focusHome.forceActiveFocus()
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
                                               ? Theme.amber : "#3d434e"
                                        font.family: Theme.mono
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
                                            root.focusHome.forceActiveFocus()
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
                                    root.focusHome.forceActiveFocus()
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
                        text: patternModel.lowKey + "-" + patternModel.highKey + " KEYS"
                        color: Theme.muted; font.pixelSize: 9; font.family: Theme.mono
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
                        // The roll shows one pass of the pattern, so the
                        // song's playhead is drawn where it falls in the bar
                        // it is in, counted in that bar's own steps.
                        x: rollArea.gutter + Math.min(rollArea.columns,
                               transport.barStepFraction) * rollArea.laneWidth
                        y: 0; width: 2; height: rollArea.height
                        color: Theme.ink; opacity: 0.85
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
                            return Math.max(0, Math.min(rollArea.columns - 1,
                                Math.floor(px / rollArea.laneWidth)))
                        }
                        function keyAt(py) {
                            return Math.max(0, Math.min(127, patternModel.highKey -
                                Math.floor(py / rollArea.laneHeight)))
                        }
                        function coveringNote(px, py) {
                            var key = keyAt(py)
                            var xStep = px / rollArea.laneWidth
                            for (var i = 0; i < rollArea.columns; ++i) {
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
                                root.noteWritten(step, key)
                        }
                        onPressed: function(mouse) {
                            root.focusHome.forceActiveFocus()
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
