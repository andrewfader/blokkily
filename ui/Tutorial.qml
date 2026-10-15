import QtQuick
import QtQuick.Controls

// The guided tour behind the HELP button. Blokkily hops from one part of the
// window to the next with a speech bubble, a ring flies to the control being
// talked about, and a keycap tooltip follows it with the key that drives it.
// The tour ends at its last step, on CANCEL, on the bubble's close button, on
// Escape or X, or when HELP is pressed again; Blokkily then jumps off screen.
//
// The window puts it in its overlay so the ring can reach the transport bar
// as well as the panels.
// It draws nothing that takes a click outside the bubble and the mascot:
// the window stays playable while it talks.
Item {
    id: tour
    objectName: "tutorial"
    anchors.fill: parent
    z: 100
    visible: running || leaving

    // Where the keyboard goes back to when the tour is over.
    property Item focusHome: null
    property bool running: false
    // True while Blokkily is on the way out.
    property bool leaving: false
    property int step: 0

    // Each stop: the objectName of what it points at ("" for none), a title,
    // what Blokkily says, and the key that drives it. A stop whose target is
    // not on screen (a view that hides it, a feature not built) is passed by.
    readonly property var steps: [
        { target: "", title: "HI, I'M BLOKKILY",
          text: "Half T-block, half broccoli, all groove. Let me show you around. Press Esc or X any time to send me away.",
          keys: "" },
        { target: "playButton", title: "TRANSPORT",
          text: "Play, record and rewind the song from here.",
          keys: "SPACE / HOME" },
        { target: "tempoBox", title: "TEMPO",
          text: "Drag it up or down, scroll it, or double-click to type a tempo.",
          keys: "SHIFT = FINE" },
        { target: "viewSwitcher", title: "VIEWS",
          text: "Step grid, tracker and piano roll are one pattern seen three ways. ALL shows them side by side.",
          keys: "" },
        { target: "arrangementView", title: "THE SONG",
          text: "Patterns placed on bars make the song. Right-click a pattern or a track for its menu.",
          keys: "RIGHT-CLICK" },
        { target: "loopButton", title: "LOOP",
          text: "LOOP wraps playback between IN and OUT. Shift-click a bar number for IN, Ctrl-click for OUT. Export still writes the whole song.",
          keys: "LOOP / IN / OUT" },
        { target: "stepGrid", title: "STEPS",
          text: "Click a step to light it. Type notes like a tracker: Z to M is one octave, Q to I the next.",
          keys: "CTRL+1..0" },
        { target: "instrumentPanel", title: "INSTRUMENT",
          text: "What the selected track plays. Search the browser for a CLAP, VST3 or SoundFont.",
          keys: "CTRL+F" },
        { target: "mixerPanel", title: "MIXER",
          text: "Every track has a fader, pan, mute and solo. You hear moves straight away.",
          keys: "CTRL+M / CTRL+L" },
        { target: "promptBar", title: "ASSISTANT",
          text: "Ask for a beat or a bassline in plain words and approve what comes back.",
          keys: "CTRL+K" },
        { target: "helpButton", title: "THAT'S THE TOUR",
          text: "Press HELP whenever you want me back. Now go make something!",
          keys: "F1" }
    ]
    readonly property var current: steps[step]
    // Worked out when the tour moves rather than bound: finding the stops
    // walks every item in the window, and a binding over that walk would be
    // re-run by any item anywhere changing its size or visibility, tour or
    // no tour.
    property bool lastStep: false
    // How many times a stop's target has been looked for. A closed tour looks
    // for none, however much the window around it is laid out again.
    property int searches: 0

    // ------------------------------------------------------------ the tour

    function start() {
        // Placed, not flown, while it is set up: a restart in the middle of
        // the exit must not animate back from where Blokkily fell to.
        animate = false
        exitAnimation.stop()
        hop.stop()
        leaving = false
        exitDrop = 0; exitSpin = 0; fade = 1
        step = 0
        running = true
        // Blokkily drops in from above the window; the bubble grows out of
        // the middle.
        mascotX = (width - mascot.width) / 2
        mascotY = -mascot.height - 20
        bubbleX = (width - bubble.width) / 2
        bubbleY = (height - bubble.height) / 2
        bubble.scale = 0.4
        animate = true
        bubble.scale = 1
        place()
        tour.forceActiveFocus()
    }

    function finish() {
        if (!running) return
        running = false
        leaving = true
        exitAnimation.restart()
        if (focusHome !== null) focusHome.forceActiveFocus()
    }

    function toggle() {
        if (running) finish()
        else start()
    }

    function next() {
        var index = nextShown(step)
        if (index < 0) { finish(); return }
        step = index
        place()
    }

    function back() {
        for (var index = step - 1; index >= 0; --index) {
            if (index === 0 || targetOf(index) !== null) {
                step = index
                place()
                return
            }
        }
    }

    // The first stop after `from` that has something to point at, or -1.
    function nextShown(from) {
        for (var index = from + 1; index < steps.length; ++index)
            if (targetOf(index) !== null) return index
        return -1
    }

    function findNamed(item, name) {
        if (item === null || item === undefined || item === tour) return null
        if (item.objectName === name) return item
        var children = item.children
        for (var index = 0; index < children.length; ++index) {
            var found = findNamed(children[index], name)
            if (found !== null) return found
        }
        return null
    }

    // The item stop `index` points at, if it is on screen and has a size.
    function targetOf(index) {
        ++searches
        var name = steps[index].target
        if (name === "") return null
        var found = findNamed(tour.Window.contentItem, name)
        if (found === null || !found.visible || found.width < 1 || found.height < 1) return null
        var area = found.mapToItem(tour, 0, 0, found.width, found.height)
        if (area.x >= width || area.y >= height
                || area.x + area.width <= 0 || area.y + area.height <= 0) return null
        return found
    }

    // ------------------------------------------------------------ layout

    // Where everything is going; the items fly there.
    property bool animate: false
    property real bubbleX: 0
    property real bubbleY: 0
    property real mascotX: 0
    property real mascotY: 0
    property bool mascotOnLeft: true
    property rect ring: Qt.rect(width / 2, height / 2, 0, 0)
    property bool hasRing: false
    // The exit: how far Blokkily has fallen and turned, and how faded the
    // rest is.
    property real exitDrop: 0
    property real exitSpin: 0
    property real fade: 1

    onWidthChanged: if (running) place()
    onHeightChanged: if (running) place()

    function place() {
        if (!running) return
        var margin = 12
        var gap = 10
        var groupWidth = mascot.width + gap + bubble.width
        var groupHeight = Math.max(mascot.height, bubble.height)
        lastStep = nextShown(step) < 0
        var target = targetOf(step)
        var centre, top
        if (target !== null) {
            var area = target.mapToItem(tour, 0, 0, target.width, target.height)
            var left = Math.max(0, area.x), right = Math.min(width, area.x + area.width)
            var upper = Math.max(0, area.y), lower = Math.min(height, area.y + area.height)
            ring = Qt.rect(left - 5, upper - 5, right - left + 10, lower - upper + 10)
            hasRing = true
            centre = (left + right) / 2
            // Below the target if there is room, above it if not, and over
            // its top edge when it fills the window both ways.
            if (lower + 18 + groupHeight <= height - margin) top = lower + 18
            else if (upper - 18 - groupHeight >= margin) top = upper - 18 - groupHeight
            else top = Math.min(height - groupHeight - margin, upper + 28)
        } else {
            hasRing = false
            ring = Qt.rect(width / 2, height / 2, 0, 0)
            centre = width / 2
            top = (height - groupHeight) / 2
        }
        var groupLeft = Math.max(margin, Math.min(width - groupWidth - margin,
                                                  centre - groupWidth / 2))
        // Blokkily stands on the side of the bubble nearer the window's
        // middle, looking at it.
        mascotOnLeft = centre >= width / 2
        if (mascotOnLeft) {
            mascotX = groupLeft
            bubbleX = groupLeft + mascot.width + gap
        } else {
            bubbleX = groupLeft
            mascotX = groupLeft + bubble.width + gap
        }
        bubbleY = top + groupHeight - bubble.height
        mascotY = top + groupHeight - mascot.height
        hop.restart()
        typed = 0
        typewriter.restart()
    }

    // ------------------------------------------------------------ keys

    function handles(event) {
        if (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)) return false
        switch (event.key) {
        case Qt.Key_Escape: case Qt.Key_X:
        case Qt.Key_Right: case Qt.Key_Left:
        case Qt.Key_Return: case Qt.Key_Enter: case Qt.Key_Space:
            return true
        }
        return false
    }
    // The window's own arrow, Space and Return shortcuts stand aside while
    // the tour has the keyboard, so they turn its pages instead.
    Keys.onShortcutOverride: function(event) { event.accepted = handles(event) }
    Keys.onPressed: function(event) {
        if (!handles(event)) return
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_X) finish()
        else if (event.key === Qt.Key_Left) back()
        else next()
        event.accepted = true
    }
    // Escape still sends Blokkily away when a click has taken the keyboard
    // somewhere else in the window.
    Shortcut {
        sequence: "Escape"
        enabled: tour.running && !tour.activeFocus
        onActivated: tour.finish()
    }

    // ------------------------------------------------------------ drawing

    // The window dimmed everywhere but the ring.
    Item {
        anchors.fill: parent
        opacity: tour.fade * 0.5
        readonly property color shade: "#000000"
        Rectangle { color: parent.shade; x: 0; y: 0; width: parent.width; height: Math.max(0, ringFrame.y) }
        Rectangle { color: parent.shade; x: 0; y: ringFrame.y + ringFrame.height
                    width: parent.width; height: Math.max(0, parent.height - y) }
        Rectangle { color: parent.shade; x: 0; y: ringFrame.y
                    width: Math.max(0, ringFrame.x); height: ringFrame.height }
        Rectangle { color: parent.shade; x: ringFrame.x + ringFrame.width; y: ringFrame.y
                    width: Math.max(0, parent.width - x); height: ringFrame.height }
    }

    Rectangle {
        id: ringFrame
        objectName: "tutorialRing"
        x: tour.ring.x; y: tour.ring.y
        width: tour.ring.width; height: tour.ring.height
        visible: tour.hasRing
        opacity: tour.fade
        color: "transparent"
        radius: 6
        border.width: 2
        border.color: Theme.acid
        Behavior on x { enabled: tour.animate; NumberAnimation { duration: 480; easing.type: Easing.OutCubic } }
        Behavior on y { enabled: tour.animate; NumberAnimation { duration: 480; easing.type: Easing.OutCubic } }
        Behavior on width { enabled: tour.animate; NumberAnimation { duration: 480; easing.type: Easing.OutCubic } }
        Behavior on height { enabled: tour.animate; NumberAnimation { duration: 480; easing.type: Easing.OutCubic } }
        SequentialAnimation on border.width {
            running: tour.running; loops: Animation.Infinite
            NumberAnimation { to: 4; duration: 500; easing.type: Easing.InOutSine }
            NumberAnimation { to: 2; duration: 500; easing.type: Easing.InOutSine }
        }
    }

    // The keycap tooltip: it trails the ring to the target's corner.
    Rectangle {
        id: keycap
        objectName: "tutorialKeycap"
        readonly property string keys: tour.current.keys
        visible: tour.hasRing && keys !== ""
        opacity: tour.fade
        x: Math.max(4, Math.min(tour.width - width - 4, tour.ring.x + tour.ring.width - width / 2))
        y: Math.max(4, tour.ring.y - height / 2)
        width: keycapText.implicitWidth + 14; height: 20; radius: 4
        color: Theme.amber
        border.color: "#0e0f12"
        Label {
            id: keycapText
            anchors.centerIn: parent
            text: keycap.keys
            color: "#0e0f12"; font.family: Theme.mono; font.pixelSize: 10; font.bold: true
        }
        Behavior on x { enabled: tour.animate; NumberAnimation { duration: 700; easing.type: Easing.OutBack } }
        Behavior on y { enabled: tour.animate; NumberAnimation { duration: 700; easing.type: Easing.OutBack } }
    }

    // The speech bubble.
    Rectangle {
        id: bubble
        objectName: "tutorialBubble"
        x: tour.bubbleX; y: tour.bubbleY
        width: 300
        height: bubbleColumn.implicitHeight + 24
        opacity: tour.fade
        radius: 8
        color: Theme.panel
        border.color: Theme.acid; border.width: 2
        Behavior on x { enabled: tour.animate; NumberAnimation { duration: 520; easing.type: Easing.OutBack } }
        Behavior on y { enabled: tour.animate; NumberAnimation { duration: 520; easing.type: Easing.OutBack } }
        Behavior on scale { enabled: tour.animate; NumberAnimation { duration: 420; easing.type: Easing.OutBack } }

        // The tail points at Blokkily.
        Rectangle {
            width: 12; height: 12; rotation: 45
            color: Theme.panel
            border.color: Theme.acid; border.width: 2
            x: tour.mascotOnLeft ? -6 : parent.width - 6
            y: parent.height - 30
            z: -1
        }
        // Covers the tail's inner half, so it joins the bubble's edge.
        Rectangle {
            width: 8; height: 16; color: Theme.panel
            x: tour.mascotOnLeft ? 2 : parent.width - 10
            y: parent.height - 32
        }
        MouseArea { anchors.fill: parent }

        Column {
            id: bubbleColumn
            x: 14; y: 12
            width: parent.width - 28
            spacing: 8
            Item {
                width: parent.width; height: 18
                Label {
                    objectName: "tutorialTitle"
                    text: tour.current.title
                    color: Theme.acid; font.pixelSize: 12; font.bold: true; font.letterSpacing: 1.5
                }
                Label {
                    anchors.right: closeButton.left; anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: (tour.step + 1) + " / " + tour.steps.length
                    color: Theme.muted; font.family: Theme.mono; font.pixelSize: 10
                }
                Chip {
                    id: closeButton
                    objectName: "tutorialClose"
                    anchors.right: parent.right
                    implicitWidth: 22; implicitHeight: 18
                    text: "✕"
                    accessibleLabel: "Close the tour"
                    onClicked: tour.finish()
                }
            }
            // What is not typed yet is drawn clear, so the bubble is the size
            // of the whole line from the first letter and does not grow.
            Text {
                objectName: "tutorialText"
                width: parent.width
                wrapMode: Text.WordWrap
                textFormat: Text.StyledText
                color: Theme.ink; font.pixelSize: 12; lineHeight: 1.15
                readonly property string whole: tour.current.text
                text: whole.substring(0, tour.typed)
                      + "<font color=\"#00000000\">" + whole.substring(tour.typed) + "</font>"
            }
            Item {
                width: parent.width; height: 26
                Chip {
                    objectName: "tutorialCancel"
                    text: "CANCEL"
                    onClicked: tour.finish()
                }
                Row {
                    anchors.right: parent.right
                    spacing: 6
                    Chip {
                        objectName: "tutorialBack"
                        text: "BACK"
                        enabled: tour.step > 0
                        onClicked: tour.back()
                    }
                    Chip {
                        objectName: "tutorialNext"
                        text: tour.lastStep ? "DONE" : "NEXT"
                        on: true
                        onClicked: tour.next()
                    }
                }
            }
        }
    }

    property int typed: 0
    Timer {
        id: typewriter
        interval: 16; repeat: true
        running: false
        onTriggered: {
            tour.typed += 2
            if (tour.typed >= tour.current.text.length) {
                tour.typed = tour.current.text.length
                stop()
            }
        }
    }

    // Blokkily.
    Item {
        id: mascotSlot
        x: tour.mascotX
        y: tour.mascotY + tour.exitDrop
        width: mascot.width; height: mascot.height
        rotation: tour.exitSpin
        Behavior on x { enabled: tour.animate; NumberAnimation { duration: 560; easing.type: Easing.InOutQuad } }
        Behavior on y { enabled: tour.animate && !tour.leaving
                        NumberAnimation { duration: 560; easing.type: Easing.OutBack } }

        property real hopHeight: 0
        Blokkily {
            id: mascot
            objectName: "tutorialMascot"
            pixel: 4
            y: -mascotSlot.hopHeight
            talking: tour.running && typewriter.running
            // The art looks right; turned round, it looks left at the bubble.
            transform: Scale { origin.x: mascot.width / 2; xScale: tour.mascotOnLeft ? 1 : -1 }
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: hop.restart()
        }
    }

    // A hop on every step: up, and down with a little squash.
    SequentialAnimation {
        id: hop
        NumberAnimation { target: mascotSlot; property: "hopHeight"; to: 26; duration: 260; easing.type: Easing.OutQuad }
        NumberAnimation { target: mascotSlot; property: "hopHeight"; to: 0; duration: 300; easing.type: Easing.OutBounce }
    }

    // The exit: a little jump, then a tumble off the bottom of the window
    // while the bubble and ring fade.
    SequentialAnimation {
        id: exitAnimation
        ParallelAnimation {
            NumberAnimation { target: tour; property: "fade"; to: 0; duration: 260 }
            NumberAnimation { target: tour; property: "exitDrop"; to: -30; duration: 200; easing.type: Easing.OutQuad }
        }
        ParallelAnimation {
            NumberAnimation { target: tour; property: "exitDrop"; to: tour.height + 120
                              duration: 420; easing.type: Easing.InQuad }
            NumberAnimation { target: tour; property: "exitSpin"; to: 200; duration: 420 }
        }
        ScriptAction { script: tour.leaving = false }
    }
}
