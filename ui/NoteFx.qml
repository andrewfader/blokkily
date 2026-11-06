import QtQuick

// Note effects (features/playable_keyboards.feature): a burst drawn over the
// key each note is played on, in the style chosen under SETTINGS · NOTE FX.
// Fire rises, lightning strikes down onto the key, water ripples out, a
// tornado spins up, and the 8-bit star flickers through its colours as it
// flies apart. A burst is a short-lived item of its own: it takes no presses
// and removes itself once it has faded, so the surface underneath is never
// touched.
Item {
    id: fx
    property string style: "NONE"
    // How many particles a burst has (0..1), how far they reach (0..1), and
    // how long a burst lasts, in milliseconds.
    property real intensity: 0.6
    property real size: 0.5
    property int decay: 700
    readonly property var styles: ["NONE", "FIRE", "LIGHTNING", "WATER", "TORNADO", "STARMAN"]
    enabled: false

    // Starts a burst at (x, y) in this item's pixels, coloured by the degree.
    function burst(x, y, degree) {
        var make = { "FIRE": fire, "LIGHTNING": lightning, "WATER": water,
                     "TORNADO": tornado, "STARMAN": starman }[style]
        if (make === undefined) return null
        return make.createObject(fx, { x: x, y: y, hue: (((degree % 12) + 12) % 12) / 12,
                                       count: Math.round(6 + 30 * intensity),
                                       reach: 30 + 110 * size, life: Math.max(120, decay) })
    }
    // A deterministic scatter, so a burst looks the same each time it is drawn.
    function rnd(seed) { return Math.abs(Math.sin(seed * 12.9898 + 78.233) * 43758.5453) % 1 }

    component Burst: Item {
        id: burstRoot
        property real hue: 0
        property int count: 12
        property real reach: 80
        property int life: 700
        Timer { interval: burstRoot.life + 60; running: true; onTriggered: burstRoot.destroy() }
    }

    // Embers rise from the key, each its own shade of yellow to red, and go out.
    Component {
        id: fire
        Burst {
            id: fireBurst
            Repeater {
                model: fireBurst.count
                Rectangle {
                    id: ember
                    required property int index
                    readonly property real r1: fx.rnd(index + 1)
                    readonly property real r2: fx.rnd(index + 101)
                    readonly property real r3: fx.rnd(index + 201)
                    width: 3 + 7 * fireBurst.reach / 140 * (0.5 + r3); height: width; radius: width / 2
                    x: -width / 2; y: -height / 2
                    color: Qt.hsla(0.02 + 0.12 * r1, 1, 0.5 + 0.3 * r2, 1)
                    ParallelAnimation {
                        running: true
                        NumberAnimation { target: ember; property: "y"; from: -ember.height / 2
                            to: -fireBurst.reach * (0.5 + ember.r2); duration: fireBurst.life * (0.6 + 0.4 * ember.r1)
                            easing.type: Easing.OutQuad }
                        NumberAnimation { target: ember; property: "x"; from: -ember.width / 2
                            to: (ember.r3 - 0.5) * fireBurst.reach * 0.6; duration: fireBurst.life }
                        NumberAnimation { target: ember; property: "opacity"; from: 1; to: 0; duration: fireBurst.life }
                        NumberAnimation { target: ember; property: "scale"; from: 1; to: 0.2; duration: fireBurst.life }
                    }
                }
            }
        }
    }

    // A bolt from the top of the surface down onto the key, flickering out.
    Component {
        id: lightning
        Burst {
            id: bolt
            Canvas {
                id: strike
                x: -30; y: -bolt.y
                width: 60; height: Math.max(12, bolt.y + 8)
                onPaint: {
                    var c = getContext("2d")
                    c.clearRect(0, 0, width, height)
                    var points = [Qt.point(30, 0)]
                    for (var py = 14; py < height - 8; py += 14)
                        points.push(Qt.point(30 + (fx.rnd(py + bolt.count) - 0.5) * 28, py))
                    points.push(Qt.point(30, height - 8))
                    for (var pass = 0; pass < 2; ++pass) {
                        c.beginPath()
                        c.moveTo(points[0].x, points[0].y)
                        for (var i = 1; i < points.length; ++i) c.lineTo(points[i].x, points[i].y)
                        c.lineWidth = pass === 0 ? 7 : 2
                        c.strokeStyle = pass === 0 ? "rgba(140, 180, 255, 0.35)" : "#f4f8ff"
                        c.stroke()
                    }
                }
                Component.onCompleted: requestPaint()
            }
            Rectangle {
                width: 10 + bolt.reach / 4; height: width; radius: width / 2
                x: -width / 2; y: -height / 2
                color: "#bfd4ff"
            }
            SequentialAnimation on opacity {
                running: true
                NumberAnimation { from: 1; to: 0.25; duration: bolt.life * 0.12 }
                NumberAnimation { to: 1; duration: bolt.life * 0.1 }
                NumberAnimation { to: 0; duration: bolt.life * 0.6 }
            }
        }
    }

    // Rings ripple out from the key.
    Component {
        id: water
        Burst {
            id: ripple
            Repeater {
                model: 3
                Rectangle {
                    id: ring
                    required property int index
                    width: 12; height: 12; radius: 6; x: -6; y: -6
                    color: "transparent"
                    border.width: 2
                    border.color: Qt.hsla(0.55 + 0.04 * index, 0.9, 0.65, 1)
                    opacity: 0; scale: 0.2
                    SequentialAnimation {
                        running: true
                        PauseAnimation { duration: ring.index * ripple.life * 0.16 }
                        ParallelAnimation {
                            NumberAnimation { target: ring; property: "scale"; from: 0.2
                                to: ripple.reach / 12 * (0.6 + 0.2 * ring.index); duration: ripple.life * 0.7
                                easing.type: Easing.OutCubic }
                            SequentialAnimation {
                                NumberAnimation { target: ring; property: "opacity"; to: 0.9; duration: 40 }
                                NumberAnimation { target: ring; property: "opacity"; to: 0; duration: ripple.life * 0.62 }
                            }
                        }
                    }
                }
            }
        }
    }

    // Dust spins up from the key in a widening funnel.
    Component {
        id: tornado
        Burst {
            id: twister
            Item {
                id: funnel
                Repeater {
                    model: twister.count
                    Rectangle {
                        required property int index
                        readonly property real t: index / Math.max(1, twister.count - 1)
                        width: 3 + 3 * t; height: width; radius: width / 2
                        color: Qt.hsla(0, 0, 0.6 + 0.3 * fx.rnd(index + 7), 1)
                        x: Math.cos(index * 0.9) * (4 + twister.reach * 0.35 * t) - width / 2
                        y: -twister.reach * 0.9 * t - height / 2
                    }
                }
                NumberAnimation on rotation { from: 0; to: 900; duration: twister.life; running: true }
                NumberAnimation on y { from: 8; to: -twister.reach * 0.3; duration: twister.life; running: true }
                NumberAnimation on opacity { from: 1; to: 0; duration: twister.life; running: true }
            }
        }
    }

    // 8-bit stars fly out in a ring, each flickering through the power-up's
    // colours.
    Component {
        id: starman
        Burst {
            id: stars
            readonly property int points: Math.max(5, Math.round(count / 3))
            Repeater {
                model: stars.points
                Text {
                    id: star
                    required property int index
                    readonly property real angle: index * 2 * Math.PI / stars.points
                    text: "★"
                    font.pixelSize: 10 + 10 * stars.reach / 140; font.family: Theme.mono
                    x: -implicitWidth / 2; y: -implicitHeight / 2
                    color: "#ffcc00"
                    SequentialAnimation on color {
                        running: true; loops: Animation.Infinite
                        ColorAnimation { to: "#ff3b30"; duration: 70 }
                        ColorAnimation { to: "#ffcc00"; duration: 70 }
                        ColorAnimation { to: "#34c759"; duration: 70 }
                        ColorAnimation { to: "#5ac8fa"; duration: 70 }
                        ColorAnimation { to: "#af52de"; duration: 70 }
                    }
                    ParallelAnimation {
                        running: true
                        NumberAnimation { target: star; property: "x"; from: -star.implicitWidth / 2
                            to: Math.cos(star.angle) * stars.reach - star.implicitWidth / 2
                            duration: stars.life; easing.type: Easing.OutQuad }
                        NumberAnimation { target: star; property: "y"; from: -star.implicitHeight / 2
                            to: Math.sin(star.angle) * stars.reach - star.implicitHeight / 2
                            duration: stars.life; easing.type: Easing.OutQuad }
                        NumberAnimation { target: star; property: "opacity"; from: 1; to: 0; duration: stars.life }
                    }
                }
            }
        }
    }
}
