# Roller

A small, complete 3D game on Vortex — and the test of whether the engine is ready for one.

```bash
cmake --build build/relwithdebinfo --target roller
./build/relwithdebinfo/games/roller/roller
```

Roll a ball around a walled arena and collect every gem before the clock runs out.

| | |
|---|---|
| `WASD` / arrows | roll (camera-relative) |
| `R` | restart |
| `Esc` | quit |

```bash
VORTEX_ROLLER_CHECK=1 ./build/relwithdebinfo/games/roller/roller   # plays itself, fails if not won
```

## What it tests

Before this, every 3D example in the repo drove its own raw render loop — 300 to 600 lines
of swapchain, render-graph and shadow-pass plumbing per program — because `App` was
**2D-only**: it built a `SpriteBatch` and a colour pass and nothing else. The renderer
underneath was already capable (forward + deferred, PBR, cascaded shadows, SSAO, fog), but
a *game* built on the `App` loop could not reach any of it.

Roller is built on a new `App` 3D path instead. It draws the same way a 2D game does —
spawn entities, let the loop draw them:

```cpp
config.render3D = true;                 // App builds a MeshRenderer, a depth buffer, a sun+shadow pass

// Spawn entities; the loop draws them. Exactly how a 2D game spawns SpriteComp.
reg.emplace<ecs::Transform3D>(e, {.position = p, .scale = s});
reg.emplace<ecs::MeshComp>(e, {.mesh = ballMesh, .color = c, .roughness = 0.35f});

app.onFixedUpdate([&](App& a, f32 dt) { /* roll the ball, write its Transform3D */ });
app.onUi([&](App& a, SpriteBatch& batch) { /* draw the HUD */ });
```

The whole game is entities: floor, walls, gems and ball are `Transform3D + MeshComp`, and the
App loop extracts and draws them itself — the 3D twin of the 2D `SpriteComp` path. A collected
gem is `disable()`d, which drops it from the draw with no other bookkeeping. `onRender3D` stays
available for immediate-mode meshes, but this game needs none.

`camera3d()` and `lighting3d()` are plain state the loop reads each frame; the game moves the
camera and sets the sun on them. The 2D sprite/UI layers compose on top of the tone-mapped
3D scene, so a 3D world with a 2D HUD — which is most 3D games — is the default shape.

## What it exercises

Perspective follow camera, the engine's primitive meshes (plane, cube, sphere, torus),
per-instance PBR colour/metallic/roughness, a directional sun with real-time shadows, HDR +
ACES tone mapping, and a UTF-8 text HUD — all through the `App` loop, none of it hand-rolled.

## What it does not

No physics engine (the ball is kinematic, walls are a clamp-and-bounce), no meshes or
textures off disk, no skinned characters, no deferred lighting or SSAO. It is deliberately
the smallest thing that proves the path works; the renderer has far more than this uses.
