#pragma once

// What the platform's own windowing says about a host window an editor is
// embedded in, for the macOS and Windows plugin-window checks: the native
// views (NSView subviews, child HWNDs) inside it, its contents as the
// platform draws them, and the close request its close button makes.

#include <QImage>
#include <QSize>
#include <QString>
#include <QWindow>

#include <utility>
#include <vector>

namespace native_probe {

// The child views or windows directly inside `host`, each with its size in
// logical units (points on macOS, pixels at 96 dpi on Windows).
std::vector<QSize> children(WId host);

// `host`'s contents, its children's drawing included, by every means the
// platform offers, each named: a screen grab and the platform's own
// rendering of the window. An image is null when that means failed.
std::vector<std::pair<QString, QImage>> grab(WId host);

// Asks the top-level window holding `host` to close, as its close button
// does. False when the request could not be made.
bool request_close(WId host);

} // namespace native_probe
