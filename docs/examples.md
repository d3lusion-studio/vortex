# Vortex — Examples & Smoke Testing

> Hướng dẫn build, chạy, và dùng các example như **smoke test** (kể cả headless,
> không cần xem cửa sổ). Mỗi example link với thư viện gộp `Vortex::vortex` và
> nằm ở `examples/<tên>/`.

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

## Chạy

```bash
./build/debug/examples/mesh3d
```

Phần lớn example mở một cửa sổ và lặp tới khi bấm **ESC** (hoặc đóng cửa sổ).
Trên Wayland, GLFW tự dùng X11/XWayland (xem log khởi động) — bình thường.

### Biến môi trường

| Biến | Phạm vi | Ý nghĩa |
|---|---|---|
| `VORTEX_RHI_API` | mọi example | Chọn backend: `vulkan`/`vk` hoặc `webgpu`/`wgpu`. Mặc định Vulkan. |
| `VORTEX_MAX_FRAMES` | mọi example | Chạy đúng N frame rồi thoát sạch (0 = vô hạn). Dùng cho smoke test headless/CI. |
| `VORTEX_SPRITES` | sprites, benchmark | Số sprite spawn. |
| `VORTEX_SATELLITES` | ecs | Số cụm entity. |
| `VORTEX_QUADS` | mt_record | Số quad (kiểm multithread recording). |
| `VORTEX_RG_NO_POST` | rendergraph | Nếu đặt → tắt post pass lúc khởi động. |

Chuyển backend chỉ là đổi env — **không sửa một dòng renderer/gameplay nào**:

```bash
VORTEX_RHI_API=webgpu ./build/debug/examples/mesh3d
```

## Danh sách example

| Target | Thư mục | Demo gì | Điều khiển / toggle |
|---|---|---|---|
| `hello` | hello/ | Khởi tạo tối thiểu, log ra console | — |
| `window` | window/ | Mở cửa sổ + đọc input (chuột/phím) | di chuột, gõ phím, ESC |
| `triangle` | triangle/ | Tam giác hard-code qua RHI (pipeline + draw) | ESC |
| `sprites` | sprites/ | `SpriteBatch`, nhiều texture, camera 2D pan/zoom | WASD pan, scroll zoom, ESC |
| `ecs` | ecs/ | Registry + transform cha-con + extract → batch | WASD pan, scroll zoom, ESC |
| `mesh3d` | mesh3d/ | **3D**: mesh lit (Blinn-Phong) + depth/cull + sprite 2D overlay, lái qua ECS | light tự xoay, ESC |
| `assets` | assets/ | `AssetManager` load PNG + **hot reload** | sửa file PNG để reload, ESC |
| `ui` | ui/ | Immediate-mode UI (panel/label/button) + font | bấm nút, ESC |
| `physics` | physics/ | Box2D: hộp rơi + va chạm + UI spawn | bấm "Spawn" hoặc chờ, ESC |
| `benchmark` | benchmark/ | Stress test số sprite lớn, đo frame time | ESC |
| `mt_record` | mt_record/ | Multithread command recording (secondary lists) | ESC |
| `rendergraph` | rendergraph/ | Render graph đa-pass (scene → post) | **P** bật/tắt post, ESC |
| `debug_draw` | debug/ | `DebugDraw` line/box/circle/text theo category | **1/2/3** toggle grid/shapes/labels, ESC |
| `inspector` | inspector/ | ImGui `EntityInspector` chỉnh component runtime | kéo cửa sổ, sửa field, ESC |
| `hotreload` | hotreload/ | Hot-reload game code qua shared library | sửa & build lại game lib |

> `mesh3d` là demo tham chiếu cho 3D (Phase 11); `rendergraph` cho kiến trúc
> multi-pass; `mt_record` cho đường multithread.

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

Ghi chú backend:
- **Vulkan**: bật validation layer nếu cài Vulkan SDK để bắt lỗi API ("0 validation error").
- **WebGPU**: có thể in `[WARN] Unknown decoration Block` cho shader có uniform
  buffer — cosmetic (từ SPIR-V reader của wgpu-native), không phải lỗi.

## Unit test (trạng thái)

`CMakePresets.json` đã có `testPresets` và `CMakeLists.txt` có option
`VORTEX_BUILD_TESTS`, nhưng phần `enable_testing()` / `add_test` hiện **đang bị
comment** và **chưa có file `TEST_CASE` nào** (dù `doctest` đã được FetchContent).
Vì vậy `ctest` chưa chạy được test thực. Cho tới khi test math/Handle được viết
và bật lại, **chạy example headless là cách kiểm thử thực tế**.

Khi test được nối lại, luồng dự kiến là:

```bash
cmake --preset debug -DVORTEX_BUILD_TESTS=ON
cmake --build build/debug
ctest --preset debug --output-on-failure
```

---

Xem thêm: [api_reference.md](api_reference.md), [getting_started.md](getting_started.md),
[architecture.md](architecture.md).
