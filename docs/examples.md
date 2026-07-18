# Vortex — Examples & Smoke Testing

> Hướng dẫn build, chạy, và dùng các example như **smoke test** (kể cả headless,
> không cần xem cửa sổ). Mỗi example link với thư viện gộp `Vortex::vortex` và
> nằm ở `examples/<tên>/`.
>
> **Mỗi example đều có README riêng** trong thư mục của nó, giải thích kỹ hơn bảng dưới đây.

## Build

Dùng CMake presets (xem [getting_started.md](getting_started.md)). Output ở `build/<preset>/`.

```bash
# Configure (một lần / khi đổi CMake)
cmake --preset debug            # hoặc release / relwithdebinfo

# Build tất cả
cmake --build build/debug

# Build một example (tên target — xem cột "Target" bên dưới)
cmake --build build/debug --target mesh3d
```

Binary nằm tại `build/debug/examples/<target>`, ví dụ `./build/debug/examples/mesh3d`.

> ⚠️ **Tên target không phải lúc nào cũng trùng tên thư mục.** Ba trường hợp lệch:
>
> | Thư mục | Target |
> |---|---|
> | `game/` | `game_example` |
> | `debug/` | `debug_draw` |
> | `text/` | `text_check` |

## Chạy

```bash
./build/debug/examples/mesh3d
```

Phần lớn example mở một cửa sổ và lặp tới khi bấm **ESC** (hoặc đóng cửa sổ).
Trên Wayland, GLFW tự dùng X11/XWayland (xem log khởi động) — bình thường.

### Biến môi trường dùng chung

| Biến | Phạm vi | Ý nghĩa |
|---|---|---|
| `VORTEX_RHI_API` | mọi example | Chọn backend: `vulkan`/`vk` hoặc `webgpu`/`wgpu`. Mặc định Vulkan. |
| `VORTEX_MAX_FRAMES` | hầu hết example | Chạy đúng N frame rồi thoát sạch (0 = vô hạn). Dùng cho smoke test headless/CI. |
| `VORTEX_LOG` | mọi example | Mức log. |
| `VORTEX_FONT_PATH` | example có chữ | Chỉ định file TTF, bỏ qua bước dò font hệ thống. |
| `VORTEX_SCREENSHOT` | xem bảng dưới | Ghi frame ra file PPM. |

Biến riêng của từng example nằm trong README của example đó.

Chuyển backend chỉ là đổi env — **không sửa một dòng renderer/gameplay nào**:

```bash
VORTEX_RHI_API=webgpu ./build/debug/examples/mesh3d
```

## Danh sách example

Cột **CI** đánh dấu example chạy **headless** (không cần cửa sổ; ✅ = không cần cả GPU) và tự
kiểm tra rồi thoát mã khác 0 khi sai — dùng được làm test hồi quy.

### Cơ bản

| Target | Thư mục | Demo gì | Điều khiển | CI |
|---|---|---|---|---|
| `hello` | [hello/](../examples/hello/) | Log, toán, `Handle` — **không cần GPU** | — | ✅ |
| `window` | [window/](../examples/window/) | Cửa sổ + input, không có RHI | chuột/phím, ESC | |
| `triangle` | [triangle/](../examples/triangle/) | Tam giác qua RHI thô (pipeline + draw) | ESC | |

### Render 2D

| Target | Thư mục | Demo gì | Điều khiển | CI |
|---|---|---|---|---|
| `sprites` | [sprites/](../examples/sprites/) | `SpriteBatch` nhiều texture, camera pan/zoom | WASD, ESC | |
| `sprite2d` | [sprite2d/](../examples/sprite2d/) | Lọc, tiling, lật, anchor, vẽ bằng CPU | ESC | |
| `atlas` | [atlas/](../examples/atlas/) | Atlas gộp 64 draw call thành 1 | Space giữ chế độ, ESC | |
| `mesh2d` | [mesh2d/](../examples/mesh2d/) | Hình, cung, màu theo đỉnh, blend mode, 9-patch | Space, ESC | |
| `bloom2d` | [bloom2d/](../examples/bloom2d/) | Bloom + ACES qua `AppConfig::postProcess` | Space bật/tắt, ESC | |
| `particles` | [particles/](../examples/particles/) | Khói, đài phun, cụm nổ theo chuột | chuột, ESC | |

### Render 3D

| Target | Thư mục | Demo gì | Điều khiển | CI |
|---|---|---|---|---|
| `mesh3d` | [mesh3d/](../examples/mesh3d/) | Sân chơi 3D: forward/deferred, SSAO, bóng, decal… | ESC | |
| `camera3d` | [camera3d/](../examples/camera3d/) | Split screen, Orthographic3D, ray cast | ESC | |
| `scene_viewer` | [scene_viewer/](../examples/scene_viewer/) | Nạp glTF qua `App` + camera orbit | A/D, ESC | |
| `gizmos3d` | [gizmos3d/](../examples/gizmos3d/) | Lưới, trục, hộp/cầu khung dây | ESC | GPU |
| `instancing` | [instancing/](../examples/instancing/) | Cả lưới quad trong **một** draw call | ESC | GPU |
| `animated` | [animated/](../examples/animated/) | Shader động qua push constant | ESC | GPU |
| `shader_defs` | [shader_defs/](../examples/shader_defs/) | 4 biến thể SPIR-V + `PipelineCache` | 1–4, Space, ESC | GPU |
| `rendergraph` | [rendergraph/](../examples/rendergraph/) | Render graph đa pass, tự sinh barrier | P bật/tắt post, ESC | |

### Animation

| Target | Thư mục | Demo gì | Điều khiển | CI |
|---|---|---|---|---|
| `skinned` | [skinned/](../examples/skinned/) | glTF có skin, 7 clip, cross-fade, mask | 1–7, M, ESC | |
| `anim_state` | [anim_state/](../examples/anim_state/) | `StateMachine` + root motion, headless | — | ✅ |
| `ui_anim` | [ui_anim/](../examples/ui_anim/) | `anim::Curve` dùng cho UI + không gian màu | ESC | |

### ECS & scene

| Target | Thư mục | Demo gì | Điều khiển | CI |
|---|---|---|---|---|
| `ecs` | [ecs/](../examples/ecs/) | Registry, transform cha-con, extract → batch | WASD, ESC | |
| `ecs_features` | [ecs_features/](../examples/ecs_features/) | 8 tiện ích Registry (command buffer, hook, event…) | — | ✅ |
| `scenes` | [scenes/](../examples/scenes/) | Lưu/nạp JSON, prefab, chuyển scene | Tab/F5/F9, ESC | |
| `inspector` | [inspector/](../examples/inspector/) | Inspector ImGui + perf overlay | ESC | |

### Vòng lặp `App`

| Target | Thư mục | Demo gì | Điều khiển | CI |
|---|---|---|---|---|
| `app_empty` | [app_empty/](../examples/app_empty/) | Game nhỏ nhất còn chạy được — ba dòng | — | |
| `app_plugin` | [app_plugin/](../examples/app_plugin/) | Plugin nối thêm hook, không ghi đè | — | |
| `app_settings` | [app_settings/](../examples/app_settings/) | Cấu hình lưu xuống đĩa, đọc trước khi dựng `App` | — | |
| `game_example` | [game/](../examples/game/) | Vòng lặp 2D hoàn chỉnh, sprite animation + follow cam | WASD, ESC | |
| `game_menu` | [game_menu/](../examples/game_menu/) | Loading screen chờ asset thật + menu chính | ESC | ✅ |
| `threaded` | [threaded/](../examples/threaded/) | Mô phỏng pipeline trên luồng riêng | — | |
| `hotreload` | [hotreload/](../examples/hotreload/) | Hot-reload code gameplay qua shared library | sửa & build lại `game` | |

### Physics & màn chơi

| Target | Thư mục | Demo gì | Điều khiển | CI |
|---|---|---|---|---|
| `physics` | [physics/](../examples/physics/) | Box2D tự dựng vòng lặp + âm va chạm | ESC | |
| `physics_app` | [physics_app/](../examples/physics_app/) | Physics qua `App`: joint, raycast, sensor | chuột, ESC | |
| `breakout` | [breakout/](../examples/breakout/) | Breakout: physics + `CommandBuffer` | A/D, Space, ESC | ✅ |
| `tilemap` | [tilemap/](../examples/tilemap/) | Màn 512×64 tile, va chạm bằng cờ tile | A/D, Space, ESC | |
| `tiled` | [tiled/](../examples/tiled/) | Cùng màn nhưng dữ liệu từ `level.tmj` (Tiled) | A/D, Space, ESC | |
| `alien_cake_addict` | [alien_cake_addict/](../examples/alien_cake_addict/) | Game 3D trên lưới ô, gameplay tách khỏi render | WASD, ESC | ✅ |

### Input, UI, chữ

| Target | Thư mục | Demo gì | Điều khiển | CI |
|---|---|---|---|---|
| `input` | [input/](../examples/input/) | Action thay vì phím, gán lại + JSON | WASD, R, ESC | |
| `ui` | [ui/](../examples/ui/) | UI immediate-mode + font | bấm nút, ESC | |
| `text_check` | [text/](../examples/text/) | Bộ giải mã UTF-8, headless | — | ✅ |

### Asset

| Target | Thư mục | Demo gì | Điều khiển | CI |
|---|---|---|---|---|
| `assets` | [assets/](../examples/assets/) | `AssetManager` load PNG + **hot reload** | sửa file PNG, ESC | |
| `assets_demo` | [assets_demo/](../examples/assets_demo/) | Loader tuỳ biến, async, subasset, ghi asset | — | ✅ |

### Picking & gizmo

| Target | Thư mục | Demo gì | Điều khiển | CI |
|---|---|---|---|---|
| `picking` | [picking/](../examples/picking/) | Hover, click, lớp, kéo-thả, backend tuỳ biến | — | ✅ |
| `mesh_picking` | [mesh_picking/](../examples/mesh_picking/) | Tia vs AABB/cầu/tam giác, backend camera | — | ✅ |
| `picking_debug` | [picking_debug/](../examples/picking_debug/) | Overlay vẽ tia pick, điểm trúng, viền | chuột, ESC | GPU |
| `transform_gizmo` | [transform_gizmo/](../examples/transform_gizmo/) | Gizmo tịnh tiến: bắt trục, kéo, khoá trục | — | ✅ |

### Hiệu năng & đa luồng

| Target | Thư mục | Demo gì | Điều khiển | CI |
|---|---|---|---|---|
| `benchmark` | [benchmark/](../examples/benchmark/) | Stress 100k sprite, đo thời gian từng pha | ESC | |
| `mt_record` | [mt_record/](../examples/mt_record/) | Ghi lệnh vẽ đa luồng (command list thứ cấp) | ESC | |
| `async_tasks` | [async_tasks/](../examples/async_tasks/) | Job + `Channel`, `tryReceive` không chặn | — | ✅ |
| `diagnostics` | [diagnostics/](../examples/diagnostics/) | `diag::` — đo một lần, đọc mọi nơi | — | ✅ |

### Toán, AI, camera (headless)

| Target | Thư mục | Demo gì | Điều khiển | CI |
|---|---|---|---|---|
| `math_primitives` | [math_primitives/](../examples/math_primitives/) | Khối bao, primitive, lấy mẫu đều, primitive tuỳ biến | — | ✅ |
| `pathfinding` | [pathfinding/](../examples/pathfinding/) | A* — tường, cắt góc, ngõ cụt, ngân sách | — | ✅ |
| `camera_rig` | [camera_rig/](../examples/camera_rig/) | Orbit, Fly, Pan2D, Follow2D, Shake, Zoom | — | ✅ |

### Audio & debug

| Target | Thư mục | Demo gì | Điều khiển | CI |
|---|---|---|---|---|
| `audio_demo` | [audio_demo/](../examples/audio_demo/) | Load/play, control, tone, procedural (FM) | — | ✅ |
| `debug_draw` | [debug/](../examples/debug/) | `DebugDraw` line/box/circle/text theo category | 1/2/3, ESC | |

> `mesh3d` là demo tham chiếu cho 3D; `scene_viewer` và [games/roller](../games/roller/)
> cho đường 3D **qua `App`**; `rendergraph` cho kiến trúc multi-pass; `mt_record` và
> `threaded` cho hai kiểu song song hoá khác nhau.

## Dùng example làm smoke test

Chưa có unit test riêng (xem [mục dưới](#unit-test-trạng-thái)). Cách kiểm tra
nhanh hiện tại là chạy example **headless N frame** rồi soi log lỗi. Mẫu kết thúc
sạch là dòng `[INFO ][App] Goodbye.` và không có `error`/`validation`/`abort`.

Chạy một example vài frame trên cả hai backend:

```bash
# Vulkan
VORTEX_MAX_FRAMES=5 ./build/debug/examples/mesh3d
# WebGPU
VORTEX_RHI_API=webgpu VORTEX_MAX_FRAMES=5 ./build/debug/examples/mesh3d
```

Quét toàn bộ example như một smoke test (Vulkan), fail nếu thấy lỗi:

```bash
set -e
for e in build/debug/examples/*; do
  [ -x "$e" ] || continue
  name=$(basename "$e")
  out=$(VORTEX_MAX_FRAMES=5 timeout 40 "$e" 2>&1) || { echo "FAIL($name) exit"; echo "$out"; exit 1; }
  echo "$out" | grep -iqE 'error|validation|abort|fail' && { echo "FAIL($name)"; echo "$out"; exit 1; }
  echo "$out" | grep -q 'Goodbye' && echo "PASS $name" || echo "WARN $name (no Goodbye)"
done
```

Lặp lại với `VORTEX_RHI_API=webgpu` để kiểm backend thứ hai. (Một số example
cần GPU + display; trong môi trường headless thực sự không có GPU, chúng sẽ báo
lỗi tạo device — đó là giới hạn môi trường, không phải lỗi code.)

### Các bài tự kiểm tra riêng

Những example đánh dấu ✅ ở cột CI **không cần GPU** và tự kiểm luật của chúng. Một số cần bật
bằng biến môi trường riêng:

```bash
VORTEX_ALIENCAKE_CHECK=1    ./build/debug/examples/alien_cake_addict
VORTEX_BREAKOUT_CHECK=1     ./build/debug/examples/breakout
VORTEX_GAMEMENU_CHECK=1     ./build/debug/examples/game_menu
VORTEX_SCENEVIEWER_CHECK=1  ./build/debug/examples/scene_viewer
```

Những cái đánh dấu **GPU** cũng tự kiểm tra, nhưng bằng cách render vào target offscreen rồi
đọc ngược pixel về — nên vẫn cần một GPU thật:

```bash
VORTEX_GIZMOS3D_CHECK=1     ./build/debug/examples/gizmos3d
VORTEX_INSTANCING_CHECK=1   ./build/debug/examples/instancing
VORTEX_ANIMATED_CHECK=1     ./build/debug/examples/animated
VORTEX_SHADERDEFS_CHECK=1   ./build/debug/examples/shader_defs
VORTEX_PICKINGDEBUG_CHECK=1 ./build/debug/examples/picking_debug
```

Ghi chú backend:
- **Vulkan**: bật validation layer nếu cài Vulkan SDK để bắt lỗi API ("0 validation error").
- **WebGPU**: có thể in `[WARN] Unknown decoration Block` cho shader có uniform
  buffer — cosmetic (từ SPIR-V reader của wgpu-native), không phải lỗi.

## Unit test (trạng thái)

`CMakePresets.json` đã có `testPresets` và `CMakeLists.txt` có option
`VORTEX_BUILD_TESTS`, nhưng phần `enable_testing()` / `add_test` hiện **đang bị
comment** và **chưa có file `TEST_CASE` nào** (dù `doctest` đã được FetchContent).
Vì vậy `ctest` chưa chạy được test thực. Cho tới khi test math/Handle được viết
và bật lại, **chạy example headless là cách kiểm thử thực tế** — và các example ✅ ở trên
đang đóng đúng vai trò đó.

Khi test được nối lại, luồng dự kiến là:

```bash
cmake --preset debug -DVORTEX_BUILD_TESTS=ON
cmake --build build/debug
ctest --preset debug --output-on-failure
```

---

Xem thêm: [api_reference.md](api_reference.md), [getting_started.md](getting_started.md),
[architecture.md](architecture.md).
