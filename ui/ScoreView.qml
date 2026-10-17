import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Smufl.js" as Smufl

// The score: a fourth projection of the canonical pattern, drawn as true
// staff notation with the Bravura SMuFL font. It draws what scoreModel lays
// out (the open part under the song's clef, key and meter) and never writes
// notes back; the clef, key and CHANT controls set the song's notation for
// the part, as one step of undo. Spacing follows the rhythm; it is readable,
// not Gould-perfect.
Rectangle {
    id: root
    objectName: "scoreView"
    color: Theme.panel; radius: 6; border.color: Theme.line; clip: true

    property var keyChoices: ["-7", "-6", "-5", "-4", "-3", "-2", "-1", "0",
                              "+1", "+2", "+3", "+4", "+5", "+6", "+7"]
    property bool warnedNoFont: false

    FontLoader { id: bravura; source: "qrc:/fonts/Bravura.otf" }
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
        // The staff scrolls sideways: a long part is drawn at its full
        // length rather than squeezed or cut off at the panel's edge.
        Flickable {
            id: scroller
            objectName: "scoreScroller"
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.HorizontalFlick
            contentWidth: scoreCanvas.width; contentHeight: height
            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

            Canvas {
                id: scoreCanvas
                objectName: "scoreCanvas"
                height: scroller.height
                // One staff space, sized so the highest and lowest things on
                // the staff (ledger notes, stems, lyrics) fit the height.
                readonly property real span: (Math.max(scoreModel.highestPos, 8) + 4
                                              - Math.min(scoreModel.lowestPos, 0) + 4) / 2
                readonly property real sp: Math.max(6, Math.min(16, height / Math.max(span, 1)))
                // The bottom line's y, leaving room above for what sits high.
                readonly property real yBottom: (Math.max(scoreModel.highestPos, 8) + 4) * sp / 2
                // Clef, key and time signature, then the notes.
                readonly property real headerWidth: (1 + 3.4 + scoreModel.keySignature.length * 1.35
                                                     + (scoreModel.unmetered ? 0.6 : 3.2)) * sp
                width: Math.max(scroller.width, headerWidth + (scoreModel.contentWidth + 1.5) * sp)

                onPaint: {
                    var ctx = getContext("2d");
                    var W = width, H = height;
                    ctx.clearRect(0, 0, W, H);
                    var sp = scoreCanvas.sp;
                    var ink = Theme.ink;
                    var lineW = Math.max(1, sp * 0.09);
                    var yBottom = scoreCanvas.yBottom;
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
                        var keepAlign = ctx.textAlign, keepFont = ctx.font;
                        ctx.textAlign = align; ctx.font = size + "px sans-serif";
                        ctx.fillText(t, x, y);
                        ctx.textAlign = keepAlign; ctx.font = keepFont;
                    }

                    // ---- staff lines
                    for (var l = 0; l < 5; l++) hline(0, yPos(2 * l), W);

                    // ---- clef (glyph origin sits on the reference line)
                    var clefX = 1.0 * sp;
                    if (ready) ctx.fillText(code(Smufl.clefGlyph(scoreModel.clefKind)), clefX, yPos(scoreModel.clefRefPos));
                    else hline(clefX, yPos(scoreModel.clefRefPos), 2.4 * sp);

                    // ---- key signature
                    var ks = scoreModel.keySignature;
                    var ksX = clefX + 3.4 * sp;
                    for (var k = 0; k < ks.length; k++) {
                        var ax = ksX + k * 1.35 * sp;
                        if (ready) drawGlyph(Smufl.glyph(ks[k].acc > 0 ? "accidentalSharp" : "accidentalFlat"), ax, yPos(ks[k].pos));
                        else hline(ax, yPos(ks[k].pos), 0.9 * sp);
                    }

                    // ---- time signature
                    var tsX = ksX + ks.length * 1.35 * sp + 0.6 * sp;
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

                    var objects = scoreModel.objects;
                    var nhw = 1.18 * sp; // noteheadBlack advance
                    for (var i = 0; i < objects.length; i++) {
                        var o = objects[i];
                        var x = xAt(o.x);
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
                        // notehead
                        if (ready) {
                            drawGlyph(Smufl.notehead(o.value), x, y);
                        } else {
                            ctx.beginPath();
                            ctx.ellipse(x + nhw / 2, y, nhw / 2, sp * 0.36, -0.3, 0, 2 * Math.PI);
                            ctx.fill();
                        }
                        // the chord's one stem, and its flag
                        if (o.stem) {
                            var stemW = 0.13 * sp;
                            var stemX = o.stemUp ? x + nhw - 0.14 * sp : x + 0.02 * sp;
                            var yFrom = yPos(o.stemFrom), yTo = yPos(o.stemTo);
                            ctx.fillRect(stemX, Math.min(yFrom, yTo), stemW, Math.abs(yTo - yFrom));
                            if (ready && o.value >= 3) {
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
                        // a tuplet's number over the group it opens
                        if (o.tuplet > 0) {
                            var tx = (x + xAt(o.tupletEndX) + nhw) / 2;
                            text(String(o.tuplet), tx, yPos(Math.max(o.stemTo, o.pos) + 3), Math.max(9, 1.3 * sp), "center");
                        }
                        // lyric syllable, or the line holding the last one
                        if (o.lyric.length > 0)
                            text(o.lyric, x + nhw / 2, yPos(-6), Math.max(9, sp * 0.95), "center");
                        else if (o.extend)
                            ctx.fillRect(x - 0.4 * sp, yPos(-6.4), nhw + 0.8 * sp, lineW);
                    }
                }

                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
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
