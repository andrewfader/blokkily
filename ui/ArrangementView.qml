import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The song timeline: one lane per mixer track, one cell per bar. A long song
// keeps every bar wide enough to read and to hit: the lanes scroll sideways
// (Shift+wheel, or the bar under them) and Ctrl+wheel or the zoom chips
// widen the bars, while the lane names stay where they are.
// Clicking a cell places the pattern that is currently open, so
// arranging and editing are the same two clicks. Each bar is drawn as
// wide as it lasts (a 7/8 bar is 7/8 of a 4/4 one), from the song's bar
// layout, so the ruler, the clips and the tempo lane line up.
ColumnLayout {
    id: root
    // The gap between two bar cells, and the width a bar of the song gets:
    // its share of the room the lanes have once the gaps are taken out.
    readonly property real barGap: 2
    // The narrowest a bar is drawn at 1x: room for its number and a clip's
    // name, and a target a pointer can hit.
    readonly property real minimumBarWidth: 44
    // 1 shows the whole song when it fits; larger values widen every bar.
    property real zoom: 1
    readonly property real fitRoom: Math.max(1, arrangeScroll.width - 64 - barGap
                                                - Math.max(0, songModel.bars - 1) * barGap)
    readonly property real laneRoom:
        Math.max(fitRoom, songModel.bars * minimumBarWidth) * zoom
    function setZoom(next, anchorX) {
        var clamped = Math.max(1, Math.min(16, next))
        if (clamped === zoom) return
        // Keep the bar under the pointer (or the middle of the view) where it is.
        var at = anchorX === undefined ? arrangeScroll.width / 2 : anchorX
        var content = arrangeScroll.contentX + at
        var ratio = clamped / zoom
        zoom = clamped
        arrangeScroll.contentX = Math.max(0, Math.min(
            arrangementRows.width - arrangeScroll.width, content * ratio - at))
    }
    // The song's bars, read once for the whole view: every cell, ruler mark
    // and clip asks where its bar lies, and each read of songModel.barLayout
    // builds the layout of every bar of the song again.
    readonly property var layout: songModel.barLayout
    // Where bar `bar` starts in the scrolled content.
    function barX(bar) {
        var entry = root.layout[bar]
        if (entry === undefined) return 0
        return 64 + barGap + entry.x0 * laneRoom + bar * barGap
    }
    // Scrolls just enough to show bar `bar`.
    function reveal(bar) {
        var left = barX(bar)
        var right = left + barWidth(bar)
        var visibleLeft = arrangeScroll.contentX + 64 + barGap
        if (left < visibleLeft)
            arrangeScroll.contentX = Math.max(0, left - 64 - barGap)
        else if (right > arrangeScroll.contentX + arrangeScroll.width)
            arrangeScroll.contentX = Math.min(arrangementRows.width - arrangeScroll.width,
                                              right - arrangeScroll.width)
    }
    // A playing song is followed: the playhead's bar is kept in view.
    Connections {
        target: transport
        function onChanged() { if (transport.playing) root.reveal(transport.bar) }
    }
    function barWidth(bar) {
        var entry = root.layout[bar]
        if (entry === undefined) return 1
        return Math.max(1, (entry.x1 - entry.x0) * laneRoom)
    }
    // Where the keyboard goes back to after a click on the timeline.
    property Item focusHome: null
    // Naming and the context menus belong to the window, which owns them.
    signal renameRequested(string kind, int index, string name)
    signal patternMenuRequested(int index, string name)
    signal trackMenuRequested(int index, string name)
    Layout.fillWidth: true
    spacing: 6

    RowLayout {
        Layout.fillWidth: true; spacing: 6
        SectionLabel { text: qsTr("ARRANGEMENT") }
        Chip { objectName: "arrangeZoomOut"; hint: qsTr("Zoom the arrangement out"); text: "-"; implicitWidth: 24; implicitHeight: 22
            accessibleLabel: "Zoom the arrangement out"
            enabled: root.zoom > 1
            onClicked: root.setZoom(root.zoom / 1.5) }
        Chip { objectName: "arrangeZoomIn"; hint: qsTr("Zoom the arrangement in"); text: "+"; implicitWidth: 24; implicitHeight: 22
            accessibleLabel: "Zoom the arrangement in"
            enabled: root.zoom < 16
            onClicked: root.setZoom(root.zoom * 1.5) }
        // The hint gives way first: it elides before anything is pushed off.
        Label {
            text: qsTr("click places on every stem  |  ctrl-click one stem  |  shift-click lengthens  |  alt-click moves  |  right-click removes")
            color: Theme.muted; font.pixelSize: 9
            Layout.fillWidth: true; Layout.minimumWidth: 0
            horizontalAlignment: Text.AlignRight; elide: Text.ElideLeft
        }
        // The song's patterns: its sections, each holding a part for every
        // stem. However many there are, the strip takes only the room its
        // chips need and scrolls beyond that, keeping the open one in view, so
        // a long list never widens the editor column.
        ListView {
            id: patternStrip
            objectName: "patternStrip"
            orientation: ListView.Horizontal
            spacing: 6
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            implicitHeight: 26
            Layout.fillWidth: true
            Layout.preferredWidth: contentWidth
            Layout.maximumWidth: contentWidth
            Layout.minimumWidth: Math.min(contentWidth, 120)
            // A looped pass's take is placed by its clip, not offered here.
            model: songModel.sections.filter(section => !section.take)
            delegate: Chip {
                required property var modelData
                objectName: "patternChip" + modelData.index
                text: modelData.name
                // Long pattern names ellipsis rather than stretch the strip.
                width: Math.min(implicitWidth, 160)
                on: modelData.current
                onClicked: songModel.selectSection(modelData.index)
                onDoubleClicked: root.renameRequested("PATTERN", modelData.index,
                                                      modelData.name)
                onRightClicked: {
                    root.patternMenuRequested(modelData.index, modelData.name)
                }
            }
            function showCurrent() {
                if (songModel.currentSection >= 0 && songModel.currentSection < count)
                    positionViewAtIndex(songModel.currentSection, ListView.Contain)
            }
            onCountChanged: Qt.callLater(showCurrent)
            onWidthChanged: Qt.callLater(showCurrent)
            Connections {
                target: songModel
                function onSongChanged() { Qt.callLater(patternStrip.showCurrent) }
            }
        }
        // A new pattern is as long as the bar the playhead is in.
        Chip { objectName: "addPatternButton"; hint: qsTr("New pattern, as long as the bar under the playhead"); text: qsTr("+PAT"); accent: Theme.blue
            onClicked: songModel.addPattern(transport.bar) }
        // A variation starts as a copy of what is open.
        Chip { objectName: "duplicatePatternButton"; hint: qsTr("Duplicate the open pattern as a variation"); text: qsTr("DUP"); accent: Theme.blue
            onClicked: songModel.duplicatePattern() }
        Chip { objectName: "addTrackButton"; hint: qsTr("New instrument track"); text: qsTr("+TRK"); accent: Theme.blue
            onClicked: appController.addTrack() }
        Chip { objectName: "addAudioButton"; hint: qsTr("Import an audio file onto a new track"); text: qsTr("+AUD"); accent: Theme.amber
            onClicked: audioClipLane.importToNewTrack() }
    }

    // LOOP / IN / OUT sit on the arrangement, on their own row so they
    // never pinch the pattern chips the launcher and the song share.
    LoopControls { objectName: "loopControls" }

    Rectangle {
        objectName: "arrangement"
        Layout.fillWidth: true
        // One lane per track plus the frame, so the timeline
        // takes only the room it needs and the editors keep
        // the rest of the window.
        Layout.preferredHeight: 28 + 42 + Math.max(1, songModel.trackCount) * 24 + 52
                                + (arrangeScroll.contentWidth > arrangeScroll.width + 1 ? 8 : 0)
        radius: 6; color: Theme.panel; border.color: Theme.line; clip: true

        Flickable {
            id: arrangeScroll
            objectName: "arrangeScroll"
            anchors.fill: parent; anchors.margins: 5
            clip: true
            // Scrolled by the bar and the wheel only: a drag belongs to the
            // clips, the tempo points and the automation under it.
            interactive: false
            contentWidth: arrangementRows.width
            contentHeight: height
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.horizontal: ScrollBar {
                objectName: "arrangeScrollBar"
                policy: arrangeScroll.contentWidth > arrangeScroll.width + 1
                        ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                height: 6
            }
            WheelHandler {
                objectName: "arrangeZoomWheel"
                acceptedModifiers: Qt.ControlModifier
                onWheel: function(event) {
                    var delta = event.angleDelta.y !== 0 ? event.angleDelta.y
                                                         : event.angleDelta.x
                    if (delta === 0) return
                    root.setZoom(root.zoom * Math.pow(1.25, delta / 120),
                                 event.x - arrangeScroll.contentX)
                }
            }
            WheelHandler {
                objectName: "arrangeScrollWheel"
                acceptedModifiers: Qt.ShiftModifier
                onWheel: function(event) {
                    var delta = event.angleDelta.x !== 0 ? event.angleDelta.x
                                                         : event.angleDelta.y
                    arrangeScroll.contentX = Math.max(0, Math.min(
                        arrangementRows.width - arrangeScroll.width,
                        arrangeScroll.contentX - delta))
                }
            }

        // The playhead where it is in its bar, over every lane: the bar it
        // is in is lit too, but on a long bar or zoomed in that says little.
        Rectangle {
            id: arrangePlayhead
            objectName: "arrangePlayhead"
            z: 10
            readonly property var entry: root.layout[transport.bar]
            readonly property real into: entry === undefined || entry.ticks <= 0 ? 0
                : Math.max(0, Math.min(1, (transport.tick - entry.start) / entry.ticks))
            visible: entry !== undefined
            x: root.barX(transport.bar) + into * root.barWidth(transport.bar) - width / 2
            y: 0
            width: 2; height: arrangementRows.height
            color: Theme.acid
            opacity: transport.playing ? 0.9 : 0.6
        }

        ColumnLayout {
            id: arrangementRows
            objectName: "arrangementRows"
            width: 64 + root.barGap + root.laneRoom
                   + Math.max(0, songModel.bars - 1) * root.barGap
            height: arrangeScroll.height
            spacing: 2

            // A ruler over the bars, so the playhead is something
            // the producer can put rather than only watch.
            RowLayout {
                objectName: "arrangeRuler"
                Layout.fillWidth: true
                Layout.preferredHeight: 14
                spacing: 2
                Rectangle { Layout.preferredWidth: 64; Layout.preferredHeight: 14
                    color: Theme.panel; z: 2
                    transform: Translate { x: arrangeScroll.contentX } }
                Repeater {
                    model: songModel.bars
                    Rectangle {
                        id: rulerCell
                        required property int index
                        readonly property var entry: root.layout[index]
                        // A bar whose meter differs from the one before it
                        // says so; the rest only carry their number.
                        readonly property bool meterChanges: entry !== undefined && (index === 0
                            || root.layout[index - 1] === undefined
                            || root.layout[index - 1].numerator !== entry.numerator
                            || root.layout[index - 1].denominator !== entry.denominator)
                        objectName: "rulerBar" + index
                        readonly property bool inLoop: songModel.loopEnabled
                            && index >= songModel.loopStartBar
                            && index < songModel.loopEndBar
                        Layout.preferredWidth: root.barWidth(index)
                        Layout.preferredHeight: 14
                        radius: 2
                        color: transport.bar === index ? Theme.acid
                               : (inLoop ? Theme.loopRegion : "transparent")
                        Label {
                            anchors.left: parent.left; anchors.leftMargin: 3
                            anchors.verticalCenter: parent.verticalCenter
                            text: (rulerCell.index + 1).toString()
                            color: transport.bar === rulerCell.index ? Theme.onAccent : Theme.muted
                            font.pixelSize: 9; font.bold: true
                            font.family: Theme.mono
                        }
                        Label {
                            objectName: "rulerMeter" + rulerCell.index
                            visible: rulerCell.meterChanges && rulerCell.width > 40
                            anchors.right: parent.right; anchors.rightMargin: 3
                            anchors.verticalCenter: parent.verticalCenter
                            text: rulerCell.entry !== undefined
                                  ? rulerCell.entry.numerator + "/" + rulerCell.entry.denominator : ""
                            color: transport.bar === rulerCell.index ? Theme.onAccent : Theme.amber
                            font.pixelSize: 9; font.bold: true
                            font.family: Theme.mono
                        }
                        // A click puts the playhead on the bar; the right
                        // button offers the meters the bar can take.
                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            cursorShape: Qt.PointingHandCursor
                            onClicked: function(mouse) {
                                root.focusHome.forceActiveFocus()
                                if (mouse.button === Qt.RightButton) {
                                    meterMenu.bar = rulerCell.index
                                    meterMenu.popup(rulerCell, 0, rulerCell.height)
                                    return
                                }
                                if (mouse.modifiers & Qt.ShiftModifier) {
                                    songModel.setLoopStartBar(rulerCell.index)
                                    return
                                }
                                if (mouse.modifiers & Qt.ControlModifier) {
                                    songModel.setLoopEndBar(rulerCell.index)
                                    return
                                }
                                appController.seekToBar(rulerCell.index)
                            }
                        }
                    }
                }
            }

            // The tempo map, over the same bars as the ruler.
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                spacing: 2
                Rectangle {
                    Layout.preferredWidth: 64; Layout.preferredHeight: 40
                    // The lane names stay put while the bars scroll under them.
                    z: 2; transform: Translate { x: arrangeScroll.contentX }
                    radius: 3; color: Theme.panel; border.color: Theme.line
                    Column {
                        anchors.centerIn: parent
                        Label { anchors.horizontalCenter: parent.horizontalCenter
                            text: qsTr("TEMPO"); color: Theme.muted
                            font.pixelSize: 8; font.bold: true; font.letterSpacing: 0.6 }
                        Label { anchors.horizontalCenter: parent.horizontalCenter
                            objectName: "laneTempoReadout"
                            text: transport.bpm.toFixed(1); color: Theme.amber
                            font.pixelSize: 10; font.bold: true; font.family: Theme.mono }
                    }
                }
                TempoLane {
                    objectName: "tempoLane"
                    viewport: arrangeScroll
                    Layout.preferredWidth: root.laneRoom + Math.max(0, songModel.bars - 1) * root.barGap
                    Layout.preferredHeight: 40
                    spacing: root.barGap
                }
            }

            // One row per track, counted rather than listed: the track list
            // is a new value after every change of the song, and rows built
            // from it would be thrown away and remade by any click that
            // selects a track, taking the click's second half with them.
            Repeater {
                model: songModel.trackCount
                RowLayout {
                    required property int index
                    // Named so the bar cells below do not have
                    // to reach through their own delegate scope.
                    readonly property var track: songModel.tracks[index] !== undefined
                                                 ? songModel.tracks[index] : ({})
                    readonly property int trackIndex: index
                    // The row's cells, read once for the row. A cell that read
                    // songModel.lanes itself would rebuild every lane of the
                    // song for each bar of each track, which on a long song
                    // left a gigabyte of garbage behind every mixer move.
                    readonly property var lane: songModel.lanes[index] !== undefined
                                                ? songModel.lanes[index] : []
                    objectName: "arrangeRow" + index
                    Layout.fillWidth: true
                    Layout.preferredHeight: 22
                    spacing: 2

                    Rectangle {
                        Layout.preferredWidth: 64; Layout.preferredHeight: 22
                        z: 2; transform: Translate { x: arrangeScroll.contentX }
                        radius: 3
                        color: track.selected ? Theme.raised : Theme.panel
                        border.color: track.selected ? Theme.acid : Theme.line
                        objectName: "trackHeader" + trackIndex
                        // The track's colour, down the header's leading edge.
                        Rectangle {
                            objectName: "trackColour" + trackIndex
                            visible: track.colour !== undefined && track.colour !== ""
                            width: 3; radius: 1.5
                            anchors { left: parent.left; top: parent.top; bottom: parent.bottom; margins: 2 }
                            color: Theme.trackColour(track.colour)
                        }
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            x: 3
                            width: parent.width - 6 - (trackFlags.width > 0 ? trackFlags.width + 2 : 0)
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                            text: track.name
                            color: track.audible ? Theme.ink : Theme.muted
                            font.pixelSize: 9; font.bold: true
                            font.letterSpacing: 0.6
                        }
                        // Muted, soloed and armed read here too, with the
                        // mixer tucked away: a muted track and one silenced
                        // by another's solo no longer look the same.
                        Row {
                            id: trackFlags
                            objectName: "trackFlags" + trackIndex
                            anchors.right: parent.right; anchors.rightMargin: 3
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 1
                            Repeater {
                                model: [{ on: track.mute === true, letter: "M", tone: Theme.amber },
                                        { on: track.solo === true, letter: "S", tone: Theme.blue },
                                        { on: track.armed === true, letter: "R", tone: Theme.record }]
                                Rectangle {
                                    required property var modelData
                                    visible: modelData.on
                                    width: 10; height: 12; radius: 2
                                    color: modelData.tone
                                    Label {
                                        anchors.centerIn: parent
                                        text: parent.modelData.letter
                                        color: Theme.onAccent
                                        font.pixelSize: 8; font.bold: true
                                    }
                                }
                            }
                        }
                        // Click selects, double-click names,
                        // the right button offers the rest.
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            onClicked: function(mouse) {
                                // Selecting rebuilds this row, so read what
                                // the menu needs before selecting.
                                const view = root
                                const index = trackIndex
                                const name = track.name
                                songModel.selectTrack(index)
                                if (mouse.button !== Qt.RightButton) return
                                view.trackMenuRequested(index, name)
                            }
                            onDoubleClicked: root.renameRequested("TRACK", trackIndex,
                                                                  track.name)
                        }
                    }

                    Repeater {
                        model: songModel.bars
                        Rectangle {
                            required property int index
                            objectName: "clip" + trackIndex + "-" + index
                            readonly property var cell: lane[index]
                            property bool filled: cell !== undefined && cell.filled
                            // The stem's part of this pattern has nothing in
                            // it yet: an outline, still ready to play.
                            property bool hollow: filled && cell.empty
                            Layout.preferredWidth: root.barWidth(index)
                            Layout.preferredHeight: 22
                            radius: 3
                            readonly property bool atPlayhead: transport.bar === index
                            readonly property color tone: Theme.trackColour(track.colour)
                            color: hollow ? Theme.hollow
                                 : filled ? (track.audible ? tone : Theme.line)
                                          : (index % 4 === 0 ? Theme.cell : Theme.stripe)
                            border.color: atPlayhead ? Theme.acid
                                        : filled ? tone : Theme.gridline
                            border.width: atPlayhead ? 2 : 1
                            Label {
                                anchors.centerIn: parent
                                visible: parent.filled
                                // The clip is named where it
                                // starts; its repeats carry on.
                                // A take reads TAKE beside its n/N badge;
                                // its section's full name is too long here.
                                text: !parent.cell.start ? "·"
                                      : parent.cell.take === true && takeBadge.visible ? "TAKE"
                                      : parent.cell.name
                                color: parent.hollow ? Theme.muted : Theme.onAccent
                                font.pixelSize: 9; font.bold: true
                                elide: Text.ElideRight
                                width: parent.width - 6 - (takeBadge.visible ? takeBadge.width + 2 : 0)
                                horizontalAlignment: Text.AlignHCenter
                            }
                            // A take with others under it: which of them
                            // plays, and a press plays the next.
                            Rectangle {
                                id: takeBadge
                                objectName: "midiTakeBadge" + trackIndex + "-" + index
                                z: 2
                                visible: parent.filled && parent.cell.take === true && parent.cell.start
                                         && parent.cell.takeCount > 1
                                anchors.right: parent.right; anchors.rightMargin: 2
                                anchors.verticalCenter: parent.verticalCenter
                                width: takeText.implicitWidth + 8; height: 16; radius: 2
                                color: Theme.record
                                Label {
                                    id: takeText
                                    anchors.centerIn: parent
                                    text: parent.visible ? parent.parent.cell.takeIndex + "/" + parent.parent.cell.takeCount : ""
                                    color: Theme.onAccent; font.pixelSize: 8; font.bold: true
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onPressed: function(mouse) {
                                        mouse.accepted = true
                                        const clip = takeBadge.parent.cell.clip
                                        songModel.chooseMidiTake(songModel.nextMidiTake(clip))
                                    }
                                }
                                Accessible.role: Accessible.Button
                                Accessible.name: "Play the next take"
                            }
                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                cursorShape: Qt.PointingHandCursor
                                // onPressed rather than onClicked: a synthetic
                                // right-button event reaches the press handler
                                // even when the composed click does not.
                                onPressed: function(mouse) {
                                    root.focusHome.forceActiveFocus()
                                    // Selecting the track changes the song, and
                                    // every change rebuilds these rows, so this
                                    // delegate is gone after the first call:
                                    // read what the press needs beforehand.
                                    const model = songModel
                                    const controller = appController
                                    const track = trackIndex
                                    const bar = index
                                    const filled = parent.filled
                                    const cell = parent.cell
                                    const lane = model.lanes[track]
                                    const playhead = transport.bar
                                    mouse.accepted = true
                                    model.selectTrack(track)
                                    if (mouse.button === Qt.RightButton) {
                                        // The right button takes a clip away;
                                        // the left button never does.
                                        model.removeClip(track, bar)
                                        return
                                    }
                                    // Shift+click sets how many bars the clip
                                    // runs: on a filled cell it ends there; on
                                    // an empty cell it extends the earlier clip.
                                    if (mouse.modifiers & Qt.ShiftModifier) {
                                        if (filled && cell && cell.startBar >= 0) {
                                            model.extendClipToBar(track, cell.startBar, bar)
                                        } else {
                                            for (var b = bar - 1; b >= 0; --b) {
                                                var earlier = lane[b]
                                                if (earlier !== undefined
                                                    && earlier.filled) {
                                                    model.extendClipToBar(
                                                        track, earlier.startBar, bar)
                                                    break
                                                }
                                            }
                                        }
                                        return
                                    }
                                    // Alt+click on an empty bar moves the clip
                                    // the playhead is sitting in on this track;
                                    // with no clip there it moves nothing, and
                                    // places nothing either.
                                    if ((mouse.modifiers & Qt.AltModifier) && !filled) {
                                        model.moveClip(track, playhead, bar)
                                        return
                                    }
                                    if (filled) {
                                        // Opening a clip puts its pattern in
                                        // every editor and puts the playhead
                                        // where that clip starts sounding.
                                        model.openClip(track, bar)
                                        controller.seekToBar(bar)
                                    } else if (mouse.modifiers & Qt.ControlModifier) {
                                        // This stem alone plays its part here.
                                        model.placeClip(track, bar)
                                    } else {
                                        // The open pattern, every stem's part.
                                        model.placeSection(bar)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // The selected track's automation, over the same bars (item 3.1).
            RowLayout {
                objectName: "automationRow"
                Layout.fillWidth: true
                Layout.preferredHeight: 48
                spacing: 2
                Rectangle {
                    objectName: "automationHeader"
                    Layout.preferredWidth: 64; Layout.preferredHeight: 48
                    z: 2; transform: Translate { x: arrangeScroll.contentX }
                    radius: 3; color: Theme.panel; border.color: Theme.line
                    readonly property var lanes: songModel.automationLanes
                    Column {
                        anchors.centerIn: parent
                        width: parent.width - 6
                        spacing: 1
                        Label { width: parent.width; horizontalAlignment: Text.AlignHCenter
                            text: qsTr("AUTO"); color: Theme.muted
                            font.pixelSize: 8; font.bold: true; font.letterSpacing: 0.6 }
                        Label { width: parent.width; horizontalAlignment: Text.AlignHCenter
                            objectName: "automationLaneName"
                            elide: Text.ElideRight
                            text: songModel.selectedLane >= 0
                                  && songModel.automationLanes[songModel.selectedLane] !== undefined
                                  ? songModel.automationLanes[songModel.selectedLane].name : "—"
                            color: Theme.record
                            font.pixelSize: 10; font.bold: true; font.family: Theme.mono }
                        Label { width: parent.width; horizontalAlignment: Text.AlignHCenter
                            text: songModel.automationLanes.length > 1
                                  ? (songModel.selectedLane + 1) + "/" + songModel.automationLanes.length
                                  : ""
                            color: Theme.muted; font.pixelSize: 8 }
                    }
                    // A click shows the track's next lane.
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            var count = songModel.automationLanes.length
                            if (count > 0) songModel.selectLane((songModel.selectedLane + 1) % count)
                        }
                    }
                }
                AutomationLane {
                    objectName: "automationLane"
                    viewport: arrangeScroll
                    Layout.preferredWidth: root.laneRoom + Math.max(0, songModel.bars - 1) * root.barGap
                    Layout.preferredHeight: 48
                    spacing: root.barGap
                }
            }
        }
        AudioClipLane { id: audioClipLane; anchors.fill: arrangementRows; rows: arrangementRows }
        }
    }

    MeterMenu {
        id: meterMenu
        objectName: "meterMenu"
    }
}
