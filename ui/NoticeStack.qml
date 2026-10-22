import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// What the window has to tell the producer (features/notices.feature): a
// plugin that would not load, a save that failed, an export that finished.
// Stacked at the foot of the window over the editors, newest at the bottom,
// whether or not the side bars are out. An error or a warning stays until
// its × is pressed; a "done" goes by itself after a few seconds. Only the ×
// takes a click: the rest of a notice lets it through to the editors under
// it, so a notice never stands in the way of the work.
ColumnLayout {
    id: stack
    objectName: "noticeStack"
    // How long a "done" stays, in milliseconds.
    property int doneFor: 4000
    spacing: 6
    width: Math.min(560, parent.width - 32)

    Repeater {
        model: appController.notices
        Rectangle {
            id: notice
            required property var modelData
            required property int index
            readonly property bool done: modelData.level === "done"
            readonly property color tone: modelData.level === "error" ? Theme.record
                                        : modelData.level === "warning" ? Theme.amber : Theme.acid
            objectName: "notice" + index
            Layout.fillWidth: true
            implicitHeight: noticeRow.implicitHeight + 16
            radius: 6
            color: Theme.panel
            border.color: tone
            opacity: 0
            Component.onCompleted: opacity = 1
            Behavior on opacity { NumberAnimation { duration: 140 } }

            // A bar of the notice's colour down its leading edge.
            Rectangle {
                width: 4; radius: 2
                anchors { left: parent.left; top: parent.top; bottom: parent.bottom; margins: 4 }
                color: notice.tone
            }
            RowLayout {
                id: noticeRow
                anchors { fill: parent; leftMargin: 16; rightMargin: 8; topMargin: 8; bottomMargin: 8 }
                spacing: 8
                Label {
                    objectName: "noticeText" + notice.index
                    Layout.fillWidth: true; Layout.minimumWidth: 0
                    text: notice.modelData.text
                    color: Theme.ink; font.pixelSize: 11
                    wrapMode: Text.Wrap; maximumLineCount: 6; elide: Text.ElideRight
                    textFormat: Text.PlainText
                }
                Chip {
                    objectName: "dismissNotice" + notice.index
                    text: "×"; implicitWidth: 24; implicitHeight: 22
                    accessibleLabel: qsTr("Dismiss")
                    onClicked: appController.dismissNotice(notice.modelData.id)
                }
            }
            Timer {
                running: notice.done
                interval: stack.doneFor
                onTriggered: appController.dismissNotice(notice.modelData.id)
            }
            Accessible.role: Accessible.AlertMessage
            Accessible.name: notice.modelData.text
        }
    }
}
