#pragma once
#include "vortex/core/types.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vortex::app {

class App;

// A plugin is a bundle of behaviour that installs itself into an App: it registers hooks, spawns
// entities, loads what it needs. Nothing else in the engine knows it exists.
//
// The point is not modularity for its own sake. It is that a debug overlay, a gameplay system and
// a cheat console are three independent things that all want to run code every frame — and until
// App kept a LIST of update hooks rather than a single one, the last one registered silently
// deleted the other two. The plugin interface is the shape that list wants; the list is the
// actual change.
class IPlugin {
public:
    virtual ~IPlugin() = default;

    IPlugin(const IPlugin&)            = delete;
    IPlugin& operator=(const IPlugin&) = delete;

    // Shown in logs and used by PluginGroup::disable. Keep it stable.
    [[nodiscard]] virtual const char* name() const = 0;

    // Register whatever this plugin needs. Called once, when the plugin is added — the App is
    // fully constructed by then, so the device, assets and scene are all available.
    virtual void build(App&) = 0;

protected:
    IPlugin() = default;
};

// An ordered set of plugins, added as one.
//
// `disable` is what makes a group more than a vector: a group is usually somebody else's list of
// defaults, and the thing you actually want is *those defaults, minus one*. Rebuilding the list by
// hand to drop a single plugin means silently missing whatever gets added to it next release.
class PluginGroup {
public:
    struct Entry {
        std::unique_ptr<IPlugin> plugin;
        bool                     enabled = true;
    };

    // Every mutator comes in two forms, and the pair is not decoration.
    //
    // A group holds unique_ptrs, so it can be MOVED but never copied. The chaining form —
    //   app.addPlugins(defaultPlugins().disable("Noisy"));
    // calls disable() on a TEMPORARY. If disable() returned a plain lvalue reference, that
    // expression would hand an lvalue to a by-value parameter, the compiler would reach for the
    // copy constructor, and there isn't one. The &&-qualified overload returns an rvalue instead,
    // so the group moves into addPlugins and the chain reads the way it should.
    PluginGroup&  add(std::unique_ptr<IPlugin> plugin) &  { doAdd(std::move(plugin)); return *this; }
    PluginGroup&& add(std::unique_ptr<IPlugin> plugin) && {
        doAdd(std::move(plugin));
        return std::move(*this);
    }

    template <typename T, typename... Args>
    PluginGroup& add(Args&&... args) & {
        return add(std::make_unique<T>(std::forward<Args>(args)...));
    }

    // Keep the plugin in the group but do not build it. Naming one that is not there is NOT an
    // error: a group you did not write is allowed to change between releases, and a hard failure
    // here would mean every version bump breaks the caller for no reason.
    PluginGroup&  disable(std::string_view name) &  { setEnabled(name, false); return *this; }
    PluginGroup&& disable(std::string_view name) && {
        setEnabled(name, false);
        return std::move(*this);
    }

    PluginGroup&  enable(std::string_view name) &  { setEnabled(name, true); return *this; }
    PluginGroup&& enable(std::string_view name) && {
        setEnabled(name, true);
        return std::move(*this);
    }

    [[nodiscard]] std::vector<Entry>& entries() { return m_plugins; }
    [[nodiscard]] usize size() const { return m_plugins.size(); }

private:
    void doAdd(std::unique_ptr<IPlugin> plugin) {
        if (plugin) m_plugins.push_back(Entry{std::move(plugin), true});
    }
    void setEnabled(std::string_view name, bool enabled) {
        for (Entry& e : m_plugins)
            if (e.plugin->name() == name) e.enabled = enabled;
    }

    std::vector<Entry> m_plugins;
};

}
