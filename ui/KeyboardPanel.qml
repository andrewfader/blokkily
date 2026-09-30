import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import "Format.js" as Format

// One playable surface over the song's own tuning and scale.
// Every surface produces the same kind of cell, so the piano,
// the isomorphic grid, the fretboard and the chord pads are one
// renderer laid out four ways rather than four editors.
Rectangle {
    // Which editors the window is showing; KEYS gives the surface the room.
    property string view: "ALL"
    // Where the keyboard goes back to after a key is played.
    property Item focusHome: null
    id: keyboardPanel
    objectName: "keyboardPanel"
    // A surface with many rows — a fretboard, or anything
    // turned on its side — needs more of the window than a
    // single strip of piano keys. The panel asks for what the
    // laid-out surface needs, up to a share of the window; past
    // that the surface scrolls rather than shrinking its keys
    // below the size of a fingertip.
    // Piano keys may be narrow; grid and chord cells need room
    // for a readable pitch name and a usable pointer target.
    // Both orientations scroll once those dimensions are met.
    readonly property real keyExtent: keyboardModel.surface === "PIANO" ? 14 : 60
    readonly property real wantedSurface:
        Math.min(360, keyboardModel.spanY * keyExtent)
    readonly property real wantedHeight: Math.max(168, wantedSurface + 104)
    Layout.fillWidth: true
    Layout.fillHeight: keyboardPanel.view === "KEYS"
    Layout.preferredHeight: wantedHeight
    Layout.maximumHeight: keyboardPanel.view === "KEYS" ? 100000 : wantedHeight
    Layout.minimumHeight: 140
    visible: keyboardPanel.view === "ALL" || keyboardPanel.view === "KEYS"
    radius: 6; color: Theme.panel; border.color: Theme.line; clip: true

    ColumnLayout {
        anchors.fill: parent; anchors.margins: 10; spacing: 7

        // The song's tuning, its scale, and the key it is in.
        // These belong to the session, not to the keyboard, so
        // changing one re-reads every editor at once.
        RowLayout {
            objectName: "tuningBar"
            Layout.fillWidth: true; Layout.minimumWidth: 0
            clip: true
            spacing: 6
            SectionLabel { text: "TUNING" }
            Picker {
                objectName: "tuningPicker"
                choices: songModel.tuningNames
                value: songModel.tuningName
                onPicked: name => songModel.setTuning(name)
            }
            Label { objectName: "divisionReadout"
                text: songModel.divisions + " STEPS"; color: Theme.muted
                font.pixelSize: 10; font.family: Theme.mono }
            SectionLabel { text: "SCALE" }
            Picker {
                objectName: "scalePicker"
                choices: songModel.scaleNames
                value: songModel.scaleName
                onPicked: name => songModel.setScale(name)
            }
            SectionLabel { text: "ROOT" }
            Chip {
                objectName: "rootDown"; text: "<"
                onClicked: songModel.setRootDegree(songModel.rootDegree - 1)
            }
            Label { objectName: "rootReadout"; text: songModel.rootName
                color: Theme.acid; font.pixelSize: 11; font.bold: true
                font.family: Theme.mono }
            Chip {
                objectName: "rootUp"; text: ">"
                onClicked: songModel.setRootDegree(songModel.rootDegree + 1)
            }
            Chip {
                objectName: "autoScaleChip"; text: "AUTO-SCALE"; accent: Theme.blue
                on: songModel.autoScale
                onClicked: songModel.toggleAutoScale()
            }
            Item { Layout.fillWidth: true }
            Label { objectName: "lastPlayed"
                text: "PLAYED " + keyboardModel.lastPlayed
                color: Theme.ink; font.pixelSize: 11; font.family: Theme.mono }
        }

        // Which surface is under the hands, and the one setting
        // that surface has of its own.
        RowLayout {
            objectName: "surfaceBar"
            // Its own contents may not set the panel's width:
            // a long layout name would otherwise push the
            // surface beside it out past the panel's edge.
            Layout.fillWidth: true; Layout.minimumWidth: 0
            clip: true
            spacing: 6
            SectionLabel { text: "SURFACE" }
            Repeater {
                model: keyboardModel.surfaces
                Chip {
                    required property var modelData
                    objectName: "surface" + modelData
                    text: modelData
                    on: keyboardModel.surface === modelData
                    onClicked: keyboardModel.setSurface(modelData)
                }
            }
            Picker {
                objectName: "layoutPicker"
                visible: keyboardModel.surface === "GRID"
                choices: keyboardModel.layoutNames
                value: keyboardModel.layoutName
                onPicked: name => keyboardModel.setLayout(name)
            }
            Picker {
                objectName: "stringPicker"
                visible: keyboardModel.surface === "FRETS"
                choices: keyboardModel.stringTuningNames
                value: keyboardModel.stringTuningName
                onPicked: name => keyboardModel.setStringTuning(name)
            }
            Picker {
                objectName: "registerPicker"
                visible: keyboardModel.surface === "PIANO"
                           || keyboardModel.surface === "GRID"
                choices: keyboardModel.registerNames
                value: keyboardModel.registerName
                onPicked: name => keyboardModel.setRegister(name)
            }
            // One chip rather than two: the bar has to fit
            // beside the surface's own setting, and a control
            // row that cannot shrink drags the keyboard out
            // past the panel that is meant to hold it.
            Chip {
                objectName: "orientToggle"
                text: "RUNS " + keyboardModel.orientation
                accent: Theme.blue
                on: keyboardModel.orientation === "DOWN"
                onClicked: keyboardModel.toggleOrientation()
            }
            Item { Layout.fillWidth: true }
            Chip {
                objectName: "keyboardRecord"
                text: keyboardModel.recording ? "WRITING STEP " +
                          Format.fmt2(patternModel.selectedStep + 1) : "AUDITION ONLY"
                accent: Theme.amber
                on: keyboardModel.recording
                onClicked: keyboardModel.toggleRecording()
            }
        }

        // The cells themselves. Row and column come from the
        // model, so nothing here has to know which surface it
        // is drawing.
        Flickable {
            id: keyboardScroll
            objectName: "keyboardScroll"
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true
            // Composite the viewport before placing it in the
            // window. This also bounds Shape painting on Qt's
            // software renderer when the surface scrolls.
            layer.enabled: true
            boundsBehavior: Flickable.StopAtBounds
            // A surface that fits is laid out to fill. One that
            // does not scrolls, because a key too small to hit
            // is not a key.
            contentWidth: Math.max(width, keyboardModel.spanX *
                keyboardPanel.keyExtent * keyboardSurface.cellAspect)
            contentHeight: Math.max(height,
                keyboardModel.spanY * keyboardPanel.keyExtent)
            ScrollBar.vertical: ScrollBar {
                objectName: "keyboardScrollBar"
                policy: ScrollBar.AsNeeded
            }
            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

        Item {
            id: keyboardSurface
            objectName: "keyboardSurface"
            width: keyboardScroll.contentWidth
            height: keyboardScroll.contentHeight
            // The model says how much room the layout needs, in
            // cell widths and heights. Staggered rows are half a
            // cell wider than their contents and hexagonal rows
            // overlap, so this is measured rather than counted.
            readonly property real spanX: Math.max(0.001, keyboardModel.spanX)
            readonly property real spanY: Math.max(0.001, keyboardModel.spanY)
            readonly property bool hex: keyboardModel.cellShape === "HEX"
            // A honeycomb turns with the surface: rows of
            // pointy-top cells become columns of flat-top ones.
            readonly property bool flatTop:
                keyboardModel.orientation === "DOWN"
            // A hexagon only reads as one, and only tiles as
            // one, while it stays close to regular. So a
            // honeycomb keeps the width its height asks for and
            // sits centred in whatever room is left, where a
            // square grid still stretches to fill the way keys
            // always have.
            readonly property real cellAspect:
                !hex ? 1.0 : (flatTop ? 2.0 / Math.sqrt(3.0)
                                      : Math.sqrt(3.0) / 2.0)
            readonly property real cellHeight:
                hex ? Math.min(height / spanY, width / spanX / cellAspect)
                    : height / spanY
            readonly property real cellWidth:
                hex ? cellHeight * cellAspect : width / spanX
            readonly property real originX: (width - cellWidth * spanX) / 2
            readonly property real originY: (height - cellHeight * spanY) / 2

            // The six corners of a cell, in its own pixels.
            function hexCorners(w, h) {
                if (flatTop)
                    return [Qt.point(0.25 * w, 0), Qt.point(0.75 * w, 0),
                            Qt.point(w, 0.5 * h), Qt.point(0.75 * w, h),
                            Qt.point(0.25 * w, h), Qt.point(0, 0.5 * h),
                            Qt.point(0.25 * w, 0)]
                return [Qt.point(0.5 * w, 0), Qt.point(w, 0.25 * h),
                        Qt.point(w, 0.75 * h), Qt.point(0.5 * w, h),
                        Qt.point(0, 0.75 * h), Qt.point(0, 0.25 * h),
                        Qt.point(0.5 * w, 0)]
            }

            // Hexagons interlock, so their rectangles overlap at
            // the corners. A press there belongs to the
            // neighbour it is drawn inside, not to whichever
            // cell happens to be painted on top.
            function insideCell(nx, ny) {
                if (!hex) return true
                var dx = Math.abs(nx - 0.5), dy = Math.abs(ny - 0.5)
                return flatTop ? (2 * dx + dy <= 1) : (2 * dy + dx <= 1)
            }

            Repeater {
                model: keyboardModel.cells
                Item {
                    id: cellItem
                    required property var modelData
                    objectName: "keyboardCell" + modelData.index
                    x: keyboardSurface.originX
                       + modelData.x * keyboardSurface.cellWidth
                    y: keyboardSurface.originY
                       + modelData.y * keyboardSurface.cellHeight
                    width: keyboardSurface.cellWidth - 2
                    height: keyboardSurface.cellHeight - 2
                    // A key says three things at once: whether
                    // it is raised, whether the scale holds it,
                    // and whether it is the root of that scale.
                    readonly property color fill: keyInput.pressed ? Theme.amber
                           : modelData.root ? Theme.acid
                           : (modelData.accidental ? "#101218"
                              : (modelData.inScale ? "#2d323d" : Theme.raised))
                    readonly property color edge:
                        modelData.root ? Theme.acid
                        : (modelData.inScale ? "#3a4150" : "#333947")
                    opacity: modelData.inScale || modelData.accidental
                             ? 1.0 : (keyboardSurface.hex ? 0.8 : 0.55)

                    Rectangle {
                        objectName: "rectTile"
                        anchors.fill: parent
                        visible: !keyboardSurface.hex
                        radius: 3
                        color: cellItem.fill
                        border.width: modelData.inScale ? 1 : 0
                        border.color: cellItem.edge
                    }

                    Shape {
                        objectName: "hexTile"
                        anchors.fill: parent
                        visible: keyboardSurface.hex
                        ShapePath {
                            fillColor: cellItem.fill
                            // Every cell of a honeycomb is
                            // outlined. One that is not is not
                            // a dim key, it is a hole in the
                            // tiling, and a player cannot count
                            // steps across a hole.
                            strokeColor: cellItem.edge
                            strokeWidth: 1
                            PathPolyline {
                                path: keyboardSurface.hexCorners(
                                    cellItem.width, cellItem.height)
                            }
                        }
                    }

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 0
                        Label {
                            id: cellName
                            Layout.alignment: Qt.AlignHCenter
                            // A name that does not fit its key
                            // is not a name, it is a smear
                            // across its neighbours. A tuning
                            // of thirty-one degrees has longer
                            // names than one of twelve, so the
                            // label measures itself rather than
                            // guessing at a cell size.
                            visible: implicitWidth
                                     <= cellItem.width * 0.9
                                     && implicitHeight
                                        <= cellItem.height * 0.55
                            text: modelData.label
                            color: modelData.root ? "#0e0f12"
                                   : (modelData.accidental ? Theme.muted : Theme.ink)
                            font.pixelSize: 9; font.family: Theme.mono
                            font.bold: modelData.root
                        }
                        // The retune is the whole point in a
                        // tuning that is not twelve tones: it
                        // is what the instrument is told.
                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            visible: modelData.retune !== ""
                                     && cellName.visible
                                     && implicitWidth
                                        <= cellItem.width * 0.9
                            text: modelData.retune
                            color: modelData.root ? "#0e0f12" : Theme.blue
                            font.pixelSize: 8; font.family: Theme.mono
                        }
                    }

                    // A key sounds when it goes down and
                    // rings until it comes back up.
                    MouseArea {
                        id: keyInput
                        objectName: "keyInput"
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onPressed: mouse => {
                            // Outside the drawn cell the press
                            // belongs to the neighbour beneath.
                            mouse.accepted = keyboardSurface.insideCell(
                                mouse.x / Math.max(1, width),
                                mouse.y / Math.max(1, height))
                            if (!mouse.accepted) return
                            keyboardPanel.focusHome.forceActiveFocus()
                            keyboardModel.hold(modelData.index)
                        }
                        onReleased: keyboardModel.release()
                        onCanceled: keyboardModel.release()
                    }
                }
            }
        }
        }
    }
}
