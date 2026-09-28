import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The rail's project half: new, open, save, the bounce and the status line.
// Every action is a request; the window owns the dialogs and the unsaved-
// changes question.
ColumnLayout {
    id: root
    // The bit depth the next bounce is written at, owned by the window.
    property string exportDepth: "PCM24"
    // Whether the next bounce has the metronome in it, owned by the window.
    property bool exportClick: false
    signal newRequested()
    signal openRequested()
    signal saveRequested()
    signal saveAsRequested()
    signal exportRequested()
    signal exportDepthPicked(string depth)
    signal exportClickToggled()
    spacing: 10

    RowLayout {
        Layout.fillWidth: true; spacing: 4
        SectionLabel { text: "PROJECT" }
        Item { Layout.fillWidth: true }
        Chip {
            objectName: "collectAudioButton"
            // Bumped from 64 because "COLLECT" was reading as "COLLE...".
            // 72 fits "COLLECT" on one line at the standard UI font.
            implicitWidth: 72; implicitHeight: 18
            text: "COLLECT"
            onClicked: appController.collectAudio()
        }
    }
    // Two rows, because four actions side by side are wider than
    // the rail, and a control row that cannot shrink widens every
    // other row of the rail with it.
    GridLayout {
        objectName: "projectActions"
        Layout.fillWidth: true; Layout.minimumWidth: 0
        columns: 2; columnSpacing: 4; rowSpacing: 4
        Chip { objectName: "newProjectButton"; text: "NEW"
            Layout.fillWidth: true
            onClicked: root.newRequested() }
        Chip { objectName: "openProjectButton"; text: "OPEN"
            Layout.fillWidth: true
            onClicked: root.openRequested() }
        Chip { objectName: "saveProjectButton"; text: "SAVE"
            Layout.fillWidth: true
            // Lit while there is something on screen that is not
            // on disk.
            on: songModel.dirty
            onClicked: root.saveRequested() }
        Chip { objectName: "saveAsProjectButton"; text: "SAVE AS"
            Layout.fillWidth: true
            onClicked: root.saveAsRequested() }
    }
    Rectangle {
        objectName: "projectCard"
        Layout.fillWidth: true; implicitHeight: 42; radius: 5
        color: Theme.panel; border.color: Theme.line
        ColumnLayout {
            anchors.fill: parent; anchors.margins: 8; spacing: 0
            Label { Layout.fillWidth: true; text: appController.projectStatus
                color: Theme.ink; font.pixelSize: 12; font.bold: true; elide: Text.ElideRight }
            Label { Layout.fillWidth: true; text: appController.projectDetail
                color: Theme.muted; font.pixelSize: 10; elide: Text.ElideRight }
        }
    }
    // The heading carries the click option (item 3.7), so the rail gives
    // no height to it: the click is left out of a bounce unless it is lit.
    RowLayout {
        Layout.fillWidth: true; spacing: 4
        SectionLabel { text: "EXPORT" }
        Item { Layout.fillWidth: true }
        Chip {
            objectName: "exportClickButton"
            implicitWidth: 64; implicitHeight: 14
            text: "+ CLICK"
            accent: Theme.amber
            on: root.exportClick
            onClicked: root.exportClickToggled()
        }
    }
    RowLayout {
        Layout.fillWidth: true; spacing: 4
        Chip {
            objectName: "exportButton"
            text: "BOUNCE"; accent: Theme.amber
            onClicked: root.exportRequested()
        }
        Repeater {
            model: ["PCM16", "PCM24", "FLOAT32"]
            Chip {
                required property var modelData
                objectName: "exportDepth" + modelData
                text: modelData === "FLOAT32" ? "F32" : modelData.substring(3)
                accent: Theme.amber
                on: root.exportDepth === modelData
                onClicked: root.exportDepthPicked(modelData)
            }
        }
    }
    Label {
        objectName: "exportStatus"
        Layout.fillWidth: true; text: appController.exportStatus
        color: Theme.muted; font.pixelSize: 10; wrapMode: Text.Wrap
    }

    Label { Layout.fillWidth: true; text: appController.status; color: Theme.muted
        wrapMode: Text.Wrap; font.pixelSize: 11 }
}
