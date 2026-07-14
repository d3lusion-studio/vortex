// Plugins and plugin groups.
//
// The demonstration that matters is the one that used to be impossible: THREE plugins each
// register an update hook, and all three run. Under the old App — where onUpdate() replaced the
// previously registered callback rather than appending to it — the last one registered would have
// silently deleted the other two, and the only symptom would have been two systems that quietly
// stopped working.
//
// So this example is also a test: if the log shows all three ticking, the list works.

#include "vortex/app/app.hpp"
#include "vortex/app/plugin.hpp"
#include "vortex/core/log.hpp"

#include <cstdlib>

using namespace vortex;

namespace {

// A plugin that counts frames and reports on a schedule.
class HeartbeatPlugin final : public app::IPlugin {
public:
    explicit HeartbeatPlugin(f32 period) : m_period(period) {}

    const char* name() const override { return "Heartbeat"; }

    void build(app::App& app) override {
        app.onStart([](app::App&) { VORTEX_INFO("Heartbeat", "started"); });

        app.onUpdate([this](app::App&, f32 dt) {
            m_time += dt;
            if (m_time >= m_period) {
                m_time -= m_period;
                VORTEX_INFO("Heartbeat", "tick %d", ++m_ticks);
            }
        });

        app.onShutdown([this](app::App&) {
            VORTEX_INFO("Heartbeat", "stopped after %d ticks", m_ticks);
        });
    }

private:
    f32 m_period = 1.0f;
    f32 m_time   = 0.0f;
    int m_ticks  = 0;
};

// A second, entirely independent plugin that also wants an update hook. This is the one that used
// to vanish.
class FpsPlugin final : public app::IPlugin {
public:
    const char* name() const override { return "Fps"; }

    void build(app::App& app) override {
        app.onUpdate([this](app::App&, f32 dt) {
            ++m_frames;
            m_time += dt;
            if (m_time >= 1.0f) {
                VORTEX_INFO("Fps", "%d frames/s", m_frames);
                m_frames = 0;
                m_time   = 0.0f;
            }
        });
    }

private:
    int m_frames = 0;
    f32 m_time   = 0.0f;
};

// A third, to make the point unmistakable — and one that logs at Trace, so it is also the example
// of a category you can silence:  VORTEX_LOG=info,Spy=trace
class SpyPlugin final : public app::IPlugin {
public:
    const char* name() const override { return "Spy"; }

    void build(app::App& app) override {
        app.onUpdate([](app::App&, f32 dt) {
            VORTEX_TRACE("Spy", "frame took %.2f ms", dt * 1000.0f);
        });
    }
};

// A plugin that does nothing, so the group has something to disable.
class NoisyPlugin final : public app::IPlugin {
public:
    const char* name() const override { return "Noisy"; }
    void build(app::App& app) override {
        app.onUpdate([](app::App&, f32) { VORTEX_INFO("Noisy", "I should not be here"); });
    }
};

// A group: somebody else's list of defaults. The caller takes it and drops the one they do not
// want — which is the entire reason a group is not just a vector. Rebuilding the list by hand to
// remove one entry means silently missing whatever gets added to it next release.
app::PluginGroup diagnosticsPlugins() {
    app::PluginGroup group;
    group.add<HeartbeatPlugin>(0.5f);
    group.add<FpsPlugin>();
    group.add<SpyPlugin>();
    group.add<NoisyPlugin>();
    return group;
}

} // namespace

int main() {
    const char* maxFramesEnv = std::getenv("VORTEX_MAX_FRAMES");

    app::App app({.title = "Vortex — Plugins",
                  .maxFrames = maxFramesEnv ? std::strtoull(maxFramesEnv, nullptr, 10) : 0});

    // Take the group, keep everything in it EXCEPT the one that misbehaves.
    app.addPlugins(diagnosticsPlugins().disable("Noisy"));

    for (const std::string& name : app.plugins())
        VORTEX_INFO("App", "plugin built: %s", name.c_str());

    // The game's own update, registered after three plugins already registered theirs. All four
    // run, in registration order.
    app.onUpdate([](app::App&, f32) {});

    return app.run();
}
