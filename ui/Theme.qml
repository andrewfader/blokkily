pragma Singleton
import QtQuick

// The palette every panel draws with.
QtObject {
    readonly property color bg: "#0e0f12"
    readonly property color ink: "#e8e9ed"
    readonly property color muted: "#868b98"
    readonly property color panel: "#181a1f"
    readonly property color raised: "#1f222a"
    readonly property color line: "#2a2e37"
    readonly property color acid: "#c8ff3d"
    readonly property color blue: "#4d7cff"
    readonly property color amber: "#ffb340"
    // Recording, and nothing else: a red that means the song is being written.
    readonly property color record: "#ff4d5e"
    // The bundled interface face. The window sets it for every control, so a
    // desktop theme (qt6ct, Plasma) cannot swap in a font whose metrics the
    // layout was not drawn for.
    readonly property string sans: "IBM Plex Sans"
    // The bundled readout face (gui_main.cpp registers it): counters, keys
    // and values line up in columns on every machine.
    readonly property string mono: "JetBrains Mono"
}
