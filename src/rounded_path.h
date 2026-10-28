#pragma once
#include <dlfcn.h>
#include <pthread.h>
#include "geometry.h"

// This CoreGraphics SPI is resolved dynamically, like the SkyLight corner
// radius query. Keep circular corners when the symbol is unavailable.
typedef CGPathRef (*border_continuous_path_fn)(CGRect,
                                              CGFloat,
                                              CGFloat,
                                              const CGAffineTransform*);
static border_continuous_path_fn border_create_continuous_path;
static pthread_once_t border_continuous_path_once = PTHREAD_ONCE_INIT;

static void border_load_continuous_path(void) {
  if (__builtin_available(macOS 26.0, *)) {
    border_create_continuous_path = (border_continuous_path_fn)dlsym(
        RTLD_DEFAULT, "CGPathCreateWithContinuousRoundedRect");
  }
}

static inline bool border_corner_radii_are_uniform(struct border_corner_radii radii) {
  return radii.top_left == radii.top_right
         && radii.top_left == radii.bottom_right
         && radii.top_left == radii.bottom_left;
}

static inline bool border_add_rounded_rect(CGMutablePathRef path,
                                           CGRect rect,
                                           struct border_corner_radii radii,
                                           bool continuous) {
  if (!path) return false;
  if (border_corner_radii_are_uniform(radii)) {
    if (continuous) {
      pthread_once(&border_continuous_path_once,
                   border_load_continuous_path);
      if (border_create_continuous_path) {
        CGPathRef rounded = border_create_continuous_path(rect,
                                                          radii.top_left,
                                                          radii.top_left,
                                                          NULL);
        if (!rounded) return false;
        CGPathAddPath(path, NULL, rounded);
        CGPathRelease(rounded);
        return true;
      }
    }
    CGPathAddRoundedRect(path,
                         NULL,
                         rect,
                         radii.top_left,
                         radii.top_left);
    return true;
  }

  CGFloat min_x = CGRectGetMinX(rect);
  CGFloat max_x = CGRectGetMaxX(rect);
  CGFloat min_y = CGRectGetMinY(rect);
  CGFloat max_y = CGRectGetMaxY(rect);

  CGPathMoveToPoint(path, NULL, min_x + radii.top_left, min_y);
  CGPathAddLineToPoint(path, NULL, max_x - radii.top_right, min_y);
  CGPathAddArcToPoint(path,
                      NULL,
                      max_x,
                      min_y,
                      max_x,
                      min_y + radii.top_right,
                      radii.top_right);
  CGPathAddLineToPoint(path, NULL, max_x, max_y - radii.bottom_right);
  CGPathAddArcToPoint(path,
                      NULL,
                      max_x,
                      max_y,
                      max_x - radii.bottom_right,
                      max_y,
                      radii.bottom_right);
  CGPathAddLineToPoint(path, NULL, min_x + radii.bottom_left, max_y);
  CGPathAddArcToPoint(path,
                      NULL,
                      min_x,
                      max_y,
                      min_x,
                      max_y - radii.bottom_left,
                      radii.bottom_left);
  CGPathAddLineToPoint(path, NULL, min_x, min_y + radii.top_left);
  CGPathAddArcToPoint(path,
                      NULL,
                      min_x,
                      min_y,
                      min_x + radii.top_left,
                      min_y,
                      radii.top_left);
  CGPathCloseSubpath(path);
  return true;
}

