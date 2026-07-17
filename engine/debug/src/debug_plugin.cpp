#include "vortex/debug/debug_plugin.hpp"

#include "vortex/app/app.hpp"
#include "vortex/core/diagnostics.hpp"
#include "vortex/core/log.hpp"
#include "vortex/debug/imgui_layer.hpp"
#include "vortex/debug/inspector.hpp"
#include "vortex/debug/perf_overlay.hpp"
#include "vortex/rhi/command_list.hpp"

#include <imgui.h>

namespace vortex::debug {

struct OverlayPlugin::Impl {
    pf::Key toggle;
    bool    visible = false;

    std::unique_ptr<ImGuiLayer> imgui;
    EntityInspector             inspector;
    PerfOverlay                 perf;

    bool showInspector = true;
    bool showPerf      = true;
    bool showDemo      = false;

    explicit Impl(pf::Key key) : toggle(key) {}
};

OverlayPlugin::OverlayPlugin(pf::Key toggle) : m_impl(std::make_unique<Impl>(toggle)) {}
OverlayPlugin::~OverlayPlugin() = default;

bool OverlayPlugin::capturingMouse() const {
    const Impl& s = *m_impl;
    return s.visible && s.imgui && s.imgui->wantsMouse();
}

void OverlayPlugin::build(app::App& app) {
    Impl& s = *m_impl;

    // The overlay lands on the backbuffer in both the plain and the post-processed path —
    // the loop runs onRawRender in whichever pass targets it — so the pipeline is built for
    // the surface's format.
    s.imgui = std::make_unique<ImGuiLayer>(app.device(), app.surfaceFormat());

    app.onUpdate([this](app::App& a, f32) {
        Impl& impl = *m_impl;
        if (a.input().isKeyPressed(impl.toggle)) impl.visible = !impl.visible;
    });

    // newFrame and render must bracket the same frame, and render() has to happen during
    // recording — so the ImGui half of the frame is built here, on the main thread, right
    // before it is recorded.
    app.onRawRender([this](app::App& a, rhi::ICommandList& cmd) {
        Impl& impl = *m_impl;
        if (!impl.visible || !impl.imgui) return;

        f32 mx = 0.0f, my = 0.0f;
        a.input().mousePosition(mx, my);

        ImGuiInput in;
        in.displayWidth  = a.camera().viewportWidth;
        in.displayHeight = a.camera().viewportHeight;
        in.mouse         = {mx, my};
        in.mouseDown[0]  = a.input().isMouseDown(pf::MouseButton::Left);
        in.mouseDown[1]  = a.input().isMouseDown(pf::MouseButton::Right);
        in.mouseDown[2]  = a.input().isMouseDown(pf::MouseButton::Middle);
        in.scroll        = a.input().scrollDelta();

        impl.imgui->newFrame(in, a.deltaTime());

        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("Debug")) {
                ImGui::MenuItem("Entities", nullptr, &impl.showInspector);
                ImGui::MenuItem("Performance", nullptr, &impl.showPerf);
                ImGui::MenuItem("ImGui demo", nullptr, &impl.showDemo);
                ImGui::EndMenu();
            }
            ImGui::Text("  |  %.1f fps   %zu sprites   %u draw calls", static_cast<f64>(a.fps()),
                        a.visibleSprites(), a.drawCalls());
            ImGui::EndMainMenuBar();
        }

        impl.perf.visible = impl.showPerf;
        if (impl.showPerf) impl.perf.draw();
        if (impl.showInspector) impl.inspector.draw(a.scene());
        if (impl.showDemo) impl.imgui->showDemoWindow(&impl.showDemo);

        impl.imgui->render(cmd);
    });

    VORTEX_INFO("Debug", "Overlay plugin ready.");
}

}
