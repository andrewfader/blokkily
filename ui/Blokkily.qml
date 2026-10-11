import QtQuick

// The Blokkily mascot: a T tetromino that grew up to be a broccoli, drawn one
// square pixel at a time. The crown of florets is the T's bar, the stalk its
// stem. It blinks now and then and bobs while it talks.
Item {
    id: root
    // The size of one art pixel on screen.
    property real pixel: 4
    // Bobs while true, the way a sprite talks.
    property bool talking: false
    // Eyes shut, for a blink.
    property bool blinking: false

    // 20 x 20. K outline, L floret highlight, G floret, D floret shade,
    // W eye white, P pupil, R blush, S stalk, s stalk shade.
    readonly property var art: [
        "...KKK..KKKK..KKK...",
        "..KLLGK.KLLGKKLLGK..",
        ".KLGGGDKLGGGDKGGGDK.",
        "KLGGDGGGGGDGGGGDGGDK",
        "KGGGGGGGGGGGGGGGGGDK",
        "KGDGGGGGGGGGGGGGGDDK",
        "KGGKKKKGGGGGGKKKKGDK",
        "KGGKWWPGGGGGGKWWPGDK",
        "KGGKWPPGGGGGGKWPPGDK",
        "KGRRGGGGGKKGGGGGRRDK",
        "KDGGGGGGGGGGGGGGGDDK",
        ".KDDDDKSSSSSSKDDDDK.",
        "..KKKKKSSSSSssKKKK..",
        "......KSSSSSssK.....",
        "......KSSSSSssK.....",
        "......KSSSSSssK.....",
        "......KSSSSSssK.....",
        "......KKSKKKSKK.....",
        ".......KSK.KSK......",
        "......KKKK.KKKK....."
    ]
    readonly property var colours: ({
        "K": "#101a08", "L": "#c8ff3d", "G": "#6cc22a", "D": "#3a7a16",
        "W": "#ffffff", "P": "#0e0f12", "R": "#ff7a96", "S": "#b0de6e", "s": "#7eaa46"
    })

    implicitWidth: 20 * pixel
    implicitHeight: 20 * pixel

    // A shut eye is a dark lid across its lower row on a green face.
    function colorAt(row, column) {
        var glyph = art[row].charAt(column)
        if (blinking && (glyph === "W" || glyph === "P"))
            glyph = row === 8 ? "K" : "G"
        return colours[glyph]
    }

    Item {
        id: sprite
        width: 20 * root.pixel; height: 20 * root.pixel
        anchors.horizontalCenter: parent.horizontalCenter
        y: 0
        Repeater {
            model: 400
            Rectangle {
                required property int index
                readonly property int row: Math.floor(index / 20)
                readonly property int column: index % 20
                visible: root.art[row].charAt(column) !== "."
                x: column * root.pixel; y: row * root.pixel
                width: root.pixel; height: root.pixel
                color: root.colorAt(row, column)
                antialiasing: false
            }
        }
        SequentialAnimation on y {
            running: root.talking && root.visible
            loops: Animation.Infinite
            NumberAnimation { to: -root.pixel; duration: 220; easing.type: Easing.OutQuad }
            NumberAnimation { to: 0; duration: 220; easing.type: Easing.InQuad }
            PauseAnimation { duration: 260 }
        }
    }

    Timer {
        interval: 2600; repeat: true; running: root.visible
        onTriggered: { root.blinking = true; unblink.restart() }
    }
    Timer { id: unblink; interval: 140; onTriggered: root.blinking = false }
}
