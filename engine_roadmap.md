# Roadmap Phát Triển 2D Game Engine (Code-Only, C++20/23)

> **Mục tiêu tài liệu:** Bản thiết kế + timeline + task breakdown để một developer (hoặc team nhỏ) có thể tự xây một game engine 2D hiệu năng cao, kiến trúc backend-neutral, mở rộng được sang 3D, theo hướng code-only như Godot nhưng có rendering architecture học hỏi từ Unreal.
>
> **Giả định lập kế hoạch:** Solo dev / part-time ~15–20h/tuần, có nền C++ tốt nhưng đang học Vulkan/ECS song song. Các con số thời gian là *khoảng ước lượng*, không phải cam kết. Toàn bộ timeline xoay quanh nguyên tắc: **làm thứ chạy được trước, tối ưu sau, không over-engineer khi chưa có dữ liệu**.

---

## Mục lục

1. [Vision tổng quan](#1-vision-tổng-quan)
2. [Kiến trúc tổng thể](#2-kiến-trúc-tổng-thể)
3. [Thiết kế module thay thế được backend](#3-thiết-kế-module-thay-thế-được-backend)
4. [Timeline theo giai đoạn](#4-timeline-theo-giai-đoạn)
5. [Task breakdown chi tiết](#5-task-breakdown-chi-tiết)
6. [Kiến trúc rendering](#6-kiến-trúc-rendering)
7. [ECS + DOD design](#7-ecs--dod-design)
8. [Developer experience](#8-developer-experience)
9. [Performance roadmap](#9-performance-roadmap)
10. [Roadmap mở rộng 3D](#10-roadmap-mở-rộng-3d)
11. [Deliverable cuối cùng](#11-deliverable-cuối-cùng)

---

## 1. Vision tổng quan

### 1.1. Mục tiêu dài hạn

Xây một engine mà **bạn sở hữu hoàn toàn từ tầng phần cứng đồ họa lên tới API gameplay**, với 4 tính chất cốt lõi:

- **Hiệu năng cao:** hot path (transform, culling, batching, physics, particle) chạy theo data-oriented, cache-friendly, song song hóa được.
- **Tốc độ phát triển nhanh:** viết một game nhỏ trong vài chục dòng code, không cần boilerplate. Workflow gọn như Godot (tạo scene, spawn entity, gắn component, chạy).
- **Scale mạnh:** từ prototype 1 file tới game thương mại nhiều scene, nhiều nghìn entity, asset streaming.
- **Backend-neutral:** Vulkan và GLFW chỉ là *một implementation*. Có thể thay bằng DX12/Metal/WebGPU và SDL/Win32 mà không sửa code gameplay hay renderer cao cấp.

### 1.2. Phạm vi: 2D trước, 3D sau

Engine **không** cố làm 3D ngay. Nhưng mọi quyết định kiến trúc lớn phải vượt được "bài test 3D": *nếu mai phải thêm mesh renderer + PBR + shadow, mình có phải viết lại tầng này không?*

- **Giai đoạn 2D (Phase 0–8):** sprite, batch, camera ortho, tilemap, text, UI, audio, physics 2D. Đây là sản phẩm thật, ship được game indie.
- **Giai đoạn cầu nối (Phase 9):** refactor những điểm 2D-only thành dạng tổng quát (render graph, material system, camera abstraction).
- **Giai đoạn 3D (sau roadmap này):** mesh, lighting, shadow, scene graph 3D — tái sử dụng RHI, ECS, asset, job system đã có.

Triết lý: **2D là một trường hợp đặc biệt của 3D với z phẳng và pipeline đơn giản.** Nếu thiết kế đúng, 3D là *mở rộng*, không phải *làm lại*.

### 1.3. Triết lý thiết kế

| Nguyên tắc | Ý nghĩa thực thi |
|---|---|
| **Layered & one-way dependency** | Tầng trên biết tầng dưới, tầng dưới *không bao giờ* biết tầng trên. Gameplay → Renderer → RHI → Backend. |
| **Abstraction nơi cần đổi, cụ thể nơi cần nhanh** | OOP/interface ở ranh giới (RHI, platform, asset loader). DOD ở hot loop. Không bọc abstraction cho thứ chạy 100k lần/frame. |
| **Handle, không phải pointer** | Tài nguyên (texture, buffer, pipeline, entity) tham chiếu qua handle `{index, generation}`, không raw pointer. An toàn, hỗ trợ hot reload & GPU lifetime. |
| **Data-driven hơn code-driven** | Asset, scene, config nạp từ data. Code định nghĩa *behavior*, data định nghĩa *content*. |
| **Đo trước khi tối ưu** | Mỗi quyết định perf phải có profiler/benchmark chống lưng. Không tối ưu theo cảm tính. |
| **Frame là đơn vị bộ nhớ** | Phần lớn allocation trong 1 frame dùng linear/arena allocator, reset cuối frame. Hạn chế `new`/`delete` trong hot path. |
| **Tách game world và render world** | ECS mô tả thế giới logic. Mỗi frame "extract" ra danh sách render item phẳng cho renderer. Cho phép tách game thread / render thread như Unreal. |

---

## 2. Kiến trúc tổng thể

### 2.1. Sơ đồ tầng (dependency đi xuống một chiều)

```
┌──────────────────────────────────────────────────────────────┐
│  Game / Application  (code-only, viết bằng API gameplay)       │
├──────────────────────────────────────────────────────────────┤
│  Gameplay Framework: Scene, Entity API, Prefab, Game Loop      │
├───────────────┬───────────────────────┬──────────────────────┤
│  ECS Core     │  Scene System         │  Input System (high)  │
│  (registry,   │  (scene graph,        │  (action mapping)     │
│   systems)    │   transform tree)     │                       │
├───────────────┴───────────┬───────────┴──────────────────────┤
│  Renderer 2D (high-level): SpriteRenderer, BatchRenderer,     │
│  Camera2D, Material, RenderPass/RenderGraph (lite)            │
├──────────────────────────────────────────────────────────────┤
│  RHI — Render Hardware Interface (abstract)                    │
│  GraphicsDevice, Swapchain, CommandList, Buffer, Texture,     │
│  Pipeline, BindGroup, Sampler  ── KHÔNG chứa vulkan.h         │
├───────────────┬──────────────────────────────────────────────┤
│  Asset System │  Resource Manager  │  Job System  │  Memory   │
│  (loaders,    │  (handle table,    │  (thread     │  (arena,  │
│   import)     │   ref/lifetime)    │   pool)      │  pool)    │
├───────────────┴──────────────────────────────────────────────┤
│  Platform Layer (abstract): IWindow, IInputProvider,          │
│  IFileSystem, IClock  ── KHÔNG chứa glfw.h                    │
├──────────────────────────────────────────────────────────────┤
│  Core / Foundation: types, math, containers, log, assert,     │
│  profiling, event bus, string id, allocators                  │
├──────────────────────────────────────────────────────────────┤
│  Backends (implementation, được chọn lúc build/runtime):      │
│   • RHI-Vulkan   • RHI-DX12*   • RHI-Metal*   • RHI-WebGPU*    │
│   • Platform-GLFW   • Platform-SDL*   • Platform-Win32*        │
└──────────────────────────────────────────────────────────────┘
        (* = giai đoạn sau, nhưng interface đã chừa chỗ)
```

### 2.2. Vai trò từng layer

- **Core / Foundation:** không phụ thuộc gì ngoài std + thư viện toán. Chứa `int32`, `f32`, math (vec/mat/quat), containers (array, hashmap, sparse set), `Log`, `Assert`, `StringId` (hash hóa chuỗi), allocators, event bus, profiler macro. Mọi tầng khác build trên đây.
- **Platform Layer:** abstraction cho thứ phụ thuộc OS/cửa sổ: tạo window, lấy surface, poll event, time, file IO. **Interface thuần**, backend GLFW nằm riêng.
- **RHI:** abstraction phần cứng đồ họa. Đây là **ranh giới quan trọng nhất** của tính backend-neutral. Mọi thứ trên RHI nói chuyện bằng `GraphicsDevice`, `CommandList`,… không bao giờ include Vulkan.
- **Renderer 2D:** dùng RHI để vẽ. Quản sprite batch, camera, material, render pass. Không biết Vulkan, chỉ biết RHI.
- **ECS Core:** lưu entity/component, chạy system. Là "cơ sở dữ liệu" của game world.
- **Scene System:** tổ chức entity thành scene, transform hierarchy, load/unload scene.
- **Asset System:** import file (PNG, font, audio, tilemap) → dạng runtime. Tách *import-time* (chậm, offline) và *load-time* (nhanh, runtime).
- **Resource Manager:** sở hữu vòng đời tài nguyên runtime qua handle table, ref-count/generation, hỗ trợ hot reload.
- **Input System (high):** trên IInputProvider, ánh xạ phím/chuột/gamepad thô → *action* ("Jump", "MoveX") cho gameplay.
- **Scripting/Gameplay code:** ở engine này = **C++ thuần** + (tùy chọn) hot-reloadable game module DLL. Không cần ngôn ngữ script riêng giai đoạn đầu.
- **Debug/Profiling/Logging:** log có level/category, profiler chèn scope, GPU timestamp, debug draw (line/box), assert có context.
- **Build/Test/Tooling:** CMake, preset, unit test (Catch2/doctest), CI, shader compile pipeline, asset cooker.

---

## 3. Thiết kế module thay thế được backend

### 3.1. Hai ranh giới bất khả xâm phạm

1. **Không thứ gì trên RHI được include `vulkan.h`.** Renderer cao cấp chỉ thấy interface RHI.
2. **Không thứ gì trên Platform Layer được include `glfw.h`.** Engine core chỉ thấy `IWindow`/`IInputProvider`.

Cách kiểm soát: đặt rule trong CI/lint — file ở thư mục `renderer/`, `ecs/`, `gameplay/` mà chứa chuỗi `vulkan` hoặc `glfw` → fail build. Đơn giản nhưng cực hiệu quả để giữ kiến trúc không bị "rò rỉ".

### 3.2. RHI nên mô phỏng theo phong cách nào?

Có 3 trường phái:

| Phong cách | Ví dụ | Ưu | Nhược |
|---|---|---|---|
| **Mô phỏng OpenGL cũ** (set state lẻ) | nhiều engine cũ | dễ viết | map sang Vulkan/DX12 rất tệ, không tận dụng explicit API |
| **Explicit kiểu WebGPU/Dawn** (Pipeline + BindGroup + RenderPass, immutable state objects) | WebGPU, một phần Unreal RHI | map *sạch* lên Vulkan/DX12/Metal; tư duy hiện đại | nhiều khái niệm hơn lúc đầu |
| **Command-list kiểu DX12/Vulkan trần** | bgfx (ẩn đi), Sokol | sát phần cứng | dễ rò rỉ chi tiết backend |

**Khuyến nghị:** thiết kế RHI **theo phong cách WebGPU-lite** — đủ explicit để map sạch sang mọi backend explicit, nhưng đơn giản hóa (bỏ bớt phần web-safety). Đây là điểm ngọt cho engine cá nhân dài hạn.

### 3.3. Cách hiện thực abstraction: vtable vs compile-time

- **Virtual interface (vtable):** `IGraphicsDevice`, `ICommandList`… Linh hoạt: đổi backend lúc runtime, build nhiều backend cùng lúc. Overhead 1 indirect call — **không đáng kể** vì bạn batch ở mức command/drawcall, không gọi virtual 100k lần/frame.
- **Compile-time (typedef/template chọn backend):** zero overhead nhưng cứng, mỗi build 1 backend, khó test chéo.

**Khuyến nghị:** dùng **virtual interface** cho RHI và Platform. Overhead không phải vấn đề khi API được thiết kế "coarse-grained" (gộp công việc lớn mỗi call). Nếu sau này profiler chỉ ra điểm nóng, có thể đặc biệt hóa cục bộ.

### 3.4. Ví dụ cấu trúc thư mục đề xuất

```
engine/
├── CMakeLists.txt
├── cmake/                      # toolchain, presets, helper
├── third_party/                # glfw, vk-bootstrap, vma, stb, glm/own-math, EnTT?, miniaudio, box2d
│
├── core/                       # KHÔNG phụ thuộc platform/rhi
│   ├── types.hpp               # int32, f32, ...
│   ├── math/                   # vec2/3/4, mat3/4, quat, rect, transform
│   ├── containers/             # array, hashmap, sparse_set, ring_buffer
│   ├── memory/                 # arena, pool, stack, frame_allocator
│   ├── string_id.hpp           # hashed string
│   ├── log.hpp / log.cpp
│   ├── assert.hpp
│   ├── profiler.hpp            # scope macros, có thể nối Tracy
│   ├── event_bus.hpp
│   └── handle.hpp              # Handle<T> = {index, generation}
│
├── platform/
│   ├── window.hpp              # IWindow (abstract)
│   ├── input_provider.hpp      # IInputProvider (abstract)
│   ├── filesystem.hpp          # IFileSystem
│   ├── clock.hpp
│   └── glfw/                   # CHỈ nơi này include glfw
│       ├── glfw_window.cpp
│       └── glfw_input.cpp
│
├── rhi/                        # abstract, KHÔNG include vulkan
│   ├── device.hpp              # IGraphicsDevice
│   ├── swapchain.hpp
│   ├── command_list.hpp
│   ├── buffer.hpp / texture.hpp / sampler.hpp
│   ├── pipeline.hpp            # GraphicsPipeline, PipelineDesc
│   ├── bind_group.hpp          # descriptor abstraction
│   ├── render_pass.hpp
│   ├── rhi_enums.hpp           # Format, LoadOp, BufferUsage...
│   ├── rhi_handle.hpp          # BufferHandle, TextureHandle...
│   └── vulkan/                 # CHỈ nơi này include vulkan
│       ├── vk_device.cpp
│       ├── vk_swapchain.cpp
│       ├── vk_command_list.cpp
│       ├── vk_pipeline.cpp
│       ├── vk_descriptor.cpp   # pool + cache
│       └── vk_memory.cpp       # VMA wrapper
│
├── renderer/                   # high-level, chỉ thấy rhi/
│   ├── render_context.hpp
│   ├── camera2d.hpp
│   ├── sprite_renderer.hpp
│   ├── batch_renderer.hpp
│   ├── material.hpp
│   ├── render_graph_lite.hpp   # pass list, mở rộng thành frame graph
│   └── shaders/                # .glsl/.slang -> .spv (compile-time)
│
├── assets/
│   ├── asset_manager.hpp
│   ├── importers/              # png, font, tilemap, audio
│   └── asset_types.hpp
│
├── resource/
│   └── resource_manager.hpp    # handle table, hot reload
│
├── ecs/
│   ├── registry.hpp            # entity/component storage
│   ├── system.hpp
│   ├── components/             # Transform2D, SpriteComp, ...
│   └── systems/                # TransformSystem, RenderExtractSystem
│
├── scene/
│   ├── scene.hpp
│   └── transform_hierarchy.hpp
│
├── input/                      # high-level action mapping
│   └── input_map.hpp
│
├── jobs/
│   └── job_system.hpp
│
├── debug/
│   ├── debug_draw.hpp
│   └── imgui_layer.hpp         # tùy chọn
│
└── engine.hpp                  # facade khởi tạo toàn bộ

games/
└── sample_pong/                # game thật đầu tiên, dùng engine như thư viện
    └── main.cpp

tests/
tools/
└── shader_compiler/, asset_cooker/
```

### 3.5. Ví dụ interface (rút gọn, minh họa tinh thần thiết kế)

```cpp
// platform/window.hpp  — KHÔNG biết GLFW
namespace pf {

struct WindowDesc { int width, height; const char* title; bool resizable; };

class IWindow {
public:
    virtual ~IWindow() = default;
    virtual bool shouldClose() const = 0;
    virtual void pollEvents() = 0;
    virtual void getFramebufferSize(int& w, int& h) const = 0;

    // Trả về native handle dạng "mờ" để backend RHI tạo surface.
    // RHI-Vulkan biết cách diễn giải; tầng engine thì không.
    virtual void* nativeWindowHandle() const = 0;
    virtual void* nativeDisplayHandle() const = 0;
};

// Factory — game/engine gọi cái này, không gọi GLFW trực tiếp
std::unique_ptr<IWindow> createWindow(const WindowDesc&);

} // namespace pf
```

```cpp
// platform/input_provider.hpp
namespace pf {
enum class Key { A, B, /*...*/ Space, Escape, Count };
enum class MouseButton { Left, Right, Middle, Count };

class IInputProvider {
public:
    virtual ~IInputProvider() = default;
    virtual bool isKeyDown(Key) const = 0;
    virtual bool isMouseDown(MouseButton) const = 0;
    virtual void mousePosition(float& x, float& y) const = 0;
    virtual void newFrame() = 0; // cập nhật trạng thái edge (pressed/released)
};
} // namespace pf
```

```cpp
// rhi/device.hpp  — KHÔNG biết Vulkan
namespace rhi {

struct BufferDesc { uint64_t size; BufferUsage usage; MemoryDomain domain; };
struct TextureDesc { uint32_t width, height; Format format; TextureUsage usage; };
struct SwapchainDesc { uint32_t width, height; Format format; PresentMode mode; };

class IGraphicsDevice {
public:
    virtual ~IGraphicsDevice() = default;

    // Tạo tài nguyên -> trả handle (không trả pointer)
    virtual BufferHandle   createBuffer(const BufferDesc&)   = 0;
    virtual TextureHandle  createTexture(const TextureDesc&) = 0;
    virtual SamplerHandle  createSampler(const SamplerDesc&) = 0;
    virtual PipelineHandle createGraphicsPipeline(const PipelineDesc&) = 0;
    virtual BindGroupHandle createBindGroup(const BindGroupDesc&) = 0;

    virtual void destroyBuffer(BufferHandle)   = 0;
    virtual void destroyTexture(TextureHandle) = 0;
    // ... (lifetime an toàn theo frame, xem mục GPU buffer management)

    virtual void* mapBuffer(BufferHandle) = 0;
    virtual void  unmapBuffer(BufferHandle) = 0;

    virtual ISwapchain*  createSwapchain(const SwapchainDesc&, pf::IWindow&) = 0;
    virtual ICommandList* acquireCommandList() = 0; // 1 list / thread / frame
    virtual void submit(ICommandList*) = 0;
    virtual void present(ISwapchain*) = 0;

    virtual void beginFrame() = 0;
    virtual void endFrame()   = 0;
};

// Chọn backend ở 1 chỗ duy nhất; phần còn lại của engine không quan tâm
std::unique_ptr<IGraphicsDevice> createDevice(GraphicsAPI api, pf::IWindow&);

} // namespace rhi
```

```cpp
// rhi/command_list.hpp
namespace rhi {
class ICommandList {
public:
    virtual ~ICommandList() = default;
    virtual void beginRenderPass(const RenderPassDesc&) = 0;
    virtual void endRenderPass() = 0;
    virtual void setPipeline(PipelineHandle) = 0;
    virtual void setBindGroup(uint32_t slot, BindGroupHandle) = 0;
    virtual void setVertexBuffer(uint32_t slot, BufferHandle, uint64_t offset) = 0;
    virtual void setIndexBuffer(BufferHandle, IndexType) = 0;
    virtual void setViewport(const Viewport&) = 0;
    virtual void pushConstants(const void* data, uint32_t size) = 0;
    virtual void draw(uint32_t vertexCount, uint32_t instanceCount,
                      uint32_t firstVertex, uint32_t firstInstance) = 0;
    virtual void drawIndexed(uint32_t indexCount, uint32_t instanceCount,
                             uint32_t firstIndex, int32_t vertexOffset,
                             uint32_t firstInstance) = 0;
};
} // namespace rhi
```

**Điểm mấu chốt:** API này map *sạch* lên Vulkan (RenderPass, Pipeline, DescriptorSet=BindGroup), DX12 (PSO, RootSignature, DescriptorTable), Metal (RenderPipelineState, ArgumentBuffer), WebGPU (gần như 1-1). Khi viết RHI-DX12 sau này, **không dòng nào của renderer phải đổi**.

---

## 4. Timeline theo giai đoạn

> Cột "Tuần" là ước lượng cho solo part-time. Nếu full-time, chia ~2–2.5. Các phase **không hoàn toàn tuần tự** — một số phần (debug, profiler, test) làm xen kẽ liên tục.

| Phase | Tên | Mục tiêu chốt | Tuần (ước lượng) | Mốc tích lũy |
|---|---|---|---|---|
| **0** | Foundation | Build system, core types, math, log, test chạy | 1–2 | ~0.5 tháng |
| **1** | Core + Platform abstraction | Mở được window qua interface, input cơ bản, không lộ GLFW | 2–3 | ~1.5 tháng |
| **2** | Vulkan RHI | Vẽ được 1 tam giác qua RHI trừu tượng | 6–10 | ~4 tháng |
| **3** | 2D Renderer | Batch hàng nghìn sprite, camera 2D, material | 4–6 | ~5.5 tháng |
| **4** | ECS + Scene | Spawn entity, transform tree, render extraction | 3–5 | ~7 tháng |
| **5** | Asset + Resource | Load PNG/atlas/tilemap qua handle, hot reload texture | 3–4 | ~8 tháng |
| **6** | Text/UI/Audio/Physics2D | Game playable thật sự (chữ, nút, âm thanh, va chạm) | 5–8 | ~10 tháng |
| **7** | Optimization | Job system, frame allocator, multithread render, profiler | 3–5 | ~11.5 tháng |
| **8** | Tooling / dev workflow | Hot reload game code, debug draw, asset cooker | 3–4 | ~12.5 tháng |
| **9** | Chuẩn bị mở rộng 3D | Render graph thật, camera/material tổng quát hóa | 2–3 | ~13 tháng |
| **10** | Multi-backend graphics | Thêm 1 backend thứ 2 (vd DX12 hoặc WebGPU) để *chứng minh* abstraction | 6–10 | ~15 tháng |

**Lưu ý chiến lược về thứ tự:**
- Phase 2 (Vulkan) là **đỉnh rủi ro & thời gian**. Đừng cầu toàn — mục tiêu là "tam giác lên màn hình qua RHI", phần tối ưu để Phase 7.
- Nên ship **một game nhỏ thật (Pong → Breakout → một platformer mini)** ngay sau Phase 4–6, thay vì xây mãi engine. Game thật là bài test kiến trúc tốt nhất.
- Phase 10 có thể hoãn vô thời hạn nếu chưa cần nhiều backend — nhưng **giữ kỷ luật abstraction từ Phase 2** để Phase 10 không thành "viết lại".

---

## 5. Task breakdown chi tiết

Mỗi phase trình bày: **Mục tiêu · Task · Output · Acceptance · Rủi ro · Thứ tự triển khai.**

### Phase 0 — Foundation

**Mục tiêu:** Có nền build/test ổn định và thư viện core để mọi thứ khác dựa vào.

**Task:**
- Thiết lập CMake + CMakePresets (Debug/Release/RelWithDebInfo), bật C++20/23, warning-as-error.
- Tích hợp third_party qua FetchContent/submodule: glfw, vk-bootstrap, VMA, stb_image, (glm hoặc tự viết math), Catch2/doctest, Tracy (profiler).
- Core types: `i8..i64`, `u8..u64`, `f32/f64`, `usize`.
- Math: `vec2/3/4`, `mat3/mat4`, `quat`, `rect`, hàm transform 2D (translate/rotate/scale), ortho projection.
- `Log` (level + category), `Assert` (có message + file/line), `StringId` (FNV-1a hash + bảng debug optional).
- Containers cơ bản hoặc wrapper std: `Array<T>`, `HashMap`, `SparseSet<T>`.
- `Handle<T>` = `{u32 index; u32 generation;}` + helper.
- Khung unit test + 1 test mẫu cho math.

**Output:** repo build sạch trên máy bạn (và CI nếu có), thư viện `core` link được, test xanh.

**Acceptance:**
- `cmake --preset debug && build` không lỗi/không warning.
- Test math (mat4 multiply, ortho, transform compose) pass.
- Log in ra có level + category; assert fail in đúng vị trí.

**Rủi ro:** sa đà viết container/math "hoàn hảo". → Chỉ viết đủ dùng, mở rộng dần.

**Thứ tự:** build system → types/math → log/assert → containers/handle → test.

---

### Phase 1 — Core + Platform abstraction

**Mục tiêu:** Mở cửa sổ và nhận input **qua interface**, GLFW bị giấu hoàn toàn.

**Task:**
- Định nghĩa `IWindow`, `IInputProvider`, `IClock`, `IFileSystem` (interface thuần).
- Hiện thực `glfw/GlfwWindow`, `glfw/GlfwInput` trong thư mục `platform/glfw` (nơi duy nhất include GLFW).
- Factory `pf::createWindow(desc)` chọn backend (hiện chỉ GLFW).
- Vòng lặp chính tối thiểu: tạo window → loop pollEvents → shouldClose.
- `nativeWindowHandle()/nativeDisplayHandle()` trả con trỏ mờ để Phase 2 tạo Vulkan surface.
- Lint/CI rule: cấm chuỗi `glfw` ngoài `platform/glfw/`.

**Output:** chương trình mở cửa sổ trống, đóng bằng nút X/ESC, in tọa độ chuột.

**Acceptance:**
- Không file nào ngoài `platform/glfw/` include GLFW (kiểm bằng grep trong CI).
- Đổi tên/ẩn GLFW thử nghiệm không làm hỏng tầng trên (chứng minh tính cô lập).
- Input edge (vừa nhấn / vừa thả) hoạt động đúng nhờ `newFrame()`.

**Rủi ro:** rò rỉ kiểu GLFW (vd trả `GLFWwindow*` ra interface). → Chỉ trả `void*` mờ.

**Thứ tự:** interface → GLFW impl → factory → loop → CI rule.

---

### Phase 2 — Vulkan RHI ⚠️ (phase nặng nhất)

**Mục tiêu:** Vẽ 1 tam giác lên màn hình **chỉ qua interface `rhi::`**, không một dòng Vulkan nào lọt lên trên.

**Task (gợi ý chia nhỏ để không ngợp):**
1. **Khởi tạo:** instance, validation layer, chọn physical device, queue family, logical device (dùng `vk-bootstrap` để bớt boilerplate).
2. **Surface & Swapchain:** tạo surface từ `IWindow::nativeWindowHandle()`, swapchain, image views, xử lý resize.
3. **Memory:** tích hợp VMA, viết `vk_memory` wrapper, `createBuffer/createTexture` map sang VMA.
4. **Command:** command pool/buffer per-frame-in-flight, `ICommandList` wrap `VkCommandBuffer`.
5. **Sync:** fence + semaphore, frames-in-flight (2–3), acquire/submit/present đúng thứ tự.
6. **RenderPass/Framebuffer** (hoặc dynamic rendering nếu nhắm Vulkan 1.3) → ẩn sau `beginRenderPass`.
7. **Pipeline:** load SPIR-V, `PipelineDesc` → `VkGraphicsPipeline`, pipeline cache.
8. **Descriptor:** descriptor pool + layout cache, `BindGroup` → `VkDescriptorSet`.
9. **Shader pipeline:** compile `.glsl/.slang` → `.spv` lúc build (glslc/slangc).
10. Vẽ tam giác hard-code, rồi quad có texture.

**Output:** tam giác/quad có màu (và texture) render qua `IGraphicsDevice` + `ICommandList`.

**Acceptance:**
- Renderer code gọi tam giác **không include Vulkan** (chỉ `rhi/`).
- Validation layer **0 error**.
- Resize cửa sổ không crash (swapchain recreate đúng).
- Chạy ổn định nhiều phút không leak (VMA report sạch khi thoát).

**Rủi ro (cao):**
- Sync sai → flicker/crash khó debug. → Bật validation + đọc kỹ frames-in-flight.
- Abstraction rò rỉ chi tiết Vulkan (vd lộ `VkFormat`). → RHI dùng enum riêng (`rhi::Format`), map nội bộ.
- Over-engineer RHI quá sớm. → Chỉ thêm API khi renderer Phase 3 thực sự cần.

**Thứ tự:** init → swapchain → command/sync → present clear color → pipeline/shader → tam giác → descriptor/texture → quad có texture.

---

### Phase 3 — 2D Renderer

**Mục tiêu:** Vẽ hàng nghìn sprite hiệu quả với camera 2D và material đơn giản.

**Task:**
- `Camera2D` (ortho): view + projection, pan/zoom, world↔screen.
- `BatchRenderer`: gom quad cùng texture/material vào 1 dynamic vertex buffer, 1 drawcall/batch.
- `SpriteRenderer` API: `drawSprite(texture, transform, color, uvRect)`.
- `Material` tối giản: pipeline + bind group + uniform (color/tint, shader).
- Texture sampler quản lý qua RHI.
- Dynamic/persistent-mapped vertex buffer + index buffer (quad indices dùng chung).
- Sort theo layer/z (painter's order) cho 2D.
- (Tùy chọn) instancing cho sprite giống nhau.

**Output:** demo vẽ 10k–100k sprite chạy mượt, đổi camera mượt, nhiều texture/atlas.

**Acceptance:**
- 10k sprite ở 60+ FPS trên GPU tầm trung.
- Số drawcall ≈ số texture/material khác nhau (batching đúng).
- Camera pan/zoom đúng tỉ lệ, không méo.

**Rủi ro:** batch break quá nhiều do đổi texture liên tục → dùng atlas / (sau) bindless. Update vertex buffer mỗi frame gây stall → dùng ring buffer + frames-in-flight.

**Thứ tự:** camera → 1 quad có texture → batch nhiều quad 1 texture → nhiều texture → sort/layer → instancing.

---

### Phase 4 — ECS + Scene

**Mục tiêu:** Mô tả game world bằng entity/component, chạy system, extract ra renderer.

**Task:**
- ECS storage. **Khuyến nghị thực dụng:** dùng **EnTT** (sparse-set, header-only, DX tốt) để đi nhanh; hoặc tự viết archetype storage nếu muốn học sâu. (So sánh ở mục 7.)
- Component cơ bản: `Transform2D` (local), `WorldTransform2D` (cached), `SpriteComp`, `Parent/Children` (hierarchy), `Velocity`, `Tag`.
- `TransformSystem`: cập nhật world transform theo cây (cha → con).
- `RenderExtractSystem`: duyệt (WorldTransform2D + SpriteComp) → tạo mảng phẳng `RenderItem` đưa cho BatchRenderer.
- `Scene`: chứa registry + danh sách system + camera; API spawn/destroy entity.
- System update order rõ ràng (xem mục 7.2).

**Output:** spawn vài nghìn entity có sprite + transform, di chuyển bằng system, render qua extraction.

**Acceptance:**
- Thêm/xóa component không làm hỏng iteration.
- Transform cha-con đúng (xoay cha → con xoay theo).
- Extraction tạo mảng contiguous, batch renderer tiêu thụ trực tiếp.

**Rủi ro:** trộn logic gameplay vào trong extraction → giữ extraction *thuần đọc*. Hierarchy update sai thứ tự → topo-sort hoặc duyệt theo độ sâu.

**Thứ tự:** registry → component cơ bản → transform system → extraction → scene wrapper → hierarchy.

---

### Phase 5 — Asset + Resource

**Mục tiêu:** Nạp tài nguyên từ file qua handle, vòng đời an toàn, hot reload texture.

**Task:**
- Tách **import-time** (offline: PNG→texture data, font→atlas, tilemap→binary) và **load-time** (runtime nhanh).
- `AssetManager`: load theo path/StringId, cache, async load (qua job system sau).
- `ResourceManager`: handle table `{index, generation}` cho texture/material/mesh; ref-count hoặc generation invalidation.
- Importers: `stb_image` cho PNG, tilemap (Tiled JSON/TMX), font (Phase 6 dùng).
- Hot reload: watch file → reload texture → cập nhật handle (data đổi, handle giữ nguyên).

**Output:** load sprite từ PNG, đổi file PNG khi đang chạy → sprite tự cập nhật.

**Acceptance:**
- Load 2 lần cùng asset → cùng handle (cache hit).
- Hủy resource → handle cũ invalid (generation tăng), truy cập handle stale bị bắt lỗi an toàn.
- Hot reload không crash, không leak GPU memory.

**Rủi ro:** GPU resource lifetime khi reload giữa lúc đang được frame trước dùng → defer-destroy theo frames-in-flight. Đường dẫn asset cứng → dùng virtual filesystem/mount point.

**Thứ tự:** handle table → importer PNG → load đồng bộ → cache → hot reload → defer destroy.

---

### Phase 6 — Text, UI, Audio, Physics 2D

**Mục tiêu:** Đủ tính năng để làm **một game thật chơi được**.

**Task:**
- **Text:** font atlas (stb_truetype / msdf-gen cho chữ sắc nét mọi cỡ), `drawText` qua batch renderer.
- **UI (code-only):** immediate-mode đơn giản (button, label, panel) hoặc tích hợp Dear ImGui cho UI debug + tự viết UI game. Layout cơ bản (anchor, stack).
- **Audio:** tích hợp **miniaudio** (đơn file, đa nền tảng) sau một `IAudioBackend` mỏng; play SFX, music, volume, loop.
- **Physics 2D:** tích hợp **Box2D** (hoặc tự viết AABB + impulse nếu muốn học), sync `Transform2D` ↔ rigid body; collision callback vào ECS.

**Output:** một game mini (vd platformer/breakout) có nhân vật di chuyển, va chạm, điểm số bằng chữ, âm thanh, nút UI.

**Acceptance:**
- Chữ sắc nét ở nhiều cỡ.
- Va chạm ổn định, không xuyên tường ở tốc độ cao (CCD nếu cần).
- Audio không giật/lệch.
- Có thể chơi từ đầu đến cuối một màn.

**Rủi ro:** ôm đồm UI framework lớn quá sớm → bắt đầu immediate-mode tối giản. Physics tự viết tốn thời gian → ưu tiên Box2D, tự viết để học sau.

**Thứ tự:** text → audio → physics → UI → ráp thành game mini.

---

### Phase 7 — Optimization

**Mục tiêu:** Biến engine "chạy được" thành "chạy nhanh & ổn định frame time".

**Task:**
- **Job system:** work-stealing thread pool, `parallel_for`, dependency tối thiểu.
- **Frame allocator (linear/arena):** reset mỗi frame, dùng cho render item, temp data.
- **Object pool** cho entity/component/particle.
- Song song hóa: extraction parallel, culling parallel, **multithread command recording** (mỗi thread 1 command list → submit gộp).
- ECS cache-friendly: bố trí component SoA, iterate tuyến tính (mục 7).
- **CPU profiler** (Tracy) chèn scope hot path; **GPU profiler** (timestamp query qua RHI).
- Thiết lập **benchmark mục tiêu** và đo định kỳ.

**Output:** frame time giảm rõ rệt, tận dụng đa nhân, profiler chỉ ra hot path.

**Acceptance:**
- 100k sprite vẫn 60 FPS (mục tiêu tham khảo).
- Frame time ổn định (ít spike GC/alloc).
- Multithread render cho speedup đo được trên máy nhiều nhân.

**Rủi ro:** tối ưu sai chỗ → **luôn đo trước**. Race condition khi parallel → ECS phân vùng rõ read/write per system. Đừng song song hóa thứ chưa phải bottleneck.

**Thứ tự:** profiler trước → frame allocator → job system → parallel extraction/culling → multithread record → ECS layout.

---

### Phase 8 — Tooling / Dev workflow

**Mục tiêu:** Vòng lặp phát triển nhanh như Godot dù code-only.

**Task:**
- **Hot reload game code:** tách game logic thành DLL/shared lib, engine reload runtime (giữ state qua serialize đơn giản hoặc reload chỉ code).
- **Debug draw:** line/box/circle/text world-space, bật/tắt theo category.
- **Asset cooker CLI:** import hàng loạt asset → định dạng runtime, kiểm tra lỗi.
- **Shader compile pipeline** tự động + báo lỗi rõ.
- Console lệnh (cvar) bật/tắt feature, đổi tham số runtime.
- (Tùy chọn) ImGui inspector: xem entity/component runtime.

**Output:** sửa code gameplay → thấy đổi trong vài giây không restart; debug draw bật được; asset cook 1 lệnh.

**Acceptance:**
- Hot reload code không crash trong kịch bản thường.
- Debug draw chính xác world-space.
- Thay cvar runtime có hiệu lực ngay.

**Rủi ro:** hot reload C++ phức tạp (state, vtable) → giới hạn phạm vi reload, hoặc dùng kiến trúc "game là DLL, engine giữ state". Đừng để tooling nuốt thời gian làm game.

**Thứ tự:** debug draw → cvar/console → shader pipeline → asset cooker → hot reload code (khó nhất, để cuối).

---

### Phase 9 — Chuẩn bị mở rộng 3D

**Mục tiêu:** Tổng quát hóa các điểm "2D-only" để 3D là *thêm vào*, không *viết lại*.

**Task:**
- Nâng `render_graph_lite` thành **render graph thật:** node pass khai báo input/output (resource), tự tính barrier/transition, cho phép nhiều pass (sau dùng cho gbuffer/shadow/post).
- Tổng quát **Material system:** shader + tham số + texture binding, không cứng cho sprite.
- **Camera abstraction:** tách `Camera` (view/proj chung) — ortho (2D) và perspective (3D) là 2 cấu hình.
- Transform: đảm bảo `mat4` + quaternion sẵn sàng cho xoay 3D (dù 2D chỉ dùng phần con).
- RHI: rà soát depth buffer, depth test, cull mode, MSAA — đã có chưa, có map đủ cho 3D chưa.
- Asset: chừa chỗ cho mesh/material/shader import.

**Output:** kiến trúc render đa-pass, material/camera tổng quát, demo 2D vẫn chạy y nguyên trên nền mới.

**Acceptance:**
- Demo 2D không hồi quy sau refactor.
- Thêm 1 pass mới (vd post-process tint toàn màn) chỉ là thêm node, không sửa renderer lõi.

**Rủi ro:** tổng quát hóa quá đà khi chưa làm 3D thật → chỉ refactor những điểm chắc chắn 3D cần (render graph, camera, material, depth).

**Thứ tự:** camera/material tổng quát → depth/pipeline state → render graph thật → kiểm hồi quy 2D.

---

### Phase 10 — Multi-backend graphics API

**Mục tiêu:** **Chứng minh** RHI abstraction đúng bằng cách thêm backend thứ 2.

**Task:**
- Chọn backend 2: **WebGPU/Dawn** (map gần 1-1, dễ nhất) hoặc **DX12** (giá trị Windows cao).
- Hiện thực toàn bộ interface `rhi::` cho backend mới trong `rhi/<backend>/`.
- Chọn backend runtime/build-time qua `createDevice(api, …)`.
- Tổng quát Platform layer cho native handle đa nền (Win32/X11/Wayland/Cocoa) nếu cần.
- Test: chạy *cùng* demo 2D trên cả 2 backend, so output.

**Output:** cùng codebase game/renderer chạy trên Vulkan **và** backend thứ 2, không sửa tầng trên RHI.

**Acceptance:**
- 0 dòng renderer/gameplay phải đổi khi chuyển backend.
- Output hình ảnh khớp giữa 2 backend (cùng demo).

**Rủi ro:** lộ chi tiết Vulkan trong RHI mới phát hiện ở đây → coi đây là bài kiểm tra: chỗ nào "khó port" chính là chỗ abstraction rò rỉ, cần sửa interface.

**Thứ tự:** rà interface rò rỉ → init/swapchain backend 2 → buffer/texture → pipeline/bindgroup → command/sync → chạy demo so sánh.

---

## 6. Kiến trúc rendering

### 6.1. Nguyên tắc: 2D thiết kế để 3D không phải viết lại

Cách tách lớp giúp 2D mở rộng sang 3D mượt mà:

```
Game/ECS
   │  (extract)
   ▼
RenderItem[]  (phẳng, DOD)  ── 2D: sprite; 3D sau: mesh draw
   │
   ▼
RenderGraph (lite → full)   ── 2D: 1 pass; 3D: shadow→gbuffer→light→post
   │
   ▼
RHI CommandList             ── chung cho cả 2D & 3D
```

Điểm "mở khóa 3D" nằm ở 4 chỗ: **RenderItem tổng quát, Material tổng quát, Camera tổng quát, RenderGraph đa-pass.** Nếu 4 thứ này không cứng-2D, 3D chỉ là thêm pass + shader + loại RenderItem mới.

### 6.2. Batch renderer & Sprite renderer

- **Sprite renderer** là API mức cao: `drawSprite(tex, transform, color, uvRect)`. Nó *không* gọi RHI trực tiếp; nó đẩy quad vào **batch renderer**.
- **Batch renderer** gom các quad **cùng material/texture** thành một batch: ghi vertex vào dynamic buffer, dùng index buffer quad dùng chung (0,1,2, 2,3,0 lặp). Khi đổi material/texture → flush batch hiện tại (1 drawcall) rồi mở batch mới.
- Mỗi vertex: `pos(vec2/3) · uv(vec2) · color(rgba8) · texIndex(u32)`. Có `texIndex` để gom nhiều texture trong 1 batch (texture array / bindless), giảm batch break.
- Sort: 2D dùng painter's order theo layer/z trước khi batch để alpha blend đúng.

### 6.3. Camera 2D (và đường lên 3D)

```cpp
struct Camera2D {
    glm::vec2 position{0,0};
    float zoom = 1.0f;
    float rotation = 0.0f;
    glm::vec2 viewportSize;

    glm::mat4 view() const;        // inverse of world transform
    glm::mat4 projection() const;  // ortho(-w/2, w/2, -h/2, h/2) / zoom
    glm::mat4 viewProj() const { return projection() * view(); }
};
```

Khi lên 3D: tách thành `Camera` chung trả `view()`/`projection()`; ortho và perspective chỉ khác hàm `projection()`. Renderer luôn nhận `viewProj` — không quan tâm 2D hay 3D.

### 6.4. Material system

Material = **shader (pipeline) + tham số uniform + texture binding**. Giai đoạn 2D giữ tối giản:

```cpp
struct Material {
    rhi::PipelineHandle pipeline;
    rhi::BindGroupHandle bindGroup;   // textures + sampler + uniform buffer
    // params: tint, custom uniforms...
};
```

Thiết kế để 3D dùng lại: thêm loại pipeline (PBR), thêm tham số (metallic/roughness), nhưng *cấu trúc* Material không đổi. Material quyết định batch boundary (cùng material mới gom được).

### 6.5. Texture / Sampler

- Texture & sampler tạo qua RHI, tham chiếu bằng handle.
- Atlas hóa sprite để giảm số texture → ít batch break.
- Hướng tới **bindless / texture array** ở Phase 7 để một batch dùng nhiều texture (giảm drawcall mạnh).

### 6.6. Render pass / Frame graph (định hướng tương lai)

- **2D (Phase 3–8):** `render_graph_lite` = một danh sách pass tối giản (thường chỉ 1 pass main + 1 UI pass). Đủ dùng, không over-engineer.
- **Phase 9+:** nâng thành **frame graph thật**: mỗi pass khai báo *đọc/ghi* resource nào; graph tự suy ra thứ tự, barrier/layout transition, và (sau) cull pass thừa. Đây là pattern Unreal/Frostbite để quản lý gbuffer, shadow map, post-process mà không thủ công đặt barrier.

```cpp
// Tinh thần API render graph (Phase 9)
auto color = graph.createTexture("scene_color", desc);
graph.addPass("sprites", [&](PassBuilder& b){
    b.write(color);
    return [=](rhi::ICommandList& cl){ batchRenderer.flush(cl); };
});
graph.addPass("ui", [&](PassBuilder& b){
    b.write(color);
    return [=](rhi::ICommandList& cl){ ui.render(cl); };
});
graph.compile(); graph.execute(commandList);
```

### 6.7. Pipeline cache & Descriptor management

- **Pipeline cache:** tạo pipeline tốn kém → cache theo `PipelineDesc` hash; tái dùng. Vulkan còn có `VkPipelineCache` lưu xuống đĩa để khởi động nhanh.
- **Descriptor (BindGroup):** quản pool + layout cache trong RHI-Vulkan. Tránh tạo descriptor set mỗi drawcall — cache theo nội dung binding. Dùng dynamic offset cho uniform thay đổi mỗi object.

### 6.8. Instancing

- Sprite giống nhau (cùng texture, khác transform): dùng **instanced draw** — 1 quad + buffer per-instance `(transform, color, uvRect)`. 1 drawcall cho N instance.
- Cực hiệu quả cho tile, particle, bullet hell. Là tiền đề cho instanced mesh ở 3D.

### 6.9. GPU buffer management

- **Dynamic data mỗi frame** (vertex batch, uniform): dùng **ring buffer** với N vùng (= frames-in-flight), persistent-mapped, ghi vùng tương ứng frame hiện tại → tránh stall do CPU ghi đè vùng GPU đang đọc.
- **Static data** (mesh, atlas): upload 1 lần qua staging buffer.
- **Defer destroy:** không hủy GPU resource ngay; đẩy vào hàng đợi, hủy sau khi qua N frame (GPU chắc chắn không còn dùng).

### 6.10. Render command abstraction

- Renderer cao không nói chuyện trực tiếp với `ICommandList` ở mọi nơi; nó có thể build một **render command buffer riêng** (danh sách lệnh mức engine: SetMaterial, DrawBatch, SetCamera…) rồi *translate* sang RHI command list.
- Lợi ích: dễ sort/merge/optimize lệnh trước khi gửi GPU; dễ ghi song song (mỗi thread build 1 phần list); tách hoàn toàn "quyết định vẽ gì" khỏi "gọi API gì".

---

## 7. ECS + DOD design

### 7.1. Đặt OOP / ECS / DOD ở đâu

| Kỹ thuật | Dùng cho | Lý do |
|---|---|---|
| **OOP / interface (virtual)** | RHI, Platform, AssetImporter, AudioBackend, subsystem lifetime | Ranh giới cần *thay thế implementation*. Gọi thưa → overhead virtual không đáng kể. |
| **ECS** | Game world: entity = tổ hợp component; gameplay logic theo system | Composition linh hoạt, dễ viết gameplay, dễ thêm/bớt behavior. |
| **DOD (SoA, contiguous, bulk)** | Hot loop: transform update, culling, sprite extraction, particle, physics broadphase | Chạy hàng chục–trăm nghìn lần/frame; cần cache-friendly + song song. |

Quy tắc nhớ: **virtual ở chỗ gọi *ít lần* và cần đổi; DOD ở chỗ gọi *nhiều lần* và cần nhanh; ECS là khung tổ chức ở giữa.**

### 7.2. Component layout & System update order

**Component:** giữ **dữ liệu thuần (POD)**, không logic, không virtual. Ví dụ:

```cpp
struct Transform2D   { glm::vec2 position; float rotation; glm::vec2 scale; };
struct WorldTransform2D { glm::mat4 matrix; };     // cache, do system tính
struct SpriteComp    { rhi::TextureHandle tex; glm::vec4 color; Rect uv; int layer; };
struct Velocity      { glm::vec2 value; };
struct Parent        { Entity value; };
```

**Storage:** archetype (component cùng archetype nằm contiguous) hoặc sparse-set. Cả hai cho phép iterate tuyến tính một nhóm component → cache-friendly.

**System update order** (ví dụ điển hình 1 frame):

```
1. Input system            (đọc input → action)
2. Gameplay systems        (AI, movement, spawn... — viết bằng C++ trên ECS)
3. Physics step            (Box2D) → ghi lại Transform2D
4. TransformSystem         (local → world, theo hierarchy, cha trước con)
5. Animation system        (cập nhật uv/frame)
6. Culling system          (loại sprite ngoài camera) [DOD, song song]
7. RenderExtractSystem     (WorldTransform + Sprite → RenderItem[]) [DOD, song song]
   ── ranh giới game thread / render thread ──
8. Renderer tiêu thụ RenderItem[] → batch → RHI
```

Thứ tự rõ ràng + khai báo system *đọc/ghi component nào* → biết system nào song song được (không xung đột write).

### 7.3. Transform hierarchy

- `Transform2D` (local) + `Parent`. `TransformSystem` tính `WorldTransform2D` = `parentWorld * local`.
- Phải duyệt **cha trước con**: hoặc sắp xếp theo độ sâu (depth), hoặc giữ danh sách topo-sorted, cập nhật incremental khi cây đổi.
- Tránh đệ quy sâu mỗi frame: cache world transform, chỉ tính lại nhánh "dirty".

### 7.4. Render extraction (mấu chốt perf + tách thread)

- Mỗi frame, `RenderExtractSystem` duyệt `(WorldTransform2D + SpriteComp)` → ghi ra **mảng `RenderItem` phẳng** (`{matrix, texIndex, color, uv, layer}`).
- Đây là cầu nối **ECS (game world) → renderer (render world)**. Renderer **không đụng vào ECS**, chỉ đọc mảng phẳng.
- Lợi ích kép: (1) cache-friendly cho batching; (2) cho phép game thread cập nhật frame N+1 trong khi render thread vẽ frame N (double-buffer mảng RenderItem). Đây là pattern Unreal.

### 7.5. Làm sao gameplay dễ viết mà runtime vẫn nhanh

- Developer viết gameplay ở mức "spawn entity, set component, viết system" — **không cần biết SoA/cache**.
- Engine lo phần DOD bên dưới: storage contiguous, iterate bulk, song song hóa system không xung đột.
- Tức là: **DX kiểu ECS ở mặt ngoài, hiệu năng kiểu DOD ở bên trong.** Đây chính là điểm engine hiện đại (EnTT, flecs, Bevy, Unity DOTS) hướng tới.

### 7.6. Tự viết ECS hay dùng EnTT?

| | EnTT (dùng sẵn) | Tự viết archetype |
|---|---|---|
| Thời gian | nhanh, đi ngay Phase 4 | tốn vài tuần |
| Hiệu năng | rất tốt (sparse-set) | tốt nếu làm đúng (archetype tốt cho iterate, kém hơn khi thêm/bớt component thường xuyên) |
| Học sâu | ít | nhiều |
| Kiểm soát | trung bình | toàn bộ |

**Khuyến nghị:** dùng **EnTT** để ship game sớm; chỉ tự viết ECS nếu (a) profiler chỉ ra EnTT là bottleneck thật, hoặc (b) bạn coi việc học ECS internal là mục tiêu. Đừng để "tự viết mọi thứ" làm chậm việc ra game.

---

## 8. Developer experience

### 8.1. Mục tiêu: nhanh như Godot, nhưng code-only

3 trụ cột DX:
1. **Boilerplate tối thiểu** — khởi tạo engine 1 dòng, vòng lặp lo sẵn.
2. **API gameplay trực giác** — spawn/component/input đọc như tiếng người.
3. **Vòng lặp sửa-thử nhanh** — hot reload code/asset, debug draw, cvar.

### 8.2. API gameplay nên thiết kế thế nào

- **Facade `Engine`** giấu hết init RHI/window/asset.
- **`Scene`** là nơi sống của entity; spawn trả về `Entity` (handle).
- Component gắn bằng template: `e.add<SpriteComp>(...)`.
- System là hàm/lambda đăng ký vào scene, hoặc class kế thừa `System`.
- Input mức action: `input.action("Jump").pressed()`.

### 8.3. Ví dụ code tạo window, scene, entity, sprite, input, update loop

```cpp
#include <engine/engine.hpp>
using namespace eng;

int main() {
    // 1 dòng: khởi tạo window + RHI(Vulkan) + asset + renderer
    Engine engine({ .width = 1280, .height = 720, .title = "My Game" });

    Scene scene;
    scene.setCamera(Camera2D{ .viewportSize = {1280, 720} });

    // Load asset qua handle (cache + hot reload tự lo)
    TextureHandle playerTex = engine.assets().loadTexture("sprites/player.png");

    // Spawn entity + gắn component (ECS bên dưới)
    Entity player = scene.spawn();
    player.add<Transform2D>({ .position = {0,0}, .scale = {1,1} });
    player.add<SpriteComp>({ .tex = playerTex, .color = {1,1,1,1} });

    // Đăng ký input action (ánh xạ phím -> action, đổi phím không sửa logic)
    engine.input().bind("MoveX", Axis{ Key::A, Key::D });
    engine.input().bind("Jump",  Key::Space);

    // Gameplay system: code C++ thuần, chạy mỗi frame
    scene.system([&](float dt){
        float x = engine.input().axis("MoveX");
        auto& t = player.get<Transform2D>();
        t.position.x += x * 300.0f * dt;
        if (engine.input().action("Jump").pressed())
            t.position.y += 50.0f; // ví dụ
    });

    // Update loop lo sẵn: poll input -> chạy system -> extract -> render -> present
    engine.run(scene);
    return 0;
}
```

So với Godot: thay vì kéo-thả node, bạn `spawn()` + `add<>()`. Vẫn ngắn gọn, mà toàn quyền kiểm soát và hiệu năng DOD bên dưới.

### 8.4. Data-driven & hot reload

- **Scene/prefab từ data:** mô tả entity + component trong file (JSON/RON/binary) → `loadScene("level1.scene")`. Designer chỉnh data, không cần compile.
- **Hot reload asset:** đổi PNG/shader/scene → engine reload runtime (Phase 5/8).
- **Hot reload code:** game logic trong DLL, engine reload mà không restart (Phase 8). Giữ state qua serialize đơn giản.
- **CVar/console:** chỉnh tham số runtime (`r.vsync 0`, `phys.debugDraw 1`) không cần build lại.

### 8.5. Vì sao thiết kế này vẫn "nhanh"

API đẹp ở *mặt ngoài* nhưng *bên trong* vẫn đẩy về DOD: `add<>()` ghi vào storage contiguous; `system()` iterate bulk; extraction tạo mảng phẳng. Lập trình viên gameplay không trả giá hiệu năng cho sự tiện lợi — đó là khác biệt then chốt so với engine OOP "mỗi entity một object có virtual update()".

---

## 9. Performance roadmap

> Nguyên tắc bao trùm: **đo trước, tối ưu sau.** Cài profiler (Tracy) sớm; mỗi tối ưu phải có số liệu chống lưng.

### 9.1. Memory allocator

- **Arena/Linear:** cấp phát bằng cách tăng con trỏ; "giải phóng" = reset toàn bộ. Cực nhanh, không phân mảnh. Dùng cho dữ liệu tạm sống trong 1 phạm vi/frame.
- **Frame allocator:** một arena reset cuối mỗi frame. Mọi temp data render/extract/ui lấy ở đây thay vì `new`. Loại bỏ phần lớn malloc trong hot path.
- **Pool allocator:** khối cố định cho object cùng kích cỡ (entity, component, particle). Cấp/thu O(1), cache-friendly.
- **Stack allocator:** LIFO, cho scope lồng nhau.
- Quy tắc: **hot path không gọi `new`/`delete`/`malloc` trực tiếp.**

### 9.2. Job system

- Work-stealing thread pool (N = số core). API: `submit(job)`, `parallel_for(range, fn)`, `wait(handle)`.
- Dùng cho: parallel extraction, parallel culling, song song system độc lập, multithread command recording, async asset load.
- Tránh: job quá nhỏ (overhead > lợi), chia sẻ state không khóa (race).

### 9.3. Multithread rendering

- **Tách game thread / render thread:** game thread cập nhật ECS frame N+1; render thread vẽ frame N từ mảng RenderItem double-buffered. Tăng throughput, giảm phụ thuộc tuần tự.
- **Parallel command recording:** chia drawcall theo thread, mỗi thread ghi 1 command list (Vulkan secondary command buffer / nhiều primary), submit gộp. RHI cần hỗ trợ `acquireCommandList()` per-thread.

### 9.4. Frame allocator (nhắc lại vì quan trọng)

Dùng cho mọi cấp phát "sống trong frame": RenderItem array, batch vertex staging, temp arrays trong system, command metadata. Reset 1 lần cuối frame → gần như zero-cost dealloc.

### 9.5. Object pool & Cache-friendly ECS

- Object pool: tái dùng slot entity/particle, tránh alloc/free liên tục.
- Cache-friendly ECS: component cùng loại nằm contiguous (SoA), iterate tuyến tính, không truy cập rải rác qua pointer. Đây là khác biệt lớn nhất giữa ECS hiện đại và OOP "object có virtual update()".

### 9.6. GPU & CPU profiling

- **CPU:** Tracy scope (`ZoneScoped`) ở các điểm: input, gameplay, physics, transform, extract, batch, submit. Xem flame graph để tìm hot path thật.
- **GPU:** timestamp query qua RHI quanh từng pass → đo thời gian GPU mỗi pass; phát hiện bottleneck GPU (overdraw, fillrate, quá nhiều drawcall).
- **Counters:** số drawcall, số batch, số entity, vertex/frame, VRAM dùng — hiển thị overlay debug.

### 9.7. Benchmark mục tiêu (tham khảo, máy tầm trung)

| Kịch bản | Mục tiêu |
|---|---|
| 10k sprite động, 1 atlas | 60+ FPS thoải mái, < 50 drawcall |
| 100k sprite (instanced/bindless) | 60 FPS sau Phase 7 |
| Frame time | ổn định, ít spike (p99 gần p50) |
| Khởi động engine | < 1–2s (pipeline cache trên đĩa giúp) |
| CPU game thread (10k entity) | < vài ms cho update + extract |
| Multithread render speedup | đo được trên ≥4 core |

(Đây là số *định hướng*, điều chỉnh theo phần cứng & loại game. Quan trọng là **có mục tiêu cụ thể và đo được**, không phải con số tuyệt đối.)

---

## 10. Roadmap mở rộng 3D

### 10.1. Những thứ cần chuẩn bị từ đầu (để không viết lại)

Đây là checklist "đừng làm cứng-2D" — tuân thủ từ Phase 2:

- [ ] **RHI có depth buffer, depth test/write, cull mode, MSAA** (2D ít dùng nhưng 3D bắt buộc; chừa sẵn trong `PipelineDesc`).
- [ ] **Transform dùng `mat4` + `quat`** ngay từ đầu (2D chỉ dùng phần con), không hardcode 2x3.
- [ ] **Camera trừu tượng** trả `view()`/`projection()`; ortho và perspective chỉ khác projection.
- [ ] **Material tổng quát** (shader + params + textures), không cứng cho sprite.
- [ ] **RenderItem tổng quát** đủ chỗ cho mesh draw (không chỉ quad).
- [ ] **RenderGraph đa-pass** (Phase 9) thay vì 1 pass cứng.
- [ ] **RHI không rò rỉ Vulkan** (để thêm backend 3D-heavy sau dễ).
- [ ] **Asset system chừa loại mesh/material/shader**.

Nếu 8 điều trên được giữ, **3D là phần *thêm vào*, không phải *làm lại*.**

### 10.2. Lộ trình 3D (sau Phase 10)

| Thành phần | Việc cần làm | Tái dùng được gì |
|---|---|---|
| **Camera 3D** | Thêm perspective projection, controller (fly/orbit) | Camera abstraction từ 9.1 |
| **Mesh renderer** | Vertex/index buffer mesh, RenderItem loại mesh, draw indexed | RHI buffer, batch/instancing |
| **Material / PBR** | Shader PBR (albedo/normal/metallic/roughness), texture set | Material system tổng quát |
| **Lighting** | Forward+ hoặc deferred (gbuffer), light culling | RenderGraph, RHI |
| **Shadow** | Shadow map pass, depth-only pipeline | RenderGraph (pass mới), depth từ RHI |
| **Scene graph** | Transform hierarchy 3D (đã có dạng 2D, mở rộng mat4) | TransformSystem hiện có |
| **Render graph** | Mở rộng: shadow→gbuffer→light→transparent→post | render graph Phase 9 |

Điểm cốt lõi: **RHI, ECS, asset, job system, memory, camera/material/rendergraph abstraction đều dùng lại nguyên vẹn.** 3D chủ yếu là: shader mới + pass mới + loại RenderItem mới + import mesh.

---

## 11. Deliverable cuối cùng

### 11.1. Bảng roadmap theo tháng (solo part-time)

| Tháng | Trọng tâm | Kết quả chốt |
|---|---|---|
| **Tháng 1** | Phase 0 + 1 | Build/test/core xong; mở window + input qua interface (GLFW ẩn) |
| **Tháng 2–4** | Phase 2 | Vulkan RHI: tam giác → quad có texture, validation sạch, resize ổn |
| **Tháng 5** | Phase 3 | 2D renderer: batch 10k sprite, camera, material |
| **Tháng 6–7** | Phase 4 + đầu 5 | ECS + scene + extraction; load PNG qua handle |
| **Tháng 7–8** | Phase 5 | Asset/resource hoàn chỉnh, hot reload texture |
| **Tháng 8–10** | Phase 6 | Text/UI/audio/physics → **game mini chơi được** |
| **Tháng 10–11** | Phase 7 | Job system, frame allocator, multithread render, profiler |
| **Tháng 12** | Phase 8 + 9 | Tooling (debug draw, cvar, hot reload code) + chuẩn bị 3D |
| **Tháng 13–15** | Phase 10 | Backend thứ 2 (WebGPU/DX12) chạy cùng demo |

> Nếu chỉ cần **engine 2D ship game**: dừng được sau **Tháng 10–11** (hết Phase 7). Phase 8–10 là đầu tư dài hạn/đa nền.

### 11.2. Bảng task ưu tiên (làm gì trước nếu thời gian giới hạn)

| Ưu tiên | Task | Vì sao |
|---|---|---|
| **P0 — bắt buộc** | Build system, core/math, platform abstraction, Vulkan RHI tam giác | Không có là không có gì |
| **P0** | 2D batch renderer + camera | Trái tim của engine 2D |
| **P0** | ECS + extraction | Cách tổ chức & vẽ game world |
| **P1 — để có game** | Asset/handle, text, input action, audio, physics 2D | Đủ làm game thật |
| **P1** | Profiler (cài sớm), frame allocator | Nền tảng perf, tránh nợ kỹ thuật |
| **P2 — chất lượng** | Job system, multithread render, instancing/bindless | Scale & perf cao |
| **P2** | Hot reload code/asset, debug draw, cvar | DX nhanh |
| **P3 — dài hạn** | Render graph thật, tổng quát camera/material | Cầu nối 3D |
| **P3** | Backend thứ 2 | Chứng minh & đa nền |

### 11.3. Kiến trúc thư mục đề xuất

(Xem chi tiết ở **mục 3.4** — tóm tắt cây phụ thuộc một chiều:)

```
core  ←  platform  ←  rhi  ←  renderer  ←  ecs/scene  ←  gameplay/game
  ↑          ↑         ↑
(không ai phụ thuộc ngược lên; backend nằm trong rhi/<api>, platform/<lib>)
```

### 11.4. Danh sách milestone

- **M0 — "Hello Window":** cửa sổ mở/đóng qua interface, GLFW ẩn. *(cuối Phase 1)*
- **M1 — "Hello Triangle":** tam giác render qua RHI trừu tượng, validation sạch. *(cuối Phase 2)*
- **M2 — "10k Sprites":** batch renderer + camera 2D, 10k sprite 60 FPS. *(cuối Phase 3)*
- **M3 — "Living World":** ECS + transform hierarchy + extraction, vài nghìn entity di chuyển. *(cuối Phase 4)*
- **M4 — "Real Assets":** load PNG/atlas/tilemap qua handle + hot reload. *(cuối Phase 5)*
- **M5 — "Playable Game":** text + UI + audio + physics → một game mini hoàn chỉnh. *(cuối Phase 6)* ★ mốc quan trọng nhất
- **M6 — "Fast & Parallel":** job system + frame allocator + multithread render, đạt benchmark. *(cuối Phase 7)*
- **M7 — "Great DX":** hot reload code, debug draw, asset cooker, cvar. *(cuối Phase 8)*
- **M8 — "3D-Ready":** render graph thật, camera/material tổng quát, demo 2D không hồi quy. *(cuối Phase 9)*
- **M9 — "Backend-Proven":** cùng demo chạy trên 2 graphics backend, 0 dòng renderer đổi. *(cuối Phase 10)*

### 11.5. Checklist "engine có thể dùng làm game thật"

- [ ] Mở/đóng cửa sổ, resize, fullscreen ổn định.
- [ ] Render 10k+ sprite ổn định 60 FPS, batching đúng.
- [ ] Camera 2D pan/zoom/rotate, world↔screen chính xác.
- [ ] Spawn/destroy entity runtime không leak/crash.
- [ ] Transform hierarchy cha-con đúng.
- [ ] Load texture/atlas/font/audio/tilemap từ file.
- [ ] Handle invalid hóa an toàn (không dùng nhầm resource đã hủy).
- [ ] Hot reload ít nhất texture (lý tưởng: + shader + code).
- [ ] Text sắc nét nhiều cỡ; UI cơ bản (button/label/panel) bấm được.
- [ ] Audio SFX + music, volume, loop.
- [ ] Physics 2D: va chạm ổn định, callback vào gameplay.
- [ ] Input action mapping (đổi phím không sửa logic), hỗ trợ gamepad.
- [ ] Lưu/tải game state (serialize scene cơ bản).
- [ ] Profiler CPU/GPU bật được; overlay counter (FPS, drawcall, entity).
- [ ] Frame time ổn định, không spike alloc.
- [ ] **Đã thực sự ship được ít nhất 1 game nhỏ từ đầu đến cuối bằng engine này.** ← test thật sự duy nhất

### 11.6. Danh sách lỗi kiến trúc cần tránh

**Về abstraction & dependency:**
- ❌ Để `vulkan.h` hoặc `glfw.h` rò lên tầng renderer/gameplay. → CI grep cấm.
- ❌ Trả native type (`VkFormat`, `GLFWwindow*`) ra interface. → Dùng enum/handle riêng của engine.
- ❌ Dependency hai chiều (core biết renderer). → Một chiều xuống, tuyệt đối.
- ❌ Bọc abstraction cho thứ gọi 100k lần/frame (virtual mỗi sprite). → DOD ở hot path, virtual ở ranh giới.

**Về RHI:**
- ❌ Thiết kế RHI kiểu OpenGL set-state (map tệ sang explicit API). → WebGPU-lite explicit.
- ❌ Tạo descriptor/pipeline mỗi drawcall. → Cache theo hash.
- ❌ Hủy GPU resource ngay khi gọi destroy. → Defer theo frames-in-flight.
- ❌ Ghi đè buffer GPU đang đọc. → Ring buffer per-frame-in-flight.

**Về ECS/DOD:**
- ❌ Component có logic/virtual/`update()`. → Component là POD, logic ở system.
- ❌ Mỗi entity là object có pointer rải rác (OOP cổ điển). → Storage contiguous.
- ❌ Trộn gameplay logic vào render extraction. → Extraction thuần đọc.
- ❌ Đệ quy tính transform toàn cây mỗi frame. → Cache + dirty flag.

**Về quy trình & phạm vi:**
- ❌ Tối ưu trước khi đo. → Cài profiler sớm, đo rồi mới tối ưu.
- ❌ Over-engineer (frame graph đầy đủ, multi-backend, ECS tự viết) trước khi có game chạy. → Làm thứ chạy được trước.
- ❌ Xây engine mãi không làm game. → Ship game mini sớm (sau Phase 4–6); game là bài test kiến trúc thật nhất.
- ❌ Cố làm 3D song song khi 2D chưa xong. → 2D trước, giữ kỷ luật "đừng làm cứng-2D", 3D sau.
- ❌ Tự viết mọi thứ vì sĩ diện (math, ECS, physics, audio). → Dùng thư viện tốt (VMA, EnTT, Box2D, miniaudio) ở chỗ không phải lợi thế cốt lõi; tự viết ở chỗ thật sự cần kiểm soát/học.
- ❌ Bỏ qua test/CI. → Test math/core + CI grep abstraction từ Phase 0.

---

### Lời kết

Engine này thành công không phải khi nó "có nhiều tính năng", mà khi **bạn ship được một game thật bằng nó, và khi thêm 3D/đổi backend không phải viết lại tầng nào**. Mọi quyết định trong roadmap đều phục vụ hai tiêu chí đó. Giữ kỷ luật: *một chiều dependency, đo trước tối ưu, ship game sớm, đừng làm cứng-2D.*
