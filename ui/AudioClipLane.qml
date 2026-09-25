import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Shapes

// The audio clips of every track, drawn over the arrangement's bar cells.
// Clips are placed by the song's bar layout (songModel.barLayout), so a clip
// sits on the same bars the ruler numbers whatever the meter. The lane itself
// takes no clicks: only the clips do, so an empty bar still takes a pattern.
//
// A clip is dragged to move it (sideways in sixteenths, up and down between
// tracks), its edges are dragged to trim it, the small squares at its top
// corners set its fades, and the wheel sets its gain. The right button
// removes it. Every gesture is committed once, on release, as one step of
// history, keyed by the clip's id. The W button at a clip's lower left opens
// its warp panel (item 3.6); a warped clip says how at its lower right, and is
// veiled while its rendition renders.
Item {
    id: lane
    objectName: "audioClipLane"
    // The arrangement's rows (arrangeRow<i>); the lane covers the same area.
    property Item rows: null
    // The bar cells start after the 64-pixel track header and the 2-pixel
    // spacing of each row.
    readonly property real cellsX: 66
    readonly property real cellsWidth: Math.max(1, width - cellsX)
    readonly property real rowPitch: 24
    // Sixteenths: where a dragged clip or a trimmed edge lands.
    readonly property real snapTicks: 120
    readonly property var layout: songModel.barLayout
    readonly property real layoutTicks: layout.length > 0
        ? layout[layout.length - 1].start + layout[layout.length - 1].ticks : 1920
    readonly property real ticksPerPixel: layoutTicks / cellsWidth

    function xOfTick(tick) {
        for (var i = 0; i < layout.length; ++i) {
            var bar = layout[i]
            if (tick < bar.start + bar.ticks || i === layout.length - 1) {
                var within = (tick - bar.start) / bar.ticks
                return cellsX + (bar.x0 + within * (bar.x1 - bar.x0)) * cellsWidth
            }
        }
        return cellsX
    }
    function tickOfX(x) {
        var fraction = (x - cellsX) / cellsWidth
        for (var i = 0; i < layout.length; ++i) {
            var bar = layout[i]
            if (fraction < bar.x1 || i === layout.length - 1)
                return Math.max(0, bar.start + (fraction - bar.x0) / (bar.x1 - bar.x0) * bar.ticks)
        }
        return 0
    }
    function snap(tick) { return Math.max(0, Math.round(tick / snapTicks) * snapTicks) }
    function rowItem(track) {
        if (rows === null) return null
        var kids = rows.children
        for (var i = 0; i < kids.length; ++i)
            if (kids[i].objectName === "arrangeRow" + track) return kids[i]
        return null
    }

    // +AUD: a file first, then a new audio track to put it on, at the bar the
    // playhead is in. Cancelling leaves the song as it was.
    function importToNewTrack() { importDialog.open() }

    // The warp panel, below the clip that asked for it.
    function openWarp(id, item) {
        warpPanel.clipId = id
        var at = item.mapToItem(lane, 0, item.height + 2)
        warpPanel.x = Math.max(0, Math.min(lane.width - warpPanel.width, at.x))
        warpPanel.y = at.y
        warpPanel.open()
    }
    ClipWarpPanel { id: warpPanel }

    FileDialog {
        id: importDialog
        objectName: "audioImportDialog"
        title: "Import audio"
        nameFilters: ["Audio files (*.wav *.flac *.aiff *.aif *.ogg *.mp3)", "All files (*)"]
        fileMode: FileDialog.OpenFile
        onAccepted: {
            appController.addAudioTrack()
            appController.importAudioFile(selectedFile.toString())
        }
    }

    Repeater {
        model: songModel.audioClips
        Rectangle {
            id: clipBox
            required property var modelData
            readonly property var row: lane.rowItem(modelData.track)
            readonly property real baseX: lane.xOfTick(modelData.startTick)
            readonly property real baseRight: lane.xOfTick(modelData.endTick)
            // What a gesture in progress shows before it is committed.
            property real moveDx: 0
            property int moveTracks: 0
            property real startDx: 0
            property real endDx: 0
            // A fade handle being dragged shows where it is; otherwise the
            // fades are where the clip says.
            property real fadeInDrag: -1
            property real fadeOutDrag: -1
            readonly property real fadeInX: fadeInDrag >= 0 ? fadeInDrag
                : lane.xOfTick(modelData.fadeInTick) - baseX
            readonly property real fadeOutX: fadeOutDrag >= 0 ? fadeOutDrag
                : lane.xOfTick(modelData.fadeOutTick) - baseX
            objectName: "audioClip" + modelData.id
            visible: row !== null
            x: baseX + moveDx + startDx
            y: (row !== null ? row.y : 0) + 1 + moveTracks * lane.rowPitch
            width: Math.max(6, baseRight - baseX - startDx + endDx)
            height: row !== null ? row.height - 2 : 20
            radius: 3
            clip: true
            color: modelData.missing ? "#3a1b21" : Theme.amber
            border.color: modelData.missing ? Theme.record : "#ffd98a"
            border.width: modelData.missing ? 2 : 1

            WaveformItem {
                objectName: "waveform" + clipBox.modelData.id
                anchors.fill: parent
                anchors.margins: 1
                color: "#3d2600"
                peaks: appController.assetRevision >= 0 && !clipBox.modelData.missing
                    ? appController.clipPeaks(clipBox.modelData.id, Math.max(1, Math.round(width / 2)))
                    : []
            }
            // The fades, as the shaded corners a producer expects.
            Shape {
                anchors.fill: parent
                ShapePath {
                    strokeWidth: 0; strokeColor: "transparent"
                    fillColor: "#660e0f12"
                    startX: 0; startY: 0
                    PathLine { x: Math.max(0, clipBox.fadeInX - clipBox.startDx); y: 0 }
                    PathLine { x: 0; y: clipBox.height }
                    PathLine { x: 0; y: 0 }
                }
                ShapePath {
                    strokeWidth: 0; strokeColor: "transparent"
                    fillColor: "#660e0f12"
                    startX: clipBox.width; startY: 0
                    PathLine { x: Math.min(clipBox.width, clipBox.fadeOutX - clipBox.startDx); y: 0 }
                    PathLine { x: clipBox.width; y: clipBox.height }
                    PathLine { x: clipBox.width; y: 0 }
                }
            }
            Label {
                // Clear of the fade handle, which a missing clip does not have.
                anchors.left: parent.left
                anchors.leftMargin: clipBox.modelData.missing ? 4 : 12
                anchors.top: parent.top; anchors.topMargin: 1
                width: parent.width - anchors.leftMargin - 4
                elide: Text.ElideRight
                text: (clipBox.modelData.missing ? "MISSING " : "") + clipBox.modelData.name
                      + (Math.abs(clipBox.modelData.gainDb) > 0.05
                         ? "  " + (clipBox.modelData.gainDb > 0 ? "+" : "")
                           + clipBox.modelData.gainDb.toFixed(1) + " dB" : "")
                color: clipBox.modelData.missing ? Theme.record : "#0e0f12"
                font.pixelSize: 8; font.bold: true
            }

            // The body: drag to move, wheel for gain, right button removes.
            MouseArea {
                id: body
                objectName: "audioClipBody" + clipBox.modelData.id
                anchors.fill: parent
                anchors.leftMargin: 6; anchors.rightMargin: 6
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                preventStealing: true
                cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                property point pressedAt
                onPressed: function(mouse) {
                    songModel.selectTrack(clipBox.modelData.track)
                    if (mouse.button === Qt.RightButton) {
                        Qt.callLater(songModel.removeAudioClip, clipBox.modelData.id)
                        return
                    }
                    pressedAt = mapToItem(lane, mouse.x, mouse.y)
                }
                onPositionChanged: function(mouse) {
                    if (!pressed || !(mouse.buttons & Qt.LeftButton)) return
                    var at = mapToItem(lane, mouse.x, mouse.y)
                    var tick = lane.snap(clipBox.modelData.startTick
                                         + (at.x - pressedAt.x) * lane.ticksPerPixel)
                    clipBox.moveDx = lane.xOfTick(tick) - clipBox.baseX
                    var tracks = Math.round((at.y - pressedAt.y) / lane.rowPitch)
                    clipBox.moveTracks = Math.max(-clipBox.modelData.track,
                        Math.min(songModel.trackCount - 1 - clipBox.modelData.track, tracks))
                }
                onReleased: function(mouse) {
                    if (mouse.button !== Qt.LeftButton) return
                    // A clip moved only between tracks keeps its tick, on the
                    // grid or not.
                    var sideways = Math.abs(clipBox.moveDx) >= 0.5
                    var tick = sideways ? lane.snap(lane.tickOfX(clipBox.baseX + clipBox.moveDx))
                                        : clipBox.modelData.startTick
                    var track = clipBox.modelData.track + clipBox.moveTracks
                    var id = clipBox.modelData.id
                    var moved = sideways || clipBox.moveTracks !== 0
                    clipBox.moveDx = 0
                    clipBox.moveTracks = 0
                    if (moved) Qt.callLater(songModel.moveAudioClip, id, tick, track)
                }
                onWheel: function(wheel) {
                    var step = wheel.angleDelta.y > 0 ? 1 : -1
                    Qt.callLater(songModel.setAudioClipGain, clipBox.modelData.id,
                                 clipBox.modelData.gainDb + step)
                }
                onDoubleClicked: Qt.callLater(songModel.setAudioClipGain, clipBox.modelData.id, 0)
            }

            // The edges: drag to trim. The file stays where it is in time.
            MouseArea {
                objectName: "trimStart" + clipBox.modelData.id
                anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                width: 6
                cursorShape: Qt.SizeHorCursor
                preventStealing: true
                property real pressedX: 0
                onPressed: function(mouse) { pressedX = mapToItem(lane, mouse.x, mouse.y).x }
                onPositionChanged: function(mouse) {
                    if (!pressed) return
                    var at = mapToItem(lane, mouse.x, mouse.y).x
                    var tick = lane.snap(lane.tickOfX(clipBox.baseX + at - pressedX))
                    clipBox.startDx = Math.min(lane.xOfTick(tick) - clipBox.baseX,
                                               clipBox.baseRight - clipBox.baseX - 6)
                }
                onReleased: {
                    var tick = lane.snap(lane.tickOfX(clipBox.baseX + clipBox.startDx))
                    var id = clipBox.modelData.id
                    var changed = Math.abs(clipBox.startDx) >= 0.5
                    clipBox.startDx = 0
                    if (changed) Qt.callLater(songModel.trimAudioClipStart, id, tick)
                }
            }
            MouseArea {
                objectName: "trimEnd" + clipBox.modelData.id
                anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
                width: 6
                cursorShape: Qt.SizeHorCursor
                preventStealing: true
                property real pressedX: 0
                onPressed: function(mouse) { pressedX = mapToItem(lane, mouse.x, mouse.y).x }
                onPositionChanged: function(mouse) {
                    if (!pressed) return
                    var at = mapToItem(lane, mouse.x, mouse.y).x
                    var tick = lane.snap(lane.tickOfX(clipBox.baseRight + at - pressedX))
                    clipBox.endDx = Math.max(lane.xOfTick(tick) - clipBox.baseRight,
                                             clipBox.baseX - clipBox.baseRight + 6)
                }
                onReleased: {
                    var tick = lane.snap(lane.tickOfX(clipBox.baseRight + clipBox.endDx))
                    var id = clipBox.modelData.id
                    var changed = Math.abs(clipBox.endDx) >= 0.5
                    clipBox.endDx = 0
                    if (changed) Qt.callLater(songModel.trimAudioClipEnd, id, tick)
                }
            }

            // The fade handles, at the top corners; dragged inwards they
            // lengthen the fade. Not snapped: a fade is a feel, not a grid.
            Rectangle {
                objectName: "fadeIn" + clipBox.modelData.id
                x: Math.max(6, Math.min(clipBox.width - 14, clipBox.fadeInX - clipBox.startDx)) - 4
                y: 0; width: 8; height: 8
                visible: !clipBox.modelData.missing
                color: "#0e0f12"; border.color: "#ffd98a"
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.SizeHorCursor
                    preventStealing: true
                    onPositionChanged: function(mouse) {
                        if (!pressed) return
                        clipBox.fadeInDrag = Math.max(0, mapToItem(clipBox, mouse.x, mouse.y).x)
                    }
                    onReleased: {
                        if (clipBox.fadeInDrag < 0) return
                        var tick = lane.tickOfX(clipBox.baseX + clipBox.fadeInDrag)
                        clipBox.fadeInDrag = -1
                        Qt.callLater(songModel.setAudioClipFadeIn, clipBox.modelData.id, tick)
                    }
                }
            }
            Rectangle {
                objectName: "fadeOut" + clipBox.modelData.id
                x: Math.min(clipBox.width - 6, Math.max(14, clipBox.fadeOutX - clipBox.startDx)) - 4
                y: 0; width: 8; height: 8
                visible: !clipBox.modelData.missing
                color: "#0e0f12"; border.color: "#ffd98a"
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.SizeHorCursor
                    preventStealing: true
                    onPositionChanged: function(mouse) {
                        if (!pressed) return
                        clipBox.fadeOutDrag = Math.max(0, Math.min(clipBox.width,
                                                    mapToItem(clipBox, mouse.x, mouse.y).x))
                    }
                    onReleased: {
                        if (clipBox.fadeOutDrag < 0) return
                        var tick = lane.tickOfX(clipBox.baseX + clipBox.fadeOutDrag)
                        clipBox.fadeOutDrag = -1
                        Qt.callLater(songModel.setAudioClipFadeOut, clipBox.modelData.id, tick)
                    }
                }
            }

            // Clip warp (item 3.6): a veil while the rendition renders (the
            // clip is silent until it is ready), what the warp does, and the
            // button that opens the panel.
            Rectangle {
                objectName: "renderingVeil" + clipBox.modelData.id
                anchors.fill: parent
                visible: clipBox.modelData.rendering === true
                color: "#b30e0f12"
                Label {
                    anchors.centerIn: parent
                    text: "RENDERING…"
                    color: Theme.amber
                    font.pixelSize: 9; font.bold: true; font.letterSpacing: 1
                }
            }
            Label {
                objectName: "warpBadge" + clipBox.modelData.id
                anchors.right: parent.right; anchors.rightMargin: 8
                anchors.bottom: parent.bottom; anchors.bottomMargin: 1
                visible: clipBox.modelData.warped === true && clipBox.width > 60
                text: {
                    var d = clipBox.modelData
                    var parts = []
                    if (d.follow && d.sourceBpm > 0) parts.push("\u2669" + Number(d.sourceBpm).toFixed(0))
                    if (Math.abs(d.ratio - 1) > 1e-9) parts.push("\u00d7" + Number(d.ratio).toFixed(2))
                    if (d.semitones !== 0 || Math.abs(d.cents) > 1e-9)
                        parts.push((d.semitones >= 0 ? "+" : "") + d.semitones + "st"
                                   + (Math.abs(d.cents) > 1e-9 ? " " + Number(d.cents).toFixed(0) + "ct" : ""))
                    return parts.join(" ")
                }
                color: clipBox.modelData.rendering ? Theme.amber : "#0e0f12"
                font.pixelSize: 8; font.bold: true
            }
            Rectangle {
                objectName: "warpButton" + clipBox.modelData.id
                visible: !clipBox.modelData.missing
                x: 8; anchors.bottom: parent.bottom; anchors.bottomMargin: 2
                width: 16; height: 11; radius: 2
                color: clipBox.modelData.warped ? "#0e0f12" : "transparent"
                border.color: "#0e0f12"
                Label {
                    anchors.centerIn: parent
                    text: "W"
                    color: clipBox.modelData.warped ? Theme.amber : "#0e0f12"
                    font.pixelSize: 8; font.bold: true
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: lane.openWarp(clipBox.modelData.id, clipBox)
                }
            }
        }
    }
}
