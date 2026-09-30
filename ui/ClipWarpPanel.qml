import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Clip warp (item 3.6): how one audio clip is stretched and pitched. Opened
// from a clip's W button. FOLLOW makes the clip follow the song's tempo map
// from its source tempo (detected when it has none), STRETCH is a plain
// length ratio, and PITCH shifts by semitones and cents without changing the
// length. Every change is one step of history. While the clip's rendition is
// being rendered off the audio thread the state reads RENDERING and the clip
// is silent; READY once it plays.
Popup {
    id: panel
    objectName: "clipWarpPanel"
    property real clipId: 0
    // The clip as the song has it now; re-read whenever the clips change.
    readonly property var row: clipId > 0 && songModel.audioClips !== undefined
        ? songModel.audioClip(clipId) : ({})
    readonly property bool known: row.id !== undefined
    padding: 12
    margins: 8
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { color: Theme.panel; border.color: Theme.amber; radius: 6 }

    // Writes the clip's whole warp, starting from what it has now.
    function commit(follow, bpm, ratio, semitones, cents) {
        if (!known) return false
        return songModel.setAudioClipWarp(clipId, follow, bpm, ratio, semitones, cents)
    }
    function typed(field, fallback) {
        var value = parseFloat(field.text)
        return isNaN(value) ? fallback : value
    }

    component FieldBox: TextField {
        Layout.preferredWidth: 64
        Layout.preferredHeight: 26
        color: Theme.ink
        font.family: Theme.mono; font.pixelSize: 12; font.bold: true
        selectByMouse: true
        horizontalAlignment: TextInput.AlignRight
        inputMethodHints: Qt.ImhFormattedNumbersOnly
        background: Rectangle {
            color: Theme.raised; radius: 3
            border.color: parent.activeFocus ? Theme.amber : Theme.line
        }
    }
    component Caption: Label {
        color: Theme.muted; font.pixelSize: 9; font.bold: true; font.letterSpacing: 1
        Layout.preferredWidth: 72
    }

    ColumnLayout {
        objectName: "clipWarpContent"
        spacing: 8
        RowLayout {
            spacing: 8
            SectionLabel { text: "WARP" }
            Label {
                Layout.preferredWidth: 150
                text: panel.known ? panel.row.name : ""
                color: Theme.ink; font.pixelSize: 11; font.bold: true
                elide: Text.ElideRight
            }
        }
        RowLayout {
            spacing: 8
            Chip {
                objectName: "warpFollow"
                text: "FOLLOW TEMPO"
                accent: Theme.amber
                on: panel.known && panel.row.follow
                onClicked: appController.setClipFollowTempo(panel.clipId, !on)
            }
            Label {
                objectName: "warpState"
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignRight
                text: !panel.known ? "" : panel.row.rendering ? "RENDERING…"
                    : panel.row.warped ? "READY" : "OFF"
                color: panel.row.rendering ? Theme.amber : panel.row.warped ? Theme.acid : Theme.muted
                font.pixelSize: 10; font.bold: true; font.letterSpacing: 1
            }
        }
        RowLayout {
            spacing: 6
            Caption { text: "SOURCE BPM" }
            FieldBox {
                id: bpmField
                objectName: "warpSourceBpm"
                text: panel.known && panel.row.sourceBpm > 0 ? panel.row.sourceBpm.toFixed(2) : ""
                validator: DoubleValidator { bottom: 0; top: 400; decimals: 2 }
                onAccepted: panel.commit(panel.row.follow, panel.typed(bpmField, 0),
                                         panel.row.ratio, panel.row.semitones, panel.row.cents)
            }
            Chip {
                objectName: "warpDetect"
                text: "DETECT"
                implicitHeight: 24
                onClicked: {
                    var bpm = appController.detectClipTempo(panel.clipId)
                    if (bpm > 0)
                        panel.commit(panel.row.follow, bpm, panel.row.ratio,
                                     panel.row.semitones, panel.row.cents)
                }
            }
        }
        RowLayout {
            spacing: 6
            Caption { text: "STRETCH ×" }
            FieldBox {
                id: ratioField
                objectName: "warpRatio"
                text: panel.known ? Number(panel.row.ratio).toFixed(3) : ""
                validator: DoubleValidator { bottom: 0.25; top: 4; decimals: 3 }
                onAccepted: panel.commit(panel.row.follow, panel.row.sourceBpm,
                                         panel.typed(ratioField, 1), panel.row.semitones,
                                         panel.row.cents)
            }
        }
        RowLayout {
            spacing: 6
            Caption { text: "PITCH" }
            FieldBox {
                id: semitoneField
                objectName: "warpSemitones"
                Layout.preferredWidth: 48
                text: panel.known ? String(panel.row.semitones) : ""
                validator: IntValidator { bottom: -24; top: 24 }
                onAccepted: panel.commit(panel.row.follow, panel.row.sourceBpm, panel.row.ratio,
                                         Math.round(panel.typed(semitoneField, 0)),
                                         panel.row.cents)
            }
            Label { text: "ST"; color: Theme.muted; font.pixelSize: 9 }
            FieldBox {
                id: centField
                objectName: "warpCents"
                Layout.preferredWidth: 56
                text: panel.known ? Number(panel.row.cents).toFixed(1) : ""
                validator: DoubleValidator { bottom: -100; top: 100; decimals: 1 }
                onAccepted: panel.commit(panel.row.follow, panel.row.sourceBpm, panel.row.ratio,
                                         panel.row.semitones, panel.typed(centField, 0))
            }
            Label { text: "CT"; color: Theme.muted; font.pixelSize: 9 }
        }
        RowLayout {
            spacing: 8
            Chip {
                objectName: "warpReset"
                text: "RESET"
                onClicked: panel.commit(false, 0, 1, 0, 0)
            }
            Label {
                Layout.fillWidth: true
                text: appController.warpStatus
                color: Theme.muted; font.pixelSize: 9
                elide: Text.ElideRight
                Layout.preferredWidth: 150
            }
        }
    }
}
