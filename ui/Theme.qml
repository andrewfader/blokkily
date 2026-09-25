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
}
