# Vortex

Game engine 2D/3D viết bằng C++20, chạy trên Vulkan và WebGPU.

```bash
cmake --preset debug
cmake --build build/debug
./build/debug/examples/hello        # không cần GPU — kiểm tra build đã đúng
./build/debug/examples/mesh3d       # sân chơi 3D
```

Đổi backend chỉ là đổi một biến môi trường, **không sửa một dòng renderer hay gameplay nào**:

```bash
VORTEX_RHI_API=webgpu ./build/debug/examples/mesh3d
```

## Bắt đầu từ đâu

| Bạn muốn | Đọc cái này |
|---|---|
| Build và chạy lần đầu | [docs/getting_started.md](docs/getting_started.md) |
| Xem engine làm được gì | [docs/examples.md](docs/examples.md) — 55 example, mỗi cái có README riêng |
| Hiểu kiến trúc | [docs/architecture.md](docs/architecture.md) |
| Tra API | [docs/api_reference.md](docs/api_reference.md) |
| Viết code theo đúng quy ước | [docs/coding_conventions.md](docs/coding_conventions.md) |
| Biết engine dùng thư viện gì | [docs/tech_stack.md](docs/tech_stack.md) |

Game nhỏ nhất còn chạy được dài đúng ba dòng — xem
[examples/app_empty](examples/app_empty/).

## Module

Mỗi module có README riêng giải thích nó làm gì và những chỗ dễ sai.

| Module | LOC | Nội dung |
|---|---|---|
| [core](engine/core/) | 3 570 | kiểu cơ bản, toán, handle, log, JSON, bộ nhớ, chẩn đoán |
| [platform](engine/platform/) | 1 140 | cửa sổ, input, thời gian, file, thư viện động |
| [rhi](engine/rhi/) | 4 192 | một API đồ hoạ, hai backend (Vulkan / WebGPU) |
| [jobs](engine/jobs/) | 266 | job system và channel |
| [renderer](engine/renderer/) | 7 657 | sprite, mesh, camera, render graph, post-process |
| [anim](engine/anim/) | 1 648 | curve, clip, bộ xương, blend, máy trạng thái |
| [ecs](engine/ecs/) | 2 059 | registry, component, scene, serialize, picking |
| [asset](engine/asset/) | 2 660 | nạp/cache/hot reload, glTF, Tiled |
| [text](engine/text/) | 557 | font TTF, render chữ, UTF-8 |
| [ui](engine/ui/) | 255 | UI immediate-mode |
| [audio](engine/audio/) | 411 | phát, tổng hợp tone, âm thanh thủ tục |
| [physics](engine/physics/) | 683 | Box2D: body, collider, joint, sensor |
| [debug](engine/debug/) | 895 | debug draw, inspector ImGui, perf overlay |
| [app](engine/app/) | 1 794 | vòng lặp game, plugin, scene manager |

Thứ tự trên cũng là thứ tự phụ thuộc: `core` không phụ thuộc gì, `app` nối tất cả lại.

Link tất cả bằng một target gộp:

```cmake
target_link_libraries(mygame PRIVATE Vortex::vortex)
```

## Game

| Game | Nội dung |
|---|---|
| [games/roller](games/roller/) | Game 3D nhỏ hoàn chỉnh — phép thử xem engine đã sẵn sàng cho một game 3D chưa |
| [games/farm_rpg](games/farm_rpg/) | Farming sim kiểu Stardew, đóng gói được thành thư mục chạy độc lập |

## Kiểm thử

Chưa có unit test được bật (xem [docs/examples.md](docs/examples.md#unit-test-trạng-thái)).
Hiện tại vai trò đó do các example headless đảm nhận — chúng tự kiểm luật của mình và thoát
mã khác 0 khi sai:

```bash
./build/debug/examples/anim_state       # máy trạng thái animation
./build/debug/examples/pathfinding      # A*
./build/debug/examples/math_primitives  # toán 2D
./build/debug/examples/picking          # picking
./build/debug/examples/text_check       # decoder UTF-8
```

Smoke test toàn bộ example bằng `VORTEX_MAX_FRAMES` — xem
[docs/examples.md](docs/examples.md#dùng-example-làm-smoke-test).

## Ghi chú môi trường

Trên phiên KDE Wayland, GLFW bị **ép chạy qua X11/XWayland** (xem log khởi động). Đây là chủ
ý cho tới khi swapchain Vulkan cho Wayland hoàn tất.
