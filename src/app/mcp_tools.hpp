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

class AppController;
class PatternModel;
class QObject;
class QQuickWindow;
class SongModel;
class Transport;

class McpTools {
public:
    McpTools(SongModel& song, PatternModel& pattern, AppController& controller,
             Transport& transport, QQuickWindow* window);

    // Runs tool `name` (one of blokkily::mcp::toolDefinitions()); returns the
    // tools/call result.
    QJsonObject call(const QString& name, const QJsonObject& arguments);

private:
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

    QJsonObject transportState() const;
    QObject* object(const QString& name) const;

    SongModel& song_;
    PatternModel& pattern_;
    AppController& controller_;
    Transport& transport_;
    QQuickWindow* window_;
};
