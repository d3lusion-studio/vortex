# Vortex — API Reference

> Tham chiếu API public theo từng layer. Mỗi mục liệt kê kiểu/hàm/phương thức kèm
> chữ ký và mô tả ngắn. Dependency đi **một chiều xuống** (xem
> [architecture.md](architecture.md)); một header chỉ được include từ layer của
> nó trở xuống.
>
> Quy ước: `pf` = platform, `rhi` = render hardware interface. Tất cả nằm trong
> namespace `vortex` (ví dụ `vortex::rhi::IGraphicsDevice`).

## Mục lục

- [Core](#core) — types, math, handle, log, assert, string id, console, profiler, memory
- [Platform](#platform-pf) — window, input, clock, filesystem, dynamic library
- [RHI](#rhi) — device, swapchain, command list, resources, enums
- [Renderer](#renderer) — camera, sprite batch, mesh, render graph, material
- [ECS](#ecs) — entity, registry, components, systems, scene
- [Asset](#asset) — asset manager, hot reload
- [Audio](#audio)
- [Physics 2D](#physics-2d)
- [Text](#text)
- [UI](#ui)
- [Jobs](#jobs)
- [Debug](#debug) — debug draw, ImGui layer, inspector

---

## Core

`#include "vortex/core/..."` — không phụ thuộc bất kỳ layer nào khác.

### types.hpp

Bí danh số nguyên cố định chiều rộng dùng xuyên suốt engine:

```cpp
i8  i16 i32 i64     // số nguyên có dấu
u8  u16 u32 u64     // số nguyên không dấu
f32 f64             // float / double
usize               // std::size_t
```

### handle.hpp — `Handle<Tag>`

Tham chiếu tài nguyên an toàn `{index, generation}` thay cho raw pointer.

```cpp
template <typename Tag> struct Handle {
    u32 index      = kInvalid;   // 0xFFFFFFFF khi rỗng
    u32 generation = 0;
    bool valid() const noexcept; // index != kInvalid
    bool operator==(const Handle&) const noexcept = default;
};
```

### Math — `vec2/3/4`, `mat4`, `quat`, `rect`

Tất cả là POD, `constexpr` ở đâu có thể. Lưu trữ **column-major**, khớp GLSL.

| Kiểu | Thành phần / hàm chính |
|---|---|
| `Vec2` | `x, y`; `+ - *(f32)`; `dot(a,b)`, `length(v)`, `normalize(v)` |
| `Vec3` | `x, y, z`; `+ - *`; `dot`, `cross`, `length`, `normalize` |
| `Vec4` | `x, y, z, w` |
| `Rect` | `x, y, width, height`; `left/right/top/bottom()`, `contains(px,py)`; hằng `kFullUV{0,0,1,1}` |

`Mat4` (hàm tĩnh dựng ma trận + toán tử):

```cpp
static Mat4 identity();
static Mat4 translation(f32 x, f32 y, f32 z);
static Mat4 scaling(f32 x, f32 y, f32 z);
static Mat4 rotationX/Y/Z(f32 radians);
static Mat4 ortho(f32 l, f32 r, f32 b, f32 t, f32 near, f32 far);   // 2D — Vulkan Y-down, depth [0,1]
static Mat4 orthoRH(f32 l, f32 r, f32 b, f32 t, f32 near, f32 far); // 3D — right-handed, đi với lookAt
static Mat4 perspective(f32 fovYRadians, f32 aspect, f32 near, f32 far);
static Mat4 lookAt(Vec3 eye, Vec3 center, Vec3 up);
f32&  at(int row, int col);                 // truy cập phần tử
Mat4  operator*(const Mat4&) const;         // nhân ma trận
Vec4  operator*(const Vec4&) const;         // biến đổi vector
```

> ⚠️ **`ortho` và `orthoRH` không thay thế được cho nhau.**
>
> | Hàm | Dùng cho | Quy ước |
> |---|---|---|
> | `Mat4::ortho` | camera 2D | Y-down kiểu Vulkan, z là khoá sắp lớp |
> | `Mat4::orthoRH` | camera 3D trực giao, shadow map | thuận tay phải, đi với `lookAt`, nhìn theo −Z |
>
> Dùng nhầm hàm thì **toàn bộ cảnh bị clip sạch** và render target ra trống trơn — không có
> lỗi, không có cảnh báo, không có validation error nào. Đây là một trong những lỗi tốn thời
> gian nhất trong repo này. Xem [examples/camera3d](../examples/camera3d/).

`Quat` (đơn vị mặc định):

```cpp
static Quat identity();
static Quat fromAxisAngle(Vec3 axis, f32 radians);
Quat  operator*(const Quat&) const;         // ghép xoay
Quat  normalized() const;
Mat4  toMat4() const;                        // ma trận xoay
```

### log.hpp

```cpp
enum class LogLevel { Trace, Info, Warn, Error };
void log(LogLevel level, const char* category, const char* fmt, ...);   // printf-style

VORTEX_INFO(cat, ...)   // macro tiện dụng
VORTEX_WARN(cat, ...)
VORTEX_ERROR(cat, ...)
```

### assert.hpp

```cpp
VORTEX_ASSERT(cond, msg);   // in file/line + msg rồi abort khi cond sai
                            // no-op nếu không định nghĩa VORTEX_ENABLE_ASSERTS
```

### string_id.hpp — `StringId`

Hash chuỗi compile-time (FNV-1a 64-bit) để so sánh/khóa nhanh.

```cpp
struct StringId { u64 value; bool operator==(...) const = default; };
constexpr u64      fnv1a64(const char* str);
constexpr StringId stringId(const char* str);
constexpr StringId operator""_sid(const char* str, usize);   // "Player"_sid
```

### console.hpp — `CVar`, `Console`

Biến cấu hình runtime + lệnh console.

```cpp
enum class CVarType { Bool, Int, Float, String };

class CVar {
    const char* name() / help() const;
    CVarType type() const;
    bool asBool();  i32 asInt();  f32 asFloat();  const std::string& asString();
    void set(bool|i32|f32|std::string);
    bool setFromString(std::string_view);
    void onChange(std::function<void(const CVar&)> cb);
    std::string valueString() const;
};

class Console {
    using Command = std::function<void(const std::vector<std::string>& args)>;
    static Console& global();
    CVar* registerBool  (const char* name, bool def,               const char* help = "");
    CVar* registerInt   (const char* name, i32 def,                const char* help = "");
    CVar* registerFloat (const char* name, f32 def,                const char* help = "");
    CVar* registerString(const char* name, const std::string& def, const char* help = "");
    void  registerCommand(const char* name, Command fn, const char* help = "");
    CVar* find(std::string_view name);
    bool  execute(std::string_view line);     // "r.vsync 0"
    std::vector<Entry> list() const;
};
```

Helper tham chiếu: `CVarBoolRef`, `CVarIntRef`, `CVarFloatRef`.

### profiler.hpp

```cpp
void beginFrame();  void endFrame();
void record(const char* name, f64 milliseconds);
const std::vector<Entry>& lastFrame();         // Entry{ name, ms, calls }

VORTEX_PROFILE_ZONE("name");      // đo scope hiện tại (nối Tracy nếu bật)
VORTEX_PROFILE_FRAME_MARK();      // mốc kết thúc frame
```

### memory/

`FrameAllocator` — arena tuyến tính, reset mỗi frame:

```cpp
explicit FrameAllocator(usize capacity);
void* allocate(usize size, usize align = alignof(max_align_t));
T*    allocArray<T>(usize count);
T*    create<T>(Args&&...);          // dựng 1 object (huỷ trivially khi reset)
void  reset();                       // O(1), trả lại toàn bộ
usize used() / capacity() const;
```

`ObjectPool<T, Tag>` — bảng slot `{index, generation}` tái dùng:

```cpp
HandleT create<Args...>(Args&&...);
T*      get(HandleT);                 // nullptr nếu handle stale
void    destroy(HandleT);             // tăng generation
void    forEach(Fn);
usize   aliveCount() / capacity() const;
void    clear();
```

---

## Platform (`pf`)

`#include "vortex/platform/..."` — cô lập GLFW. Mọi thứ tạo qua factory trả
`std::unique_ptr`.

### window.hpp — `IWindow`

```cpp
struct WindowDesc { int width; int height; const char* title; bool resizable = true; };
enum class NativeHandleKind { Unknown, Xlib, Wayland, Win32, Cocoa };

class IWindow {
    bool shouldClose() const;
    void pollEvents();
    void getFramebufferSize(int& w, int& h) const;
    NativeHandleKind nativeHandleKind() const;
    void* nativeWindowHandle()  const;     // con trỏ mờ cho RHI tạo surface
    void* nativeDisplayHandle() const;
};
std::unique_ptr<IWindow> createWindow(const WindowDesc&);
```

### input.hpp — `IInputProvider`

```cpp
enum class Key { /* A..Z, 0..9, Space, Escape, Enter, arrows, F1..., ... Count */ };
enum class MouseButton { Left, Right, Middle, Count };

class IInputProvider {
    bool isKeyDown(Key) const;       bool isKeyPressed(Key) const;   bool isKeyReleased(Key) const;
    bool isMouseDown(MouseButton) const;
    bool isMousePressed(MouseButton) const;  bool isMouseReleased(MouseButton) const;
    void mousePosition(float& x, float& y) const;
    float scrollDelta() const;
    void newFrame();                 // gọi mỗi frame để cập nhật edge (pressed/released)
};
std::unique_ptr<IInputProvider> createInputProvider(IWindow& window);
```

### clock.hpp — `IClock`

```cpp
class IClock {
    f64  time() const;          // giây từ lúc tạo
    f64  deltaTime() const;     // giây của frame trước
    void tick();                // gọi đầu mỗi frame
};
std::unique_ptr<IClock> createClock();
```

### filesystem.hpp — `IFileSystem`

```cpp
class IFileSystem {
    std::vector<std::byte> readFile(const char* path) const;
    bool writeFile(const char* path, const void* data, usize size);
    bool exists(const char* path) const;
};
std::unique_ptr<IFileSystem> createFileSystem();
```

### dynamic_library.hpp — `DynamicLibrary` (hot-reload game code)

```cpp
class DynamicLibrary {
    static std::unique_ptr<DynamicLibrary> load(const char* path);
    void* symbol(const char* name) const;
    Fn    symbolAs<Fn>(const char* name) const;     // cast tiện dụng
};
i64 fileModifiedTime(const char* path);             // mtime, cho watch reload
```

---

## RHI

`#include "vortex/rhi/..."` — abstraction phần cứng đồ hoạ, **không** lộ Vulkan/WebGPU.
Phong cách WebGPU-lite explicit. Tài nguyên tham chiếu qua handle.

### Tạo device — device.hpp

```cpp
enum class GraphicsAPI { Vulkan, WebGPU };

std::unique_ptr<IGraphicsDevice> createDevice(GraphicsAPI api, pf::IWindow&);
std::unique_ptr<IGraphicsDevice> createDevice(pf::IWindow&);   // chọn theo defaultGraphicsAPI()
GraphicsAPI defaultGraphicsAPI();   // đọc env VORTEX_RHI_API ("vulkan"|"webgpu"), fallback build-time
```

### `IGraphicsDevice`

```cpp
// Tạo / huỷ tài nguyên (trả handle, không trả pointer)
BufferHandle    createBuffer(const BufferDesc&, const void* initialData = nullptr);
void            destroyBuffer(BufferHandle);
void            updateBuffer(BufferHandle, const void* data, u64 size, u64 offset = 0);
TextureHandle   createTexture(const TextureDesc&, const void* pixels = nullptr);
void            destroyTexture(TextureHandle);
SamplerHandle   createSampler(const SamplerDesc&);
void            destroySampler(SamplerHandle);
BindGroupHandle createBindGroup(const BindGroupDesc&);
void            destroyBindGroup(BindGroupHandle);
PipelineHandle  createGraphicsPipeline(const GraphicsPipelineDesc&);
void            destroyPipeline(PipelineHandle);

std::unique_ptr<ISwapchain> createSwapchain(const SwapchainDesc&, pf::IWindow&);

// Vòng đời frame
FrameContext beginFrame(ISwapchain&);   // .valid=false nếu nên skip (vd resize)
void         endFrame();
void         waitIdle();

// Multithread recording (Phase 7)
ICommandList* acquireSecondaryCommandList();
void          executeSecondary(ICommandList& primary, ICommandList* const* lists, u32 count);

f64 gpuFrameTimeMs() const;             // 0 nếu timestamp không hỗ trợ
```

### `ISwapchain` — swapchain.hpp

```cpp
Format format() const;
void   getExtent(u32& width, u32& height) const;
void   requestResize(u32 width, u32 height);
```

### `ICommandList` — command_list.hpp

```cpp
void beginRenderPass(const RenderPassDesc&);   void endRenderPass();
void transition(TextureHandle, ResourceState newState);
void setPipeline(PipelineHandle);
void setBindGroup(u32 slot, BindGroupHandle);  // slot = set index
void pushConstants(const void* data, u32 size);// ≤ 128 byte (giới hạn WebGPU)
void setViewport(const Viewport&);
void setScissor(i32 x, i32 y, u32 width, u32 height);
void setVertexBuffer(u32 slot, BufferHandle, u64 offset = 0);
void setIndexBuffer(BufferHandle, IndexType);
void draw(u32 vertexCount, u32 instanceCount = 1, u32 firstVertex = 0, u32 firstInstance = 0);
void drawIndexed(u32 indexCount, u32 instanceCount = 1, u32 firstIndex = 0,
                 i32 vertexOffset = 0, u32 firstInstance = 0);
void dispatch(u32 groupCountX, u32 groupCountY, u32 groupCountZ);   // compute stub (chưa wired)
```

### Mô tả tài nguyên — rhi_types.hpp

```cpp
struct BufferDesc   { u64 size; BufferUsage usage; MemoryDomain domain; const char* debugName; };
struct TextureDesc  { u32 width, height; Format format; TextureUsage usage; const char* debugName; };
struct SamplerDesc  { Filter minFilter, magFilter; AddressMode addressU, addressV; };
struct SwapchainDesc{ u32 width, height; PresentMode present; };

struct BindGroupDesc {              // hoặc texture+sampler, hoặc uniform buffer
    TextureHandle texture;  SamplerHandle sampler;
    BufferHandle  uniformBuffer;  u64 uniformSize;     // khi valid → uniform group
};

struct VertexAttribute { u32 location; VertexFormat format; u32 offset; };
struct VertexLayout    { u32 stride; std::vector<VertexAttribute> attributes; };

struct GraphicsPipelineDesc {
    std::vector<std::byte> vertexSpirv, fragmentSpirv;
    VertexLayout      vertexLayout;
    PrimitiveTopology topology;     CullMode cull;
    Format colorFormat;             bool alphaBlend;
    bool hasMaterialTexture;        bool hasUniformBuffer;
    u32  pushConstantSize;
    bool depthTest, depthWrite;     CompareOp depthCompare;   Format depthFormat;
    u32  sampleCount;               // MSAA (mặc định 1)
    const char* debugName;
};

struct Viewport        { f32 x, y, width, height, minDepth, maxDepth; };
struct ColorAttachment { TextureHandle target; LoadOp loadOp; StoreOp storeOp; f32 clearColor[4]; };
struct DepthAttachment { TextureHandle target; LoadOp loadOp; StoreOp storeOp; f32 clearDepth; };
struct RenderPassDesc  { ColorAttachment color; DepthAttachment depth; u32 width, height; bool secondaryContents; };
struct FrameContext    { ICommandList* cmd; TextureHandle backbuffer; u32 width, height; bool valid; };
```

### Handle & enum — rhi_handle.hpp / rhi_enums.hpp

Handle: `BufferHandle`, `TextureHandle`, `SamplerHandle`, `PipelineHandle`, `BindGroupHandle`.

Enum chính: `Format`, `PresentMode`, `BufferUsage` (flags), `MemoryDomain {Device, Upload}`,
`LoadOp`, `StoreOp`, `PrimitiveTopology`, `IndexType {U16,U32}`,
`VertexFormat {Float1..4, UNorm4x8, UInt1}`, `CullMode {None,Front,Back}`,
`TextureUsage` (flags), `Filter`, `AddressMode`, `CompareOp`, `ResourceState`.
Hằng: `kMaxFramesInFlight = 2`.

---

## Renderer

`#include "vortex/renderer/..."` — chỉ thấy RHI.

### camera.hpp — `Camera` (2D + 3D)

```cpp
struct Camera {
    enum class Mode { Orthographic, Perspective };
    Mode mode = Orthographic;
    Vec3 position;
    f32  viewportWidth, viewportHeight, zoom;   // ortho
    Vec3 target, up;  f32 fovYRadians, aspect, nearZ, farZ;   // perspective
    Mat4 projection() const;   Mat4 view() const;   Mat4 viewProjection() const;
};
```

`camera2d.hpp` — `Camera2D` chuyên 2D: `position`, `zoom`, viewport; `projection/view/viewProjection()`,
`screenToWorld(sx, sy)`.

### sprite_batch.hpp — `SpriteBatch`

Gom quad cùng texture thành ít drawcall.

```cpp
struct Sprite { Vec2 position, size; f32 rotation; Vec4 color; Rect uv; TextureHandle texture; i32 layer; };

SpriteBatch(IGraphicsDevice&, Format colorFormat, u32 maxSprites = 100000,
            Format depthFormat = Format::Undefined);   // depthFormat=Undefined → không depth test
void begin(const Mat4& viewProjection);
void draw(const Sprite&);
void drawSprite(TextureHandle, Vec2 pos, Vec2 size, Vec4 color = {1,1,1,1}, Rect uv = kFullUV, i32 layer = 0);
void submit(const RenderItem&);                  // item đã transform sẵn (từ ECS extract)
void submit(const RenderItem* items, usize count);
void end(ICommandList& cmd);                     // sort theo layer/texture, upload, drawcall
u32  drawCallCount() / spriteCount() const;
```

`render_item.hpp` — `RenderItem { Mat4 transform; Vec4 color; Rect uv; TextureHandle texture; i32 layer; }`.

### mesh.hpp — `MeshRenderer` (3D, Phase 11)

Vẽ mesh indexed có lighting (Blinn-Phong directional), depth test/write + back-face cull.

```cpp
struct MeshVertex { Vec3 position; Vec3 normal; Vec2 uv; };
struct DirectionalLight { Vec3 direction; Vec3 color; f32 intensity; Vec3 ambient; };
using  MeshHandle = Handle<MeshTag>;
struct MeshInstance { MeshHandle mesh; Mat4 model; Vec4 color; };

// Sinh mesh sẵn (winding CCW, hợp back-face cull)
struct MeshData { std::vector<MeshVertex> vertices; std::vector<u32> indices; };
MeshData makeCube(f32 size = 1.0f);
MeshData makePlane(f32 size = 1.0f);
MeshData makeSphere(u32 rings = 16, u32 sectors = 24, f32 radius = 0.5f);

class MeshRenderer {
    MeshRenderer(IGraphicsDevice&, Format colorFormat, Format depthFormat);
    MeshHandle createMesh(const MeshVertex*, usize vcount, const u32*, usize icount);
    MeshHandle createMesh(const MeshData&);
    void destroyMesh(MeshHandle);
    void begin(const Camera&, const DirectionalLight&);
    void drawMesh(MeshHandle, const Mat4& model, Vec4 color = {1,1,1,1});
    void submit(const MeshInstance&);
    void submit(const MeshInstance* items, usize count);
    void end(ICommandList& cmd);
    u32  drawCallCount() const;
};
```

### render_graph.hpp — `RenderGraph`

Khai báo pass (đọc/ghi resource), tự chèn barrier, record theo thứ tự.

```cpp
class RenderGraph {
    using ResourceId = u32;
    explicit RenderGraph(IGraphicsDevice&);
    void beginFrame();                                          // gọi mỗi frame valid
    ResourceId importBackbuffer(TextureHandle, u32 w, u32 h);
    ResourceId colorTarget(const char* name, u32 w, u32 h, Format);   // target tạm
    ResourceId depthTarget(const char* name, u32 w, u32 h);           // D32_SFLOAT

    class PassBuilder {                                          // dùng trong setup của addPass
        void sample(ResourceId);                                // input shader-read
        void writeColor(ResourceId, const f32 clear[4], LoadOp = LoadOp::Clear);
        void writeDepth(ResourceId, f32 clearDepth = 1.0f, LoadOp = LoadOp::Clear);
    };
    void addPass(const char* name, std::function<void(PassBuilder&)> setup,
                 std::function<void(ICommandList&)> execute);
    void execute(ICommandList& cmd);                            // order + barrier + record
    TextureHandle    texture(ResourceId) const;
    BindGroupHandle  sampledBindGroup(ResourceId);              // để sample ở pass sau
};
```

### material.hpp — `Material`

`struct Material { PipelineHandle pipeline; BindGroupHandle bindGroup; ...; bool operator==(...); }`.

---

## ECS

`#include "vortex/ecs/..."`. Sparse-set, component là POD.

### entity.hpp / registry.hpp

```cpp
using Entity = Handle<EntityTag>;

class Registry {
    Entity create();
    bool   alive(Entity) const;
    void   destroy(Entity);
    T&     emplace<T>(Entity, Args&&...);     // thêm/ghi đè component
    void   remove<T>(Entity);
    bool   has<T>(Entity);
    T*     tryGet<T>(Entity);                 // nullptr nếu không có
    T&     get<T>(Entity);                    // assert nếu không có
    void   view<First, Rest...>(Fn);          // duyệt entity có đủ các component
    void   each(Fn) const;                    // duyệt mọi entity sống
    usize  aliveCount() / capacity() const;
};
```

### components.hpp

| Component | Trường |
|---|---|
| `Transform2D` | `Vec2 position; f32 rotation; Vec2 scale` |
| `WorldTransform2D` | `Mat4 matrix` (cache, do `updateTransforms` tính) |
| `Parent` | `Entity value` |
| `SpriteComp` | `TextureHandle texture; Vec4 color; Rect uv; Vec2 size; i32 layer` |
| `Velocity` | `Vec2 value` |
| `Transform3D` | `Vec3 position; Quat rotation; Vec3 scale` (Phase 11) |
| `MeshComp` | `renderer::MeshHandle mesh; Vec4 color` (Phase 11) |

### systems.hpp

```cpp
void updateTransforms(Registry&);                                       // local→world theo cha-con
void extractSprites(Registry&, std::vector<renderer::RenderItem>& out);
void extractSpritesParallel(Registry&, jobs::JobSystem&, std::vector<renderer::RenderItem>& out);
void extractMeshes(Registry&, std::vector<renderer::MeshInstance>& out); // (Transform3D+MeshComp)→model
```

### scene.hpp — `Scene` (facade 2D)

```cpp
class Scene {
    using System = std::function<void(Registry&, f32)>;
    Entity spawn();                          // sẵn Transform2D + WorldTransform2D
    void   destroy(Entity);
    Registry& registry();
    void   addSystem(System);
    void   update(f32 dt);                   // chạy systems rồi updateTransforms
    void   extract(std::vector<renderer::RenderItem>& out);
    renderer::Camera2D camera;
};
```

---

## Asset

`#include "vortex/asset/asset_manager.hpp"`.

```cpp
class AssetManager {
    AssetManager(rhi::IGraphicsDevice&, pf::IFileSystem&);
    TextureHandle loadTexture(const char* path);     // cache theo path; cùng path → cùng handle
    const TextureAsset* get(TextureHandle) const;
    rhi::TextureHandle  gpuTexture(TextureHandle) const;
    void unload(TextureHandle);
    void beginFrame();                               // tiến counter defer-destroy
    u32  pollHotReload();                            // re-import file đã đổi, giữ nguyên handle
};
```

---

## Audio

`#include "vortex/audio/audio.hpp"`.

```cpp
using SoundHandle = Handle<SoundTag>;
class IAudioEngine {
    SoundHandle load(const char* path);
    void unload(SoundHandle);
    void play(SoundHandle, bool loop = false);
    void stop(SoundHandle);
    void setVolume(SoundHandle, f32 volume);
    void setMasterVolume(f32 volume);
};
std::unique_ptr<IAudioEngine> createAudioEngine();
```

---

## Physics 2D

`#include "vortex/physics/..."` — bọc Box2D.

```cpp
enum class BodyType { Static, Kinematic, Dynamic };
struct RigidBody2D   { BodyType type; f32 density, friction, restitution, gravityScale; bool fixedRotation; };
struct BoxCollider2D { Vec2 halfExtents; bool isSensor; };

class PhysicsWorld {
    using ContactCallback = std::function<void(ecs::Entity, ecs::Entity)>;
    explicit PhysicsWorld(Vec2 gravity = {0,-9.81f}, f32 pixelsPerMeter = 50.0f);
    void step(ecs::Registry&, f32 dt);               // sync Transform2D ↔ body, chạy solver
    void setContactCallback(ContactCallback);
    void setGravity(Vec2);
    void applyLinearImpulse(ecs::Entity, Vec2 impulse);
    void setLinearVelocity(ecs::Entity, Vec2 velocity);
};
```

---

## Text

`#include "vortex/text/..."`.

```cpp
class Font {
    struct Glyph { Rect uv; Vec2 size, offset; f32 advance; };
    static std::unique_ptr<Font> loadFromFile(rhi::IGraphicsDevice&, pf::IFileSystem&,
                                              const char* path, f32 pixelHeight);
    const Glyph* glyph(char c) const;
    rhi::TextureHandle atlas() const;
    Vec2 measure(std::string_view text) const;
};

// text_renderer.hpp — đẩy glyph quad vào SpriteBatch
void drawText(renderer::SpriteBatch&, const Font&, std::string_view text,
              Vec2 topLeft, Vec4 color = {1,1,1,1}, f32 scale = 1.0f, i32 layer = 0);
```

---

## UI

`#include "vortex/ui/ui.hpp"` — immediate-mode tối giản (game UI).

```cpp
struct InputState { Vec2 mouse; bool down, pressed, released; };
struct Style { Vec4 panel, button, hovered, active, text; f32 textScale; i32 baseLayer; };

class UI {
    UI(rhi::IGraphicsDevice&);
    void begin(renderer::SpriteBatch&, const text::Font&, const InputState&);
    void end();
    // API tuyệt đối (tự đặt vị trí)
    void panel(Vec2 center, Vec2 size, Vec4 color);
    void label(Vec2 center, std::string_view text [, Vec4 color]);
    bool button(Vec2 center, Vec2 size, std::string_view label);
    // API layout cột
    void beginColumn(Vec2 topCenter, Vec2 widgetSize, f32 spacing);
    bool button(std::string_view label);
    void label(std::string_view text);
    Style style;
};
```

---

## Jobs

`#include "vortex/jobs/job_system.hpp"`.

```cpp
class JobSystem {
    explicit JobSystem(u32 workerCount = 0);   // 0 = số logical core
    void parallelFor(usize count, const std::function<void(usize)>& body, usize grain = 1024);
    u32  workerCount() const;
};
```

---

## Debug

`#include "vortex/debug/..."`.

### debug_draw.hpp — `DebugDraw`

```cpp
class DebugDraw {
    using Category = u32;  static constexpr Category kDefault = 0;
    explicit DebugDraw(rhi::IGraphicsDevice&);
    void setEnabled(Category, bool);   bool enabled(Category) const;
    void begin();                                      // xoá primitive frame trước
    void line(Vec2 a, Vec2 b, Vec4 color, f32 thickness = 1.5f, Category = kDefault);
    void box(Vec2 center, Vec2 size, Vec4 color, f32 thickness = 1.5f, Category = kDefault);
    void filledBox(Vec2 center, Vec2 size, Vec4 color, Category = kDefault);
    void circle(Vec2 center, f32 radius, Vec4 color, i32 segments = 24, f32 thickness = 1.5f, Category = kDefault);
    void text(Vec2 topLeft, std::string_view, Vec4 color, Category = kDefault);
    void flush(renderer::SpriteBatch&, const text::Font* = nullptr, i32 layer = 5000);
    u32  primitiveCount() const;
};
```

### imgui_layer.hpp — `ImGuiLayer` (debug UI)

```cpp
struct ImGuiInput { f32 displayWidth, displayHeight; Vec2 mouse; bool mouseDown[3]; f32 scroll; };
class ImGuiLayer {
    ImGuiLayer(rhi::IGraphicsDevice&, rhi::Format colorFormat);
    void newFrame(const ImGuiInput&, f32 dt);
    void render(rhi::ICommandList&);
    void showDemoWindow(bool* open = nullptr);
    bool wantsMouse() / wantsKeyboard() const;
};
```

### inspector.hpp — `EntityInspector`

```cpp
class EntityInspector {
    void draw(ecs::Scene&);          // ImGui: cây entity + component editor
    void draw(ecs::Registry&);
};
```

---

Xem thêm: [examples.md](examples.md) (cách build & chạy demo), [architecture.md](architecture.md),
[getting_started.md](getting_started.md), [coding_conventions.md](coding_conventions.md).
