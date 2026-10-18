import QtQuick
import QtQuick.Controls

// A bar at one edge of the window: the device rail on the left, the mixer on
// the right (features/window_layout.feature). Docked, it takes its width from
// the editors. Auto-hidden, it tucks into a strip at the edge while the
// editors are worked and slides back over them when wanted:
//   - the pointer resting on the strip, or a press on it, brings it out;
//     passing over the edge on the way somewhere else does not;
//   - it stays while the pointer is over it, while something in it has the
//     keyboard, while a press that began in it is held (a fader dragged past
//     its edge), while a menu or a popup is open, and while the window asks
//     (the tour pointing at it);
//   - it goes once none of that has been true for a moment.
// A window with room for both bars beside the editors docks them anyway:
// there is nothing to make room for. Its inner edge drags to widen it.
Item {
    id: bar
    // "left" or "right": the window edge it lives on.
    property string edge: "left"
    // Shown down the strip while the bar is tucked away.
    property string title: ""
    property bool autoHide: true
    // The window has room for the bar docked beside the editors.
    property bool roomy: false
    // Something outside wants it out (the tour pointing at it).
    property bool held: false
    // The width it is docked or slides out at; while its edge is dragged, the
    // width under the pointer. Widths outside the range are clamped.
    property real storedWidth: 232
    property real minimumBarWidth: 200
    property real maximumBarWidth: 420
    readonly property real barWidth: Math.max(minimumBarWidth, Math.min(maximumBarWidth,
                                              resizer.active ? resizer.dragged : storedWidth))
    // The width its edge was dragged to, once let go: the window remembers it.
    signal widthPicked(real width)
    default property alias content: panel.data

    readonly property bool docked: !autoHide || roomy
    readonly property bool shown: docked || revealed
    property bool revealed: false
    // Brought out from the keyboard: it stays until the pointer has been in
    // it and left, so it does not vanish before it is reached.
    property bool sticky: false
    readonly property real stripWidth: 14
    readonly property bool leftEdge: edge === "left"
    // What the bar takes from the editors' room.
    width: docked ? barWidth : stripWidth

    // Brings the bar out (it is already out when docked). `sticky` (the
    // default) keeps it out until the pointer has been in it and left; a
    // caller that puts the keyboard in it passes false, and the keyboard
    // holds it instead.
    function reveal(sticky) {
        if (docked) return
        revealed = true
        bar.sticky = sticky !== false
    }
    function toggle() {
        if (docked) return
        if (revealed) { revealed = false; sticky = false }
        else reveal()
    }
    onDockedChanged: { revealed = false; sticky = false }

    function contains(item) {
        for (var at = item; at !== null && at !== undefined; at = at.parent)
            if (at === panel) return true
        return false
    }
    // A menu or a popup is open somewhere in the window.
    function popupOpen() {
        var overlay = bar.Overlay.overlay
        if (overlay === null || overlay === undefined) return false
        var children = overlay.children
        for (var index = 0; index < children.length; ++index) {
            var child = children[index]
            if (child.visible && String(child).indexOf("QQuickPopupItem") === 0) return true
        }
        return false
    }
    // Why the bar is staying out, or "" when nothing is keeping it.
    function keeping() {
        if (held) return "held"
        if (sticky) return "sticky"
        if (panelHover.hovered || stripHover.hovered || edgeHover.hovered) return "pointer"
        if (pressWatch.active || resizer.active) return "pressed"
        if (contains(bar.Window.activeFocusItem)) return "focus"
        if (popupOpen()) return "popup"
        return ""
    }

    // Held open by the window (the tour) also counts as out.
    onHeldChanged: if (held && !docked) revealed = true

    Timer {
        id: hideTimer
        // Polled while out: two quiet ticks in a row and it goes.
        interval: 300; repeat: true
        running: bar.revealed && !bar.docked
        property int quiet: 0
        onRunningChanged: quiet = 0
        onTriggered: {
            if (bar.keeping() !== "") { quiet = 0; return }
            if (++quiet >= 2) bar.revealed = false
        }
    }

    // ----------------------------------------------------------- strip
    Rectangle {
        id: strip
        objectName: bar.objectName + "Strip"
        visible: !bar.docked
        width: bar.stripWidth; height: bar.height
        color: stripHover.hovered || bar.revealed ? Theme.raised : "#121317"
        Rectangle {
            width: 1; height: parent.height
            x: bar.leftEdge ? parent.width - 1 : 0
            color: Theme.line
        }
        Label {
            anchors.centerIn: parent
            rotation: bar.leftEdge ? -90 : 90
            text: bar.title
            color: stripHover.hovered ? Theme.acid : Theme.muted
            font.pixelSize: 9; font.bold: true; font.letterSpacing: 2
        }
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 8
            text: bar.leftEdge ? "›" : "‹"
            color: Theme.muted; font.pixelSize: 14; font.bold: true
        }
        HoverHandler {
            id: stripHover
            cursorShape: Qt.PointingHandCursor
        }
        // Resting on the edge, not passing over it, brings the bar out; so
        // does a press. A drag from the editors that crosses it does not.
        Timer {
            id: dwell
            interval: 220
            running: stripHover.hovered && !bar.revealed
                     && stripHover.point.pressedButtons === Qt.NoButton
            onTriggered: bar.revealed = true
        }
        TapHandler {
            objectName: bar.objectName + "StripTap"
            onTapped: bar.toggle()
        }
        Accessible.role: Accessible.Button
        Accessible.name: (bar.revealed ? "Hide " : "Show ") + bar.title.toLowerCase()
    }

    // ----------------------------------------------------------- panel
    Item {
        id: panel
        objectName: bar.objectName + "Drawer"
        z: 1
        width: bar.barWidth; height: bar.height
        x: bar.leftEdge ? (bar.shown ? 0 : -width) : (bar.shown ? bar.width - width : bar.width)
        // Off the window while tucked away, so nothing in it can be reached.
        visible: bar.shown || slide.running
        Behavior on x {
            enabled: !bar.docked
            NumberAnimation { id: slide; duration: 140; easing.type: Easing.OutCubic }
        }
        HoverHandler { id: panelHover }
        // Takes no grab of its own: a press anywhere in the bar is watched
        // until it is let go, wherever the pointer has gone.
        PointHandler {
            id: pressWatch
            acceptedButtons: Qt.AllButtons
        }
        onVisibleChanged: if (!visible) bar.sticky = false
        Connections {
            target: panelHover
            function onHoveredChanged() { if (!panelHover.hovered) bar.sticky = false }
        }
    }
    // Over the editors, a floating bar's edge is drawn so it reads as lying
    // on top of them.
    Rectangle {
        z: 1
        visible: panel.visible && !bar.docked
        x: bar.leftEdge ? panel.x + panel.width : panel.x - width
        width: 6; height: bar.height
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: bar.leftEdge ? "#66000000" : "#00000000" }
            GradientStop { position: 1; color: bar.leftEdge ? "#00000000" : "#66000000" }
        }
    }

    // ------------------------------------------------------- resize edge
    Item {
        id: resizer
        objectName: bar.objectName + "Resizer"
        z: 2
        visible: panel.visible
        width: 6; height: bar.height
        x: bar.leftEdge ? panel.x + panel.width - width / 2 : panel.x - width / 2
        property real dragged0: 0
        property real dragged: bar.storedWidth
        readonly property bool active: drag.active
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 2; height: parent.height
            color: Theme.acid
            opacity: drag.active ? 0.8 : edgeHover.hovered ? 0.45 : 0
        }
        HoverHandler { id: edgeHover; cursorShape: Qt.SizeHorCursor }
        DragHandler {
            id: drag
            target: null
            xAxis.enabled: true; yAxis.enabled: false
            cursorShape: Qt.SizeHorCursor
            onActiveChanged: {
                if (active) {
                    resizer.dragged0 = bar.barWidth
                    resizer.dragged = bar.barWidth
                } else {
                    bar.widthPicked(Math.round(bar.barWidth))
                }
            }
            onTranslationChanged: resizer.dragged =
                resizer.dragged0 + (bar.leftEdge ? translation.x : -translation.x)
        }
    }
}
