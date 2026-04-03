if (BeginSelectableSettingsTopTabItem(trc("label.overlays"))) {
    if (ImGui::BeginTabBar("OverlaySettingsTabs")) {
#include "tab_mirrors.inl"
#include "tab_images.inl"

#include "tab_window_overlays.inl"

#include "tab_browser_overlays.inl"

#include "tab_ninjabrain_overlay.inl"

        ImGui::EndTabBar();
    }

    ImGui::EndTabItem();
}