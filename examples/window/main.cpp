#include "vortex/platform/window.hpp"
#include "vortex/platform/input.hpp"
#include "vortex/platform/clock.hpp"
#include "vortex/core/log.hpp"

using namespace vortex;
using namespace vortex::pf;

int main() {
    auto window = createWindow({ .width = 1280, .height = 720, .title = "Vortex — Phase 1 (ESC to close)" });
    auto input  = createInputProvider(*window);
    auto clock  = createClock();

    VORTEX_INFO("App", "Window open. Move mouse or press keys. ESC to quit.");

    float prevX = -1.0f, prevY = -1.0f;

    while (!window->shouldClose()) {
        clock->tick();
        input->newFrame();
        window->pollEvents();

        if (input->isKeyPressed(Key::Escape))
            break;

        float mx, my;
        input->mousePosition(mx, my);
        if (static_cast<int>(mx) != static_cast<int>(prevX) ||
            static_cast<int>(my) != static_cast<int>(prevY)) {
            VORTEX_INFO("Input", "mouse=(%.0f, %.0f)  scroll=%.1f  dt=%.2fms",
                        mx, my, input->scrollDelta(), clock->deltaTime() * 1000.0);
            prevX = mx;
            prevY = my;
        }

        if (input->isKeyPressed(Key::Space)) VORTEX_INFO("Input", "Space pressed");
        if (input->isKeyPressed(Key::Enter)) VORTEX_INFO("Input", "Enter pressed");
        if (input->isKeyPressed(Key::F1))    VORTEX_INFO("Input", "F1 pressed");
    }

    VORTEX_INFO("App", "Goodbye.");
    return 0;
}
