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
#include "vortex/anim/curve.hpp"
#include "vortex/anim/graph.hpp"
#include "vortex/anim/pose.hpp"
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

// Filled in by CMake with an absolute path, so the example runs from any working directory
// rather than only from the repo root. The relative fallback keeps a standalone build working.
#ifndef VORTEX_MODEL_DIR
#define VORTEX_MODEL_DIR "assets/models"
#endif

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
    const std::string modelDir = std::string(VORTEX_MODEL_DIR) + "/cato/";
    std::string error;
    assets::GltfModel model = assets::loadGltf((modelDir + "cato.gltf").c_str(), &error);
    if (!model.valid()) {
        VORTEX_ERROR("App", "glTF failed: %s", error.c_str());
        return 1;
    }
    VORTEX_INFO("App", "Loaded: %zu primitives, %zu joints, %zu animations",
                model.primitives.size(), model.skeleton.size(), model.animations.size());
    for (const anim::Clip& c : model.animations)
        VORTEX_INFO("App", "  clip '%s' (%.2fs)", c.name.c_str(), c.duration);

    // Footstep events. glTF has nowhere to put these, so they are authored here — but they are
    // attached to the CLIP, not to the game code, because "when does the foot land" is a fact
    // about the animation. Put it in the gameplay and it rots the moment the walk is retimed.
    for (anim::Clip& c : model.animations) {
        if (c.name == "Walk" || c.name == "Run") {
            c.addEvent(c.duration * 0.25f, "footstep.left");
            c.addEvent(c.duration * 0.75f, "footstep.right");
        }
    }

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

    // --- Wave C: morph targets ---------------------------------------------
    //
    // A morph target is the mesh as a DIFFERENCE — what to add to each vertex to reach another
    // shape. Two of them here; their weights are animated independently, and because they are
    // deltas, both can be half-on at once and the result is a shape that is neither.
    renderer::MeshData blob = renderer::makeSphere(24, 32, 0.6f);
    {
        renderer::MorphTarget tall;
        tall.name = "tall";
        renderer::MorphTarget spiky;
        spiky.name = "spiky";
        for (const renderer::MeshVertex& v : blob.vertices) {
            // Stretch along Y and pinch in X/Z: an egg.
            tall.positions.push_back({-v.position.x * 0.45f, v.position.y * 1.3f,
                                      -v.position.z * 0.45f});
            tall.normals.push_back({0.0f, 0.0f, 0.0f});

            // Push every vertex out along its normal, but only on alternating bands — spikes.
            const f32 band = std::sin(v.position.y * 22.0f);
            spiky.positions.push_back(v.normal * (0.35f * band));
            spiky.normals.push_back({0.0f, 0.0f, 0.0f});
        }
        blob.morphTargets.push_back(std::move(tall));
        blob.morphTargets.push_back(std::move(spiky));
    }

    // The morphing mesh goes through the SKINNER, not straight to the renderer — that is what
    // makes the CPU and GPU backends both able to do it, and each other's test.
    auto blobSkinner = renderer::createSkinner(mesh);
    const renderer::SkinHandle blobSkin = blobSkinner->addMesh(blob);
    const renderer::MaterialHandle blobMat = mesh.createMaterial(
        {.baseColor = {0.85f, 0.45f, 0.3f, 1.0f}, .metallic = 0.1f, .roughness = 0.35f});

    // The weights are just curves. Nothing about morphing needs its own animation system.
    anim::Curve<f32> tallWeight;
    tallWeight.loop = true;
    tallWeight.add(0.0f, 0.0f).add(1.5f, 1.0f, easing::Ease::InOutCubic).add(3.0f, 0.0f,
                                                                            easing::Ease::InOutCubic);
    anim::Curve<f32> spikyWeight;
    spikyWeight.loop = true;
    spikyWeight.add(0.0f, 0.0f).add(0.75f, 1.0f, easing::Ease::OutBack)
               .add(2.25f, 0.0f, easing::Ease::InOutSine).add(3.0f, 0.0f);

    // --- Wave C: a curve-driven Transform ----------------------------------
    //
    // The same Curve, over a different type. This is the whole of "Animated Transform" — and of
    // "Animated UI", which is this exact code pointed at an opacity instead of a position.
    const renderer::MeshHandle cubeMesh = mesh.createMesh(renderer::makeCube(0.5f));
    const renderer::MaterialHandle cubeMat = mesh.createMaterial(
        {.baseColor = {0.4f, 0.7f, 0.9f, 1.0f}, .metallic = 0.2f, .roughness = 0.4f});

    anim::Curve<Vec3> hop;
    hop.loop   = true;
    hop.interp = anim::CurveInterp::CatmullRom;   // a spline: no corner at the waypoints
    hop.add(0.0f, {-2.6f, 0.30f, 1.4f})
       .add(1.0f, {-2.6f, 1.20f, 1.4f}, easing::Ease::OutQuad)   // ease out of the launch
       .add(2.0f, {-2.6f, 0.30f, 1.4f}, easing::Ease::InQuad)    // ease into the landing
       .add(3.0f, {-2.6f, 0.30f, 1.4f});

    anim::Curve<Quat> spin;
    spin.loop = true;
    spin.add(0.0f, Quat::identity())
        .add(1.5f, Quat::fromAxisAngle({0, 1, 0}, 3.14159f))
        .add(3.0f, Quat::fromAxisAngle({0, 1, 0}, 6.28318f));

    // --- Wave C: colour, in four spaces ------------------------------------
    //
    // The same two colours, the same t, four answers. Red to green through LINEAR RGB dips
    // through a dark olive, because the straight line between them in RGB passes below both in
    // perceived lightness. Oklab is built so that a straight line looks straight to a human, and
    // stays bright the whole way. This is what "which colour space" costs you.
    const renderer::MeshHandle swatchMesh = mesh.createMesh(renderer::makeSphere(16, 24, 0.22f));
    const renderer::MaterialHandle swatchMat = mesh.createMaterial(
        {.baseColor = {1.0f, 1.0f, 1.0f, 1.0f}, .metallic = 0.0f, .roughness = 0.55f});
    const Color colorA = Color::fromRgb(0xE03030);   // red
    const Color colorB = Color::fromRgb(0x30C040);   // green
    const anim::ColorSpace spaces[4] = {anim::ColorSpace::LinearRgb, anim::ColorSpace::Srgb,
                                        anim::ColorSpace::Oklab, anim::ColorSpace::Hsv};

    // --- Animation graph ---------------------------------------------------
    //
    //            maskNode  (root)          legs from the crossfade, upper body from Think
    //           /        \
    //     blendNode      overlay(Think)
    //     /      \
    //  from      to                        the crossfade: `to` is what you asked for
    //
    // The crossfade owns the bottom half; the mask layer is stacked on its root. That is the
    // point of a tree — a layer does not have to know what it is layered over.
    anim::CrossFade cross;
    cross.play(&model.animations[0], 0.0f);   // first clip: nothing to fade from

    anim::BlendTree& tree = cross.tree();

    // Everything from the spine up: neck, head, both arms. Named by ONE joint, because the
    // skeleton already knows what hangs off it — a hand-written list of joints goes stale the
    // first time the rig changes.
    const anim::JointMask upperBody = anim::JointMask::subtree(model.skeleton, "Spine");

    const anim::Clip* thinkClip = model.findClip("Think");
    const auto overlayNode = tree.addClip(thinkClip);
    const auto maskNode    = tree.addMask(tree.root(), overlayNode, upperBody, 0.0f);
    tree.setRoot(maskNode);

    usize clipIndex = 0;
    bool  maskOn    = std::getenv("VORTEX_MASK") != nullptr;
    tree.setWeight(maskNode, maskOn ? 1.0f : 0.0f);

    anim::Pose        pose;
    std::vector<Mat4> skinning;

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
        if (c != nullptr) cross.play(c, 0.0f);
    }

    // Deterministic crossfade capture: switch clip at a given frame, so the screenshot at
    // frame 30 lands part-way through the fade. A blended pose that matches neither endpoint
    // is the only proof that anything is actually being blended.
    const char* switchTo  = std::getenv("VORTEX_SWITCH_TO");
    const u64   switchAt  = std::getenv("VORTEX_SWITCH_AT")
                          ? std::strtoull(std::getenv("VORTEX_SWITCH_AT"), nullptr, 10) : 0;
    const f32   fadeTime  = std::getenv("VORTEX_FADE")
                          ? static_cast<f32>(std::atof(std::getenv("VORTEX_FADE"))) : 0.25f;

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

        // Fixed steps whenever this is a test run (a capture, or a capped frame count), so an
        // A/B diff measures the change under test rather than the fact that two runs reached
        // the same frame at slightly different times.
        const bool deterministic = shotPath != nullptr || maxFrames != 0;
        const f32  dt = deterministic ? 1.0f / 60.0f
                                      : static_cast<f32>(clock->deltaTime());
        t += dt;

        const pf::Key digits[] = {pf::Key::Num1, pf::Key::Num2, pf::Key::Num3, pf::Key::Num4,
                                  pf::Key::Num5, pf::Key::Num6, pf::Key::Num7};
        for (usize i = 0; i < model.animations.size() && i < 7; ++i)
            if (input->isKeyPressed(digits[i]) && clipIndex != i) {
                clipIndex = i;
                cross.play(&model.animations[i], fadeTime);   // fade, do not snap
                VORTEX_INFO("App", "clip: %s", model.animations[i].name.c_str());
            }

        if (input->isKeyPressed(pf::Key::M)) {
            maskOn = !maskOn;
            tree.setWeight(maskNode, maskOn ? 1.0f : 0.0f);
            VORTEX_INFO("App", "upper-body overlay: %s", maskOn ? "on" : "off");
        }

        if (switchTo != nullptr && frameCount == switchAt) {
            const anim::Clip* c = model.findClip(switchTo);
            if (c != nullptr) cross.play(c, fadeTime);
        }

        int w = 0, h = 0;
        window->getFramebufferSize(w, h);
        if (w != lastW || h != lastH) {
            swapchain->requestResize(static_cast<u32>(w), static_cast<u32>(h));
            lastW = w; lastH = h;
        }
        if (w == 0 || h == 0) continue;

        // --- The three steps of a skinned frame ---
        cross.update(dt);                                   // 1. advance every clock in the tree
        cross.pose(model.skeleton, pose);                   // 2. evaluate the tree -> a pose
        model.skeleton.computeSkinningMatrices(pose, skinning);   // 3. -> skinning matrices

        // Events fire only from clips that are actually being heard: a clip faded down to
        // nothing keeps ticking (it must, or it would jump when faded back in) but stays quiet.
        for (const anim::BlendTree::Fired& f : cross.firedEvents())
            VORTEX_INFO("Event", "frame %llu  t=%.2fs  '%s'  (weight %.2f)",
                        static_cast<unsigned long long>(frameCount), t,
                        f.event->name.c_str(), f.weight);

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

        // Morph: two weights, two curves. The mesh's SHAPE is animated; nothing is posed.
        const f32 morphWeights[2] = {tallWeight.evaluate(t), spikyWeight.evaluate(t)};
        blobSkinner->setMorphWeights(blobSkin, morphWeights, 2);
        blobSkinner->setPose(blobSkin, nullptr, 0);   // no skeleton: shape only

        renderer::MeshInstance blobInst;
        blobInst.model    = Mat4::translation(-2.0f, 0.75f, -0.6f);
        blobInst.material = blobMat;
        blobSkinner->apply(blobSkin, blobInst);
        instances.push_back(blobInst);

        // A Transform driven by curves: a Catmull-Rom path with easing, and a rotation.
        instances.push_back({.mesh  = cubeMesh,
                             .model = Mat4::translation(hop.evaluate(t).x, hop.evaluate(t).y,
                                                        hop.evaluate(t).z) *
                                      spin.evaluate(t).toMat4(),
                             .material = cubeMat});

        // Four swatches, one per colour space, all at the same t. They must not agree — that
        // disagreement IS the feature.
        const f32 ct = 0.5f - 0.5f * std::cos(t * 1.2f);   // sweep 0 -> 1 -> 0
        for (int i = 0; i < 4; ++i) {
            const Color c = anim::mixColor(colorA, colorB, ct, spaces[i]);
            instances.push_back({.mesh  = swatchMesh,
                                 .model = Mat4::translation(-1.05f + 0.7f * static_cast<f32>(i),
                                                            0.25f, 2.2f),
                                 .color = {c.r, c.g, c.b, 1.0f},
                                 .material = swatchMat});
        }
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
