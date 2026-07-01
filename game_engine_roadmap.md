# Roadmap Phát Triển 2D/3D Game Engine — C++20/23

> **Phiên bản:** Chi tiết đầy đủ · **Solo part-time ~15–20h/tuần**
> **Tổng thời gian ước lượng:** ~34–40 tháng (18 phase)
> **Nguyên tắc bao trùm:** Làm thứ chạy được trước · Đo trước tối ưu · Ship game sớm · Không làm cứng-2D

---

## Mục lục

| Phase | Tên | Nhóm | Tuần |
|---|---|---|---|
| 0 | Foundation | Nền tảng | 1–2 |
| 1 | Platform Abstraction | Nền tảng | 2–3 |
| 2 | Vulkan RHI | Nền tảng | 6–10 |
| 3 | 2D Renderer | 2D Core | 4–6 |
| 4 | ECS + Scene | 2D Core | 3–5 |
| 5 | Asset + Resource | 2D Core | 3–4 |
| 6 | Text · UI · Audio · Physics 2D | Game-Ready | 5–8 |
| 7 | Optimization | Performance | 3–5 |
| 8 | Tooling / Dev Workflow | Performance | 3–4 |
| 9 | 3D-Ready Refactor | 3D Prep | 2–3 |
| 10 | Multi-Backend (WebGPU/Dawn) | 3D Prep | 6–10 |
| 11 | 3D Renderer cơ bản | 3D | 4–6 |
| 12 | PBR · Shadow · Post-processing | 3D | 6–8 |
| 13 | Animation + Particle | 3D | 6–8 |
| 14 | Scripting · Hot-reload · Inspector | Polish | 6–8 |
| 15 | Ship Game + Build Pipeline | Ship | 4–6 |
| 16 | AI / Navigation | Bổ sung | 4–6 |
| 17 | Networking | Bổ sung | 6–8 |
| 18 | Scene Editor | Bổ sung | 6–8 |

---

## Điểm dừng chiến lược

- **Dừng sau Phase 8:** Engine 2D ship game indie được — đủ dùng thực tế
- **Dừng sau Phase 10:** Engine đa-backend, chứng minh abstraction, có WASM
- **Dừng sau Phase 13:** Engine 3D đầy đủ cho game indie 3D
- **Hết Phase 15:** Engine hoàn chỉnh + đã ship ít nhất 1 game
- **Hết Phase 18:** Engine "complete" với AI, Networking, Scene Editor

---

## Phase 0 — Foundation

**Nhóm:** Nền tảng · **Ước lượng:** 1–2 tuần · **Milestone:** `M0 — "Build xanh"`

### Mục tiêu

Có nền build/test ổn định và thư viện core để mọi tầng khác dựa vào.

### Task list

- Thiết lập CMake + CMakePresets (Debug / Release / RelWithDebInfo), bật C++20/23, `warning-as-error`
- Tích hợp third_party qua FetchContent hoặc submodule:
  - `glfw`, `vk-bootstrap`, `VMA`, `stb_image`, math (glm hoặc tự viết), `Catch2`/`doctest`, `Tracy`
- Core types: `i8..i64`, `u8..u64`, `f32/f64`, `usize`
- Math: `vec2/3/4`, `mat3/4`, `quat`, `rect`, transform 2D (translate/rotate/scale), ortho projection
- `Log` (level + category), `Assert` (message + file/line), `StringId` (FNV-1a hash + bảng debug)
- Containers cơ bản: `Array<T>`, `HashMap<K,V>`, `SparseSet<T>`
- `Handle<T>` = `{u32 index; u32 generation;}` + helper validate/lookup
- `EventBus` đơn giản (subscribe/publish typed event)
- Frame allocator skeleton (cấp phát tuyến tính, reset API — implement chi tiết Phase 7)
- Khung unit test + test mẫu cho math và Handle

### Output

Repo build sạch, thư viện `core` link được, test xanh trên CI.

### Acceptance criteria

- `cmake --preset debug && cmake --build` không lỗi, không warning
- Test mat4 multiply, ortho, transform compose → pass
- Log in ra có level + category; Assert fail in đúng file/line
- Handle invalid generation bị bắt ở `lookup()` với assert rõ ràng

### Rủi ro & biện pháp

| Rủi ro | Biện pháp |
|---|---|
| Sa đà viết math/container "hoàn hảo" | Chỉ viết đủ dùng, thêm dần khi cần |
| Build cross-platform vỡ | Test trên cả Windows + Linux từ Phase 0 |

### Thứ tự triển khai

Build system → types/math → log/assert → containers/handle → event bus → test

---

## Phase 1 — Platform Abstraction

**Nhóm:** Nền tảng · **Ước lượng:** 2–3 tuần · **Milestone:** `M1 — "Mở được cửa sổ qua interface"`

### Mục tiêu

Mở cửa sổ và nhận input **qua interface thuần**, GLFW bị giấu hoàn toàn sau backend.

### Task list

- Định nghĩa interface thuần (không include GLFW):
  - `IWindow` — `shouldClose()`, `pollEvents()`, `getFramebufferSize()`, `nativeWindowHandle()`, `nativeDisplayHandle()`
  - `IInputProvider` — `isKeyDown(Key)`, `isMouseDown(MouseButton)`, `mousePosition()`, `mouseScroll()`, `newFrame()`
  - `IClock` — `now()`, `deltaTime()`
  - `IFileSystem` — `readFile()`, `writeFile()`, `exists()`, `watchFile()` (hook cho hot reload)
- Implement `platform/glfw/GlfwWindow.cpp` và `GlfwInput.cpp` (nơi DUY NHẤT include `glfw.h`)
- Factory `pf::createWindow(WindowDesc)` chọn backend
- Vòng lặp chính tối thiểu: tạo window → poll → shouldClose
- CI lint rule: grep cấm chuỗi `glfw` ngoài `platform/glfw/`
- `IGamepadProvider` skeleton (controller input — implement chi tiết Phase 6)

### Output

Chương trình mở cửa sổ trống, đóng bằng nút X hoặc ESC, in tọa độ chuột và scroll delta.

### Acceptance criteria

- Không file nào ngoài `platform/glfw/` include GLFW (CI grep tự động kiểm)
- Input edge detection (pressed/released khác down) hoạt động đúng nhờ `newFrame()`
- `nativeWindowHandle()` trả `void*` mờ — tầng trên không diễn giải được

### Rủi ro & biện pháp

| Rủi ro | Biện pháp |
|---|---|
| Rò rỉ kiểu GLFW ra interface | Chỉ trả `void*`, không trả `GLFWwindow*` |
| `IFileSystem::watchFile` phức tạp | Stub trả callback rỗng, implement thật Phase 5/8 |

### Thứ tự triển khai

Interface → GLFW impl → factory → loop → CI rule → gamepad stub

---

## Phase 2 — Vulkan RHI ⚠️

**Nhóm:** Nền tảng · **Ước lượng:** 6–10 tuần · **Milestone:** `M2 — "Tam giác qua RHI abstract, 0 validation error"`

> Phase nặng nhất, rủi ro cao nhất. Chia nhỏ sub-task, không cầu toàn ngay lần đầu.

### Mục tiêu

Vẽ tam giác (và quad có texture) lên màn hình **chỉ qua interface `rhi::`** — không một dòng `vulkan.h` lọt lên tầng renderer.

### Task list

**Sub-task 1 — Init & Device**
- Instance, validation layer, `vk-bootstrap` chọn physical device
- Queue family (graphics + present + transfer), logical device
- RHI enum riêng: `rhi::Format`, `rhi::BufferUsage`, `rhi::TextureUsage`, v.v.

**Sub-task 2 — Surface & Swapchain**
- Surface từ `IWindow::nativeWindowHandle()`
- Swapchain, image views, xử lý resize (recreate khi `VK_ERROR_OUT_OF_DATE_KHR`)
- `ISwapchain` interface ẩn `VkSwapchainKHR`

**Sub-task 3 — Memory**
- Tích hợp VMA, `vk_memory.cpp` wrapper
- `createBuffer(BufferDesc)` → `BufferHandle`, `createTexture(TextureDesc)` → `TextureHandle`
- Staging buffer helper cho upload dữ liệu tĩnh

**Sub-task 4 — Command & Sync**
- Command pool per-frame-in-flight, command buffer → `ICommandList` wrap `VkCommandBuffer`
- Fence + semaphore, frames-in-flight (2–3)
- `acquireCommandList()` per-thread (chuẩn cho multithread Phase 7)

**Sub-task 5 — RenderPass & Framebuffer**
- Dynamic rendering (Vulkan 1.3, `VK_KHR_dynamic_rendering`) ưu tiên
- Fallback: `VkRenderPass` + `VkFramebuffer` nếu cần compat
- `beginRenderPass(RenderPassDesc)` / `endRenderPass()` ẩn chi tiết

**Sub-task 6 — Pipeline & Shader**
- Load SPIR-V từ file (compile sẵn bằng `glslc` hoặc `slangc` lúc build)
- `PipelineDesc` → `VkGraphicsPipeline`, pipeline cache (`VkPipelineCache` lưu đĩa)
- Depth buffer, cull mode, blend state đã có trong `PipelineDesc` (dù 2D chưa dùng)

**Sub-task 7 — Descriptor**
- Descriptor pool + layout cache (tránh tạo mới mỗi drawcall)
- `BindGroupDesc` → `VkDescriptorSet`, `BindGroupHandle`
- Dynamic offset cho uniform thay đổi mỗi object

**Sub-task 8 — Verify**
- Vẽ tam giác hard-code → quad → quad có texture (stb_image load PNG)
- Validation layer 0 error/warning
- VMA report sạch khi thoát (không leak)

### Output

Tam giác màu + quad có texture render qua `IGraphicsDevice` + `ICommandList`, validation sạch, resize ổn định.

### Acceptance criteria

- File renderer không include `vulkan.h` (CI grep)
- Validation layer 0 error trong suốt session
- Resize cửa sổ không crash (swapchain recreate đúng)
- VMA report `Total: 0 bytes` leak khi thoát

### Rủi ro & biện pháp

| Rủi ro | Biện pháp |
|---|---|
| Sync sai → flicker/crash | Bật validation từ đầu; đọc kỹ frames-in-flight ordering |
| Abstraction rò rỉ VkFormat | RHI dùng `rhi::Format` riêng, map nội bộ trong backend |
| Over-engineer quá sớm | Chỉ thêm API khi renderer Phase 3 thực sự cần |
| Timeline vượt ước lượng | "Tam giác lên màn hình qua RHI" là xong — không chờ perfect |

### Thứ tự triển khai

Init/device → swapchain → memory → command/sync → clear color present → pipeline/shader → tam giác → descriptor/texture → quad có texture

---

## Phase 3 — 2D Renderer

**Nhóm:** 2D Core · **Ước lượng:** 4–6 tuần · **Milestone:** `M3 — "10k sprite 60 FPS, drawcall = số material"`

### Mục tiêu

Vẽ hàng nghìn sprite hiệu quả với camera 2D, material đơn giản, và sort theo layer.

### Task list

- **Camera 2D:** ortho projection, pan/zoom/rotate, world↔screen transform, `viewProj()` mat4
- **BatchRenderer:** gom quad cùng material/texture → 1 dynamic vertex buffer → 1 drawcall/batch
  - Vertex layout: `pos(vec3) · uv(vec2) · color(rgba8) · texIndex(u32)`
  - Ring buffer (N vùng = frames-in-flight), persistent-mapped, ghi vùng frame hiện tại
  - Index buffer quad dùng chung (0,1,2,2,3,0 lặp), static upload 1 lần
- **SpriteRenderer API:** `drawSprite(TextureHandle, Transform2D, color, uvRect, layer)`
- **Material:** `PipelineHandle` + `BindGroupHandle` + uniform buffer (tint, shader variant)
- **Sort:** painter's order theo layer → z trong layer trước khi batch
- **Texture sampler:** tạo qua RHI handle, cache theo `SamplerDesc` hash
- **Texture array / bindless** skeleton (implement chi tiết Phase 7): `texIndex` trong vertex đã chừa sẵn
- **Flush + batch break:** khi đổi material/texture → flush batch hiện tại (1 drawcall) → mở batch mới
- Atlas helper: load sprite sheet → chia UV rect theo grid hoặc metadata JSON

### Output

Demo vẽ 10k–100k sprite chạy mượt, nhiều texture/atlas, camera pan/zoom đúng.

### Acceptance criteria

- 10k sprite ≥ 60 FPS trên GPU tầm trung
- Số drawcall ≈ số texture/material khác nhau (batching đúng, không batch break thừa)
- Camera pan/zoom không méo hình, world↔screen chính xác
- Ring buffer không gây GPU stall (đo bằng GPU timestamp Phase 7)

### Rủi ro & biện pháp

| Rủi ro | Biện pháp |
|---|---|
| Batch break nhiều do đổi texture | Dùng atlas, sau Phase 7 dùng bindless |
| CPU ghi đè buffer GPU đang đọc | Ring buffer đúng thứ tự frames-in-flight |
| Sort O(n log n) mỗi frame | Stable sort theo dirty flag, cache sort key |

### Thứ tự triển khai

Camera → 1 quad có texture → batch nhiều quad 1 texture → nhiều texture → sort/layer → atlas → instancing stub

---

## Phase 4 — ECS + Scene

**Nhóm:** 2D Core · **Ước lượng:** 3–5 tuần · **Milestone:** `M4 — "Entity di chuyển, transform cha-con đúng"`

### Mục tiêu

Mô tả game world bằng entity/component, chạy system, extract ra renderer theo mô hình DOD.

### Task list

- **ECS storage:** khuyến nghị `EnTT` (sparse-set, header-only) để đi nhanh; hoặc tự viết archetype nếu muốn học sâu
- **Component (POD, không logic, không virtual):**
  - `Transform2D` — `position(vec2)`, `rotation(float)`, `scale(vec2)`
  - `WorldTransform2D` — `matrix(mat4)` cache, do system tính
  - `SpriteComp` — `TextureHandle tex`, `vec4 color`, `Rect uv`, `int layer`
  - `Velocity` — `vec2 value`
  - `Parent` — `Entity value`
  - `Children` — `Array<Entity>`
  - `Tag` — `StringId value` (để query theo nhóm)
  - `Active` — bật/tắt entity mà không destroy
- **TransformSystem:** cập nhật `WorldTransform2D = parentWorld * local`, duyệt **cha trước con**
  - Dirty flag: chỉ tính lại nhánh bị đổi
  - Topo-sort hoặc depth-first duyệt đúng thứ tự
- **RenderExtractSystem:** duyệt `(WorldTransform2D + SpriteComp + Active)` → ghi ra mảng phẳng `RenderItem[]` contiguous
  - `RenderItem` = `{mat4 transform; TextureHandle tex; vec4 color; Rect uv; int layer;}`
  - Double-buffer mảng (chuẩn cho split game/render thread Phase 7)
- **Scene:** chứa registry + danh sách system + camera; API `spawn()`, `destroy(Entity)`, `loadScene()`, `saveScene()`
- **System update order rõ ràng** (xem danh sách thứ tự trong mục ECS của tài liệu gốc)
- **Culling system:** loại bỏ sprite ngoài camera frustum trước khi extract (DOD, song song được)

### Output

Spawn vài nghìn entity có sprite + transform, di chuyển bằng system, render đúng. Transform cha-con hoạt động.

### Acceptance criteria

- Thêm/xóa component không làm hỏng iteration (generation handle đúng)
- Xoay cha → con xoay theo đúng world transform
- Extraction tạo mảng contiguous, renderer tiêu thụ trực tiếp không qua pointer
- Culling loại bỏ entity ngoài frustum, không gửi sang renderer

### Rủi ro & biện pháp

| Rủi ro | Biện pháp |
|---|---|
| Trộn gameplay logic vào extraction | Extraction chỉ **đọc**, không ghi bất kỳ component nào |
| Hierarchy update sai thứ tự | Topo-sort khi cây thay đổi, cache thứ tự duyệt |
| EnTT bottleneck sau Phase 7 | Đo trước; tự viết ECS chỉ khi profiler chỉ ra thật sự |

### Thứ tự triển khai

Registry + component → transform system → culling → extraction → double-buffer → scene wrapper → hierarchy

---

## Phase 5 — Asset + Resource Manager

**Nhóm:** 2D Core · **Ước lượng:** 3–4 tuần · **Milestone:** `M5 — "Load PNG qua handle, hot reload không crash"`

### Mục tiêu

Nạp tài nguyên từ file qua handle, vòng đời an toàn, hot reload texture.

### Task list

- **Handle table:** `{index, generation}` cho mọi loại resource (Texture, Material, Mesh placeholder, Audio)
  - `lookup()` kiểm generation → invalid handle bị bắt an toàn
  - Ref-count hoặc explicit lifetime (scope-based RAII handle)
- **Import-time vs load-time:**
  - Import-time (offline, có thể chậm): PNG → packed binary, font → atlas binary, tilemap → compact binary
  - Load-time (runtime, phải nhanh): đọc packed binary → upload GPU
- **AssetManager:**
  - `loadTexture(path)` → `TextureHandle` (cache theo path hash — load 2 lần cùng asset → cùng handle)
  - `loadAtlas(path)` → atlas + UV map
  - Async load hook (stub, implement chi tiết Phase 7 khi có job system)
- **Importers:**
  - PNG: `stb_image` → pixel data → RHI `createTexture` → upload qua staging buffer
  - Tilemap: Tiled JSON/TMX → compact tile array (layer, gid, flip flags)
  - Audio placeholder (implement Phase 6)
- **Hot reload:**
  - `IFileSystem::watchFile(path, callback)` → khi file đổi → reimport + upload GPU texture mới
  - Giữ nguyên `TextureHandle` cũ, chỉ đổi data bên trong
  - **Defer destroy GPU resource** theo frames-in-flight (không hủy ngay khi GPU còn dùng)
- **Virtual filesystem / mount point:** `assets://sprites/player.png` → resolve thành path thật
  - `mountPoint("assets://", "data/assets/")` → tách engine khỏi layout thư mục cụ thể

### Output

Load sprite từ PNG, đổi file PNG lúc đang chạy → sprite tự cập nhật không crash, không leak.

### Acceptance criteria

- Load 2 lần cùng asset → cùng handle (cache hit, không duplicate GPU upload)
- Hủy resource → handle cũ invalid (generation tăng), truy cập stale handle bị assert rõ ràng
- Hot reload không crash, không GPU memory leak (VMA report sạch sau reload)
- Mount point hoạt động, path tuyệt đối không hard-code trong code gameplay

### Rủi ro & biện pháp

| Rủi ro | Biện pháp |
|---|---|
| GPU resource lifetime khi reload | Defer-destroy theo frames-in-flight (queue + counter) |
| Path cross-platform (`\` vs `/`) | Normalize tất cả path về `/` ngay khi nhận vào |
| Import blocking main thread | Stub async trước, implement thật khi có job system |

### Thứ tự triển khai

Handle table → importer PNG → load đồng bộ → cache → virtual filesystem → hot reload → defer destroy

---

## Phase 6 — Text · UI · Audio · Physics 2D

**Nhóm:** Game-Ready · **Ước lượng:** 5–8 tuần · **Milestone:** `M6 ★ — "Game mini chơi được end-to-end"`

> **Mốc quan trọng nhất giai đoạn 2D.** Sau phase này phải ship được một game nhỏ thật sự.

### Mục tiêu

Đủ tính năng để làm game indie: chữ, UI, âm thanh, va chạm vật lý.

### Task list

**Text**
- Font atlas: `stb_truetype` (bitmap, nhanh) hoặc MSDF (vector, sắc nét mọi cỡ — khuyến nghị)
- `drawText(text, position, size, color, font)` → gom glyph quad vào batch renderer
- Unicode cơ bản (Latin + Latin Extended), UTF-8 decode
- Text layout: wrap theo width, align (left/center/right)
- IME stub cho CJK/tiếng Việt (implement chi tiết Phase 18)

**UI (code-only)**
- Immediate-mode tối giản: `button()`, `label()`, `panel()`, `checkbox()`, `slider()`
- Layout: anchor (top-left, center, bottom-right), stack (vertical/horizontal), padding/margin
- Focus management, keyboard navigation (Tab), click hitbox chính xác
- Dear ImGui cho **debug UI** (inspector, profiler overlay) — tách biệt khỏi game UI
- `IAudioBackend` interface mỏng trước `miniaudio` (cô lập như IWindow/RHI)

**Audio**
- Tích hợp `miniaudio` sau `IAudioBackend` (nơi duy nhất include miniaudio.h)
- `playSound(SoundHandle, volume, loop)`, `stopSound()`, `setMasterVolume()`
- `AudioComp` trong ECS: entity phát âm thanh theo trigger
- Spatial audio 2D (pan dựa vào khoảng cách camera, stub 3D)
- Stream nhạc nền từ file (không load toàn bộ vào RAM)

**Physics 2D**
- Tích hợp `Box2D` (v3) hoặc `box2d-lite` (đơn giản hơn nếu chỉ cần AABB + impulse cơ bản)
- `PhysicsBodyComp`: `bodyType` (static/kinematic/dynamic), `linearDamping`, `angularDamping`
- `ColliderComp`: shape (box/circle/polygon), `friction`, `restitution`, `isSensor`
- Sync hai chiều: `Transform2D` → Box2D body mỗi đầu frame; Box2D → `Transform2D` cuối step
- Collision callback vào ECS: `OnCollisionEnter(entityA, entityB)` qua EventBus
- CCD (Continuous Collision Detection) cho object tốc độ cao (bullet, tên lửa)
- Physics debug draw: vẽ shape, velocity vector, contact point (bật qua cvar)

**Gamepad**
- Implement `IGamepadProvider` (đã stub Phase 1): axis, button, vibration, hot-plug
- Action mapping hỗ trợ gamepad: `input.bind("Jump", GamepadButton::A)`

### Output

Game mini (platformer/breakout/top-down) có: nhân vật di chuyển, va chạm ổn định, điểm số bằng chữ, âm thanh, nút UI, lưu điểm cao.

### Acceptance criteria

- Chữ sắc nét nhiều cỡ (MSDF không vỡ pixel khi zoom)
- Va chạm không xuyên tường ở tốc độ cao (CCD hoạt động)
- Audio không giật/lệch, stream nhạc nền không stutter
- Có thể chơi từ đầu đến cuối một màn mà không crash
- **Đã thực sự code xong 1 game mini và chạy được**

### Rủi ro & biện pháp

| Rủi ro | Biện pháp |
|---|---|
| UI framework phình to | Bắt đầu immediate-mode tối giản; không làm retained-mode lúc này |
| Physics tự viết tốn thời gian | Ưu tiên Box2D; tự viết sau để học |
| Audio thread race condition | miniaudio xử lý thread-safe nội bộ; không truy cập AudioComp từ audio thread |

### Thứ tự triển khai

Text → audio → physics cơ bản → gamepad → UI → collision callback → **ráp thành game mini**

---

## Phase 7 — Optimization

**Nhóm:** Performance · **Ước lượng:** 3–5 tuần · **Milestone:** `M7 — "100k sprite 60 FPS, profiler chỉ rõ hot path"`

### Mục tiêu

Biến engine "chạy được" thành "chạy nhanh và ổn định frame time".

> **Quy tắc số 1:** Cài profiler, đo, mới tối ưu. Không tối ưu theo cảm tính.

### Task list

**Profiler trước hết**
- `Tracy` integrate: `ZoneScoped` macro ở input, gameplay, physics, transform, extract, batch, submit
- GPU timestamp query qua RHI quanh mỗi render pass
- Debug overlay: FPS, frame time (ms), drawcall count, entity count, batch count, VRAM usage

**Frame Allocator (Linear/Arena)**
- Reset mỗi cuối frame: `frame_alloc.reset()`
- Dùng cho: `RenderItem[]`, vertex staging, temp arrays trong system, metadata command
- Zero overhead "dealloc" → loại bỏ hầu hết `new`/`delete` trong hot path
- Kiểm tra: hot path không có `malloc`/`new` (address sanitizer + custom hook)

**Job System**
- Work-stealing thread pool: N thread = số logical core
- API: `submit(Job)`, `parallel_for(begin, end, fn, grain_size)`, `wait(JobHandle)`
- Dependency graph tối giản: job B chờ job A qua `JobHandle`
- Dùng cho: parallel extraction, parallel culling, async asset load, multithread command recording

**Bindless Texture / Texture Array**
- Gom nhiều texture vào 1 `VkDescriptorSet` (bindless) hoặc `VkImageArray`
- `texIndex` trong vertex đã chừa sẵn từ Phase 3 → batch không break khi đổi texture
- Giảm drawcall cực mạnh cho scene nhiều texture

**Multithread Rendering**
- Game thread cập nhật ECS frame N+1; render thread vẽ frame N từ `RenderItem[]` double-buffered
- Parallel command recording: chia drawcall theo thread, mỗi thread 1 `ICommandList`, submit gộp
- `acquireCommandList()` per-thread an toàn (đã chuẩn từ Phase 2)

**ECS Cache-Friendly**
- Kiểm tra layout component SoA (EnTT sparse-set đã tốt về cơ bản)
- Đo iteration bandwidth; nếu bottleneck → xem xét archetype layout
- Object pool cho particle, bullet (Phase 13 dùng)

**Benchmark targets**

| Kịch bản | Mục tiêu |
|---|---|
| 10k sprite động, 1 atlas | 60+ FPS, < 10 drawcall |
| 100k sprite (bindless) | 60 FPS |
| Frame time p99 | ≤ 2× p50 (không spike) |
| Game thread 10k entity | < 3ms (update + extract) |

### Acceptance criteria

- Tracy flame graph chỉ rõ hot path thật (không đoán mò)
- 100k sprite đạt benchmark trên hardware tham chiếu
- Frame allocator xác nhận 0 `malloc` trong hot path bằng custom hook
- Speedup đo được trên ≥4 core (multithread render)

### Rủi ro & biện pháp

| Rủi ro | Biện pháp |
|---|---|
| Race condition khi parallel | ECS phân vùng read/write per system, không share state |
| Tối ưu sai chỗ | **Luôn đo trước.** Tracy trước hết |
| Job overhead > lợi cho job nhỏ | Đo grain size; `parallel_for` chỉ khi N > threshold (vd 1000) |

### Thứ tự triển khai

Tracy integrate → debug overlay → frame allocator → bindless → job system → parallel extraction → multithread command → ECS layout audit

---

## Phase 8 — Tooling / Dev Workflow

**Nhóm:** Performance · **Ước lượng:** 3–4 tuần · **Milestone:** `M8 — "Sửa code → thấy ngay <1s, debug draw bật/tắt"`

### Mục tiêu

Vòng lặp phát triển nhanh như Godot dù hoàn toàn code-only.

### Task list

**Debug Draw**
- `debugDraw.line(p0, p1, color)`, `box(center, size, color)`, `circle(center, r, color)`, `text(pos, str, color)`
- Tất cả world-space, render bằng thin wire overlay qua batch renderer
- Bật/tắt theo category: `debugDraw.enable(Category::Physics | Category::AI)`
- `physics.debugDraw` cvar → show Box2D shapes, contact points, velocity

**CVar / Console**
- `CVar<float>`, `CVar<int>`, `CVar<bool>` đăng ký global
- Console command runtime: `r.vsync 0`, `phys.debugDraw 1`, `r.wireframe 1`
- Persist config ra file giữa các session

**Shader Compile Pipeline**
- CMake custom target: `.glsl` / `.slang` → `.spv` tự động khi build
- Báo lỗi compile rõ ràng (file, line, message) — không im lặng
- Hot reload shader: detect `.spv` đổi → recreate pipeline (không cần restart)

**Asset Cooker CLI**
- `cooker import-all assets/ data/` → cook hàng loạt → packed binary
- Manifest file: liệt kê asset, checksum, dependency
- Kiểm tra asset thiếu hoặc corrupt → báo lỗi rõ ràng
- Tích hợp CI: cook → validate → build → test

**Hot Reload Game Code**
- Tách game logic thành shared library (`game.dll` / `libgame.so`)
- Engine `dlopen`/`LoadLibrary` reload khi `.dll` đổi
- Giữ state: `serialize_game_state()` trước reload → `deserialize()` sau reload
- Giới hạn phạm vi: reload code được, không reload struct layout (document rõ)

**ImGui Integration**
- Dear ImGui cho debug tools (không phải game UI)
- Entity inspector: chọn entity → xem/edit component field runtime
- Performance overlay: frame time graph, drawcall counter, memory

### Acceptance criteria

- Sửa 1 dòng code gameplay → save → engine reload < 2s không mất state (trường hợp thường)
- Debug draw physics shapes hiển thị đúng world-space
- `cooker import-all` chạy xong trong CI không lỗi
- Shader lỗi → báo đúng file+line, không crash engine

### Rủi ro & biện pháp

| Rủi ro | Biện pháp |
|---|---|
| Hot reload C++ DLL edge case | Document rõ giới hạn: không đổi layout struct giữa reload |
| Tooling nuốt thời gian | Làm đủ dùng, không over-engineer; game đang chờ |

### Thứ tự triển khai

Debug draw → CVar/console → shader pipeline → asset cooker → ImGui inspector → hot reload code

---

## Phase 9 — 3D-Ready Refactor

**Nhóm:** 3D Prep · **Ước lượng:** 2–3 tuần · **Milestone:** `M9 — "Demo 2D không hồi quy, pass mới = thêm node"`

### Mục tiêu

Tổng quát hóa các điểm "2D-only" để 3D là *thêm vào*, không *viết lại*.

### Checklist "đừng làm cứng-2D"

- [ ] RHI có depth buffer, depth test/write, cull mode, MSAA trong `PipelineDesc`
- [ ] Transform dùng `mat4` + `quat` (2D chỉ dùng phần con, không hardcode 2×3)
- [ ] Camera abstract: `view()` + `projection()` tách biệt; ortho và perspective chỉ khác `projection()`
- [ ] Material tổng quát: shader + params + texture binding (không cứng cho sprite)
- [ ] `RenderItem` tổng quát: đủ chỗ cho mesh draw (không chỉ quad)
- [ ] Render graph đa-pass (xem task bên dưới)
- [ ] RHI không rò rỉ Vulkan
- [ ] Asset system chừa loại `Mesh`, `MaterialDef`, `Shader`

### Task list

**Camera Abstraction**
- `Camera` base: `view()`, `projection()`, `viewProj()`, `frustum()`
- `OrthoCamera` (2D) và `PerspectiveCamera` (3D) kế thừa hoặc implement interface
- Renderer nhận `const Camera&`, không quan tâm loại

**Material Tổng Quát**
- `Material`: `PipelineHandle pipeline` + `BindGroupHandle bindGroup` + `UniformBuffer params`
- `MaterialDef`: mô tả schema (tên param, type, default) → instantiate thành `Material`
- 2D sprite material là 1 trường hợp đặc biệt của Material tổng quát

**Render Graph Thật**
- Node = render pass; khai báo input/output resource (texture, buffer)
- Graph compiler: suy ra thứ tự pass, image layout transition, barrier (Vulkan) tự động
- `PassBuilder` API:
  ```
  auto color = graph.createTexture("scene_color", desc);
  graph.addPass("sprites", [&](PassBuilder& b) {
      b.write(color);
      return [=](ICommandList& cl) { batchRenderer.flush(cl); };
  });
  graph.compile();
  graph.execute(commandList);
  ```
- Cull pass thừa: nếu output của pass không được ai đọc → skip pass đó
- Debug mode: in thứ tự pass, barrier được insert tự động

**RHI Audit**
- Kiểm tra `PipelineDesc` đã có: `depthTest`, `depthWrite`, `cullMode`, `MSAA`, `blendState`
- Kiểm tra `ICommandList` đã có: `setViewport`, `setScissor`, `pushConstants`, `dispatch` (compute stub)
- Thêm `copyTexture`, `generateMipmaps`, `blitTexture` nếu chưa có

### Acceptance criteria

- Demo 2D chạy không hồi quy sau toàn bộ refactor này
- Thêm 1 pass mới (vd post-process tint) = thêm `addPass()`, không sửa renderer core
- Camera đổi từ ortho → perspective = đổi 1 dòng, renderer không đổi

---

## Phase 10 — Multi-Backend (WebGPU/Dawn)

**Nhóm:** 3D Prep · **Ước lượng:** 6–10 tuần · **Milestone:** `M10 — "0 dòng renderer đổi khi switch Vulkan↔WebGPU"`

### Mục tiêu

Chứng minh RHI abstraction đúng bằng cách thêm backend thứ 2 (WebGPU/Dawn).

### Lý do chọn WebGPU/Dawn

- Concept `Device → Queue → CommandEncoder → RenderPass → Pipeline → BindGroup` gần 1:1 với RHI đã thiết kế
- Build WASM → chạy trên browser (bonus hiếm có với engine cá nhân)
- Nếu abstraction rò rỉ → WebGPU sẽ phát hiện ngay (concept khác Vulkan đủ để lộ vấn đề)

### Task list

**Rà soát RHI trước khi implement**
- Mọi chỗ "khó port" sang WebGPU = chỗ abstraction rò rỉ → sửa interface trước
- Ví dụ: nếu `PipelineDesc` chứa `VkPipelineStageFlags` → thay bằng `rhi::ShaderStage` enum

**WebGPU/Dawn Backend**
- Tích hợp Dawn qua CMake (prebuilt binary hoặc vcpkg để bắt đầu nhanh)
- Implement `rhi/webgpu/`:
  - `WebGPUDevice` → `IGraphicsDevice`
  - `WebGPUCommandList` → `ICommandList`
  - `WebGPUSwapchain` → `ISwapchain`
  - Buffer, Texture, Sampler, Pipeline, BindGroup mapping
- `createDevice(BackendType::Vulkan | BackendType::WebGPU)` — chọn lúc runtime qua flag/config

**WASM Build**
- CMakePresets cho Emscripten target
- WebGPU trên web dùng browser implementation (không cần Dawn)
- HTML shell, asset bundle, responsive canvas
- Test: Chrome + Firefox

**Cross-check**
- Chạy cùng demo 2D trên Vulkan và WebGPU, so sánh output frame (screenshot diff)
- 0 dòng renderer/gameplay phải đổi khi switch backend

### Acceptance criteria

- Demo 2D render giống nhau trên Vulkan và WebGPU (screenshot diff < threshold)
- WASM build chạy Chrome/Firefox không cần plugin
- 0 dòng renderer/gameplay đổi khi switch backend (CI verify)

---

## Phase 11 — 3D Renderer cơ bản

**Nhóm:** 3D · **Ước lượng:** 4–6 tuần · **Milestone:** `M11 — "glTF render, lighting directional, 2D vẫn đúng"`

### Mục tiêu

Thêm mesh renderer + lighting 3D tối giản **không viết lại bất kỳ tầng nào đã có**.

### Task list

**Camera 3D**
- `PerspectiveCamera`: FOV, near/far, aspect ratio
- Controllers: fly-cam (WASD + mouse look), orbit-cam (alt + drag), thêm qua component
- Frustum culling 3D: test AABB/sphere vs frustum planes

**Mesh Import**
- Importer OBJ (tối giản, nhanh làm) + glTF (khuyến nghị — industry standard)
- `cgltf` (header-only) để parse glTF
- `MeshAsset`: vertex buffer (pos/normal/uv/tangent), index buffer, submesh list, material refs
- Chỉ import: static mesh + base color texture (bỏ skin, animation cho Phase 13)

**MeshComp + Extract**
- `MeshComp { MeshHandle mesh; MaterialHandle mat; }`
- `MeshExtractSystem`: duyệt `(WorldTransform + MeshComp)` → `RenderItem` loại mesh
- `RenderItem` tổng quát đã chừa chỗ từ Phase 9

**RHI: Depth + Cull**
- Enable depth buffer, depth test/write (đã có trong `PipelineDesc` từ Phase 9)
- Back-face culling enable cho mesh (2D sprite giữ nguyên two-sided)

**Lighting tối giản**
- `LightComp { LightType type; vec3 color; float intensity; vec3 direction; }`
- Uniform buffer `LightData[]` → gửi lên shader mỗi frame
- Shader: Blinn-Phong (ambient + diffuse + specular)
- Directional light 1 cái trước, sau thêm point light

**Transform 3D**
- `TransformSystem` dùng `mat4` + `quat` (đã đúng từ Phase 9)
- Test: hierarchy 3D (parent mesh → child mesh xoay đúng)

**2D + 3D cùng scene**
- Sprite 2D: ortho camera pass
- Mesh 3D: perspective camera pass
- Render graph: `shadow_pass (stub) → mesh_pass → sprite_pass → ui_pass`

### Acceptance criteria

- Model glTF static render đúng depth, không z-fighting thô sơ
- Directional light đổi hướng → bề mặt thay đổi ánh sáng đúng
- Sprite 2D vẫn render đúng cùng scene (không hồi quy)
- 0 dòng ECS / RHI / Platform phải đổi (chỉ thêm component + system + shader mới)

---

## Phase 12 — PBR · Shadow · Post-processing

**Nhóm:** 3D · **Ước lượng:** 6–8 tuần · **Milestone:** `M12 — "Indie-modern visuals: bóng mềm, metallic đúng, bloom"`

### Mục tiêu

Nâng chất lượng hình ảnh 3D lên mức game indie hiện đại qua render graph đa-pass.

### Task list

**PBR Shader**
- Albedo, normal map, metallic, roughness, AO
- Cook-Torrance BRDF (GGX distribution, Smith geometry, Fresnel Schlick)
- IBL (Image-Based Lighting): precompute BRDF LUT, prefilter environment map, irradiance map
- `MaterialDef PBR`: extend Material system với PBR params; glTF loader đọc material PBR

**Shadow Map**
- Shadow pass: depth-only render từ góc nhìn directional light → `shadow_map` texture
- Main pass: sample `shadow_map` để tính có bóng không
- PCF (Percentage Closer Filtering): sample N điểm lân cận → bóng mềm hơn
- Depth bias + normal offset để chống shadow acne
- Cascade shadow map (CSM) stub cho scene lớn (implement đầy đủ nếu cần)

**Render Graph Multi-Pass**
- `shadow_pass → gbuffer_pass? → lighting_pass → transparent_pass → post_pass`
- Mỗi pass = 1 node trong render graph (Phase 9), tự handle barrier
- Deferred rendering (tùy chọn): gbuffer (albedo/normal/roughness/metallic), lighting pass đọc gbuffer → hiệu quả khi nhiều light

**Post-Processing Pass**
- Tone mapping: ACES filmic (default), Reinhard, linear (debug)
- Bloom: downsample → blur (Kawase/dual) → upsample → add
- FXAA: full-screen anti-aliasing đơn giản (1 pass)
- Chromatic aberration, vignette (optional aesthetic)
- Mỗi post effect = 1 node render graph

### Acceptance criteria

- Bóng đổ không flickering, PCF làm mềm rõ ràng
- PBR metallic vs non-metallic trông khác nhau rõ ràng (reflection, fresnel)
- Thêm post effect mới = thêm 1 node, không sửa code cũ
- IBL: sphere gold và sphere rubber trông đúng vật lý

### Rủi ro & biện pháp

| Rủi ro | Biện pháp |
|---|---|
| Shadow acne | Tune depth bias + normal offset; không có số magic |
| PBR phức tạp | Bắt đầu Blinn-Phong Phase 11, chuyển từng phần sang PBR |
| Deferred vs Forward | Đo trước khi quyết; Forward+ thường đủ cho game indie |

---

## Phase 13 — Animation + Particle System

**Nhóm:** 3D · **Ước lượng:** 6–8 tuần · **Milestone:** `M13 — "10k particle 60 FPS, anim blend mượt"`

### Mục tiêu

Engine đủ sức làm game hành động: nhân vật có animation xương, hiệu ứng hạt đẹp.

### Task list

**Skeletal Animation**
- Load skeleton + animation clip từ glTF (joints, bind pose, clip keyframes)
- GPU skinning shader: tối đa 4 joint/vertex, bone matrix buffer
- `AnimationSystem`: sample clip tại time T → bone pose → bone matrix array → upload UBO
- Cross-fade blend: lerp 2 pose theo blend weight (0→1 trong thời gian transition)
- Root motion: extract root bone movement → apply vào `Transform2D/3D`

**Animation State Machine**
- `AnimationGraph`: state (Idle/Run/Jump/Attack) + transition (điều kiện + duration)
- Gameplay code: `anim.setFloat("speed", vel.length())`, `anim.trigger("jump")`
- Additive layer: layer trên cùng (weapon aim, facial) blend lên layer dưới (locomotion)
- Debug draw skeleton: vẽ bone lines world-space (bật qua cvar)

**Particle System CPU**
- `ParticleEmitter` component: rate, burst, lifetime, start/end size, color gradient, gravity, drag
- `ParticleSystem` update: spawn → integrate (Euler/Verlet) → age → kill
- Instanced draw: 1 `drawInstanced()` cho N particle sống (không 1 drawcall/particle)
- Sort back-to-front theo depth cho transparent particle

**Particle System GPU (tùy chọn)**
- Compute shader spawn + update particle trên GPU (vd 1 triệu particle)
- Double-buffer particle buffer (ping-pong)
- Draw indirect: GPU quyết số particle cần vẽ

**VFX Integration**
- Particle + mesh + sprite depth-sort đúng trong transparent pass
- `ParticlePreset` asset: load emitter config từ file JSON (hot reload)

### Acceptance criteria

- Animation blend không giật (frame không "bật" đột ngột khi transition)
- 10k particle CPU ổn định 60 FPS nhờ instancing + job update
- State machine chuyển đúng khi gameplay trigger
- Root motion nhân vật di chuyển khớp animation (không trượt)

---

## Phase 14 — Scripting · Hot-reload nâng cao · Inspector

**Nhóm:** Polish · **Ước lượng:** 6–8 tuần · **Milestone:** `M14 — "Hot-reload <1s, inspector edit → game ngay"`

### Mục tiêu

Vòng lặp phát triển nhanh như engine script dù hoàn toàn C++.

### Task list

**Reflection / Introspection**
- Macro: `REFLECT_COMPONENT(MyComp, x, y, speed)` → meta-info về field
- Hoặc code-gen offline (tránh macro phức tạp, giảm compile time)
- Dùng cho: serialize/deserialize auto, inspector display, Lua binding auto

**DLL Hot-Reload Nâng Cao**
- Detect `.dll` change → trigger reload
- Trước reload: `serialize_snapshot()` → lưu state component cần giữ
- Sau reload: `deserialize_snapshot()` → restore state
- Document rõ giới hạn: không đổi layout struct giữa reload (detect + warn)
- Graceful fallback: nếu reload fail → giữ DLL cũ, báo lỗi rõ

**ImGui Scene Inspector**
- Entity tree: expand/collapse hierarchy, select entity
- Component panel: hiển thị field từ reflection meta-info, edit inline (float slider, color picker, vec3)
- Thay đổi runtime → thấy ngay trong game world
- Gizmo 2D: kéo entity bằng chuột (pick by ray, drag transform) — 3D gizmo Phase 18

**Prefab Override**
- Prefab instance có thể override một số field (khác giá trị default của prefab)
- Spawn prefab instance: instant (không đọc file runtime)
- Propagate prefab change: khi prefab đổi → instance tự update (trừ field đã override)

**Lua Binding (Tùy Chọn)**
- Embed Lua 5.4 + sol2 binding
- Auto-bind component qua reflection: `entity:get("Transform2D").position.x = 10`
- C++ vẫn là hot path; Lua chỉ cho logic level/cutscene/UI script
- Hot reload Lua script: watch `.lua` → reload script table

### Acceptance criteria

- Hot-reload không crash trong 90% trường hợp thường (thêm function, đổi constant, đổi logic)
- Reflection tự động serialize/deserialize `int/float/vec2/string/Handle`
- Inspector edit component → thấy ngay trong game không cần restart

---

## Phase 15 — Ship Game + Build Pipeline

**Nhóm:** Ship · **Ước lượng:** 4–6 tuần · **Milestone:** `M15 ★★ — "Game thật publish được"`

> **Engine xong khi ship được game thật.** Đây là acceptance test duy nhất tính.

### Mục tiêu

Build pipeline hoàn chỉnh, game thật publish trên itch.io (hoặc tương đương).

### Task list

**Build Pipeline**
- CMake release target: strip debug, LTO (Link-Time Optimization), optimized asset
- Windows: `.exe` + DLL dependencies bundle
- Linux: AppImage hoặc flatpak (no system dependency)
- Web: WASM + HTML shell (từ Phase 10)
- macOS: `.app` bundle (nếu có machine)

**Asset Cooker CI**
- `cooker import-all` tích hợp CI pipeline
- Output manifest + checksum → detect asset thay đổi (incremental cook)
- Asset bundle (1 file nén): `zstd` compress, streaming API cho asset lớn

**Save / Load**
- Serialize game state: scene snapshot, player progress, settings
- Binary format với version number (không corrupt khi format đổi minor)
- Test: save → load 1000 lần → không corrupt
- Cloud save stub (implement sau nếu cần Steam/itch)

**Settings & Localization**
- Config window: resolution, fullscreen, vsync, master/sfx/music volume, keybind rebind
- String table: `strings.vi.json`, `strings.en.json` → hot-swap locale runtime
- Font hỗ trợ Unicode đủ dùng (Latin + Vietnamese tối thiểu; CJK nếu target market)

**Crash Handler & Stability**
- Catch unhandled exception / SIGSEGV → log callstack
- Graceful shutdown: flush GPU, free asset, đóng file, flush log
- Memory leak detection: VMA report, ASAN trên Linux dev build

**Game Thật**
- Chọn 1 concept phù hợp engine (platformer, top-down, puzzle)
- Có: main menu, gameplay loop, win/lose condition, save high score, credits
- Playtest ít nhất 30 phút liên tục không crash
- **Publish lên itch.io** — đây là acceptance test duy nhất

### Acceptance criteria

- Cài trên máy không có dev environment, chạy được game
- Save/load không corrupt sau 100 lần
- WASM build chạy Chrome/Firefox không plugin
- **Đã publish lên itch.io hoặc tương đương — đây là finish line**

---

## Phase 16 — AI / Navigation

**Nhóm:** Bổ sung · **Ước lượng:** 4–6 tuần · **Milestone:** `M16 — "NPC tìm đường, behavior tree chạy"`

### Mục tiêu

Engine có AI đủ làm game với NPC di chuyển thông minh.

### Task list

**NavMesh**
- NavMesh baking offline: từ geometry (tilemap hoặc mesh 3D) → walkable polygon mesh
- `Recast & Detour` library (industry standard, MIT license) hoặc tự viết 2D grid-based
- `NavMeshComp`: gắn vào scene, load từ file baked
- Debug draw: vẽ navmesh polygon, path lines (bật qua cvar)

**Pathfinding**
- A* trên navmesh polygon (Detour) hoặc grid (2D game)
- Flow field: pre-compute vector field → nhiều agent cùng target → 1 query (hiệu quả cho RTS/crowd)
- Path smoothing: string-pulling algorithm → path tự nhiên hơn
- Async pathfinding: submit request → job system → callback khi có kết quả (không block game thread)

**Steering Behaviors**
- `SteeringComp`: seek, flee, arrive, pursue, evade, wander, separation, alignment, cohesion
- Combine: `SteeringSystem` tính tổng steering force → apply vào `Velocity`
- Obstacle avoidance: raycast check → steer quanh obstacle

**Behavior Tree**
- Node types: `Sequence`, `Selector`, `Parallel`, `Decorator`, `Leaf`
- Leaf node: `MoveTo(target)`, `AttackTarget()`, `Wait(seconds)`, `CheckCondition(fn)`
- `BehaviorTreeComp { BTreeHandle tree; BlackBoard bb; }`
- Hot reload Behavior Tree: định nghĩa từ JSON → hot swap không restart
- Debug draw: visualize tree state (running/success/failure) qua ImGui

**Perception System**
- `PerceptionComp`: sight (cone + range), hearing (radius)
- `PerceptionSystem`: kiểm tra entity nào trong tầm nhìn/nghe → trigger event
- Dùng spatial grid để query nhanh (không O(n²))

### Acceptance criteria

- NPC tìm đường vòng obstacle, cập nhật khi obstacle di chuyển
- 100 agent pathfinding async không drop frame
- Behavior tree NPC: tuần tra → phát hiện → tấn công → mất dấu → tuần tra
- NavMesh debug draw hiển thị đúng

---

## Phase 17 — Networking

**Nhóm:** Bổ sung · **Ước lượng:** 6–8 tuần · **Milestone:** `M17 — "2 client sync state qua mạng, không desync"`

### Mục tiêu

Nền tảng multiplayer: transport layer, serialization, client-server model.

### Task list

**Transport Layer**
- `INetworkTransport` interface (cô lập như RHI)
- Implement `ENetTransport` (ENet library — UDP reliable/unreliable/sequenced)
- Implement `WebSocketTransport` (cho web build — `libwebsockets` hoặc `uwebsockets`)
- Packet: header (type, sequence, ack, timestamp) + payload

**Serialization / Delta State**
- `NetSerializer`: bit-pack primitive types (bool = 1 bit, angle = 10 bit, v.v.)
- Delta encoding: chỉ gửi component đổi (không gửi toàn bộ state mỗi frame)
- `NetworkComp { uint32 netId; bool isOwner; bool isDirty; }` — đánh dấu entity được sync
- Snapshot interpolation: client nhận snapshot N, N-1 → interpolate → smooth render

**Client-Server Model**
- Authoritative server: server tính state chính thức, client predict + reconcile
- Client-side prediction: client apply input ngay (không chờ server) → smooth
- Server reconciliation: khi nhận state server → correct nếu lệch
- Entity spawning: server spawn → broadcast `SpawnMsg(netId, type, position)` → client spawn ghost

**Lag Compensation**
- Rollback: server rewind state về thời điểm client bắn → check hit → forward lại
- `HistoryBuffer<Transform>`: lưu N frame transform gần nhất để rewind

**Lobby / Session**
- Peer-to-peer lobby (P2P, 2–8 player) hoặc dedicated server (scale hơn)
- `NetworkSession`: connect, disconnect, host, join by IP/code
- Relay server stub (cho NAT traversal — STUN/TURN nếu cần sau)

### Acceptance criteria

- 2 client kết nối, di chuyển nhân vật, thấy nhau — không desync sau 10 phút
- Lag compensation: hit detection đúng khi ping 100ms
- Disconnect gracefully: game không crash khi 1 client ngắt kết nối

---

## Phase 18 — Scene Editor

**Nhóm:** Bổ sung · **Ước lượng:** 6–8 tuần · **Milestone:** `M18 — "Kéo-thả entity, undo/redo, prefab visual edit"`

### Mục tiêu

Editor trực quan đủ dùng cho game development, không cần tool bên ngoài.

### Task list

**Editor Application**
- Separate process hoặc embedded mode (khuyến nghị embedded — dùng chung engine runtime)
- Dockable window layout: Scene view | Entity hierarchy | Inspector | Asset browser | Console
- Dark theme, icon set (Tabler icons hoặc tương đương)

**Gizmo**
- 2D: translate (arrow), rotate (ring), scale (box handle)
- 3D: translate (axis arrow), rotate (3-ring), scale (axis box) — world space + local space
- Multi-select: move nhiều entity cùng lúc (average pivot)
- Grid snap: 0.1/0.5/1.0/5.0 unit snap

**Entity Picking**
- Click chuột trong viewport → raycast → pick entity (world-space ray vs AABB/mesh)
- Hover highlight: outline/tint entity đang hover
- Selection box: drag để select nhiều entity

**Undo/Redo Stack**
- `ICommand` interface: `execute()`, `undo()`
- Implement: `MoveEntityCmd`, `AddComponentCmd`, `DeleteEntityCmd`, `SetPropertyCmd`
- Ctrl+Z/Ctrl+Y; history depth (vd 100 operation)
- Group command: nhiều thao tác thành 1 undo step

**Prefab Editor**
- Mở prefab → edit như scene riêng → save → propagate change
- Override inspector: hiển thị field nào đang override (bold/highlight)
- "Revert to prefab" cho từng field

**Asset Browser**
- Duyệt thư mục asset, thumbnail preview (texture → preview, audio → waveform)
- Drag asset vào scene viewport → spawn entity với component phù hợp
- Import asset mới: kéo file từ OS → asset cooker auto-run

**Play Mode**
- Bấm Play → engine chạy game trong editor viewport (không cần build)
- Bấm Stop → restore scene về trạng thái trước Play
- Bấm Pause → có thể inspect state, step 1 frame

### Acceptance criteria

- Kéo-thả entity trong viewport, gizmo di chuyển mượt
- Undo/redo 20 operation không corrupt scene
- Edit prefab → save → instance trong scene tự update
- Play → Stop → scene restored về đúng trạng thái trước Play

---

## Checklist Engine "Hoàn chỉnh"

### Giai đoạn 2D (sau Phase 8)

- [ ] Mở/đóng cửa sổ, resize, fullscreen ổn định
- [ ] Render 10k+ sprite ổn định 60 FPS, batching đúng
- [ ] Camera 2D pan/zoom/rotate, world↔screen chính xác
- [ ] Spawn/destroy entity runtime không leak/crash
- [ ] Transform hierarchy cha-con đúng
- [ ] Load texture/atlas/font/audio/tilemap từ file
- [ ] Handle invalid hóa an toàn
- [ ] Hot reload texture + shader + code
- [ ] Text sắc nét nhiều cỡ; UI cơ bản bấm được
- [ ] Audio SFX + music, volume, loop
- [ ] Physics 2D: va chạm ổn định, callback ECS
- [ ] Input action mapping, gamepad support
- [ ] Job system, frame allocator, profiler CPU/GPU
- [ ] Debug draw, CVar console, asset cooker
- [ ] **Ship ít nhất 1 game mini** ← test thật sự

### Giai đoạn 3D (sau Phase 13)

- [ ] glTF static mesh load và render đúng depth
- [ ] PBR material (metallic/roughness/normal/AO)
- [ ] Shadow map với PCF
- [ ] IBL (BRDF LUT, prefilter env map)
- [ ] Post-processing: bloom, tone mapping, FXAA
- [ ] Render graph đa-pass (thêm pass = thêm node)
- [ ] Skeletal animation + blend
- [ ] Animation state machine
- [ ] Particle system (CPU instanced + GPU optional)
- [ ] VFX depth sort cùng scene 2D/3D

### Giai đoạn "Complete" (sau Phase 18)

- [ ] Hot-reload code DLL < 1s không mất state
- [ ] Reflection + ImGui inspector runtime
- [ ] AI: NavMesh, A*, Behavior Tree, Steering
- [ ] Networking: client-server, delta sync, lag compensation
- [ ] Scene Editor: gizmo, undo/redo, prefab visual, play mode
- [ ] Build pipeline đa nền tảng (Windows/Linux/WASM)
- [ ] Save/load ổn định sau 100 lần
- [ ] Crash handler, graceful shutdown, memory report sạch
- [ ] **Ship ít nhất 1 game hoàn chỉnh** ← finish line duy nhất

---

## Lỗi kiến trúc cần tránh

**Abstraction & Dependency**
- ❌ `vulkan.h` hoặc `glfw.h` rò lên tầng renderer/gameplay — CI grep cấm
- ❌ Trả native type (`VkFormat`, `GLFWwindow*`) ra interface — dùng enum/handle riêng
- ❌ Dependency hai chiều (core biết renderer) — một chiều xuống, tuyệt đối
- ❌ Virtual mỗi sprite (hot path) — DOD ở hot loop, virtual ở ranh giới

**RHI**
- ❌ RHI kiểu OpenGL set-state — WebGPU-lite explicit
- ❌ Tạo descriptor/pipeline mỗi drawcall — cache theo hash
- ❌ Hủy GPU resource ngay — defer theo frames-in-flight
- ❌ Ghi đè buffer GPU đang đọc — ring buffer per-frame

**ECS/DOD**
- ❌ Component có logic/virtual/`update()` — component là POD
- ❌ Mỗi entity là object rải rác OOP — storage contiguous
- ❌ Trộn gameplay logic vào extraction — extraction thuần đọc
- ❌ Đệ quy tính transform toàn cây mỗi frame — cache + dirty flag

**Quy trình**
- ❌ Tối ưu trước khi đo — profiler trước hết
- ❌ Over-engineer trước khi có game chạy — chạy được trước
- ❌ Xây engine mãi không ship game — ship mini sau Phase 6
- ❌ Làm 3D song song khi 2D chưa xong — 2D xong → 3D
- ❌ Tự viết mọi thứ vì sĩ diện — dùng EnTT/Box2D/miniaudio ở chỗ không cần kiểm soát
- ❌ Bỏ qua test/CI — test math/core + CI grep abstraction từ Phase 0

---

*Roadmap này là living document — cập nhật khi có game thật chạy qua và lộ ra điều cần thay đổi.*
