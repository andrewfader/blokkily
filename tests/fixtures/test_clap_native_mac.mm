// The CLAP fixture's editor view on macOS (see test_clap_native.hpp). Drawn
// in drawRect:, not as a layer's colour, so it reaches whatever the host
// renders its view tree into, the screen or a cached bitmap.

#include "test_clap_native.hpp"

#import <Cocoa/Cocoa.h>

@interface BlokkilyTestClapEditorView : NSView
@end

@implementation BlokkilyTestClapEditorView
- (BOOL)isOpaque {
    return YES;
}
- (void)drawRect:(NSRect)dirty {
    [[NSColor colorWithSRGBRed:200.0 / 255.0 green:1.0 blue:60.0 / 255.0 alpha:1.0] setFill];
    NSRectFill(dirty);
}
@end

namespace blokkily::test_clap_native {

void* attach(void* parent, std::uint32_t width, std::uint32_t height) {
    auto* host = static_cast<NSView*>(parent);
    if (host == nil) return nullptr;
    // Kept with the +1 alloc gives it; detach() releases it.
    auto* view = [[BlokkilyTestClapEditorView alloc]
        initWithFrame:NSMakeRect(0, 0, static_cast<CGFloat>(width), static_cast<CGFloat>(height))];
    [host addSubview:view];
    return view;
}

void resize(void* child, std::uint32_t width, std::uint32_t height) {
    auto* view = static_cast<NSView*>(child);
    [view setFrameSize:NSMakeSize(static_cast<CGFloat>(width), static_cast<CGFloat>(height))];
}

void detach(void* child) {
    auto* view = static_cast<NSView*>(child);
    [view removeFromSuperview];
    [view release];
}

} // namespace blokkily::test_clap_native
