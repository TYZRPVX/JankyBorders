#pragma once

#include <CoreGraphics/CoreGraphics.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Since macOS 27 Mission Control is drawn by the WindowManager process and the
// WindowServer no longer hides foreign overlay windows while it is shown.
// While Mission Control is open, WindowManager places one backdrop window at
// this level covering each display exactly; their lifetime brackets the
// overview. Backdrops are created at level 0 and raised and sized afterwards,
// so a new WindowManager window is probed again over a short bounded window.
// SkyLight Space create/destroy events and the Dock's AXExpose* notifications
// are not usable signals on macOS 27.
#define MISSION_CONTROL_OWNER "WindowManager"
#define MISSION_CONTROL_BACKDROP_LEVEL 19
#define MISSION_CONTROL_MAX_BACKDROPS 16u

// A backdrop is released only after it is missing or ordered out for this
// many consecutive polls, so a backdrop briefly reshuffled by a Space switch
// inside Mission Control does not flash the borders back in.
#define MISSION_CONTROL_RELEASE_POLLS 2u
#define MISSION_CONTROL_POLL_INTERVAL_US 100000u

enum { MISSION_CONTROL_PROBE_COUNT = 4 };

static const uint64_t mission_control_probe_delays_us[
    MISSION_CONTROL_PROBE_COUNT] = {
  20000,
  40000,
  90000,
  150000,
};

struct mission_control_backdrop {
  uint32_t wid;
  unsigned int missed_polls;
};

struct mission_control_state {
  struct mission_control_backdrop backdrops[MISSION_CONTROL_MAX_BACKDROPS];
  size_t count;
};

// The caller has already verified that WindowManager owns the window.
static inline bool mission_control_backdrop_matches(int64_t level,
                                                    CGRect bounds,
                                                    const CGRect* displays,
                                                    size_t display_count) {
  if (level != MISSION_CONTROL_BACKDROP_LEVEL || !displays) return false;

  for (size_t i = 0; i < display_count; ++i) {
    if (CGRectEqualToRect(bounds, displays[i])) return true;
  }
  return false;
}

static inline bool mission_control_is_active(
    const struct mission_control_state* state) {
  return state && state->count > 0;
}

static inline bool mission_control_tracks(
    const struct mission_control_state* state,
    uint32_t wid) {
  if (!state) return false;
  for (size_t i = 0; i < state->count; ++i) {
    if (state->backdrops[i].wid == wid) return true;
  }
  return false;
}

// Returns true when this backdrop starts a Mission Control session.
static inline bool mission_control_track(struct mission_control_state* state,
                                         uint32_t wid) {
  if (!state || !wid) return false;
  for (size_t i = 0; i < state->count; ++i) {
    if (state->backdrops[i].wid == wid) {
      state->backdrops[i].missed_polls = 0;
      return false;
    }
  }
  if (state->count >= MISSION_CONTROL_MAX_BACKDROPS) return false;

  bool entered = state->count == 0;
  state->backdrops[state->count++] = (struct mission_control_backdrop){
    .wid = wid,
    .missed_polls = 0,
  };
  return entered;
}

// Returns true when the last backdrop is released and the session ends.
static inline bool mission_control_poll(struct mission_control_state* state,
                                        bool (*visible)(uint32_t wid,
                                                        void* context),
                                        void* context) {
  if (!state || !visible || state->count == 0) return false;

  size_t count = 0;
  for (size_t i = 0; i < state->count; ++i) {
    struct mission_control_backdrop backdrop = state->backdrops[i];
    if (visible(backdrop.wid, context)) backdrop.missed_polls = 0;
    else backdrop.missed_polls++;

    if (backdrop.missed_polls < MISSION_CONTROL_RELEASE_POLLS) {
      state->backdrops[count++] = backdrop;
    }
  }
  state->count = count;
  return count == 0;
}
