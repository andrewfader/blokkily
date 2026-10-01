import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The scene launcher (phase 2, wave 6.1): scenes down, tracks across. A cell
// names one of the song's patterns - the same pattern every editor edits.
// Clicking an empty cell puts the open pattern in it; clicking a filled one
// launches it on its track, at its quantization; the scene's own chip
// launches the whole row, at the grid's. A launched cell is lit, a waiting
// one outlined. With REC ARRANGEMENT on, what is launched is printed into the
// arrangement as it ends.
Rectangle {
    id: root
    objectName: "launcherView"
    property string view: "ALL"
    property Item focusHome
    visible: view === "LAUNCH"
    Layout.fillWidth: true
    Layout.fillHeight: visible
    Layout.minimumHeight: visible ? 320 : 0
    implicitHeight: visible ? launcherColumn.implicitHeight + 18 : 0
    radius: 6
    color: Theme.panel
    border.color: Theme.line

    // The cell the inspector row edits.
    property int selectedScene: -1
    property int selectedTrack: -1
    readonly property var scenes: songModel.launcherScenes
    readonly property var selectedCell: selectedScene >= 0 && selectedScene < scenes.length
                                        && selectedTrack >= 0
                                        && selectedTrack < scenes[selectedScene].cells.length
                                        ? scenes[selectedScene].cells[selectedTrack] : null
    readonly property int columnWidth: 128

    function stateOf(track) {
        const all = appController.launcherState
        return track < all.length ? all[track] : ({ playing: false, queued: false,
                                                    stopping: false, scene: 0, queuedScene: 0 })
    }

    ColumnLayout {
        id: launcherColumn
        anchors.fill: parent
        anchors.margins: 9
        spacing: 8

        RowLayout {
            Layout.fillWidth: true; spacing: 6
            SectionLabel { text: "SCENE LAUNCHER" }
            Item { Layout.fillWidth: true }
            SectionLabel { text: "SCENE QUANT" }
            Picker {
                objectName: "launcherQuantization"
                implicitHeight: 22
                choices: songModel.launchQuantizations
                value: songModel.launcherQuantization
                onPicked: name => songModel.setLauncherQuantization(name)
            }
            Chip {
                objectName: "launcherRecord"
                text: "REC ARRANGEMENT"; implicitHeight: 22; accent: Theme.record
                on: appController.launcherRecording
                onClicked: appController.toggleLauncherRecording()
            }
            Chip {
                objectName: "addScene"
                text: "+ SCENE"; implicitHeight: 22
                onClicked: {
                    root.selectedScene = songModel.addScene()
                    root.selectedTrack = -1
                }
            }
            Chip {
                objectName: "stopAllLaunched"
                text: "STOP ALL"; implicitHeight: 22; accent: Theme.amber
                onClicked: appController.stopLauncher()
            }
        }

        Label {
            objectName: "launcherEmpty"
            visible: root.scenes.length === 0
            Layout.fillWidth: true
            text: "No scenes. Add one, then click a cell to put the open pattern in it."
            color: Theme.muted; font.pixelSize: 10; elide: Text.ElideRight
        }

        Flickable {
            id: gridScroll
            objectName: "launcherGrid"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 160
            clip: true
            contentWidth: gridColumn.implicitWidth
            contentHeight: gridColumn.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

            Column {
                id: gridColumn
                spacing: 4

                // Track names over their columns.
                Row {
                    spacing: 4
                    Item { width: root.columnWidth; height: 20 }
                    Repeater {
                        model: songModel.tracks
                        Label {
                            required property var modelData
                            required property int index
                            width: root.columnWidth; height: 20
                            text: modelData.name
                            color: Theme.muted; font.pixelSize: 10; font.bold: true
                            font.letterSpacing: 1
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }

                Repeater {
                    model: root.scenes
                    Row {
                        id: sceneRow
                        required property var modelData
                        required property int index
                        spacing: 4
                        Chip {
                            objectName: "launchScene" + sceneRow.index
                            width: root.columnWidth; implicitHeight: 34
                            text: "▶ " + sceneRow.modelData.name
                            accessibleLabel: "Launch " + sceneRow.modelData.name
                            onClicked: appController.launchScene(sceneRow.index)
                            onRightClicked: {
                                root.selectedScene = sceneRow.index
                                root.selectedTrack = -1
                            }
                        }
                        Repeater {
                            model: sceneRow.modelData.cells
                            Rectangle {
                                id: cell
                                required property var modelData
                                required property int index
                                readonly property var trackState: root.stateOf(index)
                                readonly property bool playing: trackState.playing && trackState.scene === sceneRow.index
                                readonly property bool queued: trackState.queued && trackState.queuedScene === sceneRow.index
                                readonly property bool selected: root.selectedScene === sceneRow.index
                                                                 && root.selectedTrack === index
                                objectName: "cell" + sceneRow.index + "_" + index
                                width: root.columnWidth; height: 34
                                radius: 4
                                color: playing ? Theme.acid
                                               : (modelData.filled ? Theme.raised : "transparent")
                                border.width: queued || selected ? 2 : 1
                                border.color: queued ? Theme.amber
                                                     : (selected ? Theme.ink : Theme.line)
                                Label {
                                    anchors.left: parent.left; anchors.leftMargin: 8
                                    anchors.right: parent.right; anchors.rightMargin: 6
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: cell.modelData.filled
                                          ? (cell.playing ? "▶ " : "") + cell.modelData.name
                                          : "+"
                                    color: cell.playing ? Theme.bg
                                                        : (cell.modelData.filled ? Theme.ink : Theme.muted)
                                    font.pixelSize: 11; font.bold: cell.modelData.filled
                                    elide: Text.ElideRight
                                }
                                Label {
                                    visible: cell.modelData.filled && cell.modelData.follow !== "LOOP"
                                    anchors.right: parent.right; anchors.rightMargin: 5
                                    anchors.bottom: parent.bottom; anchors.bottomMargin: 2
                                    text: cell.modelData.repeats + "× " + cell.modelData.follow
                                    color: cell.playing ? Theme.bg : Theme.muted
                                    font.pixelSize: 8
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                                    onClicked: function(mouse) {
                                        root.selectedScene = sceneRow.index
                                        root.selectedTrack = cell.index
                                        if (mouse.button === Qt.RightButton) return
                                        if (cell.modelData.filled)
                                            appController.launchCell(sceneRow.index, cell.index)
                                        else
                                            songModel.setLauncherCell(sceneRow.index, cell.index,
                                                songModel.partOf(songModel.currentSection, cell.index))
                                        if (root.focusHome) root.focusHome.forceActiveFocus()
                                    }
                                }
                            }
                        }
                    }
                }

                // A stop per track under its column.
                Row {
                    spacing: 4
                    visible: root.scenes.length > 0
                    Item { width: root.columnWidth; height: 24 }
                    Repeater {
                        model: songModel.tracks
                        Chip {
                            required property int index
                            objectName: "stopTrack" + index
                            width: root.columnWidth; implicitHeight: 24
                            text: "■ STOP"
                            on: root.stateOf(index).stopping
                            accent: Theme.amber
                            onClicked: appController.stopLauncherTrack(index)
                        }
                    }
                }
            }
        }

        // The selected cell: its pattern, loops, follow action and
        // quantization; or the selected scene's name.
        RowLayout {
            objectName: "cellInspector"
            Layout.fillWidth: true; spacing: 6
            visible: root.selectedCell !== null && root.selectedCell.filled
            SectionLabel {
                text: root.selectedScene >= 0 && root.selectedScene < root.scenes.length
                      ? root.scenes[root.selectedScene].name + " · "
                        + (songModel.tracks[root.selectedTrack] !== undefined
                           ? songModel.tracks[root.selectedTrack].name : "")
                      : ""
            }
            Picker {
                objectName: "cellPattern"
                implicitHeight: 22
                // A cell plays its track's part of the chosen pattern.
                choices: songModel.sections.map(section => section.name)
                value: root.selectedCell !== null ? root.selectedCell.name : ""
                onPicked: function(name) {
                    const index = choices.indexOf(name)
                    if (index >= 0)
                        songModel.setLauncherCell(root.selectedScene, root.selectedTrack,
                                                  songModel.partOf(index, root.selectedTrack))
                }
            }
            SectionLabel { text: "LOOPS" }
            Chip {
                objectName: "cellRepeatsDown"; text: "−"; implicitHeight: 22
                onClicked: songModel.setCellRepeats(root.selectedScene, root.selectedTrack,
                                                    root.selectedCell.repeats - 1)
            }
            Label {
                objectName: "cellRepeats"
                text: root.selectedCell !== null
                      ? (root.selectedCell.repeats === 0 ? "∞" : root.selectedCell.repeats) : ""
                color: Theme.ink; font.pixelSize: 11; font.bold: true
                horizontalAlignment: Text.AlignHCenter
                Layout.preferredWidth: 22
            }
            Chip {
                objectName: "cellRepeatsUp"; text: "+"; implicitHeight: 22
                onClicked: songModel.setCellRepeats(root.selectedScene, root.selectedTrack,
                                                    root.selectedCell.repeats + 1)
            }
            SectionLabel { text: "THEN" }
            Picker {
                objectName: "cellFollow"
                implicitHeight: 22
                choices: songModel.followActions
                value: root.selectedCell !== null ? root.selectedCell.follow : ""
                onPicked: name => songModel.setCellFollow(root.selectedScene, root.selectedTrack, name)
            }
            SectionLabel { text: "QUANT" }
            Picker {
                objectName: "cellQuantization"
                implicitHeight: 22
                choices: songModel.launchQuantizations
                value: root.selectedCell !== null ? root.selectedCell.quantization : ""
                onPicked: name => songModel.setCellQuantization(root.selectedScene,
                                                                root.selectedTrack, name)
            }
            Item { Layout.fillWidth: true }
            Chip {
                objectName: "clearCell"; text: "CLEAR"; implicitHeight: 22; accent: Theme.amber
                onClicked: songModel.clearLauncherCell(root.selectedScene, root.selectedTrack)
            }
        }
    }
}
