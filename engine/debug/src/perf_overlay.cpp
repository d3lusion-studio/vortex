#include "vortex/debug/perf_overlay.hpp"

#include "vortex/core/diagnostics.hpp"
#include "vortex/core/profiler.hpp"

#include <imgui.h>

namespace vortex::debug {

void PerfOverlay::draw() {
    if (!visible) return;

    // Pinned to a corner, transparent, no interaction with the mouse unless hovered —
    // a readout, not a window the user manages.
    const f32          pad      = 8.0f;
    const ImGuiViewport* vp     = ImGui::GetMainViewport();
    ImVec2 pos{
        (corner & 1) ? vp->WorkPos.x + vp->WorkSize.x - pad : vp->WorkPos.x + pad,
        (corner & 2) ? vp->WorkPos.y + vp->WorkSize.y - pad : vp->WorkPos.y + pad,
    };
    const ImVec2 pivot{(corner & 1) ? 1.0f : 0.0f, (corner & 2) ? 1.0f : 0.0f};
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always, pivot);
    ImGui::SetNextWindowBgAlpha(0.6f);

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav;
    if (!ImGui::Begin("perf##overlay", nullptr, flags)) {
        ImGui::End();
        return;
    }

    // --- The headline: FPS + frame graph -------------------------------------------
    if (const diag::Diagnostic* fps = diag::find("frame.fps"); fps && fps->count() > 0) {
        const diag::Diagnostic* ms = diag::find("frame.ms");
        ImGui::Text("%5.0f fps  %6.2f ms", fps->smoothed(), ms ? ms->smoothed() : 0.0);

        if (ms != nullptr) {
            ms->history(m_history);
            if (!m_history.empty()) {
                // Scale the plot to the WORST recent frame: spikes are the entire reason
                // to look at this graph, and autoscaling to the average hides them.
                f32 worst = 0.0f;
                for (const f32 v : m_history) worst = worst > v ? worst : v;
                ImGui::PlotLines("##frame_ms", m_history.data(),
                                 static_cast<int>(m_history.size()), 0, nullptr, 0.0f,
                                 worst * 1.1f, ImVec2(180.0f, 40.0f));
            }
        }
    } else {
        ImGui::TextDisabled("feed diag::frame(dt) for fps");
    }

    // --- Everything else measured through diag:: ------------------------------------
    bool firstExtra = true;
    for (const diag::Diagnostic* d : diag::all()) {
        if (!d->enabled || d->count() == 0) continue;
        if (d->name() == "frame.fps" || d->name() == "frame.ms") continue;
        if (firstExtra) {
            ImGui::Separator();
            firstExtra = false;
        }
        ImGui::Text("%-18s %10.2f", d->name().c_str(), d->smoothed());
    }

    // --- Profiler zones, for when "the frame is slow" needs a suspect ----------------
    const auto& zones = profiler::lastFrame();
    if (!zones.empty() && ImGui::TreeNode("zones")) {
        for (const auto& z : zones)
            ImGui::Text("%-22s %7.3f ms x%u", z.name, z.ms, z.calls);
        ImGui::TreePop();
    }

    ImGui::End();
}

}
