#include "close_observer.h"
#include "border.h"
#include "hashtable.h"
#include "misc/ax.h"
#include "misc/extern.h"
#include <pthread.h>

extern struct table g_windows;

// If the destroyed element did not actually close its window (for example an
// application recreating its AX tree), the border comes back after this.
#define CLOSE_OBSERVER_RECHECK_MS 1000

// Accessed only from the main thread: AX observer callbacks are delivered
// through a main run loop source and focus changes are committed on main.
static struct {
  pid_t pid;
  AXObserverRef observer;
  AXUIElementRef window;
  uint32_t wid;
} close_observer;

static void close_observer_unwatch_window(void) {
  if (close_observer.window) {
    if (close_observer.observer) {
      AXObserverRemoveNotification(close_observer.observer,
                                   close_observer.window,
                                   kAXUIElementDestroyedNotification);
    }
    CFRelease(close_observer.window);
    close_observer.window = NULL;
  }
  close_observer.wid = 0;
}

static void close_observer_reset(void) {
  close_observer_unwatch_window();
  if (close_observer.observer) {
    CFRunLoopRemoveSource(
        CFRunLoopGetMain(),
        AXObserverGetRunLoopSource(close_observer.observer),
        kCFRunLoopDefaultMode);
    CFRelease(close_observer.observer);
    close_observer.observer = NULL;
  }
  close_observer.pid = 0;
}

static void close_observer_schedule_recheck(uint32_t wid) {
  // Only the window ID is captured. A border that was destroyed meanwhile is
  // simply no longer in the table.
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW,
                               CLOSE_OBSERVER_RECHECK_MS * NSEC_PER_MSEC),
                 dispatch_get_main_queue(), ^{
    uint32_t target_wid = wid;
    struct border* border = table_find(&g_windows, &target_wid);
    if (!border) return;

    pthread_mutex_lock(&border->mutex);
    bool was_closing = border->closing;
    border->closing = false;
    pthread_mutex_unlock(&border->mutex);
    if (was_closing) border_update(border, true);
  });
}

static void close_observer_callback(AXObserverRef observer,
                                    AXUIElementRef element,
                                    CFStringRef notification,
                                    void* context) {
  (void)observer;
  (void)element;
  if (CFStringCompare(notification,
                      kAXUIElementDestroyedNotification,
                      0) != kCFCompareEqualTo) {
    return;
  }

  uint32_t wid = (uint32_t)(uintptr_t)context;
  if (wid == close_observer.wid) close_observer_unwatch_window();

  struct border* border = table_find(&g_windows, &wid);
  if (!border) return;

  debug("Window Closing: %u\n", wid);
  pthread_mutex_lock(&border->mutex);
  border->closing = true;
  pthread_mutex_unlock(&border->mutex);
  border_hide(border);
  close_observer_schedule_recheck(wid);
}

static AXUIElementRef close_observer_copy_window(AXUIElementRef app,
                                                 uint32_t wid) {
  CFTypeRef focused = NULL;
  if (AXUIElementCopyAttributeValue(app,
                                    kAXFocusedWindowAttribute,
                                    &focused) == kAXErrorSuccess
      && focused) {
    uint32_t focused_wid = 0;
    _AXUIElementGetWindow(focused, &focused_wid);
    if (focused_wid == wid) return (AXUIElementRef)focused;
    CFRelease(focused);
  }

  CFTypeRef windows = NULL;
  if (AXUIElementCopyAttributeValue(app,
                                    kAXWindowsAttribute,
                                    &windows) != kAXErrorSuccess
      || !windows) {
    return NULL;
  }

  AXUIElementRef match = NULL;
  if (CFGetTypeID(windows) == CFArrayGetTypeID()) {
    CFIndex count = CFArrayGetCount(windows);
    for (CFIndex i = 0; i < count && !match; ++i) {
      CFTypeRef candidate = CFArrayGetValueAtIndex(windows, i);
      uint32_t candidate_wid = 0;
      _AXUIElementGetWindow(candidate, &candidate_wid);
      if (candidate_wid == wid) match = (AXUIElementRef)CFRetain(candidate);
    }
  }
  CFRelease(windows);
  return match;
}

void close_observer_track(uint32_t wid) {
  if (wid && wid == close_observer.wid) return;
  close_observer_unwatch_window();
  if (!wid || !AXIsProcessTrusted()) return;

  int cid = SLSMainConnectionID();
  int wid_cid = 0;
  pid_t pid = 0;
  if (SLSGetWindowOwner(cid, wid, &wid_cid) != kCGErrorSuccess
      || !wid_cid
      || SLSConnectionGetPID(wid_cid, &pid) != kCGErrorSuccess
      || pid <= 0) {
    return;
  }

  if (pid != close_observer.pid) {
    close_observer_reset();
    AXObserverRef observer = NULL;
    if (AXObserverCreate(pid,
                         close_observer_callback,
                         &observer) != kAXErrorSuccess
        || !observer) {
      return;
    }
    CFRunLoopAddSource(CFRunLoopGetMain(),
                       AXObserverGetRunLoopSource(observer),
                       kCFRunLoopDefaultMode);
    close_observer.observer = observer;
    close_observer.pid = pid;
  }

  AXUIElementRef app = AXUIElementCreateApplication(pid);
  if (!app) return;
  AXUIElementSetMessagingTimeout(app, AX_MESSAGING_TIMEOUT_S);
  AXUIElementRef window = close_observer_copy_window(app, wid);
  CFRelease(app);
  if (!window) return;

  AXUIElementSetMessagingTimeout(window, AX_MESSAGING_TIMEOUT_S);
  if (AXObserverAddNotification(close_observer.observer,
                                window,
                                kAXUIElementDestroyedNotification,
                                (void*)(uintptr_t)wid) != kAXErrorSuccess) {
    CFRelease(window);
    return;
  }
  close_observer.window = window;
  close_observer.wid = wid;
}
