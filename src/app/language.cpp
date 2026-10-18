#include "language.hpp"

#include "app_controller.hpp"

#include <QCoreApplication>
#include <QLocale>
#include <QQmlEngine>

namespace blokkily {

Language::Language(AppController& controller, QQmlEngine& engine) : controller_(controller), engine_(engine) {
    QObject::connect(&controller_, &AppController::uiSettingsChanged, &engine_, [this] { apply(); });
    apply();
}

Language::~Language() {
    if (installed_) QCoreApplication::removeTranslator(&translator_);
}

void Language::apply() {
    const QVariant setting = controller_.uiSettings().value(QStringLiteral("ui/language"));
    const QString choice = setting.isValid() ? setting.toString() : QStringLiteral("system");
    const QLocale locale = choice == QStringLiteral("system") ? QLocale::system() : QLocale(choice);
    // The translation that would be shown: "" for English, the interface's own.
    QTranslator candidate;
    const QString file = candidate.load(locale, QStringLiteral("blokkily"), QStringLiteral("_"),
                                        QStringLiteral(":/i18n"))
                             ? candidate.filePath() : QString();
    if (file == shown_) return;
    if (installed_) QCoreApplication::removeTranslator(&translator_);
    installed_ = !file.isEmpty() && translator_.load(file) && QCoreApplication::installTranslator(&translator_);
    shown_ = installed_ ? file : QString();
    engine_.retranslate();
}

} // namespace blokkily
