import QtQuick
import QtQuick.Controls

// One automation lane of the selected track, drawn over the bars it plays
// through (item 3.1). The lane is a breakpoint envelope: each point is a
// handle that is dragged to move it (in time and value, kept between its
// neighbours), removed with the right button, and a double-click on the lane
// adds one where it was clicked. A track with no lane yet is given a gain lane
// by the first double-click. Every edit goes through the song model as one
// step of history - a drag is one step however far it goes - and reaches the
// running engine as a recompile, so the song keeps playing through it.
Rectangle {
    id: lane
    // The bars as the arrangement lays them out, and the gap between them, so
    // a tick is drawn over the bar cell it falls in.
    property var layout: songModel.barLayout
    property real spacing: 2
    readonly property int track: songModel.selectedTrack
    readonly property int laneIndex: songModel.selectedLane
    readonly property var entry: laneIndex >= 0 && songModel.automationLanes[laneIndex] !== undefined
                                 ? songModel.automationLanes[laneIndex] : null
    readonly property var points: entry !== null ? entry.points : []
    readonly property real low: entry !== null ? entry.minimum : 0
    readonly property real high: entry !== null ? entry.maximum : 1
    readonly property int barCount: layout.length
    readonly property real barRoom: Math.max(1, width - Math.max(0, barCount - 1) * spacing)
    readonly property real pad: 6

    color: "#15171c"; radius: 3; border.color: "#212530"
    clip: true

    // Where tick `tick` is drawn, in the bar cell it falls in.
    function xAt(tick) {
        if (barCount === 0) return 0
        var bar = barCount - 1
        for (var i = 0; i < barCount; ++i) {
            if (tick < layout[i].start + layout[i].ticks) { bar = i; break }
        }
        var cellEntry = layout[bar]
        var left = cellEntry.x0 * barRoom + bar * spacing
        var cell = (cellEntry.x1 - cellEntry.x0) * barRoom
        return left + Math.min(1, (tick - cellEntry.start) / cellEntry.ticks) * cell
    }
    // The tick drawn at `x`, on a sixteenth.
    function tickAt(x) {
        for (var i = 0; i < barCount; ++i) {
            var cellEntry = layout[i]
            var left = cellEntry.x0 * barRoom + i * spacing
            var cell = (cellEntry.x1 - cellEntry.x0) * barRoom
            if (x < left + cell + spacing / 2 || i === barCount - 1) {
                var tick = cellEntry.start
                    + Math.max(0, Math.min(1, (x - left) / cell)) * cellEntry.ticks
                return Math.max(0, Math.round(tick / 120) * 120)
            }
        }
        return 0
    }
    function yAt(value) {
        return pad + (high - value) / Math.max(1e-9, high - low) * (height - 2 * pad)
    }
    function valueAt(y) {
        var fraction = (y - pad) / Math.max(1, height - 2 * pad)
        return high - Math.max(0, Math.min(1, fraction)) * (high - low)
    }

    onPointsChanged: curve.requestPaint()
    onLayoutChanged: curve.requestPaint()
    onWidthChanged: curve.requestPaint()
    onHeightChanged: curve.requestPaint()

    // Bar lines, so a point can be read against the ruler.
    Repeater {
        model: lane.layout
        Rectangle {
            required property var modelData
            required property int index
            x: modelData.x0 * lane.barRoom + index * lane.spacing
            y: 0; width: 1; height: lane.height
            color: "#212530"
        }
    }

    // The envelope: flat before the first point and after the last, a ramp
    // between two points, a jump where two share a tick.
    Canvas {
        id: curve
        anchors.fill: parent
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var pts = lane.points
            if (pts.length === 0) return
            ctx.strokeStyle = Theme.record
            ctx.lineWidth = 2
            ctx.beginPath()
            ctx.moveTo(0, lane.yAt(pts[0].value))
            for (var i = 0; i < pts.length; ++i)
                ctx.lineTo(lane.xAt(pts[i].at), lane.yAt(pts[i].value))
            ctx.lineTo(lane.width, lane.yAt(pts[pts.length - 1].value))
            ctx.stroke()
        }
    }

    MouseArea {
        objectName: "automationLaneArea"
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        onDoubleClicked: function(mouse) {
            var index = lane.laneIndex
            if (index < 0) index = songModel.addAutomationLane(lane.track, "GAIN")
            if (index < 0) return
            songModel.selectLane(index)
            songModel.addAutomationPoint(lane.track, index, lane.tickAt(mouse.x),
                                         lane.valueAt(mouse.y))
        }
    }

    // One handle per point. Counted rather than given the list, so a drag
    // that moves a point updates its handle in place instead of rebuilding it
    // under the pointer.
    Repeater {
        model: lane.points.length
        Rectangle {
            id: handle
            required property int index
            readonly property var point: lane.points[index] !== undefined
                                         ? lane.points[index] : ({at: 0, value: 0})
            objectName: "automationPoint" + lane.track + "_" + index
            width: 10; height: 10; radius: 5
            x: lane.xAt(point.at) - width / 2
            y: lane.yAt(point.value) - height / 2
            color: handleMouse.pressed ? Theme.acid : Theme.record
            border.color: "#0e0f12"
            MouseArea {
                id: handleMouse
                anchors.fill: parent
                anchors.margins: -3
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                cursorShape: Qt.SizeAllCursor
                onPressed: function(mouse) {
                    if (mouse.button === Qt.RightButton) {
                        songModel.removeAutomationPoint(lane.track, lane.laneIndex, handle.index)
                        return
                    }
                    // A drag is one step of history however far it goes.
                    songModel.beginGesture()
                }
                onPositionChanged: function(mouse) {
                    if (!(mouse.buttons & Qt.LeftButton)) return
                    var at = mapToItem(lane, mouse.x, mouse.y)
                    songModel.moveAutomationPoint(lane.track, lane.laneIndex, handle.index,
                                                  lane.tickAt(at.x), lane.valueAt(at.y))
                }
                onReleased: songModel.endGesture()
                onCanceled: songModel.endGesture()
            }
        }
    }

    Label {
        anchors.left: parent.left; anchors.leftMargin: 4
        anchors.top: parent.top; anchors.topMargin: 1
        visible: lane.points.length === 0
        text: "no lane: move a control in TOUCH, LATCH or WRITE while playing, or double-click"
        color: Theme.muted; font.pixelSize: 8
    }
}
