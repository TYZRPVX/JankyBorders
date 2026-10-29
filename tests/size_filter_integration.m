// Manual native-window test; requires a running Borders PID configured with
// width=3 min_width=600 min_height=400 active_only=on.
// Build: clang -fobjc-arc tests/size_filter_integration.m -o /tmp/size-filter-test -framework AppKit
// Run: /tmp/size-filter-test <borders-pid>
// Exit 77 means another application took focus and interrupted the test.
#import <AppKit/AppKit.h>
#include <unistd.h>

static int failures;

static void check_border(pid_t pid, NSWindow *expected, const char *stage) {
  NSArray *windows = CFBridgingRelease(CGWindowListCopyWindowInfo(
      kCGWindowListOptionOnScreenOnly, kCGNullWindowID));
  CGRect target = CGRectNull;
  NSMutableArray *helpers = [NSMutableArray array];
  for (NSDictionary *window in windows) {
    CGRect bounds;
    CGRectMakeWithDictionaryRepresentation(
        (__bridge CFDictionaryRef)window[(id)kCGWindowBounds], &bounds);
    if (expected && [window[(id)kCGWindowNumber] unsignedIntValue]
                    == expected.windowNumber) target = bounds;
    if ([window[(id)kCGWindowOwnerPID] intValue] == pid)
      [helpers addObject:[NSValue valueWithRect:bounds]];
  }
  BOOL passed = expected ? helpers.count == 1 && !CGRectIsNull(target)
                         : helpers.count == 0;
  if (passed && expected) {
    CGRect helper = [helpers[0] rectValue];
    // width=3 plus BORDER_PADDING=8 gives an 11-point helper margin.
    passed = fabs(helper.origin.x - (target.origin.x - 11)) < 1
          && fabs(helper.origin.y - (target.origin.y - 11)) < 1
          && fabs(helper.size.width - target.size.width - 22) < 1
          && fabs(helper.size.height - target.size.height - 22) < 1;
  }
  printf("%s: %s (visible helpers=%lu)\n", stage,
         passed ? "PASS" : "FAIL", (unsigned long)helpers.count);
  fflush(stdout);
  if (!passed) ++failures;
}

int main(int argc, char **argv) {
  @autoreleasepool {
    if (argc != 2 || atoi(argv[1]) <= 0) return 2;
    pid_t pid = atoi(argv[1]);
    NSRunningApplication *previous = NSWorkspace.sharedWorkspace.frontmostApplication;
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp finishLaunching];
    NSUInteger style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                        | NSWindowStyleMaskResizable;
    NSWindow *main = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(200, 200, 850, 600)
        styleMask:style backing:NSBackingStoreBuffered defer:NO];
    NSWindow *small = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(350, 350, 350, 250)
        styleMask:style backing:NSBackingStoreBuffered defer:NO];
    main.title = @"Borders size test — main";
    small.title = @"Borders size test — small";
    main.releasedWhenClosed = NO;
    small.releasedWhenClosed = NO;
    [NSApp activateIgnoringOtherApps:YES];
    [main makeKeyAndOrderFront:nil];
    __block int stage = 0;
    [NSTimer scheduledTimerWithTimeInterval:2 repeats:YES block:^(NSTimer *timer) {
      if (NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier != getpid()) {
        puts("SKIP: another application took focus");
        [timer invalidate];
        [small close];
        [main close];
        exit(77);
      }
      switch (stage++) {
        case 0:
          check_border(pid, main, "main focused");
          [small makeKeyAndOrderFront:nil];
          break;
        case 1:
          check_border(pid, main, "small focus retains main");
          [small setContentSize:NSMakeSize(650, 450)];
          break;
        case 2:
          check_border(pid, small, "grow without changing focus");
          [small setContentSize:NSMakeSize(350, 250)];
          break;
        case 3:
          check_border(pid, main, "shrink restores main");
          [main close];
          break;
        case 4:
          check_border(pid, nil, "no eligible main");
          [small setContentSize:NSMakeSize(650, 450)];
          break;
        case 5:
          check_border(pid, small, "grow after main closed");
          [timer invalidate];
          [small close];
          [previous activateWithOptions:0];
          exit(failures ? 1 : 0);
      }
    }];
    [NSApp run];
  }
  return 0;
}
