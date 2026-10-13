// native_window_probe.hpp on macOS: the host's NSView, its subviews, and its
// contents rendered by AppKit (cacheDisplayInRect, which draws every subview
// through drawRect:) and grabbed from the screen by Qt.

#include "native_window_probe.hpp"

#import <Cocoa/Cocoa.h>

#include <cmath>

#include <QGuiApplication>
#include <QPixmap>
#include <QScreen>

namespace native_probe {

std::vector<QSize> children(WId host) {
    std::vector<QSize> found;
    auto* view = reinterpret_cast<NSView*>(host);
    if (view == nil) return found;
    for (NSView* child in [view subviews])
        found.emplace_back(static_cast<int>(std::lround(child.frame.size.width)),
                           static_cast<int>(std::lround(child.frame.size.height)));
    return found;
}

namespace {

QImage cached_display(NSView* view) {
    const NSRect bounds = [view bounds];
    NSBitmapImageRep* rep = [view bitmapImageRepForCachingDisplayInRect:bounds];
    if (rep == nil) return {};
    [view cacheDisplayInRect:bounds toBitmapImageRep:rep];
    CGImageRef image = [rep CGImage];
    if (image == nullptr) return {};
    const auto width = static_cast<int>(CGImageGetWidth(image));
    const auto height = static_cast<int>(CGImageGetHeight(image));
    if (width <= 0 || height <= 0) return {};
    QImage result(width, height, QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::black);
    CGColorSpaceRef srgb = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef context = CGBitmapContextCreate(
        result.bits(), static_cast<size_t>(width), static_cast<size_t>(height), 8,
        static_cast<size_t>(result.bytesPerLine()), srgb,
        kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host);
    CGColorSpaceRelease(srgb);
    if (context == nullptr) return {};
    CGContextDrawImage(context, CGRectMake(0, 0, width, height), image);
    CGContextRelease(context);
    return result;
}

} // namespace

std::vector<std::pair<QString, QImage>> grab(WId host) {
    std::vector<std::pair<QString, QImage>> grabs;
    auto* view = reinterpret_cast<NSView*>(host);
    grabs.emplace_back(QStringLiteral("cacheDisplayInRect"),
                       view != nil ? cached_display(view) : QImage());
    QImage screen;
    if (auto* primary = QGuiApplication::primaryScreen())
        screen = primary->grabWindow(host).toImage();
    grabs.emplace_back(QStringLiteral("screen"), screen);
    return grabs;
}

bool request_close(WId host) {
    auto* view = reinterpret_cast<NSView*>(host);
    NSWindow* window = view != nil ? [view window] : nil;
    if (window == nil) return false;
    [window performClose:nil];
    return true;
}

} // namespace native_probe
