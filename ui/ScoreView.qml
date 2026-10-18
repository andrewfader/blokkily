import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "Smufl.js" as Smufl

// The score: a fourth projection of the canonical pattern, drawn as true
// staff notation with the Bravura SMuFL font. It draws what scoreModel lays
// out (the open part under the song's clef, key and meter). Notes are entered
// on the staff: a click writes a note of the chosen value into the pattern
// (beside one already sounding there, as a chord), a click on a notehead
// selects it, and the arrows, the accidental chips and Delete edit it - each
// one step of undo, through the same pattern edits the piano roll makes. The
// clef, key and CHANT controls set the song's notation for the part. Spacing
// follows the rhythm; it is readable, not Gould-perfect.
Rectangle {
    id: root
    objectName: "scoreView"
    color: Theme.panel; radius: 6; border.color: Theme.line; clip: true

    property var keyChoices: ["-7", "-6", "-5", "-4", "-3", "-2", "-1", "0",
                              "+1", "+2", "+3", "+4", "+5", "+6", "+7"]
    property bool warnedNoFont: false

    FontLoader { id: bravura; source: "qrc:/fonts/Bravura.otf" }
    // Laid out only while it is on screen.
    Binding { target: scoreModel; property: "active"; value: root.visible }

    FileDialog {
        id: importScoreDialog
        objectName: "importScoreDialog"
        title: "Import a MusicXML score as a new section"
        nameFilters: ["MusicXML scores (*.musicxml *.xml)"]
        fileMode: FileDialog.OpenFile
        onAccepted: appController.importMusicXmlFile(selectedFile.toString())
    }
    FileDialog {
        id: exportScoreDialog
        objectName: "exportScoreDialog"
        title: "Export the song as a MusicXML score"
        nameFilters: ["MusicXML scores (*.musicxml)"]
        fileMode: FileDialog.SaveFile
        defaultSuffix: "musicxml"
        onAccepted: appController.exportMusicXmlFile(selectedFile.toString())
    }
    readonly property bool fontOk: bravura.status === FontLoader.Ready

    ColumnLayout {
        anchors.fill: parent; anchors.margins: 10; spacing: 6
        RowLayout {
            Layout.fillWidth: true
            Label { text: "SCORE"; color: Theme.ink; font.bold: true
                font.pixelSize: 12; font.letterSpacing: 1 }
            Label { text: scoreModel.keyLabel; color: Theme.muted; font.pixelSize: 9
                font.family: Theme.mono }
            Item { Layout.fillWidth: true }
            // Scores in and out as MusicXML: a hymn, a choral piece or a
            // concerto from notation software becomes a section with a
            // track per staff; the whole song goes out as a score.
            Chip {
                objectName: "importScoreButton"; text: "SCORE IN"
                implicitHeight: 20; accessibleLabel: "Import a MusicXML score"
                onClicked: importScoreDialog.open()
            }
            Chip {
                objectName: "exportScoreButton"; text: "SCORE OUT"
                implicitHeight: 20; accessibleLabel: "Export the song as a MusicXML score"
                onClicked: exportScoreDialog.open()
            }
            // Every track's part of the section, a staff each, lined up.
            Chip {
                objectName: "fullScoreChip"; text: "FULL"
                implicitHeight: 20; accessibleLabel: "Full score: every part of the section"
                on: scoreModel.fullScore
                onClicked: scoreModel.fullScore = !scoreModel.fullScore
            }
            Chip {
                objectName: "chantChip"; text: "CHANT"
                implicitHeight: 20; accessibleLabel: "Unmetered chant staff"
                on: scoreModel.unmetered
                onClicked: scoreModel.unmetered = !scoreModel.unmetered
            }
            Picker {
                objectName: "clefPicker"
                choices: scoreModel.clefNames()
                value: scoreModel.clefName
                onPicked: name => scoreModel.clefKind = scoreModel.clefNames().indexOf(name)
            }
            Picker {
                objectName: "keyPicker"
                choices: root.keyChoices
                value: (scoreModel.keyFifths > 0 ? "+" : "") + scoreModel.keyFifths
                onPicked: name => scoreModel.keyFifths = parseInt(name)
            }
        }
        // The part's lyrics as one line, typed the way they are sung:
        // "A-ma-zing grace", one syllable per note in time order, "_" to
        // hold a syllable over another note. Any script, Hebrew included.
        RowLayout {
            Layout.fillWidth: true; spacing: 6
            Label { text: "LYRICS"; color: Theme.muted; font.pixelSize: 9; font.letterSpacing: 1 }
            TextField {
                id: lyricsField
                objectName: "lyricsField"
                Layout.fillWidth: true
                font.pixelSize: 12
                placeholderText: "A-ma-zing grace how sweet the sound"
                // The part being typed for: the one open when typing began,
                // even if playback or an agent opens another meanwhile.
                property int part: -1
                onActiveFocusChanged: if (activeFocus) part = songModel.currentPattern
                function commit() {
                    if (text !== patternModel.lyricsTextOf(part)) patternModel.setLyrics(text, part);
                }
                onAccepted: commit()
                onEditingFinished: commit()
            }
            // The open part's lyrics, except while a line is being typed.
            Binding {
                target: lyricsField; property: "text"
                value: patternModel.lyricsText
                when: !lyricsField.activeFocus
            }
        }

        // What a click on the staff writes, and edits of the selected note.
        RowLayout {
            Layout.fillWidth: true; spacing: 4
            Label { text: "NOTE"; color: Theme.muted; font.pixelSize: 9; font.letterSpacing: 1 }
            Repeater {
                model: [{ value: 1, label: "1" }, { value: 2, label: "1/2" }, { value: 4, label: "1/4" },
                        { value: 8, label: "1/8" }, { value: 16, label: "1/16" }]
                Chip {
                    required property var modelData
                    objectName: "noteValue" + modelData.value
                    text: modelData.label; implicitHeight: 20
                    accessibleLabel: "Enter " + modelData.label + " notes"
                    on: scoreModel.noteValue === modelData.value
                    onClicked: scoreModel.noteValue = modelData.value
                }
            }
            Chip {
                objectName: "noteDotted"; text: "DOT"; implicitHeight: 20
                accessibleLabel: "Dotted notes"
                on: scoreModel.dotted
                onClicked: scoreModel.dotted = !scoreModel.dotted
            }
            Item { Layout.preferredWidth: 8 }
            Chip {
                objectName: "noteSharpen"; text: "\u266F"; implicitHeight: 20
                accessibleLabel: "Raise the selected note a semitone"
                enabled: scoreModel.selectedStep >= 0
                onClicked: { scoreModel.alterSelected(1); staffInput.forceActiveFocus(); }
            }
            Chip {
                objectName: "noteFlatten"; text: "\u266D"; implicitHeight: 20
                accessibleLabel: "Lower the selected note a semitone"
                enabled: scoreModel.selectedStep >= 0
                onClicked: { scoreModel.alterSelected(-1); staffInput.forceActiveFocus(); }
            }
            Chip {
                objectName: "noteDelete"; text: "DEL"; implicitHeight: 20
                accessibleLabel: "Delete the selected note"
                enabled: scoreModel.selectedStep >= 0
                onClicked: { scoreModel.deleteSelected(); staffInput.forceActiveFocus(); }
            }
            Item { Layout.fillWidth: true }
            Label {
                text: "click the staff to write \u00B7 \u2191\u2193 move \u00B7 shift octave \u00B7 del"
                color: Theme.muted; font.pixelSize: 9; elide: Text.ElideRight
                Layout.fillWidth: true; horizontalAlignment: Text.AlignRight
            }
        }

        // The staff scrolls sideways: a long part is drawn at its full
        // length rather than squeezed or cut off at the panel's edge. The
        // full score stacks a staff per part and scrolls down too.
        Flickable {
            id: scroller
            objectName: "scoreScroller"
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: scoreModel.fullScore ? Flickable.HorizontalAndVerticalFlick
                                                     : Flickable.HorizontalFlick
            contentWidth: scoreCanvas.contentWidthPx
            contentHeight: Math.max(height, scoreCanvas.totalHeight)
            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            Canvas {
                id: scoreCanvas
                objectName: "scoreCanvas"
                readonly property var staves: scoreModel.staves
                // A staff's reach in staff positions, with room around it.
                function reachOf(staff) {
                    return { high: Math.max(staff.highestPos, 8) + 4, low: Math.min(staff.lowestPos, 0) - 4 };
                }
                // One staff space: a part's staff fills the height; a full
                // score's staves keep a readable size and the view scrolls.
                readonly property real sp: {
                    if (scoreModel.fullScore) return 8;
                    var only = staves.length > 0 ? reachOf(staves[0]) : { high: 12, low: -4 };
                    return Math.max(6, Math.min(16, scroller.height / Math.max((only.high - only.low) / 2, 1)));
                }
                // Each staff's top, bottom line and height, top to bottom; a
                // full score names each part above its staff.
                readonly property real labelRoom: scoreModel.fullScore ? 14 : 0
                readonly property var geometry: {
                    var placed = [];
                    var top = 0;
                    for (var i = 0; i < staves.length; i++) {
                        var reach = reachOf(staves[i]);
                        var height = labelRoom + (reach.high - reach.low) * sp / 2;
                        placed.push({ top: top, height: height, yBottom: top + labelRoom + reach.high * sp / 2 });
                        top += height;
                    }
                    return placed;
                }
                readonly property real totalHeight: geometry.length > 0
                    ? geometry[geometry.length - 1].top + geometry[geometry.length - 1].height : 0
                readonly property int selectedIndex: {
                    for (var i = 0; i < staves.length; i++) if (staves[i].selected) return i;
                    return 0;
                }
                // The written-on staff's bottom line, for note entry.
                readonly property real yBottom: geometry.length > selectedIndex ? geometry[selectedIndex].yBottom : 0
                // Clef, key and time signature, then the notes; the widest
                // key signature of the staves sets where every one begins.
                readonly property int keyRoom: {
                    var widest = 0;
                    for (var i = 0; i < staves.length; i++) widest = Math.max(widest, staves[i].keySignature.length);
                    return widest;
                }
                readonly property real headerWidth: (1 + 3.4 + keyRoom * 1.35
                                                     + (scoreModel.unmetered ? 0.6 : 3.2)) * sp
                // The whole part's width. The canvas itself is only the
                // visible window onto it, following the scroll: a long part
                // drawn whole was wider than a GPU texture can be.
                readonly property real contentWidthPx: Math.max(scroller.width,
                                                                headerWidth + (scoreModel.contentWidth + 1.5) * sp)
                x: Math.max(0, Math.min(scroller.contentX, contentWidthPx - scroller.width))
                width: scroller.width
                height: Math.max(scroller.height, totalHeight)

                onPaint: {
                    var ctx = getContext("2d");
                    ctx.clearRect(0, 0, width, height);
                    ctx.save();
                    ctx.translate(-x, 0);
                    try {
                        for (var i = 0; i < staves.length && i < geometry.length; i++)
                            paintStaff(ctx, staves[i], geometry[i], x, x + width);
                    } finally { ctx.restore(); }
                }
                // Draws one staff in the part's coordinates; what lies wholly
                // outside left..right is skipped.
                function paintStaff(ctx, staff, place, left, right) {
                    var W = contentWidthPx;
                    var sp = scoreCanvas.sp;
                    var ink = Theme.ink;
                    var lineW = Math.max(1, sp * 0.09);
                    var yBottom = place.yBottom;
                    // Staff position p (0 = bottom line, 2 = next line, ...) to y.
                    function yPos(p) { return yBottom - p * sp / 2; }
                    function hline(x, y, w) { ctx.fillRect(x, y - lineW / 2, w, lineW); }

                    var ready = root.fontOk;
                    if (!ready && !root.warnedNoFont) {
                        root.warnedNoFont = true;
                        console.warn("ScoreView: Bravura not loaded; fallback shapes");
                    }
                    var musicFont = (4 * sp) + 'px "' + bravura.name + '"';
                    if (ready) ctx.font = musicFont;
                    ctx.fillStyle = ink;
                    ctx.strokeStyle = ink;
                    ctx.textBaseline = "alphabetic";
                    ctx.textAlign = "left";
                    function code(g) { return String.fromCodePoint(g.cp); }
                    // Glyph drawn with its visual center at (x, yCenter).
                    function drawGlyph(g, x, yCenter) { ctx.fillText(code(g), x, yCenter + g.cy * sp); }
                    function text(t, x, y, size, align) {
                        var keepAlign = ctx.textAlign;
                        // Hebrew is drawn in the bundled Hebrew face, so it
                        // reads the same on every machine.
                        var face = /[\u0590-\u05FF\uFB1D-\uFB4F]/.test(t) ? '"Noto Sans Hebrew"' : "sans-serif";
                        ctx.textAlign = align; ctx.font = size + "px " + face;
                        ctx.fillText(t, x, y);
                        // The music font is set again by name: a font read
                        // back from the context does not reselect it.
                        ctx.textAlign = keepAlign;
                        ctx.font = ready ? musicFont : "12px sans-serif";
                    }

                    // ---- the part's name, over a full score's staff; the
                    // staff notes are written on in the accent colour
                    if (scoreModel.fullScore && staff.name.length > 0) {
                        ctx.fillStyle = staff.selected ? Theme.acid : Theme.muted;
                        text(staff.name.toUpperCase(), left + 0.6 * sp, place.top + 11, 10, "left");
                        ctx.fillStyle = ink;
                    }

                    // ---- staff lines
                    for (var l = 0; l < 5; l++) hline(0, yPos(2 * l), W);

                    // ---- clef (glyph origin sits on the reference line)
                    var clefX = 1.0 * sp;
                    if (ready) ctx.fillText(code(Smufl.clefGlyph(staff.clefKind)), clefX, yPos(staff.clefRefPos));
                    else hline(clefX, yPos(staff.clefRefPos), 2.4 * sp);

                    // ---- key signature
                    var ks = staff.keySignature;
                    var ksX = clefX + 3.4 * sp;
                    for (var k = 0; k < ks.length; k++) {
                        var ax = ksX + k * 1.35 * sp;
                        if (ready) drawGlyph(Smufl.glyph(ks[k].acc > 0 ? "accidentalSharp" : "accidentalFlat"), ax, yPos(ks[k].pos));
                        else hline(ax, yPos(ks[k].pos), 0.9 * sp);
                    }

                    // ---- time signature
                    var tsX = ksX + scoreCanvas.keyRoom * 1.35 * sp + 0.6 * sp;
                    if (!scoreModel.unmetered) {
                        if (ready) {
                            var top = Smufl.digits(scoreModel.timeBeats);
                            var bot = Smufl.digits(scoreModel.timeBeatUnit);
                            var cx = tsX + 0.9 * sp;
                            for (var t = 0; t < top.length; t++)
                                drawGlyph(top[t], cx - top.length * 0.875 * sp + t * 1.75 * sp, yPos(6));
                            for (var b = 0; b < bot.length; b++)
                                drawGlyph(bot[b], cx - bot.length * 0.875 * sp + b * 1.75 * sp, yPos(2));
                        } else {
                            text(scoreModel.timeBeats + "/" + scoreModel.timeBeatUnit, tsX, yPos(4), 2.4 * sp, "left");
                        }
                    }

                    var x0 = scoreCanvas.headerWidth;
                    function xAt(units) { return x0 + units * sp; }
                    // ---- barlines (the last one double)
                    var bars = scoreModel.barlines;
                    for (var bl = 0; bl < bars.length; bl++) {
                        var bx = xAt(bars[bl].x);
                        ctx.fillRect(bx, yPos(8), Math.max(1, 0.12 * sp), yPos(0) - yPos(8));
                        if (bars[bl].final)
                            ctx.fillRect(bx + 0.35 * sp, yPos(8), 0.42 * sp, yPos(0) - yPos(8));
                    }

                    function accGlyph(a) {
                        return Smufl.glyph(a === 2 ? "accidentalDoubleSharp" : a === 1 ? "accidentalSharp"
                                         : a === 0 ? "accidentalNatural" : a === -1 ? "accidentalFlat"
                                         : "accidentalDoubleFlat");
                    }

                    var objects = staff.objects;
                    var nhw = 1.18 * sp; // noteheadBlack advance
                    for (var i = 0; i < objects.length; i++) {
                        var o = objects[i];
                        var x = xAt(o.x);
                        // Off screen, unless a tie, hyphen or tuplet from it reaches in.
                        if (x > right + 2 * sp) continue;
                        var reach = Math.max(o.x, o.tieToX || 0, o.hyphenToX || 0, o.tupletEndX || 0);
                        if (xAt(reach) + 3 * sp < left) continue;
                        if (o.kind === "rest") {
                            var r = Smufl.rest(o.value);
                            if (ready) {
                                if (o.value <= 0) ctx.fillText(code(r), x, yPos(o.pos));
                                else if (o.value === 1) ctx.fillText(code(r), x, yPos(o.pos));
                                else drawGlyph(r, x, yPos(o.pos));
                            } else {
                                ctx.fillRect(x, yPos(o.pos) - sp / 2, 1.1 * sp, sp / 2);
                            }
                            for (var rd = 0; rd < o.dots; rd++) {
                                if (ready) drawGlyph(Smufl.glyph("augmentationDot"), x + 1.6 * sp + rd * 0.6 * sp, yPos(o.pos + 1));
                                else ctx.fillRect(x + 1.6 * sp + rd * 0.6 * sp, yPos(o.pos + 1) - 1, 2, 2);
                            }
                            continue;
                        }
                        var y = yPos(o.pos);
                        // ledger lines
                        for (var lg = 1; lg <= o.ledger; lg++) {
                            var lp = o.pos > 8 ? 8 + 2 * lg : -2 * lg;
                            ctx.fillRect(x - 0.4 * sp, yPos(lp) - lineW / 2, nhw + 0.8 * sp, lineW);
                        }
                        // accidental, natural included
                        if (o.showAcc) {
                            if (ready) drawGlyph(accGlyph(o.acc), x - 1.35 * sp, y);
                            else hline(x - 1.35 * sp, y, 0.8 * sp);
                        }
                        // notehead, the selected one in the accent colour
                        if (o.selected) ctx.fillStyle = Theme.acid;
                        if (ready) {
                            drawGlyph(Smufl.notehead(o.value), x, y);
                        } else {
                            ctx.beginPath();
                            ctx.ellipse(x + nhw / 2, y, nhw / 2, sp * 0.36, -0.3, 0, 2 * Math.PI);
                            ctx.fill();
                        }
                        if (o.selected) ctx.fillStyle = ink;
                        // the chord's one stem, and its flag
                        if (o.stem) {
                            var stemW = 0.13 * sp;
                            var stemX = o.stemUp ? x + nhw - 0.14 * sp : x + 0.02 * sp;
                            var yFrom = yPos(o.stemFrom), yTo = yPos(o.stemTo);
                            ctx.fillRect(stemX, Math.min(yFrom, yTo), stemW, Math.abs(yTo - yFrom));
                            if (ready && o.value >= 3 && !o.beamed) {
                                var fl = Smufl.flag(o.value, o.stemUp);
                                if (fl) ctx.fillText(code(fl), stemX, yTo);
                            }
                        }
                        // augmentation dots, in the space above a line note
                        if (o.dots > 0) {
                            var dy = y - (o.pos % 2 === 0 ? sp * 0.5 : 0);
                            for (var d = 0; d < o.dots; d++) {
                                if (ready) drawGlyph(Smufl.glyph("augmentationDot"), x + nhw + 0.4 * sp + d * 0.6 * sp, dy);
                                else ctx.fillRect(x + nhw + 0.4 * sp + d * 0.6 * sp, dy - 1, 2, 2);
                            }
                        }
                        // the tie, to the note it continues into
                        if (o.tieStart && o.tieToX >= 0) {
                            var x2 = xAt(o.tieToX);
                            var side = o.stemUp ? 1 : -1;
                            ctx.lineWidth = Math.max(1, 0.12 * sp);
                            ctx.beginPath();
                            ctx.moveTo(x + nhw * 0.8, y + side * 0.6 * sp);
                            ctx.quadraticCurveTo((x + x2 + nhw) / 2, y + side * 1.6 * sp, x2 + nhw * 0.2, y + side * 0.6 * sp);
                            ctx.stroke();
                        }
                        // a tuplet's bracket over the group it opens, its
                        // number in a gap at the middle: flags have no beam
                        // to say which notes the number counts
                        if (o.tuplet > 0) {
                            var bx0 = x, bx1 = xAt(o.tupletEndX) + nhw;
                            var tx = (bx0 + bx1) / 2, by = yPos(o.tupletPos);
                            var gap = 0.9 * sp, hook = 0.6 * sp;
                            ctx.fillRect(bx0, by, lineW, hook);
                            ctx.fillRect(bx1 - lineW, by, lineW, hook);
                            if (tx - gap > bx0) ctx.fillRect(bx0, by, tx - gap - bx0, lineW);
                            if (bx1 > tx + gap) ctx.fillRect(tx + gap, by, bx1 - tx - gap, lineW);
                            text(String(o.tuplet), tx, by + 0.45 * sp, Math.max(9, 1.3 * sp), "center");
                        }
                        // lyric syllable, the hyphen to the next syllable of its
                        // word (between the two, whichever way the script
                        // runs), or the line holding the last one
                        if (o.lyric.length > 0)
                            text(o.lyric, x + nhw / 2, yPos(-6), Math.max(9, sp * 0.95), "center");
                        if (o.hyphen && o.hyphenToX >= 0)
                            ctx.fillRect((x + xAt(o.hyphenToX)) / 2 + nhw / 2 - 0.35 * sp, yPos(-6) - 0.32 * sp, 0.7 * sp, lineW);
                        else if (o.extend)
                            ctx.fillRect(x - 0.4 * sp, yPos(-6.4), nhw + 0.8 * sp, lineW);
                    }

                    // ---- beams, from stem to stem; a hook is a short beam from one
                    var beams = staff.beams;
                    var beamH = 0.5 * sp, beamGap = 0.75 * sp, stemW = 0.13 * sp;
                    for (var bi = 0; bi < beams.length; bi++) {
                        var bm = beams[bi];
                        var stemOf = function(units) {
                            return bm.up ? xAt(units) + nhw - 0.14 * sp : xAt(units) + 0.02 * sp;
                        };
                        var bx0 = stemOf(bm.fromX), bx1 = stemOf(bm.toX) + stemW;
                        if (bm.hook !== 0) {
                            if (bm.hook > 0) bx1 = bx0 + 1.1 * sp;
                            else { bx1 = bx0 + stemW; bx0 = bx1 - 1.1 * sp; }
                        }
                        if (bx1 < left - sp || bx0 > right + sp) continue;
                        var by = bm.up ? yPos(bm.pos) + bm.level * beamGap
                                       : yPos(bm.pos) - beamH - bm.level * beamGap;
                        ctx.fillRect(bx0, by, bx1 - bx0, beamH);
                    }
                }

                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onXChanged: requestPaint()
            }

            // A full score's other staves: a press on one makes its track the
            // one written on.
            Repeater {
                model: scoreModel.fullScore ? scoreCanvas.staves.length : 0
                MouseArea {
                    required property int index
                    readonly property var staff: scoreCanvas.staves[index]
                    objectName: "scoreStaff" + index
                    visible: staff !== undefined && !staff.selected
                    y: scoreCanvas.geometry[index] ? scoreCanvas.geometry[index].top : 0
                    width: scoreCanvas.contentWidthPx
                    height: scoreCanvas.geometry[index] ? scoreCanvas.geometry[index].height : 0
                    cursorShape: Qt.PointingHandCursor
                    onClicked: songModel.selectTrack(staff.track)
                }
            }

            // Note entry, over the staff written on: x in staff spaces from
            // the first column (to the notehead's left edge, as objects' x), y
            // in staff positions.
            MouseArea {
                id: staffInput
                objectName: "staffInput"
                y: scoreCanvas.geometry[scoreCanvas.selectedIndex] ? scoreCanvas.geometry[scoreCanvas.selectedIndex].top : 0
                width: scoreCanvas.contentWidthPx
                height: scoreModel.fullScore && scoreCanvas.geometry[scoreCanvas.selectedIndex]
                        ? scoreCanvas.geometry[scoreCanvas.selectedIndex].height : scroller.height
                activeFocusOnTab: true
                cursorShape: Qt.CrossCursor
                function unitsAt(px) { return (px - scoreCanvas.headerWidth) / scoreCanvas.sp - 0.59; }
                function posAt(py) { return (scoreCanvas.yBottom - y - py) / (scoreCanvas.sp / 2); }
                onPressed: mouse => {
                    forceActiveFocus();
                    scoreModel.click(unitsAt(mouse.x), posAt(mouse.y));
                }
                // The window's step-cursor shortcuts stand aside while
                // the staff has the keyboard (Main.qml's staffEditing).
                function handles(event) {
                    return scoreModel.selectedStep >= 0
                        && (event.key === Qt.Key_Up || event.key === Qt.Key_Down
                            || event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace
                            || event.key === Qt.Key_Escape);
                }
                Keys.onPressed: event => {
                    if (!handles(event)) return;
                    const octave = (event.modifiers & Qt.ShiftModifier) ? 7 : 1;
                    if (event.key === Qt.Key_Up) scoreModel.moveSelected(octave);
                    else if (event.key === Qt.Key_Down) scoreModel.moveSelected(-octave);
                    else if (event.key === Qt.Key_Escape) scoreModel.deselect();
                    else scoreModel.deleteSelected();
                    event.accepted = true;
                }
            }
        }
    }

    Connections {
        target: scoreModel
        function onScoreChanged() { scoreCanvas.requestPaint(); }
    }
    Connections {
        target: bravura
        function onStatusChanged() { scoreCanvas.requestPaint(); }
    }
}
