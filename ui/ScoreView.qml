import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Smufl.js" as Smufl

// The score: a fourth projection of the canonical pattern, drawn as true
// staff notation with the Bravura SMuFL font. Read-only for v1 — it observes
// scoreModel (which re-spells the pattern on every change) and never writes
// back into the pattern. Spacing is readable, not Gould-perfect.
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
        Canvas {
            id: scoreCanvas
            objectName: "scoreCanvas"
            Layout.fillWidth: true; Layout.fillHeight: true

            onPaint: {
                var ctx = getContext("2d");
                var W = width, H = height;
                ctx.clearRect(0, 0, W, H);
                // One staff space in px; the system needs ~13 (staff + clef
                // tops + ledger room + lyrics).
                var sp = Math.max(7, Math.min(16, H / 13));
                var ink = Theme.ink;
                var lineW = Math.max(1, sp * 0.09);
                // Staff position p (0 = bottom line, 2 = next line, ...) to y.
                // The system sits near the top with room for lyrics below; in
                // a tall view it drops to the upper third instead of hugging
                // the header.
                var yBottom = Math.max(4.8 * sp, H * 0.30);
                function yPos(p) { return yBottom - p * sp / 2; }
                function hline(x, y, w) { ctx.fillRect(x, y - lineW / 2, w, lineW); }

                var ready = root.fontOk;
                if (!ready && !root.warnedNoFont) {
                    root.warnedNoFont = true;
                    console.warn("ScoreView: Bravura not loaded; fallback shapes");
                }
                if (ready)
                    ctx.font = (4 * sp) + 'px "' + bravura.name + '"';
                ctx.fillStyle = ink;
                ctx.textBaseline = "alphabetic";
                ctx.textAlign = "left";
                function code(g) { return String.fromCodePoint(g.cp); }
                // Glyph drawn with its visual center at (x, yCenter).
                function drawGlyph(g, x, yCenter) {
                    ctx.fillText(code(g), x, yCenter + g.cy * sp);
                }

                var objects = scoreModel.objects;
                var n = objects.length;
                var unmetered = scoreModel.unmetered;

                // ---- staff lines
                for (var l = 0; l < 5; l++)
                    hline(0, yPos(2 * l), W);

                // ---- clef (glyph origin sits on the reference line)
                var clefX = 1.0 * sp;
                if (ready)
                    ctx.fillText(code(Smufl.clefGlyph(scoreModel.clefKind)),
                                 clefX, yPos(scoreModel.clefRefPos));
                else
                    hline(clefX, yPos(scoreModel.clefRefPos), 2.4 * sp);

                // ---- key signature
                var ks = scoreModel.keySignature;
                var ksX = clefX + 3.4 * sp;
                for (var k = 0; k < ks.length; k++) {
                    var ax = ksX + k * 1.35 * sp;
                    if (ready)
                        drawGlyph(ks[k].acc > 0 ? Smufl.glyph("accidentalSharp")
                                                : Smufl.glyph("accidentalFlat"),
                                  ax, yPos(ks[k].pos));
                    else
                        hline(ax, yPos(ks[k].pos), 0.9 * sp);
                }

                // ---- time signature
                var tsX = ksX + ks.length * 1.35 * sp + 0.6 * sp;
                if (!unmetered) {
                    if (ready) {
                        var top = Smufl.digits(scoreModel.timeBeats);
                        var bot = Smufl.digits(scoreModel.timeBeatUnit);
                        var cx = tsX + 0.9 * sp;
                        for (var t = 0; t < top.length; t++)
                            drawGlyph(top[t], cx - top.length * 0.875 * sp + t * 1.75 * sp,
                                      yPos(6));
                        for (var b = 0; b < bot.length; b++)
                            drawGlyph(bot[b], cx - bot.length * 0.875 * sp + b * 1.75 * sp,
                                      yPos(2));
                    } else {
                        ctx.font = (2.4 * sp) + "px sans-serif";
                        ctx.fillText(scoreModel.timeBeats + "/" + scoreModel.timeBeatUnit,
                                     tsX, yPos(4));
                        if (ready)
                            ctx.font = (4 * sp) + 'px "' + bravura.name + '"';
                    }
                }

                // ---- notes and rests, evenly spaced (v1: readable spacing)
                var notesX0 = tsX + (unmetered ? 0.6 : 2.6) * sp;
                var gap = Math.max(3.8 * sp, (W - notesX0 - 2.5 * sp) / Math.max(n, 1));
                function noteX(i) { return notesX0 + i * gap; }
                function accGlyph(a) {
                    return a === 2 ? Smufl.glyph("accidentalDoubleSharp")
                         : a === 1 ? Smufl.glyph("accidentalSharp")
                         : a === -1 ? Smufl.glyph("accidentalFlat")
                         : Smufl.glyph("accidentalDoubleFlat");
                }

                for (var i = 0; i < n; i++) {
                    var o = objects[i];
                    var x = noteX(i);
                    if (o.kind === "rest") {
                        var r = Smufl.rest(o.value);
                        if (ready) {
                            if (o.value === 0)      // whole: hangs from line 4
                                ctx.fillText(code(r), x, yPos(6));
                            else if (o.value === 1) // half: sits on line 3
                                ctx.fillText(code(r), x, yPos(4));
                            else
                                drawGlyph(r, x, yPos(4));
                        } else {
                            ctx.fillRect(x, yPos(4) - sp / 2, 1.1 * sp, sp / 2);
                        }
                        continue;
                    }
                    var y = yPos(o.pos);
                    // ledger lines
                    for (var lg = 1; lg <= o.ledger; lg++) {
                        var lp = o.pos > 8 ? 8 + 2 * lg : -2 * lg;
                        ctx.fillRect(x - 1.1 * sp, yPos(lp) - lineW / 2, 2.2 * sp, lineW);
                    }
                    // accidental
                    if (o.showAcc) {
                        if (ready)
                            drawGlyph(accGlyph(o.acc), x - 1.75 * sp, y);
                        else
                            hline(x - 1.75 * sp, y, 0.8 * sp);
                    }
                    // notehead
                    var nhw = 1.18 * sp; // noteheadBlack advance
                    if (ready)
                        drawGlyph(Smufl.notehead(o.value), x, y);
                    else {
                        ctx.beginPath();
                        ctx.ellipse(x + nhw / 2, y, nhw / 2, sp * 0.36, -0.3, 0, 2 * Math.PI);
                        ctx.fill();
                    }
                    // stem + flag
                    var stemW = 0.13 * sp, stemLen = 3.5 * sp;
                    var stemX = o.stemUp ? x + nhw - 0.14 * sp : x + 0.02 * sp;
                    var tipY = o.stemUp ? y - stemLen : y + stemLen;
                    if (!o.stemless && o.value >= 1 && o.value <= 7) {
                        if (o.stemUp)
                            ctx.fillRect(stemX, y - stemLen, stemW, stemLen + 0.15 * sp);
                        else
                            ctx.fillRect(stemX, y - 0.15 * sp, stemW, stemLen + 0.15 * sp);
                        if (ready && o.value >= 3) {
                            var fl = Smufl.flag(o.value, o.stemUp);
                            if (fl) ctx.fillText(code(fl), stemX, tipY);
                        }
                    }
                    // augmentation dots
                    if (o.dots > 0) {
                        var dy = y - (o.pos % 2 === 0 ? sp * 0.5 : 0);
                        for (var d = 0; d < o.dots; d++) {
                            if (ready)
                                drawGlyph(Smufl.glyph("augmentationDot"),
                                          x + 1.95 * sp + d * 0.75 * sp, dy);
                            else
                                ctx.fillRect(x + 1.95 * sp + d * 0.75 * sp, dy - 1, 2, 2);
                        }
                    }
                    // tie into the next note
                    if (o.tieStart && i + 1 < n && objects[i + 1].kind === "note") {
                        var x2 = noteX(i + 1), y2 = yPos(objects[i + 1].pos);
                        var side = o.stemUp ? 1 : -1;
                        var belly = side === 1 ? Math.max(y, y2) + 1.9 * sp
                                               : Math.min(y, y2) - 1.9 * sp;
                        ctx.lineWidth = Math.max(1, 0.12 * sp);
                        ctx.beginPath();
                        ctx.moveTo(x + 1.2 * sp, y + side * 0.9 * sp);
                        ctx.quadraticCurveTo((x + x2) / 2 + 0.6 * sp, belly,
                                             x2 - 0.1 * sp, y2 + side * 0.9 * sp);
                        ctx.stroke();
                    }
                    // lyric
                    if (o.lyric && o.lyric.length > 0) {
                        var keepAlign = ctx.textAlign, keepFont = ctx.font;
                        ctx.textAlign = "center";
                        ctx.font = Math.max(9, sp * 0.95) + "px sans-serif";
                        ctx.fillText(o.lyric, x + nhw / 2, yBottom + 2.7 * sp);
                        ctx.textAlign = keepAlign; ctx.font = keepFont;
                    }
                }

                // ---- final barline
                if (n > 0) {
                    var bx = noteX(n - 1) + 2.4 * sp;
                    ctx.fillRect(bx, yPos(8), Math.max(1, 0.1 * sp), yPos(0) - yPos(8));
                    ctx.fillRect(bx + 0.35 * sp, yPos(8), 0.42 * sp, yPos(0) - yPos(8));
                }
            }

            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
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
