// features/musicxml_window.feature, run on its own by the bdd_score_file gate
// (`--verify --scenario score-file`). A hymn of our own - Shalom Aleichem's
// words, pointed Hebrew, under a tune in F major 3/4 for voice and organ
// (tests/fixtures/shalom-aleichem.musicxml, named by BLOKKILY_SCORE_FIXTURE)
// - comes in through the SCORE IN button and its file dialog, is checked in
// the song, heard from the production render callback, read under the
// staff, given new lyrics through the LYRICS field, and goes out again
// through SCORE OUT, where the written file is read back. Each button is
// shown to open its file dialog; the file is then handed to the method the
// dialog's accept calls, as a file URL, the way the dialog hands it.

#include "verify/harness.hpp"

#include "blokkily/notation/lyrics.hpp"
#include "blokkily/notation/musicxml.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <iostream>
#include <source_location>

namespace blokkily::verify {
namespace {

QObject* context_object(VerifyContext& ctx, const char* name) {
    auto* engine = qmlEngine(ctx.window);
    return engine == nullptr ? nullptr : engine->rootContext()->contextProperty(name).value<QObject*>();
}

void run_score_file(VerifyContext& ctx) {
    auto* const window = ctx.window;
    auto& song = ctx.song;
    auto& controller = ctx.controller;
    const auto check = [&ctx](bool ok, std::source_location at = std::source_location::current()) {
        if (!ok) std::cerr << "score-file check failed at line " << at.line() << '\n';
        ctx.check(ok);
    };
    const auto lay_out = [window] {
        QCoreApplication::processEvents();
        (void)window->grabWindow();
    };
    const auto settle = [&lay_out] {
        VerifyContext::settle(300);
        lay_out();
    };
    const auto press = [&](const char* name) {
        auto* item = ctx.named(QString::fromLatin1(name));
        if (item == nullptr || !item->isVisible()) return false;
        ctx.click_at(item, {item->width() / 2, item->height() / 2}, Qt::LeftButton);
        return true;
    };
    // The button opened its file dialog; it is closed again, and the file
    // is given to the method its accept calls - an offscreen dialog cannot
    // be made to choose a file.
    const auto opened = [&](const char* dialog_name) {
        auto* dialog = window->findChild<QObject*>(QString::fromLatin1(dialog_name));
        const bool shown = dialog != nullptr && dialog->property("visible").toBool();
        if (dialog != nullptr) QMetaObject::invokeMethod(dialog, "reject");
        settle();
        return shown;
    };
    const auto lyrics_of = [&](std::size_t track) {
        QStringList sung;
        const auto part = song.song().part(static_cast<std::size_t>(song.currentSection()), track);
        if (part)
            for (const auto& trigger : song.song().patterns[*part].pattern.events())
                if (!trigger.lyric.empty()) sung << QString::fromStdString(trigger.lyric);
        return sung;
    };
    // The hymn's first syllable: shin, qamats, shin dot - the canonical order.
    const QString shin = QString::fromUtf8("\xD7\xA9\xD6\xB8\xD7\x81-");

    // A song with nothing in it, playing the CLAP fixture.
    check(controller.verifyClap(ctx.parser.value("clap-fixture")));
    const auto clap = song.song().tracks.at(0).instrument;
    {
        Song fresh;
        fresh.tracks.front().name = "LEAD";
        fresh.tracks.front().instrument = clap;
        fresh.clips.clear();
        song.replace(std::move(fresh));
    }
    settle();
    const QString hymn = qEnvironmentVariable("BLOKKILY_SCORE_FIXTURE");
    check(!hymn.isEmpty() && QFileInfo::exists(hymn));

    // ---------------------------------------------------------------------
    ctx.reached("score-file: SCORE IN builds the song from a hymn");
    // The score's own buttons, in the SCORE view.
    check(window->setProperty("view", "SCORE"));
    settle();
    check(VerifyContext::usable(ctx.named("importScoreButton"), 60, 16));
    check(press("importScoreButton"));
    settle();
    check(opened("importScoreDialog"));
    check(controller.importMusicXmlFile(QUrl::fromLocalFile(hymn).toString()));
    settle();
    const auto& built = song.song();
    check(built.tracks.size() == 3 && built.tracks[0].name == "Voice" && built.tracks[1].name == "Organ 1" &&
          built.tracks[2].name == "Organ 2");
    const auto section = static_cast<std::size_t>(song.currentSection());
    check(section < built.sections.size() && built.sections[section].name == "Shalom Aleichem");
    check(built.sections[section].key_fifths == -1 && built.sections[section].meter_beats == 3);
    check(built.meter.changes.size() == 1 && built.meter.changes[0].numerator == 3 &&
          built.meter.changes[0].denominator == 4);
    check(std::abs(built.tempo.points.front().bpm - 76.0) < 1e-9 && std::abs(ctx.transport.bpm() - 76.0) < 1e-6);
    const auto part_of = [&](std::size_t track) -> const PatternSlot* {
        const auto part = built.part(section, track);
        return part ? &built.patterns[*part] : nullptr;
    };
    check(part_of(0) && part_of(0)->pattern.events().size() == 11 && part_of(0)->pattern.length() == 4 * 1440);
    check(part_of(1) && part_of(1)->pattern.events().size() == 8 && part_of(1)->clef == "Treble");
    check(part_of(2) && part_of(2)->pattern.events().size() == 4 && part_of(2)->clef == "Bass");
    check(built.tracks[1].instrument.format == "CLAP" && built.tracks[2].instrument.format == "CLAP");
    check(lyrics_of(0).size() == 11 && lyrics_of(0).front() == shin);
    check(controller.status().startsWith("Imported 23 notes on 3 tracks"));
    // It plays: the production callback hears the hymn from bar one.
    controller.seekToBar(0);
    controller.togglePlayback();
    float loudest = 0.0F;
    for (int block = 0; block < 96; ++block) loudest = std::max(loudest, ctx.pump());
    controller.togglePlayback();
    check(loudest > 0.001F);
    // One step of undo takes the whole import back, and redo brings it again.
    check(song.undo());
    settle();
    check(song.song().tracks.size() == 1);
    check(song.redo());
    settle();
    check(song.song().tracks.size() == 3);

    // ---------------------------------------------------------------------
    ctx.reached("score-file: the hymn's lyrics show under the staff, in Hebrew");
    check(window->setProperty("view", "SCORE"));
    song.selectTrack(0);
    settle();
    auto* const score = context_object(ctx, "scoreModel");
    check(score != nullptr);
    QStringList drawn;
    if (score != nullptr)
        for (const auto& item : score->property("objects").toList())
            if (!item.toMap().value("lyric").toString().isEmpty()) drawn << item.toMap().value("lyric").toString();
    // Drawn without their hyphens, which go between the syllables instead.
    QStringList unhyphenated;
    for (const QString& syllable : lyrics_of(0)) unhyphenated << (syllable.endsWith('-') ? syllable.chopped(1) : syllable);
    check(drawn == unhyphenated);
    int hyphens = 0;
    if (score != nullptr)
        for (const auto& item : score->property("objects").toList())
            hyphens += item.toMap().value("hyphen").toBool() && item.toMap().value("hyphenToX").toDouble() >
                                                                     item.toMap().value("x").toDouble();
    check(hyphens == 7); // every hyphenated syllable reaches the next one sung
    check(score != nullptr && score->property("keyLabel").toString() == "F major" &&
          score->property("timeBeats").toInt() == 3);
    auto* const field = ctx.named("lyricsField");
    check(field != nullptr && VerifyContext::usable(field, 200, 20) &&
          field->property("text").toString().startsWith(shin));
    check(ctx.save_screenshot());

    // ---------------------------------------------------------------------
    ctx.reached("score-file: typing lyrics lays them on the notes");
    const QString typed = QStringLiteral("Peace be up-on you, an-gels of the High-est, of the High-est One");
    if (field != nullptr) {
        field->setProperty("text", typed);
        QMetaObject::invokeMethod(field, "accepted");
    }
    settle();
    const QStringList english = lyrics_of(0);
    check(english.size() == 11 && english.front() == "Peace" && english[2] == "up-" && english[3] == "on");
    check(ctx.pattern.lyricsText() == "Peace be up-on you, an-gels of the High-est,");
    check(song.undo());
    settle();
    check(lyrics_of(0).front() == shin);

    // ---------------------------------------------------------------------
    ctx.reached("score-file: retyping keeps every other syllable on its note");
    // A note left unsung (a melisma a score marks with no lyric) shows as
    // "_", so changing a word moves no syllable after it.
    {
        // A copy: the pattern's own storage moves as it is edited.
        const auto events = [&] {
            std::vector<Trigger> copy;
            if (const auto part = song.song().part(static_cast<std::size_t>(song.currentSection()), 0)) {
                const auto& live = song.song().patterns[*part].pattern.events();
                copy.assign(live.begin(), live.end());
            }
            return copy;
        };
        const auto before = events();
        check(before.size() == 11);
        const int second = static_cast<int>(before[1].start / 120);
        ctx.pattern.setStepLyric(second, QString());
        settle();
        const QString line = ctx.pattern.lyricsText();
        auto syllables = blokkily::notation::split_lyrics(line.toStdString());
        check(syllables.size() == 11 && syllables[1] == "_");
        if (field != nullptr && !syllables.empty()) {
            syllables[0] = "Shin-"; // the first word's first syllable, retyped
            field->setProperty("text", QString::fromStdString(blokkily::notation::join_lyrics(syllables)));
            QMetaObject::invokeMethod(field, "accepted");
        }
        settle();
        const auto after = events();
        bool kept = after.size() == before.size() && after[0].lyric == "Shin-" && after[1].lyric == "_";
        for (std::size_t i = 2; kept && i < after.size(); ++i) kept = after[i].lyric == before[i].lyric;
        check(kept);
        check(song.undo() && song.undo());
        settle();
        check(lyrics_of(0).size() == 11 && lyrics_of(0).front() == shin);
    }
    // The line being typed belongs to the part it was typed for, even when
    // another part opens before ENTER.
    if (field != nullptr) {
        field->forceActiveFocus();
        settle();
        const QString typed_line = QStringLiteral("La la la");
        field->setProperty("text", typed_line);
        song.selectTrack(1);
        settle();
        check(field->property("text").toString() == typed_line); // not replaced under the cursor
        QMetaObject::invokeMethod(field, "accepted");
        settle();
        check(lyrics_of(0).size() == 3 && lyrics_of(0).front() == "La");
        check(lyrics_of(1).isEmpty());
        field->setFocus(false);
        window->contentItem()->forceActiveFocus();
        song.selectTrack(0);
        check(song.undo());
        settle();
        check(lyrics_of(0).front() == shin);
    }

    // ---------------------------------------------------------------------
    ctx.reached("score-file: SCORE OUT writes the song and reads it back");
    const QTemporaryDir folder;
    const QString written = folder.filePath("shalom.musicxml");
    check(VerifyContext::usable(ctx.named("exportScoreButton"), 60, 16));
    check(press("exportScoreButton"));
    settle();
    check(opened("exportScoreDialog"));
    check(controller.exportMusicXmlFile(QUrl::fromLocalFile(written).toString()));
    settle();
    check(QFileInfo::exists(written) && controller.lastScoreExportMatches());
    check(controller.exportStatus().contains("plays back the same"));
    // A tempo change the score cannot hold is said, not passed off as the same.
    check(song.setTempoPoint(1440.0, 92.0));
    check(controller.exportMusicXmlFile(QUrl::fromLocalFile(folder.filePath("changes.musicxml")).toString()));
    check(!controller.lastScoreExportMatches() && controller.exportStatus().contains("leaves out tempo changes"));
    check(song.undo());
    settle();
    QFile file(written);
    check(file.open(QIODevice::ReadOnly));
    const auto back = blokkily::notation::parse(file.readAll().toStdString());
    check(back.parts.size() == 3 && back.tempo_bpm && std::abs(*back.tempo_bpm - 76.0) < 1e-9);
    // The written score comes in again as a second hymn after the first.
    check(controller.importMusicXmlFile(written));
    settle();
    check(song.song().tracks.size() == 6 && lyrics_of(3).size() == 11 && lyrics_of(3).front() == shin);

    // ---------------------------------------------------------------------
    ctx.reached("score-file: a broken file is refused and changes nothing");
    const QString broken = folder.filePath("broken.musicxml");
    {
        QFile out(broken);
        check(out.open(QIODevice::WriteOnly));
        out.write("<score-partwise><part id=\"P1\"><measure><attributes><staves>100000000</staves>");
    }
    const auto tracks = song.song().tracks.size();
    check(!controller.importMusicXmlFile(broken));
    check(controller.status().startsWith("MusicXML import failed") && song.song().tracks.size() == tracks);
    check(!controller.importMusicXmlFile(folder.filePath("missing.musicxml")) && song.song().tracks.size() == tracks);

    ctx.finish(" SCORE IN, Hebrew lyrics, LYRICS field, SCORE OUT read back, refusals");
}

[[maybe_unused]] const bool registered = register_scenario("score-file", run_score_file);

}  // namespace
}  // namespace blokkily::verify
