import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The song timeline: one lane per mixer track, one cell per bar.
// Clicking a cell places the pattern that is currently open, so
// arranging and editing are the same two clicks.
ColumnLayout {
    id: root
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
        Chip { objectName: "addPatternButton"; text: "+PAT"; accent: Theme.blue
            onClicked: songModel.addPattern() }
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
        Layout.preferredHeight: 28 + Math.max(1, songModel.trackCount) * 24
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
                        objectName: "rulerBar" + index
                        Layout.fillWidth: true
                        Layout.preferredHeight: 14
                        radius: 2
                        color: transport.bar === index ? Theme.acid : "transparent"
                        Label {
                            anchors.centerIn: parent
                            text: (rulerCell.index + 1).toString()
                            color: transport.bar === rulerCell.index ? "#0e0f12" : Theme.muted
                            font.pixelSize: 9; font.bold: true
                            font.family: "monospace"
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                root.focusHome.forceActiveFocus()
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
                            Layout.fillWidth: true
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
        }
        AudioClipLane { id: audioClipLane; anchors.fill: arrangementRows; rows: arrangementRows }
    }
}
