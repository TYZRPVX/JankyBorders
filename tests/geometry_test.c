#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "../src/rounded_path.h"

static bool close_enough(CGFloat a, CGFloat b) {
  return fabs(a - b) < 0.001;
}

static void assert_rect(CGRect actual,
                        CGFloat x,
                        CGFloat y,
                        CGFloat width,
                        CGFloat height) {
  assert(close_enough(actual.origin.x, x));
  assert(close_enough(actual.origin.y, y));
  assert(close_enough(actual.size.width, width));
  assert(close_enough(actual.size.height, height));
}

static void test_selects_display_with_largest_intersection(void) {
  CGRect displays[] = {
    CGRectMake(0, 0, 100, 100),
    CGRectMake(100, 0, 100, 100),
  };
  CGRect selected = border_geometry_select_display(
      CGRectMake(80, 10, 80, 80), displays, 2);
  assert(CGRectEqualToRect(selected, displays[1]));
  assert(CGRectIsNull(border_geometry_select_display(
      CGRectMake(250, 0, 10, 10), displays, 2)));
}

static void test_position_parser_is_exact(void) {
  enum border_position position = BORDER_POSITION_AUTO;
  assert(border_position_parse("inside", &position));
  assert(position == BORDER_POSITION_INSIDE);
  assert(border_position_parse("center", &position));
  assert(position == BORDER_POSITION_CENTER);
  assert(border_position_parse("outside", &position));
  assert(position == BORDER_POSITION_OUTSIDE);
  assert(border_position_parse("auto", &position));
  assert(position == BORDER_POSITION_AUTO);
  assert(!border_position_parse("inner", &position));
  assert(position == BORDER_POSITION_AUTO);
}

static void test_auto_preserves_legacy_geometry_away_from_edges(void) {
  struct border_geometry geometry = border_geometry_calculate(
      CGRectMake(10, 10, 80, 80),
      CGRectMake(0, 0, 100, 100),
      4,
      8,
      BORDER_POSITION_AUTO);

  assert_rect(geometry.frame, 0, 0, 104, 104);
  assert_rect(geometry.drawing_bounds, 12, 12, 80, 80);
  assert_rect(geometry.path_bounds, 12, 12, 80, 80);
  assert_rect(geometry.clip_bounds, 13, 13, 78, 78);
  assert(geometry.inset_edges == 0);
  assert(!geometry.force_above);
}

static void test_auto_insets_each_overflowing_edge(void) {
  struct border_geometry geometry = border_geometry_calculate(
      CGRectMake(0, 0, 100, 100),
      CGRectMake(0, 0, 100, 100),
      4,
      8,
      BORDER_POSITION_AUTO);

  assert_rect(geometry.path_bounds, 14, 14, 96, 96);
  assert_rect(geometry.clip_bounds, 14, 14, 96, 96);
  assert(geometry.inset_edges == (BORDER_INSET_LEFT
                                  | BORDER_INSET_RIGHT
                                  | BORDER_INSET_TOP
                                  | BORDER_INSET_BOTTOM));
  assert(geometry.force_above);

  geometry = border_geometry_calculate(CGRectMake(10, 10, 89, 80),
                                       CGRectMake(0, 0, 100, 100),
                                       4,
                                       8,
                                       BORDER_POSITION_AUTO);
  assert(geometry.inset_edges == BORDER_INSET_RIGHT);
  assert_rect(geometry.path_bounds, 12, 12, 87, 80);
  assert_rect(geometry.clip_bounds, 12, 12, 87, 80);

  geometry = border_geometry_calculate(CGRectMake(1, 10, 80, 80),
                                       CGRectMake(0, 0, 100, 100),
                                       4,
                                       8,
                                       BORDER_POSITION_AUTO);
  assert(geometry.inset_edges == BORDER_INSET_LEFT);

  geometry = border_geometry_calculate(CGRectMake(10, 1, 80, 80),
                                       CGRectMake(0, 0, 100, 100),
                                       4,
                                       8,
                                       BORDER_POSITION_AUTO);
  assert(geometry.inset_edges == BORDER_INSET_TOP);
  // Unflipped context: the screen top edge is the context MaxY edge.
  assert_rect(geometry.path_bounds, 12, 12, 80, 78);
  assert_rect(geometry.clip_bounds, 12, 12, 80, 78);

  geometry = border_geometry_calculate(CGRectMake(10, 19, 80, 80),
                                       CGRectMake(0, 0, 100, 100),
                                       4,
                                       8,
                                       BORDER_POSITION_AUTO);
  assert(geometry.inset_edges == BORDER_INSET_BOTTOM);
  assert_rect(geometry.path_bounds, 12, 14, 80, 78);
  assert_rect(geometry.clip_bounds, 12, 14, 80, 78);

  geometry = border_geometry_calculate(CGRectMake(2, 2, 96, 96),
                                       CGRectMake(0, 0, 100, 100),
                                       4,
                                       8,
                                       BORDER_POSITION_AUTO);
  assert(geometry.inset_edges == 0);
}

static void test_explicit_positions_have_full_width_geometry(void) {
  CGRect window = CGRectMake(10, 10, 80, 80);
  CGRect display = CGRectMake(0, 0, 100, 100);
  struct border_geometry outside = border_geometry_calculate(
      window, display, 4, 8, BORDER_POSITION_OUTSIDE);
  struct border_geometry center = border_geometry_calculate(
      window, display, 4, 8, BORDER_POSITION_CENTER);
  struct border_geometry inside = border_geometry_calculate(
      window, display, 4, 8, BORDER_POSITION_INSIDE);

  assert_rect(outside.path_bounds, 10, 10, 84, 84);
  assert_rect(outside.clip_bounds, 12, 12, 80, 80);
  assert(!outside.force_above);
  assert_rect(center.path_bounds, 12, 12, 80, 80);
  assert_rect(center.clip_bounds, 14, 14, 76, 76);
  assert(!center.force_above);
  assert_rect(inside.path_bounds, 14, 14, 76, 76);
  assert_rect(inside.clip_bounds, 16, 16, 72, 72);
  assert(inside.force_above);
}

static void test_corner_radii_follow_each_inset_edge(void) {
  CGRect outer = CGRectMake(0, 0, 100, 100);
  struct border_corner_radii radii = border_geometry_corner_radii(
      outer,
      CGRectMake(0, 0, 98, 100),
      10);
  assert(close_enough(radii.top_left, 10));
  assert(close_enough(radii.bottom_left, 10));
  assert(close_enough(radii.top_right, 8));
  assert(close_enough(radii.bottom_right, 8));

  radii = border_geometry_corner_radii(outer,
                                       CGRectInset(outer, 2, 2),
                                       10);
  assert(close_enough(radii.top_left, 8));
  assert(close_enough(radii.top_right, 8));
  assert(close_enough(radii.bottom_right, 8));
  assert(close_enough(radii.bottom_left, 8));

  radii = border_geometry_corner_radii(outer,
                                       CGRectMake(1, 1, 95, 98),
                                       10);
  assert(close_enough(radii.top_left, 9));
  assert(close_enough(radii.bottom_left, 9));
  assert(close_enough(radii.top_right, 6));
  assert(close_enough(radii.bottom_right, 6));
}

static void test_auto_clip_preserves_corner_center(void) {
  struct border_geometry geometry = border_geometry_calculate(
      CGRectMake(10, 10, 100, 100), CGRectNull, 3, 8,
      BORDER_POSITION_AUTO);
  struct border_corner_radii clip = border_geometry_corner_radii(
      geometry.drawing_bounds, geometry.clip_bounds, 16);
  assert(close_enough(clip.top_left, 15));
  assert(close_enough(CGRectGetMinX(geometry.clip_bounds) + clip.top_left,
                      CGRectGetMinX(geometry.drawing_bounds) + 16));
  assert(close_enough(CGRectGetMaxY(geometry.clip_bounds) - clip.bottom_right,
                      CGRectGetMaxY(geometry.drawing_bounds) - 16));
}

static void test_continuous_path_and_circular_fallback(void) {
  CGRect rect = CGRectMake(0, 0, 100, 100);
  struct border_corner_radii radii = { 16, 16, 16, 16 };
  CGMutablePathRef circular = CGPathCreateMutable();
  CGMutablePathRef continuous = CGPathCreateMutable();
  assert(circular && continuous);
  assert(border_add_rounded_rect(circular, rect, radii, false));
  assert(border_add_rounded_rect(continuous, rect, radii, true));
  assert(CGRectEqualToRect(CGPathGetPathBoundingBox(continuous), rect));
  assert(CGPathContainsPoint(continuous, NULL, CGPointMake(50, 50), false));
  assert(!CGPathContainsPoint(continuous, NULL, CGPointZero, false));
  if (border_create_continuous_path) {
    // A continuous corner has a longer transition into the straight edge.
    // These points are inside a circular corner but outside the native curve.
    CGPoint points[] = { {16, 0.1}, {84, 0.1}, {16, 99.9}, {84, 99.9} };
    for (size_t i = 0; i < sizeof(points) / sizeof(points[0]); ++i) {
      assert(CGPathContainsPoint(circular, NULL, points[i], false));
      assert(!CGPathContainsPoint(continuous, NULL, points[i], false));
    }
  } else {
    assert(CGPathEqualToPath(circular, continuous));
  }

  border_continuous_path_fn saved = border_create_continuous_path;
  border_create_continuous_path = NULL;
  CGMutablePathRef fallback = CGPathCreateMutable();
  assert(border_add_rounded_rect(fallback, rect, radii, true));
  assert(CGPathEqualToPath(circular, fallback));
  border_create_continuous_path = saved;
  CGPathRelease(fallback);
  CGPathRelease(continuous);
  CGPathRelease(circular);
}

int main(void) {
  test_auto_clip_preserves_corner_center();
  test_continuous_path_and_circular_fallback();
  test_position_parser_is_exact();
  test_selects_display_with_largest_intersection();
  test_auto_preserves_legacy_geometry_away_from_edges();
  test_auto_insets_each_overflowing_edge();
  test_explicit_positions_have_full_width_geometry();
  test_corner_radii_follow_each_inset_edge();
  puts("geometry tests passed");
  return 0;
}
