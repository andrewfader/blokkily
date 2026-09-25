import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The song timeline: one lane per mixer track, one cell per bar.
// Clicking a cell places the pattern that is currently open, so
// arranging and editing are the same two clicks. Each bar is drawn as
// wide as it lasts (a 7/8 bar is 7/8 of a 4/4 one), from the song's bar
// layout, so the ruler, the clips and the tempo lane line up.
ColumnLayout {
    id: root
    // The gap between two bar cells, and the width a bar of the song gets:
    // its share of the room the lanes have once the gaps are taken out.
    readonly property real barGap: 2
    readonly property real laneRoom: Math.max(1, arrangementRows.width - 64 - barGap
                                              - Math.max(0, songModel.bars - 1) * barGap)
    function barWidth(bar) {
        var entry = songModel.barLayout[bar]
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
        SectionLabel { text: "ARRANGEMENT" }
        Item { Layout.fillWidth: true }
        Label { text: "shift-click lengthens  |  alt-click moves  |  right-click removes"
            color: Theme.muted; font.pixelSize: 9 }
        Repeater {
            model: songModel.patterns
            Chip {
                required property var modelData
                objectName: "patternChip" + modelData.index
                text: modelData.name
                on: modelData.current
                onClicked: songModel.selectPattern(modelData.index)
                onDoubleClicked: root.renameRequested("PATTERN", modelData.index,
                                                      modelData.name)
                onRightClicked: {
                    root.patternMenuRequested(modelData.index, modelData.name)
                }
            }
        }
        // A new pattern is as long as the bar the playhead is in.
        Chip { objectName: "addPatternButton"; text: "+PAT"; accent: Theme.blue
            onClicked: songModel.addPattern(transport.bar) }
        // A variation starts as a copy of what is open.
        Chip { objectName: "duplicatePatternButton"; text: "DUP"; accent: Theme.blue
            onClicked: songModel.duplicatePattern() }
        Chip { objectName: "addTrackButton"; text: "+TRK"; accent: Theme.blue
            onClicked: appController.addTrack() }
        Chip { objectName: "addAudioButton"; text: "+AUD"; accent: Theme.amber
            onClicked: audioClipLane.importToNewTrack() }
    }

    Rectangle {
        objectName: "arrangement"
        Layout.fillWidth: true
        // One lane per track plus the frame, so the timeline
        // takes only the room it needs and the editors keep
        // the rest of the window.
        Layout.preferredHeight: 28 + 42 + Math.max(1, songModel.trackCount) * 24 + 52
        radius: 6; color: Theme.panel; border.color: Theme.line; clip: true

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
                        readonly property var entry: songModel.barLayout[index]
                        // A bar whose meter differs from the one before it
                        // says so; the rest only carry their number.
                        readonly property bool meterChanges: entry !== undefined && (index === 0
                            || songModel.barLayout[index - 1] === undefined
                            || songModel.barLayout[index - 1].numerator !== entry.numerator
                            || songModel.barLayout[index - 1].denominator !== entry.denominator)
                        objectName: "rulerBar" + index
                        Layout.preferredWidth: root.barWidth(index)
                        Layout.preferredHeight: 14
                        radius: 2
                        color: transport.bar === index ? Theme.acid : "transparent"
                        Label {
                            anchors.left: parent.left; anchors.leftMargin: 3
                            anchors.verticalCenter: parent.verticalCenter
                            text: (rulerCell.index + 1).toString()
                            color: transport.bar === rulerCell.index ? "#0e0f12" : Theme.muted
                            font.pixelSize: 9; font.bold: true
                            font.family: "monospace"
                        }
                        Label {
                            objectName: "rulerMeter" + rulerCell.index
                            visible: rulerCell.meterChanges && rulerCell.width > 40
                            anchors.right: parent.right; anchors.rightMargin: 3
                            anchors.verticalCenter: parent.verticalCenter
                            text: rulerCell.entry !== undefined
                                  ? rulerCell.entry.numerator + "/" + rulerCell.entry.denominator : ""
                            color: transport.bar === rulerCell.index ? "#0e0f12" : Theme.amber
                            font.pixelSize: 9; font.bold: true
                            font.family: "monospace"
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
                    radius: 3; color: "transparent"; border.color: Theme.line
                    Column {
                        anchors.centerIn: parent
                        Label { anchors.horizontalCenter: parent.horizontalCenter
                            text: "TEMPO"; color: Theme.muted
                            font.pixelSize: 8; font.bold: true; font.letterSpacing: 0.6 }
                        Label { anchors.horizontalCenter: parent.horizontalCenter
                            objectName: "laneTempoReadout"
                            text: transport.bpm.toFixed(1); color: Theme.amber
                            font.pixelSize: 10; font.bold: true; font.family: "monospace" }
                    }
                }
                TempoLane {
                    objectName: "tempoLane"
                    Layout.preferredWidth: root.laneRoom + Math.max(0, songModel.bars - 1) * root.barGap
                    Layout.preferredHeight: 40
                    spacing: root.barGap
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
                        color: track.selected ? Theme.raised : "transparent"
                        border.color: track.selected ? Theme.acid : Theme.line
                        objectName: "trackHeader" + trackIndex
                        Label {
                            anchors.centerIn: parent
                            width: parent.width - 6
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                            text: track.name
                            color: track.audible ? Theme.ink : Theme.muted
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
                                root.trackMenuRequested(trackIndex, track.name)
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
                            readonly property var cell:
                                songModel.lanes[trackIndex] !== undefined
                                ? songModel.lanes[trackIndex][index] : undefined
                            property bool filled: cell !== undefined && cell.filled
                            Layout.preferredWidth: root.barWidth(index)
                            Layout.preferredHeight: 22
                            radius: 3
                            readonly property bool atPlayhead: transport.bar === index
                            color: filled ? (track.audible ? Theme.blue : Theme.line)
                                          : (index % 4 === 0 ? "#191c22" : "#15171c")
                            border.color: atPlayhead ? Theme.acid
                                        : filled ? Theme.blue : "#212530"
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
                                    root.focusHome.forceActiveFocus()
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

            // The selected track's automation, over the same bars (item 3.1).
            RowLayout {
                objectName: "automationRow"
                Layout.fillWidth: true
                Layout.preferredHeight: 48
                spacing: 2
                Rectangle {
                    objectName: "automationHeader"
                    Layout.preferredWidth: 64; Layout.preferredHeight: 48
                    radius: 3; color: "transparent"; border.color: Theme.line
                    readonly property var lanes: songModel.automationLanes
                    Column {
                        anchors.centerIn: parent
                        width: parent.width - 6
                        spacing: 1
                        Label { width: parent.width; horizontalAlignment: Text.AlignHCenter
                            text: "AUTO"; color: Theme.muted
                            font.pixelSize: 8; font.bold: true; font.letterSpacing: 0.6 }
                        Label { width: parent.width; horizontalAlignment: Text.AlignHCenter
                            objectName: "automationLaneName"
                            elide: Text.ElideRight
                            text: songModel.selectedLane >= 0
                                  && songModel.automationLanes[songModel.selectedLane] !== undefined
                                  ? songModel.automationLanes[songModel.selectedLane].name : "—"
                            color: Theme.record
                            font.pixelSize: 10; font.bold: true; font.family: "monospace" }
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
                    Layout.preferredWidth: root.laneRoom + Math.max(0, songModel.bars - 1) * root.barGap
                    Layout.preferredHeight: 48
                    spacing: root.barGap
                }
            }
        }
        AudioClipLane { id: audioClipLane; anchors.fill: arrangementRows; rows: arrangementRows }
    }

    MeterMenu {
        id: meterMenu
        objectName: "meterMenu"
    }
}
