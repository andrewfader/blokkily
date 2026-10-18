import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// QUANT and HUMAN: timing and dynamics of the open part as a whole
// (blokkily/model/groove.hpp). QUANT moves every step onto the grid it
// shows; a right click picks the grid, Shift-click moves halfway. HUMAN
// loosens every step's timing and velocity a little, differently each time.
// Each is one step of history.
Rectangle {
    id: root
    objectName: "patternGroove"
    // Grid choices, in ticks: sixteenths, eighths, quarter notes.
    readonly property var grids: [{"name": "1/16", "ticks": 120},
                                  {"name": "1/8", "ticks": 240},
                                  {"name": "1/4", "ticks": 480}]
    property int grid: 0
    implicitWidth: row.implicitWidth + 8; implicitHeight: 22
    radius: 3; color: "transparent"; border.color: Theme.line
    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: 2
        Rectangle {
            objectName: "quantizeButton"
            implicitWidth: quantLabel.implicitWidth + 12; implicitHeight: 18; radius: 2
            color: quantMouse.containsMouse ? Theme.raised : "transparent"
            Label { id: quantLabel; anchors.centerIn: parent
                text: qsTr("QUANT ") + root.grids[root.grid].name
                color: Theme.ink; font.pixelSize: 8; font.bold: true; font.letterSpacing: 0.6 }
            MouseArea { id: quantMouse; anchors.fill: parent; hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                ToolTip.visible: containsMouse; ToolTip.delay: 600
                ToolTip.text: qsTr("Move every step onto the grid. Right-click: grid. Shift: halfway.")
                onClicked: function(mouse) {
                    if (mouse.button === Qt.RightButton) {
                        root.grid = (root.grid + 1) % root.grids.length
                        return
                    }
                    patternModel.quantizePattern(root.grids[root.grid].ticks,
                        (mouse.modifiers & Qt.ShiftModifier) ? 0.5 : 1.0)
                } }
        }
        Rectangle { implicitWidth: 1; implicitHeight: 12; color: Theme.line }
        Rectangle {
            objectName: "humanizeButton"
            implicitWidth: humanLabel.implicitWidth + 12; implicitHeight: 18; radius: 2
            color: humanMouse.containsMouse ? Theme.raised : "transparent"
            Label { id: humanLabel; anchors.centerIn: parent; text: qsTr("HUMAN")
                color: Theme.ink; font.pixelSize: 8; font.bold: true; font.letterSpacing: 0.6 }
            MouseArea { id: humanMouse; anchors.fill: parent; hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                ToolTip.visible: containsMouse; ToolTip.delay: 600
                ToolTip.text: qsTr("Loosen every step's timing (±8 ticks) and velocity (±0.08).")
                onClicked: patternModel.humanizePattern(8, 0.08,
                                                        Math.floor(Math.random() * 2147483647)) }
        }
    }
}
