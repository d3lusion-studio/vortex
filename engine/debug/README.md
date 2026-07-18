# vortex::debug

Công cụ để nhìn thấy thứ đang xảy ra: debug draw, inspector ImGui, overlay hiệu năng.

```cmake
target_link_libraries(mygame PRIVATE Vortex::debug)
```

| Header | Nội dung |
|---|---|
| `debug_draw.hpp` | `DebugDraw` — đường, hình, nhãn trong world space, phân theo category |
| `imgui_layer.hpp` | `ImGuiLayer` — lớp nền ImGui |
| `inspector.hpp` | `Inspector` — duyệt và sửa entity lúc chạy |
| `perf_overlay.hpp` | `PerfOverlay` — biểu đồ thời gian frame |
| `debug_plugin.hpp` | gói tất cả thành một plugin cho `App` |

## Category: lý do code debug được ở lại

Mỗi lệnh debug draw thuộc về một category, và **tắt category thì lệnh vẽ không tốn gì cả**.

Nhờ vậy code debug nằm lại vĩnh viễn trong game thay vì bị comment ra rồi comment vào — và
thứ ta cần khi có bug thì đã có sẵn ở đó, chỉ việc bật lên.

## Inspector không cần đăng ký component

`Inspector` đọc thẳng từ `ecs::Registry`. Thêm một component mới vào game là nó **tự xuất
hiện** trong inspector.

Đó là khác biệt giữa một công cụ debug được dùng và một công cụ debug bị bỏ quên vì phải bảo
trì riêng song song với code thật.

## Overlay đọc cùng số liệu với mọi thứ khác

`PerfOverlay` đọc registry `diag::` trong [core](../core/) — cùng chuỗi số liệu mà bản dump ra
log và test đều đọc. Thêm một chỉ số là một dòng tại chỗ đo:

```cpp
diag::add("physics.bodies", world.bodyCount());
```

Xem [examples/debug](../../examples/debug/) (target `debug_draw`),
[examples/inspector](../../examples/inspector/),
[examples/diagnostics](../../examples/diagnostics/),
[examples/picking_debug](../../examples/picking_debug/).
