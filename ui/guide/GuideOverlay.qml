import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The floating guide overlay: the Blokkily mascot (ui/Blokkily.qml, Andrew's
// Mooninite-style sprite), a speech bubble, a spotlight ring
// that dims the rest of the window, a question box, and the lesson controls.
//
// It draws nothing that takes a click outside the bubble, the mascot and the
// question box: the window stays playable while it talks. All state lives in
// guideController (the C++ bridge); this file only renders it.
Item {
    id: overlay
    objectName: "guideOverlay"
    anchors.fill: parent
    z: 1000
    visible: guideController.guideEnabled

    // The item a spotlight name points at, if it is on screen and has a size.
    // A stop whose target is missing (a view that hides it, a feature not
    // built) gets the bubble without the ring.
    function findNamed(item, name) {
        if (item === null || item === undefined || item === overlay) return null
        if (item.objectName === name) return item
        var children = item.children
        for (var i = 0; i < children.length; ++i) {
            var found = findNamed(children[i], name)
            if (found !== null) return found
        }
        return null
    }

    function targetItem() {
        var name = guideController.spotlightTarget
        if (name === "") return null
        var found = findNamed(overlay.Window.contentItem, name)
        if (found === null || !found.visible || found.width < 1 || found.height < 1) return null
        var area = found.mapToItem(overlay, 0, 0, found.width, found.height)
        if (area.x >= overlay.width || area.y >= overlay.height
                || area.x + area.width <= 0 || area.y + area.height <= 0) return null
        return { x: area.x, y: area.y, w: area.width, h: area.height }
    }

    // Turns the guide on and opens its bubble with the keyboard in the
    // question box: where the tour hands over.
    function ask() {
        guideController.guideEnabled = true
        guideController.open()
        questionField.forceActiveFocus()
    }

    property var spot: targetItem()
    property bool hasSpot: guideController.spotlightTarget !== "" && spot !== null

    // Recompute when the layout settles or the target changes.
    Connections {
        target: guideController
        function onSpotlightChanged() { overlay.spot = overlay.targetItem() }
    }
    onWidthChanged: spot = targetItem()
    onHeightChanged: spot = targetItem()
    // Mascot and bubble follow the target, the window and the bubble's own
    // size: placed again whenever any of them changes.
    onSpotChanged: place()

    // ------------------------------------------------------- the spotlight
    // Four dimming rectangles around the target; the ring on top of them.
    Repeater {
        model: overlay.hasSpot ? 4 : 0
        Rectangle {
            required property int index
            color: "#000000"; opacity: 0.45
            x: [0, overlay.spot.x + overlay.spot.w, overlay.spot.x, overlay.spot.x][index]
            y: [0, 0, 0, overlay.spot.y + overlay.spot.h][index]
            width: [overlay.spot.x, overlay.width - overlay.spot.x - overlay.spot.w,
                    overlay.spot.w, overlay.spot.w][index]
            height: [overlay.height, overlay.height, overlay.spot.y,
                     overlay.height - overlay.spot.y - overlay.spot.h][index]
        }
    }
    Rectangle {
        objectName: "guideRing"
        visible: overlay.hasSpot
        x: overlay.spot ? overlay.spot.x - 5 : 0
        y: overlay.spot ? overlay.spot.y - 5 : 0
        width: overlay.spot ? overlay.spot.w + 10 : 0
        height: overlay.spot ? overlay.spot.h + 10 : 0
        color: "transparent"
        border.color: Theme.acid; border.width: 2
        radius: 8
    }

    // ------------------------------------------------- mascot and bubble
    // The mascot shows only while the guide talks, beside its bubble: idle,
    // the overlay draws nothing that takes a click. The bubble sits low in
    // the middle, or flies to the spotlight target (below it with room,
    // above without).
    property real bubbleX: (overlay.width - bubble.width) / 2
    property real bubbleY: overlay.height - bubble.height - mascot.height - 40

    function place() {
        var s = overlay.spot
        if (overlay.hasSpot) {
            var margin = 12, gap = 10
            var groupW = mascot.width + gap + bubble.width
            var groupH = Math.max(mascot.height, bubble.height)
            var left = Math.max(0, s.x), right = Math.min(overlay.width, s.x + s.w)
            var upper = Math.max(0, s.y), lower = Math.min(overlay.height, s.y + s.h)
            var centre = (left + right) / 2, top
            if (lower + 18 + groupH <= overlay.height - margin) top = lower + 18
            else if (upper - 18 - groupH >= margin) top = upper - 18 - groupH
            else top = Math.min(overlay.height - groupH - margin, upper + 28)
            var groupLeft = Math.max(margin, Math.min(overlay.width - groupW - margin,
                                                      centre - groupW / 2))
            var mascotLeft = centre >= overlay.width / 2
            if (mascotLeft) {
                mascot.x = groupLeft; bubbleX = groupLeft + mascot.width + gap
            } else {
                bubbleX = groupLeft; mascot.x = groupLeft + bubble.width + gap
            }
            mascot.y = top + (groupH - mascot.height) / 2
            bubbleY = top
        } else {
            bubbleX = (overlay.width - bubble.width) / 2
            bubbleY = overlay.height - bubble.height - 40
            mascot.x = Math.max(12, bubbleX - mascot.width - 10)
            mascot.y = bubbleY + bubble.height - mascot.height
        }
    }

    onHasSpotChanged: place()
    Component.onCompleted: place()

    Blokkily {
        id: mascot
        objectName: "guideMascot"
        pixel: 3
        talking: guideController.bubbleVisible
        visible: guideController.bubbleVisible
        Behavior on x { NumberAnimation { duration: 350; easing.type: Easing.InOutQuad } }
        Behavior on y { NumberAnimation { duration: 350; easing.type: Easing.InOutQuad } }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: guideController.dismiss()
        }
    }

    Rectangle {
        id: bubble
        objectName: "guideBubble"
        visible: guideController.bubbleVisible
        width: Math.min(380, overlay.width - 32)
        height: bubbleColumn.implicitHeight + 28
        x: bubbleX; y: bubbleY
        Behavior on x { NumberAnimation { duration: 350; easing.type: Easing.InOutQuad } }
        Behavior on y { NumberAnimation { duration: 350; easing.type: Easing.InOutQuad } }
        onHeightChanged: overlay.place()
        color: Theme.raised
        border.color: guideController.bubbleKind === "llm" ? Theme.acid : Theme.line
        // A click on the bubble is the bubble's, never the editor's beneath.
        MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }
        border.width: guideController.bubbleKind === "llm" ? 2 : 1
        radius: 10

        Column {
            id: bubbleColumn
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 14 }
            spacing: 8

            RowLayout {
                width: parent.width
                spacing: 8
                Label {
                    objectName: "guideBubbleTitle"
                    text: guideController.bubbleTitle === "" ? (guideController.bubbleKind === "llm" ? "AI ANSWER" : "GUIDE") : guideController.bubbleTitle
                    color: Theme.acid; font.pixelSize: 10; font.bold: true; font.letterSpacing: 1
                }
                Item { width: 1; height: 1; Layout.fillWidth: true }
                Label {
                    text: "✕"; color: Theme.dim; font.pixelSize: 12
                    MouseArea {
                        anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                        onClicked: guideController.dismiss()
                    }
                }
            }

            Label {
                objectName: "guideBubbleText"
                width: parent.width
                text: guideController.bubbleText
                color: Theme.ink; font.pixelSize: 13
                wrapMode: Text.WordWrap
            }

            // Lesson progress + controls.
            Column {
                width: parent.width; spacing: 6
                visible: guideController.lessonActive
                Label {
                    objectName: "guideLessonStep"
                    text: "Step " + (guideController.lessonStepIndex + 1) + " of " + guideController.lessonStepCount
                          + (guideController.lessonProgress !== "" ? "  ·  " + guideController.lessonProgress + " done" : "")
                    color: Theme.dim; font.pixelSize: 11
                }
                Row {
                    spacing: 8
                    Button { objectName: "guideLessonBack"; text: "← Back"; onClicked: guideController.lessonBack() }
                    Button { objectName: "guideLessonNext"; text: "Next →"; onClicked: guideController.lessonNext() }
                    Button { objectName: "guideLessonEnd"; text: "End lesson"; onClicked: guideController.stopLesson() }
                }
            }

            // Destructive-action confirmation.
            Column {
                width: parent.width; spacing: 6
                visible: guideController.confirmVisible
                Label {
                    objectName: "guideConfirmText"
                    width: parent.width; wrapMode: Text.WordWrap
                    text: guideController.confirmText
                    color: Theme.warn; font.pixelSize: 13; font.bold: true
                }
                Row {
                    spacing: 8
                    Button { objectName: "guideConfirmYes"; text: "Yes, do it"; onClicked: guideController.confirmPending() }
                    Button { objectName: "guideConfirmCancel"; text: "Cancel"; onClicked: guideController.cancelPending() }
                }
            }

            // Whether a question the rules miss may go to the prompt bar's
            // model: off until the producer says so.
            CheckBox {
                objectName: "guideAskModel"
                visible: !guideController.lessonActive && !guideController.confirmVisible
                text: "ASK MODEL when my rules have no answer"
                checked: guideController.askModel
                onToggled: guideController.askModel = checked
                font.pixelSize: 10
            }

            // Question box + teach-me.
            Row {
                width: parent.width; spacing: 8
                visible: !guideController.lessonActive && !guideController.confirmVisible
                TextField {
                    id: questionField
                    objectName: "guideQuestionField"
                    width: parent.width - askButton.width - teachButton.width - 16
                    placeholderText: guideController.llmEnabled ? "Ask about theory, or anything…" : "Ask about triads, cadences, scales…"
                    enabled: !guideController.asking
                    onAccepted: {
                        guideController.askQuestion(text)
                        text = ""
                    }
                }
                Button {
                    id: askButton; objectName: "guideAskButton"
                    text: guideController.asking ? "…" : "Ask"
                    enabled: !guideController.asking
                    onClicked: { guideController.askQuestion(questionField.text); questionField.text = "" }
                }
                Button {
                    id: teachButton; objectName: "guideTeachButton"
                    text: "Teach me"
                    onClicked: lessonPicker.open()
                }
            }

            Button {
                objectName: "guideOffButton"
                visible: !guideController.lessonActive && !guideController.confirmVisible
                text: "Turn the guide off"
                onClicked: guideController.guideEnabled = false
            }
        }
    }

    // Lesson picker popup.
    Popup {
        id: lessonPicker
        objectName: "guideLessonPicker"
        anchors.centerIn: parent
        width: 300
        modal: true
        Column {
            width: parent.width; spacing: 4
            Label { text: "PICK A LESSON"; color: Theme.acid; font.pixelSize: 10; font.bold: true; font.letterSpacing: 1 }
            Repeater {
                model: guideController.lessonIds
                Button {
                    required property string modelData
                    objectName: "guideLesson-" + modelData
                    width: parent.width
                    text: guideController.lessonTitleFor(modelData)
                    onClicked: { guideController.startLesson(modelData); lessonPicker.close() }
                }
            }
            Button { text: "Close"; width: parent.width; onClicked: lessonPicker.close() }
        }
    }
}
