import QtQuick

MouseArea {
    required property int track
    required property var pickTrack
    property bool dragged: false
    anchors.fill: parent
    cursorShape: Qt.PointingHandCursor
    acceptedButtons: Qt.LeftButton | Qt.RightButton
    onClicked: function(mouse) {
        if (!dragged)
            pickTrack(track, mouse.button === Qt.RightButton ? mapToItem(null, mouse.x, mouse.y) : null)
    }
}
