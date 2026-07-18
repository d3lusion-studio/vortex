// Load a glTF model and orbit the camera around it — the App-loop version of Bevy's
// "Load glTF" / "Scene Viewer", and the proof that a model can reach an App-based 3D game.
//
// The model is spawned as Transform3D + MeshComp entities by app::loadModel; the render3D
// loop draws them, the same path roller's primitives go through. Nothing here touches the
// MeshRenderer, a render graph or a shadow pass by hand.
//
// VORTEX_SCENEVIEWER_CHECK=1 loads the model, asserts the import produced geometry and
// materials, and exits non-zero if it did not. Needs a GPU, like every App example.

#include "vortex/app/app.hpp"
#include "vortex/app/model_loader.hpp"
#include "vortex/core/log.hpp"
#include "vortex/core/math/math.hpp"
#include "vortex/ecs/components.hpp"
#include "vortex/ecs/registry.hpp"
#include "vortex/platform/input.hpp"
#include "vortex/renderer/camera.hpp"
#include "vortex/renderer/mesh.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace vortex;

#ifndef VORTEX_MODEL_DIR
#define VORTEX_MODEL_DIR "assets/models"
#endif

namespace {

struct Viewer {
    app::LoadedModel model;
    f32              orbit  = 0.0f;
    bool             checkMode = false;

    std::string shotPath;
    i32         shotFrame = 0;
    i32         shotAt    = 60;
};

}   // namespace

int main() {
    app::AppConfig config;
    config.title      = "Vortex Scene Viewer";
    config.width      = 1280;
    config.height     = 720;
    config.clearColor = Color::fromRgb(0x20242E);
    config.render3D   = true;
    if (const char* frames = std::getenv("VORTEX_MAX_FRAMES"))
        config.maxFrames = std::strtoull(frames, nullptr, 10);

    app::App app(config);
    Viewer   viewer;
    viewer.checkMode = std::getenv("VORTEX_SCENEVIEWER_CHECK") != nullptr;
    if (const char* shot = std::getenv("VORTEX_SCREENSHOT")) viewer.shotPath = shot;
    if (const char* at = std::getenv("VORTEX_SHOT_FRAME")) viewer.shotAt = std::atoi(at);

    app.onStart([&viewer](app::App& a) {
        // Ground, so the model has something to cast a shadow onto and does not float in a void.
        if (renderer::MeshRenderer* mesh = a.mesh3d()) {
            const renderer::MeshHandle floor = mesh->createMesh(renderer::makePlane(12.0f));
            const ecs::Entity e = a.registry().create();
            a.registry().emplace<ecs::Transform3D>(e, ecs::Transform3D{});
            a.registry().emplace<ecs::MeshComp>(e, ecs::MeshComp{
                .mesh = floor, .color = Color::fromRgb(0x3A4658), .roughness = 0.9f});
        }

        const std::string path = std::string(VORTEX_MODEL_DIR) + "/cato/cato.gltf";
        viewer.model = app::loadModel(a, path.c_str(), {0.0f, 0.0f, 0.0f}, 1.0f);
        if (!viewer.model.ok())
            VORTEX_ERROR("Viewer", "No model loaded — is %s present?", path.c_str());

        renderer::SceneLighting& lit = a.lighting3d();
        lit.sun.direction  = normalize(Vec3{-0.4f, -1.0f, -0.5f});
        lit.sun.intensity  = 3.0f;
        lit.sun.ambient    = {0.35f, 0.4f, 0.5f};
        lit.shadow.enabled = true;

        if (!viewer.model.animations.empty()) {
            std::string names;
            for (const std::string& n : viewer.model.animations) names += " " + n;
            VORTEX_INFO("Viewer", "Model has %zu clips:%s (static load — not played yet)",
                        viewer.model.animations.size(), names.c_str());
        }
    });

    app.onFixedUpdate([&viewer](app::App& a, f32 dt) {
        f32 turn = 0.0f;
        if (!viewer.checkMode) {
            pf::IInputProvider& in = a.input();
            if (in.isKeyDown(pf::Key::A) || in.isKeyDown(pf::Key::Left))  turn -= 1.0f;
            if (in.isKeyDown(pf::Key::D) || in.isKeyDown(pf::Key::Right)) turn += 1.0f;
        }
        // Steered by input; drifts on its own only when the player is NOT holding a key, so a
        // held angle stays put but an untouched viewer still shows the model turning.
        viewer.orbit += (turn != 0.0f ? turn * 1.5f : 0.5f) * dt;

        // Frame the model: orbit at a fixed radius, looking at chest height.
        renderer::Camera& cam = a.camera3d();
        const f32 r = 5.0f;
        cam.position    = {std::sin(viewer.orbit) * r, 2.6f, std::cos(viewer.orbit) * r};
        cam.target      = {0.0f, 1.2f, 0.0f};
        cam.fovYRadians = 0.9f;

        if (viewer.checkMode) a.quit();
    });

    app.onUpdate([&viewer](app::App& a, f32) {
        if (!viewer.shotPath.empty() && ++viewer.shotFrame >= viewer.shotAt) {
            a.requestScreenshot(viewer.shotPath);
            viewer.shotPath.clear();
        }
        if (!viewer.checkMode && a.input().isKeyPressed(pf::Key::Escape)) a.quit();
    });

    const int rc = app.run();

    if (viewer.checkMode) {
        const bool ok = viewer.model.ok() && viewer.model.primitiveCount > 0 &&
                        viewer.model.materialCount > 0;
        std::printf("\n[%s] Scene-viewer self-check: %u primitive(s), %u material(s), "
                    "%u texture(s), %zu animation(s)\n",
                    ok ? "PASS" : "FAIL", viewer.model.primitiveCount, viewer.model.materialCount,
                    viewer.model.textureCount, viewer.model.animations.size());
        return ok ? 0 : 1;
    }
    return rc;
}
