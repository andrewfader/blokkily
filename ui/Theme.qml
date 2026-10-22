pragma Singleton
import QtQuick

// The palette every panel draws with, dark or light (SETTINGS · WINDOW ·
// THEME; the window sets `light`). The accents keep their hue in both; in the
// light theme they are deepened so text in them reads on white, and text on a
// lit chip (onAccent) stays dark in both.
QtObject {
    property bool light: false

    readonly property color bg: light ? "#f4f5f7" : "#0e0f12"
    readonly property color ink: light ? "#1b1d22" : "#e8e9ed"
    readonly property color muted: light ? "#5f6673" : "#868b98"
    // Text that steps back further: an empty strip's name, a hint.
    readonly property color faint: light ? "#8a909c" : "#5c626e"
    readonly property color panel: light ? "#ffffff" : "#181a1f"
    readonly property color raised: light ? "#eceef2" : "#1f222a"
    readonly property color line: light ? "#d3d7de" : "#2a2e37"
    readonly property color acid: light ? "#5a8200" : "#c8ff3d"
    readonly property color blue: light ? "#2f5fe0" : "#4d7cff"
    readonly property color amber: light ? "#c77700" : "#ffb340"
    // LV2 plugins' badge.
    readonly property color violet: light ? "#7a5ce0" : "#a98bff"
    // Recording, and nothing else: a red that means the song is being written.
    readonly property color record: light ? "#d92b3d" : "#ff4d5e"
    // The colours a track can be given (its menu's COLOUR), mid-tones that
    // read on both themes and under onAccent text. A track not given one is
    // drawn in blue, as every track was before.
    readonly property var trackPalette: ["#e5484d", "#f76b15", "#ffb224", "#46a758",
                                         "#12a594", "#4d7cff", "#8e4ec6", "#d6409f"]
    function trackColour(chosen) { return chosen !== undefined && chosen !== "" ? chosen : blue }
    // Text and marks drawn on a lit accent.
    readonly property color onAccent: "#0e0f12"

    // Surfaces: the side bars, the transport, an inset well (meters, lanes),
    // a stripe (black keys' rows, every other cell), the grid's lines, a
    // part with nothing in it yet, and a scrim over the window.
    readonly property color rail: light ? "#e9ebef" : "#121317"
    readonly property color transport: light ? "#e4e7ec" : "#141519"
    readonly property color well: light ? "#dfe2e7" : "#0c0e11"
    readonly property color stripe: light ? "#eef0f3" : "#15171c"
    readonly property color cell: light ? "#f6f7f9" : "#191c22"
    readonly property color gridline: light ? "#dde1e7" : "#22262e"
    readonly property color gridStrong: light ? "#b9c0ca" : "#3d434e"
    readonly property color hollow: light ? "#e3e9f7" : "#1a2233"
    readonly property color scrim: light ? "#b3f4f5f7" : "#b30e0f12"
    // Rows the tracker marks: the beat, the selection, the playhead.
    readonly property color beatRow: light ? "#f3f5f8" : "#1e2128"
    readonly property color selectedRow: light ? "#e4e8ef" : "#2b2f38"
    readonly property color playRow: light ? "#e3f0c8" : "#3c4930"
    readonly property color lockRow: light ? "#eef3dc" : "#3a3f2a"
    readonly property color loopRegion: light ? "#e6f5c8" : "#2a3a14"
    // The piano roll's keys and the playable keyboard's.
    readonly property color whiteKey: light ? "#ffffff" : "#39404b"
    readonly property color blackKey: light ? "#3a3f48" : "#111318"
    readonly property color scaleKey: light ? "#dfe6f5" : "#2d323d"
    readonly property color scaleBorder: light ? "#9fb0d6" : "#3a4150"
    readonly property color keyBorder: light ? "#c3c9d3" : "#333947"
    // A light that is on but quiet (the MIDI input idle), a choice not taken
    // in the sampler, an audio clip whose file is missing.
    readonly property color idleLight: light ? "#b9d27a" : "#4a5a22"
    readonly property color unchosen: light ? "#f0dcb6" : "#4d3a16"
    readonly property color missing: light ? "#f6d3d8" : "#3a1b21"
    readonly property color bypassed: light ? "#e2e4e8" : "#15161a"

    // The bundled interface face. The window sets it for every control, so a
    // desktop theme (qt6ct, Plasma) cannot swap in a font whose metrics the
    // layout was not drawn for.
    readonly property string sans: "IBM Plex Sans"
    // The bundled readout face (gui_main.cpp registers it): counters, keys
    // and values line up in columns on every machine.
    readonly property string mono: "JetBrains Mono"
}
