#pragma once
#include <stdint.h>

// WindowServer fades a closing window out before it orders it out and sends
// the SkyLight hide/close/destroy notifications, so the border used to stay
// fully opaque for the whole animation. The application reports the window
// element destroyed through Accessibility before the animation starts; this
// observer watches the focused window for that and hides its border early.
//
// Requires Accessibility trust; without it the SkyLight notifications remain
// the only signal. Must be called on the main thread.
void close_observer_track(uint32_t wid);
