#pragma once

#include <QString>
#include <QTranslator>

class AppController;
class QQmlEngine;

namespace blokkily {

// The window's language (SETTINGS · WINDOW · LANGUAGE): the ui/language
// setting names a locale ("de"), "en" for the English the interface is
// written in, or "system" for the desktop's own languages, as it ships. The
// translations are compiled into the program under :/i18n; a language with
// none, or a string a translation leaves out, reads in English.
class Language {
public:
    Language(AppController& controller, QQmlEngine& engine);
    Language(const Language&) = delete;
    Language& operator=(const Language&) = delete;
    ~Language();

    // Follows the setting: installs the translation it names and has the
    // engine re-read every qsTr in the window. Nothing is redrawn when the
    // language is the one already shown.
    void apply();

private:
    AppController& controller_;
    QQmlEngine& engine_;
    QTranslator translator_;
    bool installed_ = false;
    QString shown_;
};

} // namespace blokkily
