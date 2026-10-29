// Manual test: clang -fobjc-arc tests/window_close_integration.m -o /tmp/window-close-test -framework AppKit
// Run /tmp/window-close-test <borders-pid> with active_only=on.
// Checks close latency and guards against the delayed AX recheck reviving a helper.
#import <AppKit/AppKit.h>
#include <unistd.h>

static NSArray *visible_windows(void) {
  return CFBridgingRelease(CGWindowListCopyWindowInfo(
      kCGWindowListOptionOnScreenOnly, kCGNullWindowID));
}

int main(int argc, char **argv) {
  @autoreleasepool {
    if (argc != 2 || atoi(argv[1]) <= 0) return 2;
    pid_t daemon = atoi(argv[1]);
    NSRunningApplication *previous = NSWorkspace.sharedWorkspace.frontmostApplication;
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp finishLaunching];
    NSWindow *window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(200, 200, 850, 600)
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
        backing:NSBackingStoreBuffered defer:NO];
    window.title = @"Borders close regression";
    window.releasedWhenClosed = NO;
    [NSApp activateIgnoringOtherApps:YES];
    [window makeKeyAndOrderFront:nil];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC),
                   dispatch_get_main_queue(), ^{
      if (NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier != getpid()) {
        puts("SKIP: external focus change"); exit(77);
      }
      NSMutableSet *helpers = [NSMutableSet set];
      for (NSDictionary *info in visible_windows()) {
        if ([info[(id)kCGWindowOwnerPID] intValue] == daemon)
          [helpers addObject:info[(id)kCGWindowNumber]];
      }
      if (helpers.count != 1) { puts("FAIL: expected one initial border"); exit(1); }
      CFAbsoluteTime start = CFAbsoluteTimeGetCurrent();
      [window close];
      __block double hidden_at = -1;
      __block BOOL revived = NO;
      [NSTimer scheduledTimerWithTimeInterval:0.01 repeats:YES block:^(NSTimer *timer) {
        BOOL visible = NO;
        for (NSDictionary *info in visible_windows())
          if ([helpers containsObject:info[(id)kCGWindowNumber]]) visible = YES;
        double elapsed = CFAbsoluteTimeGetCurrent() - start;
        if (!visible && hidden_at < 0) hidden_at = elapsed;
        if (visible && hidden_at >= 0) revived = YES;
        if (elapsed >= 1.6) {
          [timer invalidate];
          BOOL passed = hidden_at >= 0 && hidden_at <= 0.5 && !revived;
          printf("%s: close latency %.1f ms; revived=%s\n",
                 passed ? "PASS" : "FAIL", hidden_at * 1000, revived ? "yes" : "no");
          [previous activateWithOptions:0];
          exit(passed ? 0 : 1);
        }
      }];
    });
    [NSApp run];
  }
  return 0;
}
