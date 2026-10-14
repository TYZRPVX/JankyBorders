#include <assert.h>
#include <stdio.h>
#include "../src/mission_control.h"

struct visibility {
  uint32_t visible[MISSION_CONTROL_MAX_BACKDROPS];
  size_t count;
};

static bool backdrop_visible(uint32_t wid, void* context) {
  struct visibility* visibility = context;
  for (size_t i = 0; i < visibility->count; ++i) {
    if (visibility->visible[i] == wid) return true;
  }
  return false;
}

static void test_backdrop_requires_level_and_display_bounds(void) {
  CGRect displays[] = {
    CGRectMake(0, 0, 2560, 1440),
    CGRectMake(2560, 180, 1728, 1117),
  };
  CGRect secondary = CGRectMake(2560, 180, 1728, 1117);

  assert(mission_control_backdrop_matches(19, secondary, displays, 2));
  assert(!mission_control_backdrop_matches(0, secondary, displays, 2));
  assert(!mission_control_backdrop_matches(17, secondary, displays, 2));
  assert(!mission_control_backdrop_matches(19,
                                           CGRectMake(0, 0, 2560, 96),
                                           displays, 2));
  assert(!mission_control_backdrop_matches(19, secondary, NULL, 0));
}

static void test_probe_plan_is_bounded_and_increasing(void) {
  uint64_t total = 0;
  for (size_t i = 0; i < MISSION_CONTROL_PROBE_COUNT; ++i) {
    if (i) {
      assert(mission_control_probe_delays_us[i]
             > mission_control_probe_delays_us[i - 1]);
    }
    total += mission_control_probe_delays_us[i];
  }
  assert(total <= 500000);
}

static void test_first_backdrop_enters_once(void) {
  struct mission_control_state state = { 0 };
  assert(!mission_control_is_active(&state));
  assert(mission_control_track(&state, 10));
  assert(!mission_control_track(&state, 11));
  assert(!mission_control_track(&state, 10));
  assert(!mission_control_track(&state, 0));
  assert(state.count == 2);
  assert(mission_control_is_active(&state));
  assert(mission_control_tracks(&state, 11));
  assert(!mission_control_tracks(&state, 12));
}

static void test_exit_waits_for_every_backdrop_to_stay_gone(void) {
  struct mission_control_state state = { 0 };
  mission_control_track(&state, 10);
  mission_control_track(&state, 11);
  struct visibility visibility = { .visible = { 11 }, .count = 1 };

  assert(!mission_control_poll(&state, backdrop_visible, &visibility));
  assert(state.count == 2);
  assert(!mission_control_poll(&state, backdrop_visible, &visibility));
  assert(state.count == 1);
  assert(mission_control_tracks(&state, 11));

  visibility.count = 0;
  assert(!mission_control_poll(&state, backdrop_visible, &visibility));
  assert(mission_control_poll(&state, backdrop_visible, &visibility));
  assert(!mission_control_is_active(&state));
}

static void test_reappearing_backdrop_resets_release(void) {
  struct mission_control_state state = { 0 };
  mission_control_track(&state, 10);
  struct visibility visibility = { .count = 0 };

  assert(!mission_control_poll(&state, backdrop_visible, &visibility));
  visibility.visible[0] = 10;
  visibility.count = 1;
  assert(!mission_control_poll(&state, backdrop_visible, &visibility));
  visibility.count = 0;
  assert(!mission_control_poll(&state, backdrop_visible, &visibility));
  assert(mission_control_is_active(&state));

  // A repeated observation also counts as proof of life.
  mission_control_track(&state, 10);
  assert(!mission_control_poll(&state, backdrop_visible, &visibility));
  assert(mission_control_is_active(&state));
  assert(mission_control_poll(&state, backdrop_visible, &visibility));
}

static void test_capacity_is_bounded(void) {
  struct mission_control_state state = { 0 };
  for (uint32_t wid = 1; wid <= MISSION_CONTROL_MAX_BACKDROPS; ++wid) {
    mission_control_track(&state, wid);
  }
  assert(state.count == MISSION_CONTROL_MAX_BACKDROPS);
  assert(!mission_control_track(&state, 100));
  assert(state.count == MISSION_CONTROL_MAX_BACKDROPS);
}

int main(void) {
  test_backdrop_requires_level_and_display_bounds();
  test_probe_plan_is_bounded_and_increasing();
  test_first_backdrop_enters_once();
  test_exit_waits_for_every_backdrop_to_stay_gone();
  test_reappearing_backdrop_resets_release();
  test_capacity_is_bounded();
  printf("mission_control tests passed\n");
  return 0;
}
