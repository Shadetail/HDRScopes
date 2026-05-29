// ShareX-style interactive region selection. Freezes a screenshot of the given
// output, shows it full-screen dimmed, and lets the user drag a rectangle. The
// selected rect is returned in DESKTOP coordinates (ready for Region/DragRect).
// Blocks (runs its own message loop) until the user confirms or cancels.
#pragma once

#include "util/Common.h"

namespace regionpicker {

// Returns true and fills outDesktopRect on confirm; false on cancel/empty.
bool PickScreenRegion(const RECT& outputRect, RECT& outDesktopRect);

} // namespace regionpicker
