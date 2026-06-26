# Vortex — Architecture

## Layer Diagram

Dependencies are **strictly one-directional downward**. An upper layer may include headers from any lower layer; a lower layer must never include anything from a layer above it.

```
┌──────────────────────────────────────────────────────────────┐
│  Game / Application  (viết bằng gameplay API thuần C++)       │
├──────────────────────────────────────────────────────────────┤
│  Gameplay Framework  (Scene, Entity API, Prefab, Game Loop)   │
├───────────────┬───────────────────────┬──────────────────────┤
│  ECS Core     │  Scene System         │  Input (high-level)  │
│  (registry,   │  (transform tree,     │  (action mapping)    │
│   systems)    │   scene lifecycle)    │                      │
├───────────────┴───────────┬───────────┴──────────────────────┤
│  Renderer 2D              │  Asset / Resource System          │
│  (Batch, Camera, Material,│  (handle table, importers,        │
│   RenderGraph)            │   hot reload)                     │
├──────────────────────────────────────────────────────────────┤
│  RHI — Render Hardware Interface  ◄── abstraction boundary   │
│  (IGraphicsDevice, ICommandList, ISwapchain, handles)         │
├──────────────────────────────────────────────────────────────┤
│  Job System  │  Memory (arena, pool, frame allocator)         │
├──────────────────────────────────────────────────────────────┤
│  Platform Layer  ◄── abstraction boundary                     │
│  (IWindow, IInputProvider, IClock, IFileSystem)               │
├──────────────────────────────────────────────────────────────┤
│  Core / Foundation                                            │
│  (types, math, containers, log, assert, handle, allocators)   │
├──────────────────────────────────────────────────────────────┤
│  Backends  (selected at build time / runtime)                 │
│   • rhi/vulkan    • rhi/dx12*    • rhi/webgpu*                │
│   • platform/glfw • platform/sdl*                             │
└──────────────────────────────────────────────────────────────┘
                  (* planned, interface already designed)
```

## Abstraction Boundaries — Two Inviolable Rules

1. **Nothing above `rhi/` may include `vulkan.h`.**
   The renderer and all layers above communicate only through `rhi::IGraphicsDevice`, `rhi::ICommandList`, and opaque handle types (`rhi::BufferHandle`, etc.).

2. **Nothing above `platform/` may include `glfw.h`.**
   Engine code sees only `pf::IWindow` and `pf::IInputProvider`. Native pointer types (`GLFWwindow*`) never cross this boundary — only `void*` opaque handles used to create the Vulkan surface.

CI enforces both rules with a grep check on every pull request.

## Module Reference

### `core/`
No dependencies beyond the C++ standard library.

| Header | Contents |
|---|---|
| `types.hpp` | `i8..i64`, `u8..u64`, `f32`, `f64`, `usize` |
| `math/` | `Vec2/3/4`, `Mat3/4`, `Quat`, `Rect`, `Transform2D`, ortho projection |
| `handle.hpp` | `Handle<Tag> = {u32 index, u32 generation}` — universal resource reference |
| `log.hpp` | `log(level, category, fmt, ...)`, macros `VORTEX_INFO/WARN/ERROR` |
| `assert.hpp` | `VORTEX_ASSERT(cond, msg)` — active in Debug & RelWithDebInfo builds |
| `containers/` | `Array<T>`, `HashMap<K,V>`, `SparseSet<T>` |
| `memory/` | Arena, pool, stack, frame allocator |
| `string_id.hpp` | FNV-1a hashed string constant (`StringId`) |
| `event_bus.hpp` | Typed publish/subscribe (decoupled subsystem communication) |
| `profiler.hpp` | Scope macros; connects to Tracy when available |

### `platform/`
Pure abstract interfaces for OS/window/input dependencies.

| Interface | Role |
|---|---|
| `IWindow` | Create window, `pollEvents`, `shouldClose`, framebuffer size, opaque native handles |
| `IInputProvider` | Key/mouse/gamepad state + edge detection (`newFrame()`) |
| `IClock` | High-resolution time, delta time |
| `IFileSystem` | Read/write files; virtual mount points |

`platform/glfw/` — the **sole** file tree that includes GLFW headers.

### `rhi/`
Graphics hardware abstraction. Style: **WebGPU-like** (explicit, immutable pipeline state objects, bind groups). Maps cleanly to Vulkan, DX12, Metal, and WebGPU backends.

| Interface / Type | Role |
|---|---|
| `IGraphicsDevice` | Create & destroy buffers, textures, samplers, pipelines, bind groups; submit, present |
| `ICommandList` | `beginRenderPass`, `setPipeline`, `setBindGroup`, `draw`, `drawIndexed`, push constants |
| `ISwapchain` | Swapchain lifetime & resize |
| `BufferHandle` / `TextureHandle` / … | Opaque 32-bit handles — never raw GPU pointers |
| `rhi::Format`, `rhi::BufferUsage`, … | Engine-side enums — no Vulkan/DX12 enum leakage |

`rhi/vulkan/` — the **sole** file tree that includes Vulkan headers.

### `renderer/`
High-level 2D rendering built exclusively on `rhi/`. Zero Vulkan knowledge.

| Component | Role |
|---|---|
| `Camera2D` | Ortho view + projection, pan/zoom/rotate, world↔screen helpers |
| `SpriteRenderer` | `drawSprite(tex, transform, color, uvRect)` — pushes quads to batch |
| `BatchRenderer` | Groups quads by material; ring-buffered dynamic vertex upload; one draw call per batch |
| `Material` | Pipeline handle + bind group + uniform buffer; defines batch boundary |
| `RenderGraphLite` | Ordered pass list (Phase 3–8); upgrades to full frame graph in Phase 9 |

### `ecs/`
Entity/component storage and systems.

- Components are **pure POD** — no virtual functions, no game logic.
- Storage is cache-friendly (sparse-set via EnTT, or archetype layout).
- `RenderExtractSystem` reads `(WorldTransform2D + SpriteComp)` and writes a flat `RenderItem[]` array. This array is the only data the renderer consumes — it never touches the ECS registry.

### `scene/`
- `Scene` — owns a registry, ordered system list, and active camera.
- `TransformHierarchy` — maintains `WorldTransform2D` cache via dirty-flag propagation; always processes parent before child.

### `assets/` + `resource/`
Two distinct responsibilities:

| Layer | Runs | Does |
|---|---|---|
| Asset importer | Offline / build time | PNG → texture data; Tiled JSON → binary tilemap; font → atlas |
| Resource manager | Runtime | Deserialise → GPU resource; handle table with generation counters; defer-destroy |

Hot reload: file watcher triggers re-import; handle value is preserved; old GPU resource destroyed after `frames-in-flight` frames.

### `input/`
Action mapping above `IInputProvider`. Gameplay code polls `input.action("Jump").pressed()` and never deals with raw `pf::Key` values. Rebinding is data-driven.

### `jobs/`
Work-stealing thread pool. Used for parallel extraction, culling, multithread command recording, async asset loading.

```cpp
jobs::parallelFor(0, N, [&](int i){ extract(items[i]); });
```

### `debug/`
- `DebugDraw` — `line`, `box`, `circle`, `text` in world space; toggleable per category.
- `ImGuiLayer` (optional) — entity/component inspector, profiler overlay.

---

## Render Pipeline (per frame)

```
1. Input system reads IInputProvider → action state
2. Gameplay systems update ECS (movement, AI, spawn…)
3. Physics step → writes Transform2D
4. TransformSystem → recomputes WorldTransform2D (parent-first)
5. Animation system → updates SpriteComp UV frame
6. CullingSystem → marks visible entities  [parallel]
7. RenderExtractSystem → writes RenderItem[]  [parallel]
         ──── game thread / render thread boundary ────
8. BatchRenderer consumes RenderItem[] → builds vertex batches
9. RenderGraph executes passes → ICommandList draw calls
10. IGraphicsDevice::submit + present
```

`RenderItem[]` is double-buffered: the render thread draws frame N while the game thread extracts frame N+1.

---

## 3D Extension Points

Every architectural decision was made so that 3D is an **addition**, not a rewrite.

| Current decision | Why it future-proofs 3D |
|---|---|
| Transforms use `mat4` + `quat` | No 2x3 lock-in; 3D uses same types |
| Camera returns `view()` / `projection()` | Perspective camera only differs in `projection()` |
| Material = shader + params + textures | PBR adds parameters/textures; struct stays the same |
| `RenderItem` is generic | Mesh draw is another `RenderItem` subtype |
| RenderGraph is multi-pass | Shadow / GBuffer / lighting are extra passes |
| RHI `PipelineDesc` has depth, cull mode, MSAA | 3D pipeline state already modelled |
| `IGraphicsDevice::createDevice(api, …)` factory | Swap backend without touching renderer code |
