import QtQuick
import QtQuick.Controls

// The song's tempo map drawn over the bars it plays through. Each tempo point
// is a handle: dragged up or down it changes that tempo, the right button
// removes it (the first one stays), and the diamond after it switches between
// a step (the tempo jumps at the next point) and a ramp (the tempo glides to
// the next point). A double-click on the lane adds a point on the nearest beat
// at the tempo sounding there. Every edit goes through the song model, so the
// engine, the transport and the bounce hear the same map.
Rectangle {
    id: lane
    // The bars as the arrangement lays them out, and the gap between them, so
    // a tick is drawn over the bar cell it falls in.
    property var layout: songModel.barLayout
    property real spacing: 2
    readonly property var points: songModel.tempoPoints
    readonly property int barCount: layout.length
    readonly property real barRoom: Math.max(1, width - Math.max(0, barCount - 1) * spacing)
    // The tempo range drawn, a little wider than the map uses.
    readonly property real low: {
        var lowest = 300
        for (var i = 0; i < points.length; ++i) lowest = Math.min(lowest, points[i].bpm)
        return Math.max(0, lowest - 20)
    }
    readonly property real high: {
        var highest = 20
        for (var i = 0; i < points.length; ++i) highest = Math.max(highest, points[i].bpm)
        return highest + 20
    }
    readonly property real pad: 7

    color: "#15171c"; radius: 3; border.color: "#212530"
    clip: true

    // Where tick `tick` is drawn, in the bar cell it falls in.
    function xAt(tick) {
        if (barCount === 0) return 0
        var bar = barCount - 1
        for (var i = 0; i < barCount; ++i) {
            if (tick < layout[i].start + layout[i].ticks) { bar = i; break }
        }
        var entry = layout[bar]
        var left = entry.x0 * barRoom + bar * spacing
        var cell = (entry.x1 - entry.x0) * barRoom
        return left + (tick - entry.start) / entry.ticks * cell
    }
    // The tick drawn at `x`, and the beat nearest it.
    function tickAt(x) {
        for (var i = 0; i < barCount; ++i) {
            var entry = layout[i]
            var left = entry.x0 * barRoom + i * spacing
            var cell = (entry.x1 - entry.x0) * barRoom
            if (x < left + cell + spacing / 2 || i === barCount - 1)
                return entry.start + Math.max(0, Math.min(1, (x - left) / cell)) * entry.ticks
        }
        return 0
    }
    function beatAt(x) {
        var tick = tickAt(x)
        for (var i = 0; i < barCount; ++i) {
            var entry = layout[i]
            if (tick < entry.start + entry.ticks || i === barCount - 1) {
                var beat = 1920 / entry.denominator
                return entry.start + Math.round((tick - entry.start) / beat) * beat
            }
        }
        return 0
    }
    function yAt(bpm) {
        return pad + (high - bpm) / Math.max(1, high - low) * (height - 2 * pad)
    }
    function endX(index) {
        return index + 1 < points.length ? xAt(points[index + 1].at) : width
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

    Canvas {
        id: curve
        anchors.fill: parent
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            ctx.strokeStyle = Theme.amber
            ctx.lineWidth = 2
            ctx.beginPath()
            var pts = lane.points
            for (var i = 0; i < pts.length; ++i) {
                var x = lane.xAt(pts[i].at)
                var y = lane.yAt(pts[i].bpm)
                if (i === 0) ctx.moveTo(x, y)
                else ctx.lineTo(x, y)
                var last = i + 1 >= pts.length
                var nextX = lane.endX(i)
                // A ramp arrives at the next point's tempo; a step holds its
                // own until the next point and jumps there.
                var nextY = !last && pts[i].ramp ? lane.yAt(pts[i + 1].bpm) : y
                ctx.lineTo(nextX, nextY)
            }
            ctx.stroke()
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        onDoubleClicked: function(mouse) {
            var at = lane.beatAt(mouse.x)
            songModel.setTempoPoint(at, songModel.bpmAt(at), false)
        }
    }

    // One handle per tempo point, and a ramp switch after each one that has
    // a point to glide to.
    // Counted rather than given the list, so a drag that changes a point's
    // tempo updates the handle in place instead of rebuilding it under the
    // pointer (which would drop the drag after its first move).
    Repeater {
        model: lane.points.length
        Item {
            id: point
            required property int index
            readonly property var modelData: lane.points[index] !== undefined
                                             ? lane.points[index] : ({at: 0, bpm: 120, ramp: false})
            readonly property bool ramp: modelData.ramp
            anchors.fill: parent

            Rectangle {
                id: handle
                objectName: "tempoPoint" + point.index
                readonly property real centreX: lane.xAt(point.modelData.at)
                x: Math.max(0, centreX - width / 2)
                y: lane.yAt(point.modelData.bpm) - height / 2
                width: 12; height: 12; radius: 2
                color: handleMouse.pressed ? Theme.acid : Theme.amber
                border.color: "#0e0f12"
                MouseArea {
                    id: handleMouse
                    anchors.fill: parent
                    anchors.margins: -3
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    cursorShape: Qt.SizeVerCursor
                    property real anchorY: 0
                    property real anchorBpm: 120
                    onPressed: function(mouse) {
                        if (mouse.button === Qt.RightButton) {
                            songModel.removeTempoPoint(point.modelData.at)
                            return
                        }
                        // A drag is one step of history however far it goes.
                        songModel.beginGesture()
                        anchorY = mapToItem(lane, mouse.x, mouse.y).y
                        anchorBpm = point.modelData.bpm
                    }
                    onPositionChanged: function(mouse) {
                        if (!(mouse.buttons & Qt.LeftButton)) return
                        var y = mapToItem(lane, mouse.x, mouse.y).y
                        var fine = mouse.modifiers & Qt.ShiftModifier
                        var bpm = anchorBpm + (anchorY - y) * (fine ? 0.1 : 0.5)
                        bpm = Math.max(20, Math.min(300, Math.round(bpm * 100) / 100))
                        songModel.setTempoPoint(point.modelData.at, bpm, point.ramp)
                    }
                    onReleased: songModel.endGesture()
                    onCanceled: songModel.endGesture()
                }
            }
            Label {
                objectName: "tempoLabel" + point.index
                x: Math.min(lane.width - width - 2, handle.x + handle.width + 3)
                y: handle.y + handle.height / 2 > lane.height / 2 ? handle.y - height + 2
                                                                  : handle.y + handle.height - 2
                text: point.modelData.bpm.toFixed(point.modelData.bpm % 1 === 0 ? 0 : 2)
                color: Theme.ink; font.pixelSize: 9; font.family: "monospace"
                font.bold: true
            }
            // The ramp switch sits halfway to the next point.
            Rectangle {
                objectName: "tempoRamp" + point.index
                visible: point.index + 1 < lane.points.length
                readonly property real fromX: lane.xAt(point.modelData.at)
                readonly property real toX: lane.endX(point.index)
                x: (fromX + toX) / 2 - width / 2
                y: lane.height - height - 2
                width: 14; height: 14; radius: 3
                color: point.ramp ? Theme.acid : "transparent"
                border.color: point.ramp ? Theme.acid : Theme.muted
                Label {
                    anchors.centerIn: parent
                    text: point.ramp ? "/" : "┐"
                    color: point.ramp ? "#0e0f12" : Theme.muted
                    font.pixelSize: 9; font.bold: true; font.family: "monospace"
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: songModel.setTempoPoint(point.modelData.at, point.modelData.bpm,
                                                       !point.ramp)
                }
            }
        }
    }

    Label {
        anchors.left: parent.left; anchors.leftMargin: 4
        anchors.top: parent.top; anchors.topMargin: 1
        visible: lane.points.length === 1
        text: "double-click adds a tempo point"
        color: Theme.muted; font.pixelSize: 8
    }
}
