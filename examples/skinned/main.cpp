// Skinned, animated glTF: the Đợt A slice end to end.
//
//   assets/models/cato/cato.gltf  ->  assets::loadGltf
//     -> renderer::MeshData per primitive (positions, UVs, joint indices, weights)
//     -> anim::Skeleton  (41 joints: parent, inverse bind, bind pose)
//     -> anim::Clip x7   (Idle, Walk, Run, Think, ...)
//
// Each frame: the player advances a clock and samples the clip into a local pose; the
// skeleton folds that into one skinning matrix per joint; the renderer copies them into a
// storage buffer and the vertex shader blends each vertex by its four bones.
//
// Keys 1..7 switch clip. That is the "Animated Mesh Control" example, and it is also the
// honest test: if the pose pipeline is wrong, switching clips shows it immediately.

#include "vortex/anim/clip.hpp"
#include "vortex/anim/skeleton.hpp"
#include "vortex/asset/gltf.hpp"
#include "vortex/asset/image.hpp"
#include "vortex/core/log.hpp"
#include "vortex/platform/clock.hpp"
#include "vortex/platform/input.hpp"
#include "vortex/platform/window.hpp"
#include "vortex/renderer/camera.hpp"
#include "vortex/renderer/mesh.hpp"
#include "vortex/renderer/post_process.hpp"
#include "vortex/renderer/render_graph.hpp"
#include "vortex/renderer/skinning.hpp"
#include "vortex/rhi/command_list.hpp"
#include "vortex/rhi/device.hpp"
#include "vortex/rhi/swapchain.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

using namespace vortex;

namespace {

rhi::TextureHandle loadTexture(rhi::IGraphicsDevice& device, const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        VORTEX_WARN("App", "missing texture: %s", path.c_str());
        return {};
    }
    const auto size = in.tellg();
    in.seekg(0);
    std::vector<std::byte> bytes(static_cast<usize>(size));
    in.read(reinterpret_cast<char*>(bytes.data()), size);

    const assets::Image img = assets::decodeImage(bytes.data(), bytes.size());
    if (!img.valid()) {
        VORTEX_WARN("App", "cannot decode: %s", path.c_str());
        return {};
    }
    return device.createTexture({.width = img.width, .height = img.height,
                                 .debugName = "gltf_texture"},
                                img.pixels.data());
}

} // namespace

int main() {
    auto window    = pf::createWindow({.width = 1280, .height = 720, .title = "Vortex Skinned"});
    auto input     = pf::createInputProvider(*window);
    auto clock     = pf::createClock();
    auto device    = rhi::createDevice(*window);

    int fbw = 0, fbh = 0;
    window->getFramebufferSize(fbw, fbh);
    auto swapchain = device->createSwapchain(
        {.width = static_cast<u32>(fbw), .height = static_cast<u32>(fbh)}, *window);

    const rhi::Format swapFormat  = swapchain->format();
    const rhi::Format depthFormat = rhi::Format::D32_SFLOAT;
    const rhi::Format hdrFormat   = rhi::Format::R16G16B16A16_SFLOAT;
    constexpr u32     kShadowRes  = 2048;

    renderer::MeshRenderer mesh(*device, hdrFormat, depthFormat);
    renderer::PostProcess  post(*device, hdrFormat, swapFormat);
    renderer::RenderGraph  graph(*device);

    // --- Import ------------------------------------------------------------
    const std::string modelDir = "assets/models/cato/";
    std::string error;
    const assets::GltfModel model = assets::loadGltf((modelDir + "cato.gltf").c_str(), &error);
    if (!model.valid()) {
        VORTEX_ERROR("App", "glTF failed: %s", error.c_str());
        return 1;
    }
    VORTEX_INFO("App", "Loaded: %zu primitives, %zu joints, %zu animations",
                model.primitives.size(), model.skeleton.size(), model.animations.size());
    for (const anim::Clip& c : model.animations)
        VORTEX_INFO("App", "  clip '%s' (%.2fs)", c.name.c_str(), c.duration);

    // Upload each primitive with the material glTF asked for.
    // Which skinning backend is decided the way the graphics backend is: by the environment,
    // not by this code. VORTEX_SKINNING=cpu|gpu.
    auto skinner = renderer::createSkinner(mesh);

    struct Part {
        renderer::SkinHandle     skin;
        renderer::MaterialHandle material;
    };
    std::vector<Part> parts;
    std::vector<rhi::TextureHandle> textures(model.materials.size());

    for (usize i = 0; i < model.materials.size(); ++i) {
        const assets::GltfMaterial& m = model.materials[i];
        if (!m.baseColorTexture.empty())
            textures[i] = loadTexture(*device, modelDir + m.baseColorTexture);
    }

    for (const assets::GltfPrimitive& prim : model.primitives) {
        Part part;
        part.skin = skinner->addMesh(prim.mesh);

        renderer::MaterialDesc desc;
        if (prim.material >= 0 && static_cast<usize>(prim.material) < model.materials.size()) {
            const assets::GltfMaterial& m = model.materials[static_cast<usize>(prim.material)];
            desc.albedo      = textures[static_cast<usize>(prim.material)];
            desc.baseColor   = m.baseColor;
            desc.metallic    = m.metallic;
            desc.roughness   = m.roughness;
            desc.doubleSided = m.doubleSided;
        }
        part.material = mesh.createMaterial(desc);
        parts.push_back(part);
    }

    // A floor to catch the shadow — without one, a skinned shadow has nowhere to land and
    // there is no way to see whether the shadow pass is skinning too.
    const renderer::MeshHandle floorMesh = mesh.createMesh(renderer::makePlane(20.0f));
    const renderer::MaterialHandle floorMat = mesh.createMaterial(
        {.baseColor = {0.35f, 0.36f, 0.40f, 1.0f}, .metallic = 0.0f, .roughness = 0.9f});

    // A cutout panel: a solid quad whose texture is mostly holes. It is the test for whether
    // the shadow pass reads the material — a shadow pass that only looks at geometry casts the
    // shadow of the quad, and the holes never appear on the floor.
    constexpr u32 kGrate = 64;
    std::vector<u8> grate(static_cast<usize>(kGrate) * kGrate * 4);
    for (u32 y = 0; y < kGrate; ++y)
        for (u32 x = 0; x < kGrate; ++x) {
            const bool bar = (x % 16 < 5) || (y % 16 < 5);   // a lattice of bars
            const usize i = (static_cast<usize>(y) * kGrate + x) * 4;
            grate[i + 0] = 90; grate[i + 1] = 70; grate[i + 2] = 60;
            grate[i + 3] = bar ? 255 : 0;                    // the holes are alpha 0
        }
    const rhi::TextureHandle grateTex = device->createTexture(
        {.width = kGrate, .height = kGrate, .debugName = "grate"}, grate.data());

    const renderer::MeshHandle grateMesh = mesh.createMesh(renderer::makeQuad(2.4f, 2.0f));
    const renderer::MaterialHandle grateMat = mesh.createMaterial({
        .albedo = grateTex,
        .metallic = 0.0f, .roughness = 0.8f,
        .alphaCutoff = 0.5f,          // below this, the fragment is thrown away — in BOTH passes
        .doubleSided = true});

    // --- Animation ---------------------------------------------------------
    anim::Player player;
    usize clipIndex = 0;
    if (!model.animations.empty()) player.play(&model.animations[0]);

    std::vector<anim::Transform> pose;
    std::vector<Mat4>            skinning;

    renderer::Camera cam;
    cam.mode        = renderer::Camera::Mode::Perspective;
    cam.fovYRadians = 0.9f;
    cam.nearZ       = 0.1f;
    cam.farZ        = 100.0f;
    cam.target      = {0.0f, 1.0f, 0.0f};

    VORTEX_INFO("App", "Keys 1-%zu switch animation. ESC quits.", model.animations.size());

    const char* shotPath     = std::getenv("VORTEX_SCREENSHOT");
    const char* maxFramesEnv = std::getenv("VORTEX_MAX_FRAMES");
    const u64   maxFrames = maxFramesEnv ? std::strtoull(maxFramesEnv, nullptr, 10) : 0;
    const char* clipEnv   = std::getenv("VORTEX_CLIP");
    if (clipEnv != nullptr) {
        const anim::Clip* c = model.findClip(clipEnv);
        if (c != nullptr) player.play(c);
    }

    u64 frameCount = 0;
    int lastW = fbw, lastH = fbh;
    f32 t = 0.0f;

    std::vector<renderer::MeshInstance> instances;

    while (!window->shouldClose()) {
        clock->tick();
        input->newFrame();
        window->pollEvents();
        if (input->isKeyPressed(pf::Key::Escape)) break;
        if (maxFrames != 0 && frameCount >= maxFrames) break;

        // Fixed steps while capturing, so an A/B image diff measures the change under test
        // and not the fact that two runs reached the capture frame at different times.
        const f32 dt = shotPath != nullptr ? 1.0f / 60.0f
                                           : static_cast<f32>(clock->deltaTime());
        t += dt;

        const pf::Key digits[] = {pf::Key::Num1, pf::Key::Num2, pf::Key::Num3, pf::Key::Num4,
                                  pf::Key::Num5, pf::Key::Num6, pf::Key::Num7};
        for (usize i = 0; i < model.animations.size() && i < 7; ++i)
            if (input->isKeyPressed(digits[i]) && clipIndex != i) {
                clipIndex = i;
                player.play(&model.animations[i]);
                VORTEX_INFO("App", "clip: %s", model.animations[i].name.c_str());
            }

        int w = 0, h = 0;
        window->getFramebufferSize(w, h);
        if (w != lastW || h != lastH) {
            swapchain->requestResize(static_cast<u32>(w), static_cast<u32>(h));
            lastW = w; lastH = h;
        }
        if (w == 0 || h == 0) continue;

        // --- The three steps of a skinned frame ---
        player.update(dt);                                  // 1. advance the clock
        player.pose(model.skeleton, pose);                  // 2. sample -> a local pose
        model.skeleton.computeSkinningMatrices(pose, skinning);   // 3. -> skinning matrices

        cam.aspect   = static_cast<f32>(w) / static_cast<f32>(h);
        cam.position = {std::sin(t * 0.25f) * 4.5f, 2.2f, std::cos(t * 0.25f) * 4.5f};

        renderer::SceneLighting scene;
        scene.sun.direction = {-0.5f, -1.0f, -0.35f};
        scene.sun.intensity = 3.0f;
        scene.shadow.cascadeCount = 4;
        scene.shadow.maxDistance  = 20.0f;
        scene.shadow.resolution   = kShadowRes;

        rhi::FrameContext frame = device->beginFrame(*swapchain);
        if (!frame.valid) continue;

        instances.clear();
        instances.push_back({.mesh = floorMesh,
                             .model = Mat4::translation(0.0f, 0.0f, 0.0f),
                             .material = floorMat});
        instances.push_back({.mesh  = grateMesh,
                             .model = Mat4::translation(1.9f, 1.0f, -0.4f) *
                                      Mat4::rotationY(0.35f),
                             .material = grateMat});
        // Hand the pose to whichever backend is in play, then let it fill in the instance.
        // Nothing here knows or cares which one that is.
        for (const Part& part : parts) {
            skinner->setPose(part.skin, skinning.data(), static_cast<u32>(skinning.size()));

            renderer::MeshInstance inst;
            inst.model    = model.rootTransform;
            inst.material = part.material;
            skinner->apply(part.skin, inst);
            instances.push_back(inst);
        }

        mesh.begin(cam, scene);
        mesh.submit(instances.data(), instances.size());

        graph.beginFrame();
        const auto backbuffer = graph.importBackbuffer(frame.backbuffer, frame.width, frame.height);
        const auto sceneHdr   = graph.colorTarget("scene_hdr", frame.width, frame.height, hdrFormat);
        const auto sceneDepth = graph.depthTarget("scene_depth", frame.width, frame.height);
        const auto shadowMap  = graph.depthTarget("shadow_map", kShadowRes, kShadowRes, true);

        const f32 clear[4] = {0.02f, 0.03f, 0.05f, 1.0f};
        const rhi::Viewport vp{.x = 0.0f, .y = 0.0f,
                               .width = static_cast<f32>(frame.width),
                               .height = static_cast<f32>(frame.height)};

        graph.addPass("shadow",
            [&](renderer::RenderGraph::PassBuilder& b) { b.writeDepth(shadowMap); },
            [&](rhi::ICommandList& cmd) { mesh.renderShadow(cmd); });

        mesh.setShadowMap(graph.texture(shadowMap));

        graph.addPass("mesh",
            [&](renderer::RenderGraph::PassBuilder& b) {
                b.sample(shadowMap);
                b.writeColor(sceneHdr, clear);
                b.writeDepth(sceneDepth);
            },
            [&](rhi::ICommandList& cmd) {
                cmd.setViewport(vp);
                cmd.setScissor(0, 0, frame.width, frame.height);
                mesh.renderSkybox(cmd);
                mesh.end(cmd);
            });

        post.addPasses(graph, sceneHdr, backbuffer, frame.width, frame.height);

        graph.execute(*frame.cmd);
        device->endFrame();
        ++frameCount;

        if (shotPath != nullptr && frameCount == 30) {
            std::vector<u8> px(static_cast<usize>(frame.width) * frame.height * 4);
            device->readTexture(frame.backbuffer, px.data());
            if (std::FILE* f = std::fopen(shotPath, "wb")) {
                std::fprintf(f, "P6\n%u %u\n255\n", frame.width, frame.height);
                for (usize i = 0; i < px.size(); i += 4) {
                    const u8 rgb[3] = {px[i + 2], px[i + 1], px[i]};   // backbuffer is BGRA
                    std::fwrite(rgb, 1, 3, f);
                }
                std::fclose(f);
                VORTEX_INFO("App", "Wrote screenshot: %s", shotPath);
            }
            break;
        }
    }

    device->waitIdle();
    swapchain.reset();
    return 0;
}
