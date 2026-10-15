import QtQuick
import QtQuick.Controls

// What the window shows while something it has to wait for loads: a song
// opening, the instruments it plays, an export (features/loading_progress.
// feature). The work reports into appController.progress and lets a frame be
// drawn now and then; this is the frame. It covers the whole window, so a
// click lands on it and not on a half-loaded song.
Rectangle {
    id: overlay
    objectName: "loadingOverlay"
    readonly property var progress: appController.progress
    // Negative while there is nothing to count yet.
    readonly property bool counted: progress.fraction >= 0
    anchors.fill: parent
    visible: progress.active
    color: "#b30e0f12"
    z: 1000

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.AllButtons
        preventStealing: true
        onWheel: wheel => wheel.accepted = true
    }

    Rectangle {
        id: card
        objectName: "loadingCard"
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 420)
        height: column.implicitHeight + 32
        radius: 6
        color: Theme.panel
        border.color: Theme.line

        Column {
            id: column
            anchors.fill: parent
            anchors.margins: 16
            spacing: 10

            Item {
                width: parent.width
                height: titleLabel.implicitHeight
                Label {
                    id: titleLabel
                    objectName: "loadingTitle"
                    anchors.left: parent.left
                    anchors.right: percent.left
                    anchors.rightMargin: 8
                    text: overlay.progress.title
                    color: Theme.ink
                    font.pixelSize: 13; font.bold: true; font.letterSpacing: 1
                    elide: Text.ElideRight
                }
                Label {
                    id: percent
                    objectName: "loadingPercent"
                    anchors.right: parent.right
                    visible: overlay.counted
                    text: Math.floor(overlay.progress.fraction * 100) + "%"
                    color: Theme.acid
                    font.pixelSize: 13; font.bold: true; font.family: Theme.mono
                }
            }

            // The bar: filled as far as the work has got, or a block moving
            // along it while there is nothing to count yet.
            Rectangle {
                id: track
                objectName: "loadingBar"
                readonly property real value: overlay.counted ? overlay.progress.fraction : -1
                width: parent.width
                height: 8
                radius: 4
                color: Theme.raised
                border.color: Theme.line
                clip: true
                Accessible.role: Accessible.ProgressBar
                Accessible.name: overlay.progress.title

                Rectangle {
                    id: fill
                    objectName: "loadingFill"
                    visible: overlay.counted
                    height: parent.height
                    width: Math.max(parent.radius * 2, parent.width * Math.max(0, track.value))
                    radius: parent.radius
                    color: Theme.acid
                }
                Rectangle {
                    id: sweep
                    visible: !overlay.counted
                    height: parent.height
                    width: parent.width / 4
                    radius: parent.radius
                    color: Theme.acid
                    opacity: 0.8
                    NumberAnimation on x {
                        running: overlay.visible && !overlay.counted
                        loops: Animation.Infinite
                        from: -sweep.width; to: track.width
                        duration: 1100
                    }
                }
            }

            Label {
                objectName: "loadingDetail"
                width: parent.width
                text: overlay.progress.detail
                visible: text !== ""
                color: Theme.muted
                font.pixelSize: 11; font.family: Theme.mono
                elide: Text.ElideMiddle
            }
        }
    }
}
