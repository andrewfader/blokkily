import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// EDITOR in the instrument panel: opens the selected track's instrument in
// its own window. Beside it, the parameter a knob in that window last moved,
// and why a window could not be opened when one could not.
Rectangle {
    id: root
    objectName: "editorBar"
    readonly property int track: songModel.selectedTrack
    readonly property bool open: appController.openEditors.indexOf(track) >= 0
    implicitHeight: Math.max(34, editorRow.implicitHeight + 8)
    radius: 4
    color: Theme.panel
    border.color: open ? Theme.acid : Theme.line

    RowLayout {
        id: editorRow
        anchors.fill: parent; anchors.leftMargin: 6; anchors.rightMargin: 8
        spacing: 8
        Chip {
            objectName: "editorButton"
            text: "EDITOR"
            accent: Theme.acid
            enabled: songModel.tracks[root.track] !== undefined
                     && songModel.tracks[root.track].hasInstrument
            on: root.open
            onClicked: appController.toggleEditor(root.track)
        }
        ColumnLayout {
            Layout.fillWidth: true; spacing: 0
            Label {
                objectName: "editorReadout"
                Layout.fillWidth: true
                text: appController.editorReadout !== "" ? appController.editorReadout : "—"
                color: appController.editorReadout !== "" ? Theme.ink : Theme.muted
                font.pixelSize: 11; font.bold: true; font.family: "monospace"
                elide: Text.ElideRight
            }
            Label {
                objectName: "editorStatus"
                Layout.fillWidth: true
                visible: text !== ""
                text: appController.editorStatus
                color: Theme.muted; font.pixelSize: 9
                // Why a window could not open is read in full.
                wrapMode: Text.WordWrap; maximumLineCount: 2
            }
        }
    }
}
