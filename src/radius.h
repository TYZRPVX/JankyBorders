#pragma once
#include <CoreFoundation/CoreFoundation.h>
#include <math.h>
#include <stdbool.h>

static inline float border_radius_from_number(CFTypeRef value) {
  float radius = 0.0f;
  if (value && CFGetTypeID(value) == CFNumberGetTypeID()) {
    // Preserve fractional radii returned by custom application windows.
    CFNumberGetValue((CFNumberRef)value, kCFNumberFloatType, &radius);
  }
  return isfinite(radius) && radius > 0.0f ? radius : 0.0f;
}

static inline float border_radius_resolve(float system_radius,
                                          bool overridden,
                                          float configured_radius,
                                          bool macos_27_or_newer) {
  if (overridden) return configured_radius;
  if (isfinite(system_radius) && system_radius > 0.0f) return system_radius;
  // 16 points matches ordinary windows observed on macOS 27. Custom-drawn
  // windows can report zero even when their visible content is rounded.
  return macos_27_or_newer ? 16.0f : 9.0f;
}
