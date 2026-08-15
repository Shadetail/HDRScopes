// Reset-to-default gestures for individual controls: call right after the
// widget. Sliders/drags reset on double-click OR right-click; toggle-style
// widgets (checkbox/combo/radio/button) reset on right-click only, since a
// double-click there is just two activations.
//
// Defaults come from a default-constructed Settings, so they always match the
// initializers in Settings.h.
#pragma once

#include "util/Settings.h"
#include "imgui.h"

inline const Settings& UiDefaults() { static const Settings d; return d; }

inline bool UiResetGestureSlider() {
    return ImGui::IsItemHovered() &&
           (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right));
}
inline bool UiResetGestureToggle() {
    return ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
}

// For sliders / drag widgets (previous item).
template <typename T>
inline bool UiResetSlider(T& v, const T& def) {
    if (UiResetGestureSlider()) { v = def; return true; }
    return false;
}

// For checkboxes / combos / radios / toggle buttons (previous item).
template <typename T>
inline bool UiResetToggle(T& v, const T& def) {
    if (UiResetGestureToggle()) { v = def; return true; }
    return false;
}
