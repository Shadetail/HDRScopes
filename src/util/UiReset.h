// Reset-to-default gesture for individual controls: right-clicking a control
// resets it to its default. Call right after the widget.
//
// Defaults come from a default-constructed Settings, so they always match the
// initializers in Settings.h.
#pragma once

#include "util/Settings.h"
#include "imgui.h"

inline const Settings& UiDefaults() { static const Settings d; return d; }

inline bool UiResetClicked() {
    return ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
}

// Applies to the previous item (slider, checkbox, combo, radio, button...).
template <typename T>
inline bool UiReset(T& v, const T& def) {
    if (UiResetClicked()) { v = def; return true; }
    return false;
}
