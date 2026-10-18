import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// SETTINGS: what belongs to this machine rather than to the song - the audio
// system and device the song plays through, the MIDI clock, and how the
// window lays itself out. Remembered between launches, never saved with a
// song (features/audio_settings.feature, features/window_layout.feature).
Rectangle {
    id: root
    objectName: "settingsPanel"
    property string view: "ALL"
    property Item focusHome
    visible: view === "SETTINGS"
    Layout.fillWidth: true
    Layout.fillHeight: visible
    Layout.minimumHeight: visible ? 320 : 0
    implicitHeight: visible ? column.implicitHeight + 24 : 0
    radius: 6
    color: Theme.panel
    border.color: Theme.line

    readonly property int labelWidth: 112
    readonly property string systemName: appController.audioSystem
    readonly property var devices: appController.audioDevices
    readonly property var outputs: devices.filter(function(device) { return device.outputs > 0 })
    readonly property var inputs: devices.filter(function(device) { return device.inputs > 0 })
    readonly property var ui: appController.uiSettings
    function uiFlag(key, fallback) { return ui[key] === undefined ? fallback : ui[key] === true || ui[key] === "true" }

    // The devices are read when the page opens (not at launch: probing them
    // takes a moment), so one plugged in since is there.
    onVisibleChanged: if (visible) appController.refreshAudioDevices()

    component SettingRow: RowLayout {
        property alias label: rowLabel.text
        Layout.fillWidth: true; spacing: 8
        SectionLabel { id: rowLabel; Layout.preferredWidth: root.labelWidth }
    }
    component Hint: Label {
        Layout.fillWidth: true; Layout.minimumWidth: 0
        color: Theme.muted; font.pixelSize: 10; wrapMode: Text.Wrap
    }

    ScrollView {
        anchors.fill: parent; anchors.margins: 12
        contentWidth: availableWidth
        clip: true
        ColumnLayout {
            id: column
            width: parent.width
            spacing: 10

            RowLayout {
                Layout.fillWidth: true; spacing: 8
                SectionLabel { text: "SETTINGS" }
                Label {
                    text: "this machine's · remembered between launches, not saved with the song"
                    color: Theme.muted; font.pixelSize: 10
                    Layout.fillWidth: true; Layout.minimumWidth: 0; elide: Text.ElideRight
                }
            }

            // ------------------------------------------------------- audio
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.line }
            SectionLabel { text: "AUDIO"; color: Theme.acid }
            SettingRow {
                label: "SYSTEM"
                // The audio system the device is opened on: JACK is also how
                // PipeWire's JACK interface is reached.
                Picker {
                    objectName: "audioSystem"
                    Layout.preferredWidth: 220; Layout.minimumWidth: 60
                    implicitHeight: 24
                    readonly property var systems: appController.audioSystems
                    choices: systems.map(function(system) { return system.display })
                    value: {
                        for (var i = 0; i < systems.length; ++i)
                            if (systems[i].name === root.systemName) return systems[i].display
                        return "AUTO"
                    }
                    onPicked: function(display) {
                        for (var i = 0; i < systems.length; ++i)
                            if (systems[i].display === display) appController.setAudioSystem(systems[i].name)
                    }
                }
                Hint {
                    text: "PipeWire: JACK (through pipewire-jack) or PULSE. Windows: WASAPI, DirectSound "
                          + "or ASIO. Mac: Core Audio. Only what this build can open is listed."
                }
            }
            SettingRow {
                label: "OUTPUT"
                visible: !appController.serverClocked
                Picker {
                    objectName: "audioOutputDevice"
                    Layout.preferredWidth: 320; Layout.minimumWidth: 60
                    implicitHeight: 24
                    choices: ["DEFAULT"].concat(root.outputs.map(function(device) { return device.name }))
                    value: appController.audioOutputDevice !== "" ? appController.audioOutputDevice : "DEFAULT"
                    onPicked: name => appController.setAudioOutputDevice(name === "DEFAULT" ? "" : name)
                }
                Chip {
                    objectName: "refreshAudioDevices"
                    text: "REFRESH"; implicitHeight: 24
                    accessibleLabel: "List the audio devices again"
                    onClicked: appController.refreshAudioDevices()
                }
                Item { Layout.fillWidth: true }
            }
            SettingRow {
                label: "INPUT"
                visible: !appController.serverClocked
                Picker {
                    objectName: "audioInputDevice"
                    Layout.preferredWidth: 320; Layout.minimumWidth: 60
                    implicitHeight: 24
                    choices: ["DEFAULT"].concat(root.inputs.map(function(device) { return device.name }))
                    value: appController.audioInputDevice !== "" ? appController.audioInputDevice : "DEFAULT"
                    onPicked: name => appController.setAudioInputDevice(name === "DEFAULT" ? "" : name)
                }
                Hint { text: "opened only while a track records audio input" }
            }
            SettingRow {
                label: "SAMPLE RATE"
                Picker {
                    objectName: "audioSampleRate"
                    Layout.preferredWidth: 140
                    implicitHeight: 24
                    enabled: !appController.serverClocked
                    opacity: enabled ? 1 : 0.5
                    choices: appController.audioSampleRates.map(function(rate) { return rate + " Hz" })
                    value: appController.audioSampleRate + " Hz"
                    onPicked: name => appController.setAudioSampleRate(parseInt(name))
                }
                Hint {
                    text: appController.serverClocked ? "set by the JACK server" : "asked for; the device may run at another"
                }
            }
            SettingRow {
                label: "BUFFER"
                Picker {
                    objectName: "audioBufferFrames"
                    Layout.preferredWidth: 140
                    implicitHeight: 24
                    enabled: !appController.serverClocked
                    opacity: enabled ? 1 : 0.5
                    choices: appController.audioBufferSizes.map(function(frames) { return frames + " frames" })
                    value: appController.audioBufferFrames + " frames"
                    onPicked: name => appController.setAudioBufferFrames(parseInt(name))
                }
                Hint {
                    text: appController.serverClocked ? "set by the JACK server"
                          : "smaller answers sooner, larger is safer against dropouts ("
                            + (1000 * appController.audioBufferFrames / appController.audioSampleRate).toFixed(1)
                            + " ms)"
                }
            }
            SettingRow {
                label: "RUNNING"
                Label {
                    objectName: "audioDeviceStatus"
                    Layout.fillWidth: true; Layout.minimumWidth: 0
                    text: appController.audioDevice
                    color: Theme.ink; font.family: Theme.mono; font.pixelSize: 11; font.bold: true
                    elide: Text.ElideRight
                }
            }

            // -------------------------------------------------------- midi
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.line }
            SectionLabel { text: "MIDI"; color: Theme.acid }
            SettingRow {
                label: "CLOCK OUT"
                Picker {
                    objectName: "midiClockPort"
                    Layout.preferredWidth: 320; Layout.minimumWidth: 60
                    implicitHeight: 24
                    choices: ["OFF"].concat(appController.midiOutPorts)
                    value: appController.midiClockPort !== "" ? appController.midiClockPort : "OFF"
                    onPicked: name => appController.setMidiClockPort(name === "OFF" ? "" : name)
                }
                Hint { text: "drum machines and sequencers on this port follow the song" }
            }
            // Following another machine's clock, from the MIDI input: its
            // Start, Stop and position move the transport, and its tempo is
            // played.
            SettingRow {
                label: "SYNC"
                Chip {
                    objectName: "followClock"
                    text: "EXT"; implicitHeight: 24
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
                    font.pixelSize: 10; font.family: Theme.mono; font.bold: true
                    elide: Text.ElideRight
                }
            }

            // ----------------------------------------------------- plugins
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.line }
            SectionLabel { text: "PLUGINS"; color: Theme.acid }
            SettingRow {
                label: "ISOLATE"
                Chip {
                    objectName: "pluginIsolation"
                    text: appController.pluginIsolation ? "ON" : "OFF"; implicitHeight: 24
                    implicitWidth: 52
                    enabled: appController.pluginIsolationAvailable
                    accessibleLabel: "Run each plugin in a process of its own"
                    on: appController.pluginIsolation
                    onClicked: appController.setPluginIsolation(!appController.pluginIsolation)
                }
                Hint {
                    text: appController.pluginIsolationAvailable
                          ? "each plugin runs in a process of its own: one that crashes or hangs goes "
                            + "silent and can be reloaded, and Blokkily keeps running. Costs one "
                            + "block of latency, which is compensated"
                          : "not available in this build: plugins run inside Blokkily"
                }
            }

            // --------------------------------------------------- interface
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.line }
            SectionLabel { text: "WINDOW"; color: Theme.acid }
            SettingRow {
                label: "AUTO-HIDE"
                // Off, a bar stays docked; on, it tucks into the window's edge
                // while the editors are worked and comes back when wanted.
                Chip {
                    objectName: "autoHideRail"
                    text: "LEFT BAR"; implicitHeight: 24
                    accessibleLabel: "Auto-hide the left bar"
                    on: root.uiFlag("ui/autoHideRail", true)
                    onClicked: appController.setUiSetting("ui/autoHideRail", !on)
                }
                Chip {
                    objectName: "autoHideMixer"
                    text: "RIGHT BAR"; implicitHeight: 24
                    accessibleLabel: "Auto-hide the right bar"
                    on: root.uiFlag("ui/autoHideMixer", true)
                    onClicked: appController.setUiSetting("ui/autoHideMixer", !on)
                }
                Hint {
                    text: "a hidden bar comes back when the pointer rests on its edge, and stays while "
                          + "it is used; both stay docked when the window has room for them"
                }
            }
            SettingRow {
                label: "THEME"
                Repeater {
                    model: [{ name: "DARK", value: "dark" }, { name: "LIGHT", value: "light" },
                            { name: "SYSTEM", value: "system" }]
                    Chip {
                        required property var modelData
                        objectName: "theme" + modelData.name
                        text: modelData.name; implicitHeight: 24
                        accessibleLabel: modelData.name.toLowerCase() + " theme"
                        on: (root.ui["ui/theme"] !== undefined ? String(root.ui["ui/theme"]) : "dark")
                            === modelData.value
                        onClicked: appController.setUiSetting("ui/theme", modelData.value)
                    }
                }
                Hint { text: "SYSTEM follows the desktop's light or dark setting" }
            }
            SettingRow {
                label: "LAYOUT"
                Chip {
                    objectName: "resetLayout"
                    text: "RESET"; implicitHeight: 24
                    accessibleLabel: "Lay the window out as shipped"
                    onClicked: appController.resetUiSettings()
                }
                Hint {
                    text: "drag a panel's grip in the left margin to move it, the gap under it to size it, "
                          + "and a bar's inner edge to widen it"
                }
            }
            Item { Layout.fillHeight: true }
        }
    }
}
