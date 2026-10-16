#pragma once

// What each MCP tool does to the running session (features/mcp.feature). The
// tools read the one canonical song and write through the same methods the
// window's controls call, so an agent's edit is an edit like a click: it is
// heard from the next block, drawn in every editor, and undone by UNDO.
// Main (GUI) thread only.

#include "blokkily/model/song.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "blokkily/notation/staff.hpp"

class AppController;
class GuideController;
class PatternModel;
class QObject;
class QQuickWindow;
class SongModel;
class Transport;

class McpTools {
public:
    McpTools(SongModel& song, PatternModel& pattern, AppController& controller,
             Transport& transport, QQuickWindow* window, GuideController* guide = nullptr);

    // Runs tool `name` (one of blokkily::mcp::toolDefinitions()); returns the
    // tools/call result.
    QJsonObject call(const QString& name, const QJsonObject& arguments);

private:
    // The dispatch itself, so call() can stamp the narration source around it.
    QJsonObject dispatch(const QString& name, const QJsonObject& arguments);
    QJsonObject getSession() const;
    QJsonObject getPattern(const QJsonObject& arguments) const;
    QJsonObject writeNotes(const QJsonObject& arguments);
    QJsonObject setSection(const QJsonObject& arguments);
    QJsonObject placeSection(const QJsonObject& arguments);
    QJsonArray effects(const std::vector<blokkily::EffectSlot>& chain) const;
    QJsonObject runTransport(const QJsonObject& arguments);
    QJsonObject listPlugins() const;
    QJsonObject setInstrument(const QJsonObject& arguments);
    QJsonObject listParameters(const QJsonObject& arguments) const;
    QJsonObject setParameter(const QJsonObject& arguments);
    QJsonObject setSoundFontPreset(const QJsonObject& arguments);
    QJsonObject screenshot() const;
    QJsonObject analyzeRender(const QJsonObject& arguments);
    QJsonObject describe(const QJsonObject& arguments) const;
    QJsonObject invoke(const QJsonObject& arguments);
    QJsonObject getProperty(const QJsonObject& arguments) const;
    // Staff notation (feature/staff-notation): the score projection of a
    // pattern, its clef/key/meter settings, and MusicXML in and out.
    QJsonObject getScore(const QJsonObject& arguments);
    QJsonObject setClef(const QJsonObject& arguments);
    QJsonObject setKeySignature(const QJsonObject& arguments);
    QJsonObject setTimeSignature(const QJsonObject& arguments);
    QJsonObject exportMusicXml(const QJsonObject& arguments);
    QJsonObject importMusicXml(const QJsonObject& arguments);
    // The floating guide (feature/staff-notation): its narration feed, its
    // lessons, and its rule-based answers.
    QJsonObject guideEvents(const QJsonObject& arguments);
    QJsonObject guideLesson(const QJsonObject& arguments);
    QJsonObject guideAsk(const QJsonObject& arguments);

    QJsonObject transportState() const;
    QObject* object(const QString& name) const;
    // The clef/key/meter a score tool reads a part in: the part's stored
    // settings, overridden by the call's arguments.
    struct ScoreDressing {
        blokkily::notation::Clef clef{blokkily::notation::ClefKind::Treble};
        blokkily::notation::KeySignature key{0};
        blokkily::notation::TimeSignature time{4, 4};
    };
    ScoreDressing scoreDressing(int track, int section, const QJsonObject& arguments);
    blokkily::notation::Part scorePart(int track, int section);

    SongModel& song_;
    PatternModel& pattern_;
    AppController& controller_;
    Transport& transport_;
    QQuickWindow* window_;
    GuideController* guide_;
};
