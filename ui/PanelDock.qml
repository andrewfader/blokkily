import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The editors' panels can be moved and sized (features/window_layout.feature).
// This lies over the column that holds them: each panel has a grip in the
// margin to its left, dragged up or down to move the panel, and a panel that
// can be sized has the gap under it, dragged to size it (a double-click gives
// it back the height it wants). The column places each panel on the row
// rowOf() names; the order and the heights are the window's, remembered by
// whoever handles orderPicked and heightPicked.
Item {
    id: dock
    // key -> the panel item it moves.
    property var panels: ({})
    // The order asked for; keys it lacks follow in the shipped order, and
    // keys it does not know are ignored.
    property var order: []
    readonly property var shippedOrder: ["arrangement", "editors", "keyboard", "inspector", "modulation"]
    readonly property var effectiveOrder: {
        var known = []
        for (var index = 0; index < order.length; ++index)
            if (shippedOrder.indexOf(order[index]) >= 0 && known.indexOf(order[index]) < 0)
                known.push(order[index])
        for (var at = 0; at < shippedOrder.length; ++at)
            if (known.indexOf(shippedOrder[at]) < 0) known.push(shippedOrder[at])
        return known
    }
    // The first grid row the panels take, and the gap between rows.
    property int rowBase: 0
    property real spacing: 10
    // key -> the height asked for (0: what the panel wants).
    property var heights: ({})
    // The keys whose gap sizes them.
    property var resizable: []
    signal orderPicked(var order)
    signal heightPicked(string key, real height)

    function rowOf(key) { return rowBase + Math.max(0, effectiveOrder.indexOf(key)) }
    function heightOf(key) {
        if (sizing.key === key) return sizing.height
        var value = heights[key]
        return value === undefined || isNaN(value) ? 0 : value
    }

    // The drag under way: the panel's key, and where it would land.
    QtObject {
        id: moving
        property string key: ""
        property int slot: -1
        property real pointerY: 0
    }
    QtObject {
        id: sizing
        property string key: ""
        property real height: 0
        property real height0: 0
    }
    // The panels being moved past, in the order they are laid out.
    function shownKeys() {
        var keys = []
        for (var index = 0; index < effectiveOrder.length; ++index) {
            var panel = panels[effectiveOrder[index]]
            if (panel && panel.visible && panel.height > 0) keys.push(effectiveOrder[index])
        }
        return keys
    }
    // Where a panel dropped at `y` goes: before the first shown panel whose
    // middle is below it, or after them all.
    function slotAt(y) {
        var keys = shownKeys()
        for (var index = 0; index < keys.length; ++index) {
            var panel = panels[keys[index]]
            if (y < panel.y + panel.height / 2) return index
        }
        return keys.length
    }
    function drop(key, slot) {
        var keys = shownKeys()
        var before = slot < keys.length ? keys[slot] : ""
        if (before === key) return
        var next = effectiveOrder.filter(function(other) { return other !== key })
        var at = before === "" ? -1 : next.indexOf(before)
        if (at < 0) {
            // After the last shown panel, ahead of any hidden ones after it.
            var last = keys.filter(function(other) { return other !== key })
            at = last.length === 0 ? next.length : next.indexOf(last[last.length - 1]) + 1
        }
        next.splice(at, 0, key)
        if (next.join(",") !== effectiveOrder.join(",")) orderPicked(next)
    }

    // Where the moved panel would land.
    Rectangle {
        objectName: "panelDropMarker"
        visible: moving.key !== "" && moving.slot >= 0
        x: 0; width: dock.width; height: 2; radius: 1
        color: Theme.acid
        y: {
            if (!visible) return 0
            var keys = dock.shownKeys()
            if (moving.slot < keys.length) return dock.panels[keys[moving.slot]].y - dock.spacing / 2 - 1
            var last = dock.panels[keys[keys.length - 1]]
            return last.y + last.height + dock.spacing / 2 - 1
        }
    }

    Repeater {
        model: dock.effectiveOrder
        delegate: Item {
            id: handles
            required property string modelData
            readonly property Item panel: dock.panels[modelData] || null
            readonly property bool shown: panel !== null && panel.visible && panel.height > 0
            anchors.fill: parent

            // ------------------------------------------------------ grip
            Item {
                id: grip
                objectName: "grip_" + handles.modelData
                visible: handles.shown
                x: -12; width: 11
                y: handles.shown ? handles.panel.y + 2 : 0
                height: 22
                Grid {
                    anchors.centerIn: parent
                    columns: 2; spacing: 3
                    opacity: gripArea.pressed ? 1 : gripArea.containsMouse ? 0.9 : 0.35
                    Repeater {
                        model: 6
                        Rectangle {
                            width: 2.5; height: 2.5; radius: 1.25
                            color: gripArea.pressed || gripArea.containsMouse ? Theme.acid : Theme.muted
                        }
                    }
                }
                // A MouseArea rather than a DragHandler: the column scrolls,
                // and a drag here must not become a scroll.
                MouseArea {
                    id: gripArea
                    anchors.fill: parent
                    hoverEnabled: true
                    preventStealing: true
                    cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                    onPressed: {
                        moving.key = handles.modelData
                        moving.slot = -1
                        handles.panel.opacity = 0.55
                    }
                    onPositionChanged: function(mouse) {
                        if (!pressed) return
                        var at = mapToItem(dock, mouse.x, mouse.y)
                        moving.pointerY = at.y
                        moving.slot = dock.slotAt(at.y)
                    }
                    function end(apply) {
                        handles.panel.opacity = 1
                        var key = moving.key, slot = moving.slot
                        moving.key = ""
                        moving.slot = -1
                        if (apply && slot >= 0) dock.drop(key, slot)
                    }
                    onReleased: end(true)
                    onCanceled: end(false)
                }
                Accessible.role: Accessible.Grip
                Accessible.name: "Move the " + handles.modelData + " panel"
            }

            // ---------------------------------------------- the gap under
            Item {
                id: sizer
                objectName: "sizer_" + handles.modelData
                visible: handles.shown && dock.resizable.indexOf(handles.modelData) >= 0
                         && !handles.panel.Layout.fillHeight
                x: 0; width: dock.width
                y: handles.shown ? handles.panel.y + handles.panel.height : 0
                height: dock.spacing
                Rectangle {
                    anchors.centerIn: parent
                    width: 36; height: 3; radius: 1.5
                    color: sizeArea.pressed ? Theme.acid : Theme.muted
                    opacity: sizeArea.pressed ? 1 : sizeArea.containsMouse ? 0.8 : 0.25
                }
                MouseArea {
                    id: sizeArea
                    anchors.fill: parent
                    hoverEnabled: true
                    preventStealing: true
                    cursorShape: Qt.SizeVerCursor
                    property real pressY: 0
                    property bool moved: false
                    onPressed: function(mouse) {
                        pressY = mapToItem(dock, mouse.x, mouse.y).y
                        moved = false
                        sizing.height0 = handles.panel.height
                        sizing.height = handles.panel.height
                        sizing.key = handles.modelData
                    }
                    onPositionChanged: function(mouse) {
                        if (!pressed) return
                        var delta = mapToItem(dock, mouse.x, mouse.y).y - pressY
                        moved = moved || Math.abs(delta) >= 2
                        if (moved) sizing.height = Math.max(40, sizing.height0 + delta)
                    }
                    // Only a drag sizes: a click, or a double-click's
                    // presses, leave the height as it was.
                    onReleased: {
                        var height = sizing.height
                        sizing.key = ""
                        if (moved) dock.heightPicked(handles.modelData, height)
                    }
                    onCanceled: sizing.key = ""
                    onDoubleClicked: dock.heightPicked(handles.modelData, 0)
                }
                Accessible.role: Accessible.Separator
                Accessible.name: "Size the " + handles.modelData + " panel"
            }
        }
    }
}
