/*
 * UWP ImGui backend stubs.
 *
 * imgui_impl_uwp.cpp is excluded from the ARM64EC build: it needs the WinRT
 * headers (Windows.UI.Core.h, Windows.UI.Xaml.*) that mingw-w64 does not ship.
 * UWP is also meaningless for the Proton/Wine target -- OptiScaler only calls
 * these when it has been loaded into a UWP app, which cannot happen here.
 *
 * MenuCommon still references them unconditionally (the UWP branches are
 * runtime-guarded by _isUWP, not compile-time), so provide no-op definitions
 * rather than patching every call site.
 */
#include <imgui.h>

typedef void (*PFN_KeyUp)(unsigned int);

extern "C++"
{
    bool ImGui_ImplUwp_Init(void*) { return false; }
    bool ImGui_ImplUwp_InitForCurrentView() { return false; }
    bool ImGui_ImplUwp_InitForSwapChainPanel(void*) { return false; }
    void ImGui_ImplUwp_Shutdown() {}
    void ImGui_ImplUwp_NewFrame(ImVec2) {}
    void ImGui_BindUwpKeyUp(PFN_KeyUp) {}
}
