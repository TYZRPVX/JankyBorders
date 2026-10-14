#include "events.h"
#include "misc/extern.h"
#include "windows.h"
#include "border.h"
#include "focus_recovery.h"
#include "misc/window.h"
#include "mission_control.h"
#include "space_recovery.h"
#include <libproc.h>
#include <string.h>

extern struct table g_windows;
extern pid_t g_pid;
extern volatile bool g_mission_control_active;

static volatile uint64_t focus_change_generation;
static volatile bool space_transition_active;
// SkyLight notifications are drained by the main CFRunLoop in main.c and all
// delayed probes target the main dispatch queue, so this state is serialized.
static struct focus_recovery_scheduler focus_refresh_scheduler;
static void events_schedule_focus_refresh(bool invalidate_fallback);

static void events_cancel_focus_refresh(void) {
  __sync_add_and_fetch(&focus_change_generation, 1);
  focus_recovery_scheduler_cancel(&focus_refresh_scheduler);
  windows_focus_probe_reset();
}

static void events_restart_focus_refresh(void) {
  // A front-application notification is a state boundary, not another noisy
  // window update. Anchor a fresh bounded series here so it cannot be folded
  // into probes that began for the previously active application.
  events_cancel_focus_refresh();
  events_schedule_focus_refresh(true);
}

static void events_schedule_focus_refresh(bool invalidate_fallback) {
  // Mission Control exit starts a fresh Space refresh, which also resolves
  // focus, so nothing is lost by not probing while the overview is shown.
  if (g_mission_control_active) return;
  if (!focus_recovery_scheduler_schedule(&focus_refresh_scheduler)) {
    // The in-flight callbacks always read current WindowServer state, so a
    // burst must not postpone the already scheduled probe. Structural/order
    // events can mean a same-process window switch, so they invalidate the
    // fallback identity; noisy title/content updates leave it intact.
    if (invalidate_fallback) windows_focus_probe_reset();
    return;
  }

  uint64_t generation = __sync_add_and_fetch(&focus_change_generation, 1);
  windows_focus_probe_reset();

  for (size_t i = 0; i < FOCUS_RECOVERY_RETRY_COUNT; ++i) {
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,
                                 focus_recovery_retry_delays_us[i]
                                 * NSEC_PER_USEC),
                   dispatch_get_main_queue(), ^{
      if (!focus_recovery_retry_is_current(
              __sync_fetch_and_add(&focus_change_generation, 0),
              generation)) {
        return;
      }

      bool final_retry = i == FOCUS_RECOVERY_RETRY_COUNT - 1;
      // Keep the dirty bit latched for the whole series. If the last attempt
      // still has to HOLD, any coalesced event warrants exactly one fresh
      // bounded series; clearing it on an earlier probe could lose that rearm.
      bool observed_new_event = final_retry
                                && focus_recovery_scheduler_observe_events(
                                    &focus_refresh_scheduler);
      enum windows_focus_refresh_result result = windows_refresh_active_window(
          &g_windows,
          final_retry && !space_transition_active);
      if (final_retry) {
        bool rearm = focus_recovery_scheduler_finish(
            &focus_refresh_scheduler,
            result == WINDOWS_FOCUS_REFRESH_HELD,
            observed_new_event);
        if (rearm) {
          events_schedule_focus_refresh(false);
        }
      }
    });
  }
}

#ifdef DEBUG
static void dump_event(void* data, size_t data_length) {
  if (!data) return;
  for (size_t i = 0; i < data_length; i++) {
    printf("%02x ", *((unsigned char*)data + i));
  }
  printf("\n");
}

static void event_watcher(uint32_t event, void* data, size_t data_length, void* context) {
  static int count = 0;
  printf("(%d) Event: %d; Payload:\n", ++count, event);
  dump_event(data, data_length);
}
#endif

// SkyLight sends the spawn payload packed as { uint64_t sid; uint32_t wid; },
// which is 12 bytes. A C struct with those members is padded to 16 bytes, so
// its sizeof would reject every real create and destroy notification.
#define WINDOW_SPAWN_DATA_SID_OFFSET 0
#define WINDOW_SPAWN_DATA_WID_OFFSET sizeof(uint64_t)
#define WINDOW_SPAWN_DATA_SIZE (sizeof(uint64_t) + sizeof(uint32_t))

static bool is_own_window(int cid, uint32_t wid) {
  int wid_cid = 0;
  SLSGetWindowOwner(cid, wid, &wid_cid);
  pid_t pid = 0;
  SLSConnectionGetPID(wid_cid, &pid);
  return pid == g_pid;
}

// SkyLight notifications, probes and the poll below all run on the main
// queue, so the backdrop state is serialized without a lock.
static struct mission_control_state mission_control;

static bool events_window_owned_by(int cid, uint32_t wid, const char* name) {
  int wid_cid = 0;
  pid_t pid = 0;
  char owner[PROC_PIDPATHINFO_MAXSIZE];
  return SLSGetWindowOwner(cid, wid, &wid_cid) == kCGErrorSuccess
         && wid_cid
         && SLSConnectionGetPID(wid_cid, &pid) == kCGErrorSuccess
         && pid > 0
         && proc_name(pid, owner, sizeof(owner)) > 0
         && strcmp(owner, name) == 0;
}

static bool events_mission_control_is_backdrop(int cid, uint32_t wid) {
  int64_t level = 0;
  CGRect bounds;
  if (SLSGetWindowLevel(cid, wid, &level) != kCGErrorSuccess
      || SLSGetWindowBounds(cid, wid, &bounds) != kCGErrorSuccess) {
    return false;
  }

  CGDirectDisplayID display_ids[MISSION_CONTROL_MAX_BACKDROPS];
  uint32_t display_count = 0;
  if (CGGetActiveDisplayList(MISSION_CONTROL_MAX_BACKDROPS,
                             display_ids,
                             &display_count) != kCGErrorSuccess) {
    return false;
  }
  CGRect displays[MISSION_CONTROL_MAX_BACKDROPS];
  for (uint32_t i = 0; i < display_count; ++i) {
    displays[i] = CGDisplayBounds(display_ids[i]);
  }

  return mission_control_backdrop_matches(level,
                                          bounds,
                                          displays,
                                          display_count);
}

static bool events_mission_control_backdrop_visible(uint32_t wid,
                                                    void* context) {
  int cid = (int)(intptr_t)context;
  int wid_cid = 0;
  bool shown = false;
  return SLSGetWindowOwner(cid, wid, &wid_cid) == kCGErrorSuccess
         && wid_cid
         && SLSWindowIsOrderedIn(cid, wid, &shown) == kCGErrorSuccess
         && shown;
}

static void events_mission_control_schedule_poll(int cid) {
  // Destroy notifications also fire when a backdrop merely moves between
  // Spaces, so the exit is decided by polling the tracked backdrops instead.
  // The poll only runs while Mission Control is shown.
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW,
                               MISSION_CONTROL_POLL_INTERVAL_US
                               * NSEC_PER_USEC),
                 dispatch_get_main_queue(), ^{
    if (!mission_control_poll(&mission_control,
                              events_mission_control_backdrop_visible,
                              (void*)(intptr_t)cid)) {
      events_mission_control_schedule_poll(cid);
      return;
    }

    debug("Mission Control Exit\n");
    g_mission_control_active = false;
    events_schedule_space_refresh();
  });
}

static void events_mission_control_probe(int cid,
                                         uint32_t wid,
                                         size_t attempt) {
  if (mission_control_tracks(&mission_control, wid)) return;

  if (!events_mission_control_is_backdrop(cid, wid)) {
    if (attempt >= MISSION_CONTROL_PROBE_COUNT) return;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,
                                 mission_control_probe_delays_us[attempt]
                                 * NSEC_PER_USEC),
                   dispatch_get_main_queue(), ^{
      events_mission_control_probe(cid, wid, attempt + 1);
    });
    return;
  }

  if (!mission_control_track(&mission_control, wid)) return;

  debug("Mission Control Enter (backdrop %u)\n", wid);
  g_mission_control_active = true;
  events_cancel_focus_refresh();
  windows_hide_all(&g_windows);
  events_mission_control_schedule_poll(cid);
}

static void events_mission_control_observe(int cid, uint32_t wid) {
  // Backdrops are created at level 0 and sized and raised only afterwards,
  // so WindowManager windows are re-probed briefly after their creation.
  if (!events_window_owned_by(cid, wid, MISSION_CONTROL_OWNER)) return;
  events_mission_control_probe(cid, wid, 0);
}

static void window_spawn_handler(uint32_t event,
                                 void* data,
                                 size_t data_length,
                                 int cid) {
  if (!data || data_length < WINDOW_SPAWN_DATA_SIZE) return;

  uint64_t sid = 0;
  uint32_t wid = 0;
  memcpy(&sid,
         (const uint8_t*)data + WINDOW_SPAWN_DATA_SID_OFFSET,
         sizeof(sid));
  memcpy(&wid,
         (const uint8_t*)data + WINDOW_SPAWN_DATA_WID_OFFSET,
         sizeof(wid));
  struct table* windows = &g_windows;

  if (!wid || !sid || is_own_window(cid, wid)) return;

  if (event == EVENT_WINDOW_CREATE) {
    events_mission_control_observe(cid, wid);
    if (windows_window_create(windows, wid, sid)) {
      debug("Window Created: %d %d\n", wid, sid);
      events_schedule_space_refresh();
    }
    events_schedule_focus_refresh(true);
  } else if (event == EVENT_WINDOW_DESTROY) {
    if (windows_window_destroy(windows, wid, sid)) {
      debug("Window Destroyed: %d %d\n", wid, sid);
    }
    events_schedule_focus_refresh(true);
  }
}

static void window_modify_handler(uint32_t event,
                                  void* data,
                                  size_t data_length,
                                  int cid) {
  if (!data || data_length < sizeof(uint32_t)) return;

  uint32_t wid = 0;
  memcpy(&wid, data, sizeof(wid));
  if (!wid) return;
  struct table* windows = &g_windows;

  if (is_own_window(cid, wid)) return;

  if (event == EVENT_WINDOW_MOVE) {
    debug("Window Move: %d\n", wid);
    windows_window_move(windows, wid);
  } else if (event == EVENT_WINDOW_RESIZE) {
    debug("Window Resize: %d\n", wid);
    windows_window_resize(windows, wid);
  } else if (event == EVENT_WINDOW_REORDER) {
    debug("Window Reorder (and focus): %d\n", wid);
    windows_window_update(windows, wid);
    // A reorder can switch between two windows of the same application, so
    // treat it as the same hard boundary as an application activation.
    events_restart_focus_refresh();
  } else if (event == EVENT_WINDOW_LEVEL) {
    debug("Window Level: %d\n", wid);
    windows_window_update(windows, wid);
  } else if (event == EVENT_WINDOW_TITLE || event == EVENT_WINDOW_UPDATE) {
    debug("Window Focus\n");
    events_schedule_focus_refresh(false);
  } else if (event == EVENT_WINDOW_UNHIDE) {
    debug("Window Unhide: %d\n", wid);
    windows_window_unhide(windows, wid);
    events_schedule_focus_refresh(true);
  } else if (event == EVENT_WINDOW_HIDE) {
    debug("Window Hide: %d\n", wid);
    windows_window_hide(windows, wid);
    events_schedule_focus_refresh(true);
  } else if (event == EVENT_WINDOW_CLOSE) {
    debug("Window Close: %d\n", wid);
    windows_window_destroy(windows, wid, 0);
    events_schedule_focus_refresh(true);
  }
}

static void front_app_handler(uint32_t event,
                              void* data,
                              size_t data_length,
                              void* context) {
  (void)event;
  (void)data;
  (void)data_length;
  (void)context;
  debug("Window Focus\n");
  events_restart_focus_refresh();
}

void events_workspace_did_activate(void) {
  debug("Workspace Application Activated\n");
  events_restart_focus_refresh();
}

static volatile uint64_t space_change_generation;

void events_schedule_space_refresh(void) {
  // Native-fullscreen transitions publish their window and Space changes at
  // different times, and a newly created helper may not yet be registered when
  // its first move is submitted. Retry a bounded number of consistency scans,
  // cancelling the older series when a newer refresh begins. The blocks only
  // retain a generation value and the process-lifetime window table, never a
  // border pointer that could have been destroyed before the delay expires.
  events_cancel_focus_refresh();
  uint64_t generation = __sync_add_and_fetch(&space_change_generation, 1);

  for (size_t i = 0; i < SPACE_CHANGE_RETRY_COUNT; ++i) {
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,
                                 space_change_retry_delays_us[i]
                                 * NSEC_PER_USEC),
                   dispatch_get_main_queue(), ^{
      if (!space_change_retry_is_current(
              __sync_fetch_and_add(&space_change_generation, 0),
              generation)) {
        return;
      }
      if (g_mission_control_active) return;
      windows_refresh_after_space_change(
          &g_windows,
          i == SPACE_CHANGE_RETRY_COUNT - 1);
      if (i == SPACE_CHANGE_RETRY_COUNT - 1) {
        space_transition_active = false;
      }
    });
  }
}

static void space_handler(uint32_t event,
                          void* data,
                          size_t data_length,
                          void* context) {
  (void)event;
  (void)data;
  (void)data_length;
  (void)context;
  space_transition_active = true;
  windows_adaptive_space_change_started(&g_windows);
  events_schedule_space_refresh();
}

void events_register(int cid) {
  void* cid_ctx = (void*)(intptr_t)cid;

  SLSRegisterNotifyProc(window_modify_handler, EVENT_WINDOW_CLOSE, cid_ctx);
  SLSRegisterNotifyProc(window_modify_handler, EVENT_WINDOW_MOVE, cid_ctx);
  SLSRegisterNotifyProc(window_modify_handler, EVENT_WINDOW_RESIZE, cid_ctx);
  SLSRegisterNotifyProc(window_modify_handler, EVENT_WINDOW_LEVEL, cid_ctx);
  SLSRegisterNotifyProc(window_modify_handler, EVENT_WINDOW_UNHIDE, cid_ctx);
  SLSRegisterNotifyProc(window_modify_handler, EVENT_WINDOW_HIDE, cid_ctx);
  SLSRegisterNotifyProc(window_modify_handler, EVENT_WINDOW_TITLE, cid_ctx);
  SLSRegisterNotifyProc(window_modify_handler, EVENT_WINDOW_REORDER, cid_ctx);
  SLSRegisterNotifyProc(window_modify_handler, EVENT_WINDOW_UPDATE, cid_ctx);
  SLSRegisterNotifyProc(window_spawn_handler, EVENT_WINDOW_CREATE, cid_ctx);
  SLSRegisterNotifyProc(window_spawn_handler, EVENT_WINDOW_DESTROY, cid_ctx);

  SLSRegisterNotifyProc(space_handler, EVENT_SPACE_CHANGE, cid_ctx);

  SLSRegisterNotifyProc(front_app_handler, EVENT_FRONT_CHANGE, cid_ctx);

#ifdef DEBUG
  for (int i = 0; i < 2000; i++) {
    SLSRegisterNotifyProc(event_watcher, i, NULL);
  }
#endif
}
