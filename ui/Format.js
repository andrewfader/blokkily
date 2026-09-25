.pragma library

// Text formatting shared by every panel.

// A number as two digits: step 3 prints as "03".
function fmt2(n) { return n.toString().padStart(2, "0") }

// A MIDI key as a note name with its octave: 60 prints as "C4".
function keyName(key) {
    var names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
    return names[((key % 12) + 12) % 12] + (Math.floor(key / 12) - 1)
}
