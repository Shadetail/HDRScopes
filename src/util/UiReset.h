// Reset-to-default gesture for individual controls: right-clicking a control
// resets it to its default. Call right after the widget.
//
// Defaults come from a default-constructed Settings, so they always match the
// initializers in Settings.h.
#pragma once

#include "util/Settings.h"
#include "imgui.h"

inline const Settings& UiDefaults() { static const Settings d; return d; }

// Current UI scale (window DPI / 96). Fixed pixel sizes in UI code multiply by
// this; fonts and style paddings are rebuilt in main.cpp's ApplyUiScale.
inline float& UiScale() { static float s = 1.0f; return s; }

inline bool UiResetClicked() {
    return ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
}

// Applies to the previous item (slider, checkbox, combo, radio, button...).
template <typename T>
inline bool UiReset(T& v, const T& def) {
    if (UiResetClicked()) { v = def; return true; }
    return false;
}

// ---- Delayed hover tooltips -------------------------------------------------
// Gated by Settings::showTooltips (synced once per frame from the controls
// window). The ForTooltip flag applies the stationary + delay policy set on
// ImGuiIO in main.cpp, so tooltips never flash during ordinary mousing.

inline bool& UiTipsEnabled() { static bool on = true; return on; }

// Call right after a widget (and its UiReset) to attach its explanation.
inline void UiTip(const char* text) {
    if (!UiTipsEnabled()) return;
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) return;
    if (ImGui::BeginTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 22.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}
