#include <assert.h>

#include "../src/window_policy.h"

int main(void) {
  assert(window_policy_size_allowed(600, 400, 600, 400));
  assert(!window_policy_size_allowed(599, 900, 600, 400));
  assert(!window_policy_size_allowed(1200, 399, 600, 400));
  assert(window_policy_size_allowed(50, 50, 0, 0));
  assert(window_policy_size_allowed(50, 400, 0, 400));
  assert(!window_policy_size_allowed(NAN, 400, 0, 0));
  assert(!window_policy_size_allowed(600, INFINITY, 0, 0));
  // A resize crosses the threshold in both directions without a focus change.
  assert(!window_policy_size_allowed(599.5, 400, 600, 400));
  assert(window_policy_size_allowed(600.5, 400, 600, 400));
  assert(!window_policy_size_allowed(600.5, 399.5, 600, 400));
  uint64_t required_attribute = 0x2;

  assert(window_policy_is_suitable(WINDOW_TAG_DOCUMENT,
                                   required_attribute,
                                   0));
  assert(window_policy_is_suitable(WINDOW_TAG_DOCUMENT
                                   | WINDOW_TAG_IGNORES_CYCLE,
                                   required_attribute,
                                   0));
  assert(window_policy_is_suitable(WINDOW_TAG_FLOATING | WINDOW_TAG_MODAL,
                                   required_attribute,
                                   0));
  assert(!window_policy_is_suitable(WINDOW_TAG_FLOATING
                                    | WINDOW_TAG_MODAL
                                    | WINDOW_TAG_IGNORES_CYCLE,
                                    required_attribute,
                                    0));
  assert(!window_policy_is_suitable(WINDOW_TAG_DOCUMENT
                                    | WINDOW_TAG_ATTACHED,
                                    required_attribute,
                                    0));
  assert(!window_policy_is_suitable(WINDOW_TAG_DOCUMENT,
                                    required_attribute,
                                    42));
  assert(!window_policy_is_suitable(WINDOW_TAG_DOCUMENT, 0, 0));
  return 0;
}
