import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The rail's instrument half: what the selected track plays, the MIDI
// keyboard that plays it, and the plugin browser that finds another.
ColumnLayout {
    id: root
    // Where the keyboard goes back to when the browser lets go of it.
    property Item focusHome: null
    spacing: 8

    // Puts the keyboard in the browser's search field, selecting what is there.
    function findInstrument() {
        pluginFilter.forceActiveFocus()
        pluginFilter.selectAll()
    }

    SectionLabel { text: "DEVICES" }
    // A sampler's panel names its track itself, so it takes this box's place
    // and the browser below keeps room to be used.
    Rectangle {
        visible: appController.sampler.active !== true
        Layout.fillWidth: true; implicitHeight: 56; radius: 6
        color: Theme.panel; border.color: Theme.acid
        RowLayout {
            anchors.fill: parent; anchors.margins: 10; spacing: 8
            Rectangle {
                Layout.preferredWidth: 38; Layout.preferredHeight: 30; radius: 4
                color: appController.activeInstrument.startsWith("VST3") ? Theme.blue
                     : appController.activeInstrument.startsWith("LV2") ? Theme.violet
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
                    text: songModel.trackNames[songModel.selectedTrack] !== undefined
                          ? songModel.trackNames[songModel.selectedTrack]
                          : "NO TRACK"
                    color: Theme.ink; font.bold: true; font.pixelSize: 10
                    font.letterSpacing: 0.8; elide: Text.ElideRight
                }
                Label { Layout.fillWidth: true; text: appController.soundfontStatus
                    color: Theme.muted; font.pixelSize: 10; elide: Text.ElideMiddle
                    visible: appController.activeInstrument === "Choose an instrument" }
                Label { objectName: "selectedInstrument"
                    Layout.fillWidth: true; text: appController.activeInstrument
                    color: Theme.acid; font.pixelSize: 10; elide: Text.ElideMiddle
                    visible: appController.activeInstrument !== "Choose an instrument" }
            }
        }
    }

    SamplerPanel { Layout.fillWidth: true; visible: appController.sampler.active === true }
    // A sampler has no window of its own; its panel is its editor.
    PluginEditorBar { Layout.fillWidth: true; visible: appController.sampler.active !== true }

    RowLayout {
        Layout.fillWidth: true; spacing: 6
        SectionLabel { text: "MIDI IN" }
        Item { Layout.fillWidth: true }
        // MPE: each key's own bend, CC 74 and pressure move that note alone,
        // and are recorded with it. UPPER reads the upper zone (master 16,
        // members 1..15) instead of the lower (master 1, members 2..16).
        Chip {
            objectName: "mpeZoneChip"
            visible: appController.mpe
            text: "UPPER"; implicitHeight: 20
            accessibleLabel: "MPE upper zone"
            on: appController.mpeUpper
            onClicked: appController.setMpeUpper(!appController.mpeUpper)
        }
        Chip {
            objectName: "mpeChip"
            text: "MPE"; implicitHeight: 20
            accessibleLabel: "MPE keyboard"
            on: appController.mpe
            onClicked: appController.setMpe(!appController.mpe)
        }
    }
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
                color: Theme.muted; font.pixelSize: 10; font.family: Theme.mono
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

    // Outboard gear: the selected track plays a MIDI output port on one of
    // its channels instead of an instrument of its own, and the song can
    // send MIDI clock to a port so drum machines and sequencers follow it.
    ColumnLayout {
        id: midiOutPanel
        objectName: "midiOutPanel"
        Layout.fillWidth: true
        spacing: 4
        // Read again whenever the song changes, so a track that is given
        // another instrument stops showing a port.
        readonly property string trackPort:
            (songModel.tracks, appController.trackMidiOut(songModel.selectedTrack))
        readonly property int trackChannel:
            (songModel.tracks, appController.trackMidiOutChannel(songModel.selectedTrack))
        readonly property var channels: {
            var names = []
            for (var channel = 1; channel <= 16; ++channel) names.push("CH " + channel)
            return names
        }
        readonly property int labelWidth: 62
        Component.onCompleted: appController.refreshMidiPorts()
        RowLayout {
            Layout.fillWidth: true; spacing: 6
            SectionLabel { text: "MIDI OUT"; Layout.preferredWidth: midiOutPanel.labelWidth }
            Picker {
                objectName: "midiOutPort"
                Layout.fillWidth: true; Layout.minimumWidth: 60
                implicitHeight: 22
                choices: ["OFF"].concat(appController.midiOutPorts)
                value: midiOutPanel.trackPort !== "" ? midiOutPanel.trackPort : "OFF"
                onPicked: name => {
                    if (name !== "OFF")
                        appController.setTrackMidiOut(songModel.selectedTrack, name,
                                                      midiOutPanel.trackChannel)
                    else if (midiOutPanel.trackPort !== "")
                        appController.setTrackMidiOut(songModel.selectedTrack, "")
                }
            }
            Picker {
                objectName: "midiOutChannel"
                visible: midiOutPanel.trackPort !== ""
                implicitHeight: 22
                choices: midiOutPanel.channels
                value: "CH " + (midiOutPanel.trackChannel + 1)
                onPicked: name => appController.setTrackMidiOutChannel(
                              songModel.selectedTrack, parseInt(name.slice(3)) - 1)
            }
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 6
            SectionLabel { text: "CLOCK"; Layout.preferredWidth: midiOutPanel.labelWidth }
            Picker {
                objectName: "midiClockPort"
                Layout.fillWidth: true; Layout.minimumWidth: 60
                implicitHeight: 22
                choices: ["OFF"].concat(appController.midiOutPorts)
                value: appController.midiClockPort !== "" ? appController.midiClockPort : "OFF"
                onPicked: name => appController.setMidiClockPort(name === "OFF" ? "" : name)
            }
        }
        // Following another machine's clock, from the MIDI input: its Start,
        // Stop and position move the transport, and its tempo is played.
        RowLayout {
            Layout.fillWidth: true; spacing: 6
            SectionLabel { text: "SYNC"; Layout.preferredWidth: midiOutPanel.labelWidth }
            Chip {
                objectName: "followClock"
                text: "EXT"; implicitHeight: 22
                accessibleLabel: "Follow an external MIDI clock"
                accent: Theme.blue
                on: appController.followClock
                onClicked: appController.followClock = !appController.followClock
            }
            Label {
                objectName: "clockStatus"
                Layout.fillWidth: true; Layout.minimumWidth: 0
                text: appController.clockStatus
                color: appController.clockStatus.indexOf("BPM") > 0 ? Theme.blue : Theme.muted
                font.pixelSize: 9; font.family: Theme.mono; font.bold: true
                elide: Text.ElideRight
            }
        }
        // The selected track's arpeggiator: the notes it holds play one at a
        // time on the grid, before its instrument hears them.
        readonly property var arp: songModel.arpeggiators[songModel.selectedTrack] || ({})
        // Three short rows that fit the rail, each picker sharing what its
        // row has left, so no name is cut short.
        RowLayout {
            Layout.fillWidth: true; spacing: 6
            SectionLabel { text: "ARP"; Layout.preferredWidth: midiOutPanel.labelWidth }
            Chip {
                objectName: "arpEnabled"
                text: midiOutPanel.arp.enabled ? "ON" : "OFF"
                implicitHeight: 22; implicitWidth: 44
                on: midiOutPanel.arp.enabled === true
                onClicked: songModel.setArpEnabled(songModel.selectedTrack, !midiOutPanel.arp.enabled)
            }
            Chip {
                objectName: "arpLatch"
                text: "LATCH"; implicitHeight: 22
                on: midiOutPanel.arp.latch === true
                onClicked: songModel.setArpLatch(songModel.selectedTrack, !midiOutPanel.arp.latch)
            }
            Item { Layout.fillWidth: true }
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 6
            Picker {
                objectName: "arpMode"
                Layout.fillWidth: true; Layout.preferredWidth: 1; Layout.minimumWidth: 0
                implicitHeight: 22
                choices: songModel.arpModes
                value: midiOutPanel.arp.mode || "UP"
                onPicked: name => songModel.setArpMode(songModel.selectedTrack, name)
            }
            Picker {
                objectName: "arpRate"
                Layout.fillWidth: true; Layout.preferredWidth: 1; Layout.minimumWidth: 0
                implicitHeight: 22
                choices: songModel.arpRates
                value: midiOutPanel.arp.rate || "1/16"
                onPicked: name => songModel.setArpRate(songModel.selectedTrack, name)
            }
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 6
            Picker {
                objectName: "arpOctaves"
                Layout.fillWidth: true; Layout.preferredWidth: 1; Layout.minimumWidth: 0
                implicitHeight: 22
                choices: ["1 OCT", "2 OCT", "3 OCT", "4 OCT"]
                value: (midiOutPanel.arp.octaves || 1) + " OCT"
                onPicked: name => songModel.setArpOctaves(songModel.selectedTrack, parseInt(name))
            }
            Picker {
                objectName: "arpGate"
                Layout.fillWidth: true; Layout.preferredWidth: 1; Layout.minimumWidth: 0
                implicitHeight: 22
                choices: ["GATE 25%", "GATE 50%", "GATE 75%", "GATE 100%"]
                value: "GATE " + Math.round((midiOutPanel.arp.gate || 0.5) * 100) + "%"
                onPicked: name => songModel.setArpGate(songModel.selectedTrack, parseInt(name.slice(5)) / 100)
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
                color: Theme.muted; font.pixelSize: 10; font.family: Theme.mono
            }
        }
    }

    ListView {
        id: pluginBrowser
        objectName: "pluginBrowser"
        Layout.fillWidth: true; Layout.fillHeight: true
        // Never squeezed to nothing by the panels above it: two rows at least.
        Layout.minimumHeight: 88
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
                         : modelData.format === "LV2" ? Theme.violet
                         : modelData.format === "Built-in" ? Theme.line : Theme.amber
                    Label { anchors.centerIn: parent
                        text: modelData.format === "Built-in" ? "INT"
                            : modelData.format === "Sampler" ? "SMP" : modelData.format
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
