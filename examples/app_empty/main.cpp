// The smallest thing that is still a game.
//
// Three lines. What they get you: a window, a Vulkan device and swapchain, a sprite batcher, a
// job system, an asset manager, an ECS scene with a 2D camera, an input map, a particle world,
// lazily-created audio and physics — and a loop that drives them with a fixed timestep for
// simulation and a variable one for rendering, and that will not spiral into a death loop when a
// frame runs long.
//
// That is what "with defaults" means here. There is no plugin to add to get any of it: it is what
// App IS. Plugins are for what YOU add on top — see examples/app_plugin.

#include "vortex/app/app.hpp"

using namespace vortex;

int main() {
    app::App app({.title = "Vortex — Empty"});
    return app.run();
}
