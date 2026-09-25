import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The rail's instrument half: what the selected track plays, the MIDI
// keyboard that plays it, and the plugin browser that finds another.
ColumnLayout {
    id: root
    // Where the keyboard goes back to when the browser lets go of it.
    property Item focusHome: null
    spacing: 10

    // Puts the keyboard in the browser's search field, selecting what is there.
    function findInstrument() {
        pluginFilter.forceActiveFocus()
        pluginFilter.selectAll()
    }

    SectionLabel { text: "DEVICES" }
    Rectangle {
        Layout.fillWidth: true; implicitHeight: 56; radius: 6
        color: Theme.panel; border.color: Theme.acid
        RowLayout {
            anchors.fill: parent; anchors.margins: 10; spacing: 8
            Rectangle {
                Layout.preferredWidth: 38; Layout.preferredHeight: 30; radius: 4
                color: appController.activeInstrument.startsWith("VST3") ? Theme.blue
                     : appController.activeInstrument.startsWith("SF") ? Theme.amber : Theme.acid
                Label { anchors.centerIn: parent
                    text: appController.activeInstrument === "Choose an instrument" ? "—"
                          : appController.activeInstrument.split(" ")[0]
                    color: "#0e0f12"
                    font.pixelSize: 11; font.bold: true }
            }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 1
                // The selected mixer track, and what is loaded on it.
                Label {
                    objectName: "selectedTrackName"
                    Layout.fillWidth: true
                    text: songModel.tracks[songModel.selectedTrack] !== undefined
                          ? songModel.tracks[songModel.selectedTrack].name
                          : "NO TRACK"
                    color: Theme.ink; font.bold: true; font.pixelSize: 10
                    font.letterSpacing: 0.8; elide: Text.ElideRight
                }
                Label { Layout.fillWidth: true; text: appController.soundfontStatus
                    color: Theme.muted; font.pixelSize: 10; elide: Text.ElideMiddle
                    visible: appController.activeInstrument === "Choose an instrument" }
                Label { Layout.fillWidth: true; text: appController.activeInstrument
                    color: Theme.acid; font.pixelSize: 10; elide: Text.ElideMiddle
                    visible: appController.activeInstrument !== "Choose an instrument" }
            }
        }
    }

    SectionLabel { text: "MIDI IN" }
    // The keyboard being played, and what it last sent. The light
    // flashes on every key the port delivers, so a controller that
    // is not reaching the application is seen not to be.
    Rectangle {
        objectName: "midiPanel"
        Layout.fillWidth: true; implicitHeight: 30; radius: 4
        color: midiMouse.containsMouse ? Theme.raised : Theme.panel
        border.color: appController.midiPort !== "" ? Theme.acid : Theme.line
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 9; anchors.rightMargin: 8
            spacing: 7
            Rectangle {
                objectName: "midiLight"
                implicitWidth: 8; implicitHeight: 8; radius: 4
                color: midiFlash.running ? Theme.acid
                     : appController.midiPort !== "" ? "#4a5a22" : Theme.line
            }
            Label {
                objectName: "midiPortName"
                Layout.fillWidth: true
                text: appController.midiPort !== "" ? appController.midiPort
                                                    : "No MIDI input"
                color: appController.midiPort !== "" ? Theme.ink : Theme.muted
                font.pixelSize: 10; font.bold: true; elide: Text.ElideRight
            }
            Label {
                objectName: "midiActivity"
                visible: appController.midiPort !== ""
                text: appController.midiActivity
                color: Theme.muted; font.pixelSize: 10; font.family: "monospace"
            }
            Label { text: "\u25be"; color: Theme.muted; font.pixelSize: 10 }
        }
        Timer { id: midiFlash; interval: 120 }
        Connections {
            target: appController
            function onMidiActivityChanged() { midiFlash.restart() }
        }
        MouseArea {
            id: midiMouse; anchors.fill: parent; hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            // A keyboard plugged in after launch is listed the next
            // time the list is opened.
            onClicked: { appController.refreshMidiPorts(); midiMenu.popup() }
        }
        Menu {
            id: midiMenu
            objectName: "midiMenu"
            MenuItem { text: "None"; onTriggered: appController.selectMidiPort(-1) }
            Instantiator {
                model: appController.midiPorts
                delegate: MenuItem {
                    required property var modelData
                    required property int index
                    text: modelData
                    checkable: true
                    checked: modelData === appController.midiPort
                    onTriggered: appController.selectMidiPort(index)
                }
                onObjectAdded: (index, object) => midiMenu.insertItem(index + 1, object)
                onObjectRemoved: (index, object) => midiMenu.removeItem(object)
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true; spacing: 6
        SectionLabel { text: "PLUGINS" }
        Item { Layout.fillWidth: true }
        // Instruments play a track's notes; effects go into the rack.
        Chip {
            objectName: "browserKindInstrument"
            text: "INSTR"; implicitHeight: 20
            on: appController.browserKind === "instrument"
            onClicked: appController.setBrowserKind("instrument")
        }
        Chip {
            objectName: "browserKindEffect"
            text: "FX"; implicitHeight: 20; accent: Theme.amber
            on: appController.browserKind === "effect"
            onClicked: appController.setBrowserKind("effect")
        }
    }

    // An installation holds hundreds of instruments, so the browser
    // is searched by typing a few letters of one rather than by
    // scrolling past the rest. The letters need only appear in
    // order — "fbs" reaches "Fat Bass" — and the arrow keys and
    // Return move and load without leaving the field.
    Rectangle {
        Layout.fillWidth: true; implicitHeight: 28; radius: 4
        color: Theme.raised
        border.color: pluginFilter.activeFocus ? Theme.acid : Theme.line
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 6
            Label {
                text: "⌕"; color: pluginFilter.activeFocus ? Theme.acid : Theme.muted
                font.pixelSize: 12
            }
            TextField {
                id: pluginFilter
                objectName: "pluginFilter"
                Layout.fillWidth: true
                padding: 0
                placeholderText: appController.browserKind === "effect"
                                 ? "Find effect" : "Find instrument"
                placeholderTextColor: Theme.muted
                color: Theme.ink; font.pixelSize: 11
                selectByMouse: true
                background: Item {}
                // The list is the filter's own answer, so it is
                // rebuilt as the letters arrive and the cursor goes
                // back to the closest match.
                onTextChanged: {
                    appController.browserFilter = text
                    pluginBrowser.currentIndex = pluginBrowser.count > 0 ? 0 : -1
                }
                Keys.onDownPressed: pluginBrowser.step(1)
                Keys.onUpPressed: pluginBrowser.step(-1)
                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        pluginBrowser.load(pluginBrowser.currentIndex)
                        event.accepted = true
                    } else if (event.key === Qt.Key_Escape) {
                        // A cleared field hands the keyboard back to
                        // the tracker, so note entry resumes without
                        // a click.
                        if (text === "") root.focusHome.forceActiveFocus()
                        text = ""
                        event.accepted = true
                    }
                }
            }
            // How much of the installation is on screen, so a filter
            // that hides everything says so rather than looking like
            // an empty scan.
            Label {
                objectName: "pluginBrowserCount"
                text: pluginFilter.text === ""
                      ? appController.browserTotal
                      : appController.browserPlugins.length + "/"
                        + appController.browserTotal
                color: Theme.muted; font.pixelSize: 10; font.family: "monospace"
            }
        }
    }

    ListView {
        id: pluginBrowser
        objectName: "pluginBrowser"
        Layout.fillWidth: true; Layout.fillHeight: true
        clip: true; spacing: 4
        model: appController.browserPlugins
        currentIndex: 0
        highlightMoveDuration: 90
        // Keeps the row the arrow keys landed on in view, which is
        // what makes a long list navigable from the keyboard.
        highlightRangeMode: ListView.ApplyRange
        preferredHighlightBegin: 0
        preferredHighlightEnd: Math.max(0, height - 44)
        // Loads the instrument on a row of the *filtered* list: the
        // entry carries where it sits in the full list, so what was
        // pointed at is what is loaded.
        function load(row) {
            if (row < 0 || row >= count) return
            appController.selectInstrument(model[row].source)
        }
        function step(delta) {
            if (count === 0) return
            currentIndex = Math.max(0, Math.min(count - 1, currentIndex + delta))
        }

        ScrollBar.vertical: ScrollBar {
            objectName: "pluginScrollBar"
            policy: ScrollBar.AsNeeded
            // Drawn only when there is somewhere to scroll to, so a
            // short list does not carry a stray bar down its side.
            visible: size < 1.0
            contentItem: Rectangle {
                implicitWidth: 5; radius: 2
                color: parent.pressed ? Theme.acid : Theme.line
            }
        }

        // A filter nothing answers is a fact about the query, not
        // about the installation.
        Label {
            anchors.centerIn: parent
            width: parent.width - 16
            visible: pluginBrowser.count === 0 && appController.browserTotal > 0
            text: "No " + (appController.browserKind === "effect" ? "effect" : "instrument")
                  + " matches “" + pluginFilter.text + "”"
            color: Theme.muted; font.pixelSize: 11
            horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap
        }

        delegate: Rectangle {
            required property var modelData
            required property int index
            objectName: "browserRow" + index
            width: ListView.view.width; height: 40; radius: 5
            color: ListView.isCurrentItem || rowMouse.containsMouse ? Theme.raised : Theme.panel
            border.color: ListView.isCurrentItem ? Theme.acid : Theme.line
            MouseArea {
                id: rowMouse
                anchors.fill: parent; hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    pluginBrowser.currentIndex = index
                    appController.selectInstrument(modelData.source)
                }
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 8
                Rectangle {
                    Layout.preferredWidth: 38; Layout.preferredHeight: 16; radius: 3
                    color: modelData.format === "CLAP" ? Theme.acid
                         : modelData.format === "VST3" ? Theme.blue
                         : modelData.format === "Built-in" ? Theme.line : Theme.amber
                    Label { anchors.centerIn: parent
                        text: modelData.format === "Built-in" ? "INT" : modelData.format
                        color: modelData.format === "CLAP" ? "#0e0f12" : Theme.ink
                        font.pixelSize: 9; font.bold: true; font.letterSpacing: 1 }
                }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 0
                    Label { Layout.fillWidth: true; text: modelData.name; color: Theme.ink
                        font.pixelSize: 12; elide: Text.ElideRight }
                    Label { Layout.fillWidth: true; text: modelData.vendor; color: Theme.muted
                        font.pixelSize: 10; elide: Text.ElideMiddle }
                }
            }
        }
    }
}
