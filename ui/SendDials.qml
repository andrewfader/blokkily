import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A track's sends, one per return bus. A send is post-fader and pre-pan
// unless PRE takes it before the fader. Levels are mixer moves: they reach the
// running engine without a rebuild.
ColumnLayout {
    id: root
    required property int trackIndex
    spacing: 2

    // One row per return: the rows follow the song's structure, their values
    // follow the mix (songModel.sendLevels), so dragging a send never has its
    // row rebuilt under the pointer.
    Repeater {
        model: songModel.returns.length
        ColumnLayout {
            id: send
            required property int index
            readonly property var modelData: {
                const rows = songModel.sendLevels[root.trackIndex]
                return rows !== undefined && rows[index] !== undefined
                       ? rows[index]
                       : ({bus: index, letter: "", levelDb: -60, levelText: "off",
                           pre: false, active: false})
            }
            Layout.fillWidth: true; spacing: 1
            RowLayout {
                Layout.fillWidth: true; spacing: 6
                Label { text: "SEND " + send.modelData.letter; color: Theme.muted
                    font.pixelSize: 10; font.letterSpacing: 1 }
                Item { Layout.fillWidth: true }
                Label { text: send.modelData.levelText; color: Theme.ink
                    font.pixelSize: 11; font.family: "monospace"; font.bold: true }
                Chip {
                    objectName: "sendPre" + root.trackIndex + "_" + send.modelData.bus
                    text: "PRE"; implicitHeight: 18; accent: Theme.amber
                    on: send.modelData.pre
                    onClicked: songModel.setSendPreFader(root.trackIndex, send.modelData.bus,
                                                         !send.modelData.pre)
                }
            }
            // The slider has a height of its own, so it can be pressed and
            // dragged anywhere along its travel.
            Slider {
                id: level
                objectName: "send" + root.trackIndex + "_" + send.modelData.bus
                Layout.fillWidth: true
                Layout.preferredHeight: 18; Layout.minimumHeight: 18
                focusPolicy: Qt.NoFocus
                from: -60; to: 6
                value: send.modelData.levelDb
                onMoved: songModel.setSendLevel(root.trackIndex, send.modelData.bus,
                                                value <= -59.95 ? -96 : value)
                onPressedChanged: pressed ? songModel.beginGesture() : songModel.endGesture()
                background: Rectangle {
                    x: level.leftPadding
                    y: level.topPadding + level.availableHeight / 2 - height / 2
                    width: level.availableWidth; height: 4; radius: 2
                    color: Theme.line
                    Rectangle {
                        width: parent.width * level.visualPosition; height: parent.height
                        radius: 2; color: send.modelData.active ? Theme.amber : Theme.muted
                    }
                }
                handle: Rectangle {
                    x: level.leftPadding + level.visualPosition * (level.availableWidth - width)
                    y: level.topPadding + level.availableHeight / 2 - height / 2
                    width: 12; height: 12; radius: 6
                    color: send.modelData.active ? Theme.amber : Theme.muted
                    border.color: "#0e0f12"; border.width: 2
                }
            }
        }
    }
}
