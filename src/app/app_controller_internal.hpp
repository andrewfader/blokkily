#pragma once

#include <QString>
#include <QVariantMap>

#include <string>

// Helpers the AppController translation units share. Nothing outside the
// application's controller includes this.
namespace app_detail {

inline QVariantMap plugin_entry(const QString& format, const std::string& name,
                                const std::string& vendor, const std::string& path = {},
                                const std::string& identifier = {}, int index = 0) {
    return {{"format", format},
            {"name", QString::fromStdString(name)},
            {"vendor", QString::fromStdString(vendor)},
            {"path", QString::fromStdString(path)},
            {"identifier", QString::fromStdString(identifier)},
            {"index", index}};
}

} // namespace app_detail
