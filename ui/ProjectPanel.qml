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
    // WAV, MP3 or OGG: what a bounce and a stem export are written as.
    property string exportFormat: "WAV"
    // Whether a 16- or 24-bit WAV is dithered, owned by the window.
    property bool exportDither: true
    signal exportDitherToggled()
    // Whether the next bounce has the metronome in it, owned by the window.
    property bool exportClick: false
    signal newRequested()
    signal openRequested()
    signal saveRequested()
    signal saveAsRequested()
    signal newWindowRequested()
    signal openWindowRequested()
    signal importMidiRequested()
    signal exportMidiRequested()
    signal exportDawProjectRequested()
    signal importDawProjectRequested()
    signal exportRequested()
    signal exportDepthPicked(string depth)
    signal exportFormatPicked(string format)
    signal exportStemsRequested()
    signal exportScopeRequested()
    signal exportClickToggled()
    spacing: 10

    RowLayout {
        Layout.fillWidth: true; spacing: 4
        SectionLabel { text: qsTr("PROJECT") }
        Item { Layout.fillWidth: true }
        Chip {
            objectName: "collectAudioButton"
            // Bumped from 64 because "COLLECT" was reading as "COLLE...".
            // 72 fits "COLLECT" on one line at the standard UI font.
            implicitWidth: 72; implicitHeight: 18
            text: qsTr("COLLECT")
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
        Chip { objectName: "newProjectButton"; text: qsTr("NEW")
            Layout.fillWidth: true
            onClicked: root.newRequested() }
        Chip { objectName: "openProjectButton"; text: qsTr("OPEN")
            Layout.fillWidth: true
            onClicked: root.openRequested() }
        Chip { objectName: "saveProjectButton"; text: qsTr("SAVE")
            Layout.fillWidth: true
            // Lit while there is something on screen that is not
            // on disk.
            on: songModel.dirty
            onClicked: root.saveRequested() }
        Chip { objectName: "saveAsProjectButton"; text: qsTr("SAVE AS")
            Layout.fillWidth: true
            onClicked: root.saveAsRequested() }
        // Another song alongside this one, in a window of its own.
        Chip { objectName: "newWindowButton"; text: qsTr("+ WINDOW")
            Layout.fillWidth: true
            onClicked: root.newWindowRequested() }
        Chip { objectName: "openWindowButton"; text: qsTr("OPEN NEW")
            Layout.fillWidth: true
            onClicked: root.openWindowRequested() }
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
        SectionLabel { text: qsTr("EXPORT") }
        Item { Layout.fillWidth: true }
        // Dither applies to a 16- or 24-bit WAV; a float or lossy file has
        // nothing to round.
        Chip {
            objectName: "exportDitherButton"
            implicitWidth: 64; implicitHeight: 14
            text: qsTr("DITHER")
            accent: Theme.amber
            on: root.exportDither
            enabled: root.exportFormat === "WAV" && root.exportDepth !== "FLOAT32"
            onClicked: root.exportDitherToggled()
        }
        Chip {
            objectName: "exportClickButton"
            implicitWidth: 64; implicitHeight: 14
            text: qsTr("+ CLICK")
            accent: Theme.amber
            on: root.exportClick
            onClicked: root.exportClickToggled()
        }
    }
    RowLayout {
        Layout.fillWidth: true; spacing: 4
        // The chips share the row rather than each insisting on its width:
        // four fixed chips were wider than the rail and widened every row
        // of it with them.
        Chip {
            objectName: "exportButton"
            text: qsTr("BOUNCE"); accent: Theme.amber
            Layout.fillWidth: true; Layout.preferredWidth: implicitWidth; Layout.minimumWidth: 40
            onClicked: root.exportRequested()
        }
        Repeater {
            model: ["PCM16", "PCM24", "FLOAT32"]
            Chip {
                required property var modelData
                Layout.fillWidth: true; Layout.preferredWidth: implicitWidth; Layout.minimumWidth: 28
                objectName: "exportDepth" + modelData
                text: modelData === "FLOAT32" ? "F32" : modelData.substring(3)
                accent: Theme.amber
                on: root.exportDepth === modelData
                // A depth is a WAV's; MP3 and Ogg have none.
                enabled: root.exportFormat === "WAV"
                onClicked: root.exportDepthPicked(modelData)
            }
        }
    }
    RowLayout {
        Layout.fillWidth: true; spacing: 4
        Repeater {
            model: ["WAV", "MP3", "OGG"]
            Chip {
                required property var modelData
                objectName: "exportFormat" + modelData
                Layout.fillWidth: true
                text: modelData
                accent: Theme.amber
                on: root.exportFormat === modelData
                onClicked: root.exportFormatPicked(modelData)
            }
        }
    }
    // Every track as a file of its own, and the scope video of them all.
    RowLayout {
        Layout.fillWidth: true; spacing: 4
        Chip { objectName: "exportStemsButton"; text: qsTr("STEMS")
            Layout.fillWidth: true; accent: Theme.amber
            onClicked: root.exportStemsRequested() }
        Chip { objectName: "exportScopeButton"; text: qsTr("SCOPE VIDEO")
            Layout.fillWidth: true; accent: Theme.amber
            onClicked: root.exportScopeRequested() }
    }
    RowLayout {
        Layout.fillWidth: true; spacing: 4
        Chip { objectName: "importMidiButton"; text: qsTr("MIDI IN")
            Layout.fillWidth: true
            onClicked: root.importMidiRequested() }
        Chip { objectName: "exportMidiButton"; text: qsTr("MIDI OUT")
            Layout.fillWidth: true
            onClicked: root.exportMidiRequested() }
    }
    // The song for another DAW (Bitwig Studio, Studio One, Cubase), and a
    // song from one.
    RowLayout {
        Layout.fillWidth: true; spacing: 6
        Chip { objectName: "importDawProjectButton"; text: qsTr("DAWPROJ IN")
            Layout.fillWidth: true
            accessibleLabel: "Open a DAWproject from another DAW"
            onClicked: root.importDawProjectRequested() }
        Chip { objectName: "exportDawProjectButton"; text: qsTr("DAWPROJ OUT")
            Layout.fillWidth: true
            accessibleLabel: "Export the song as a DAWproject"
            onClicked: root.exportDawProjectRequested() }
    }
    Label {
        objectName: "exportStatus"
        Layout.fillWidth: true; text: appController.exportStatus
        color: Theme.muted; font.pixelSize: 10; wrapMode: Text.Wrap
    }

    Label { Layout.fillWidth: true; text: appController.status; color: Theme.muted
        wrapMode: Text.Wrap; font.pixelSize: 11 }
    // MIDI learn: what it waits for, or what it bound last.
    Label {
        objectName: "midiLearnStatus"
        Layout.fillWidth: true; visible: text !== ""
        text: appController.midiLearnStatus
        color: appController.midiLearning ? Theme.amber : Theme.muted
        wrapMode: Text.Wrap; font.pixelSize: 11; font.bold: appController.midiLearning
    }
}
