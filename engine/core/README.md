# vortex::core

Nền móng. Không phụ thuộc module nào khác, và mọi module khác đều phụ thuộc nó.

```cmake
target_link_libraries(mygame PRIVATE Vortex::core)
```

## Nội dung

| Nhóm | Header | Nội dung |
|---|---|---|
| Kiểu cơ bản | `types.hpp` | `u8`…`u64`, `i8`…`i64`, `f32`, `f64`, `usize` |
| Handle | `handle.hpp` | `Handle<T>` — chỉ số + thế hệ |
| Log | `log.hpp`, `console.hpp` | `VORTEX_INFO/WARN/ERROR/TRACE`, console lúc chạy |
| Assert | `assert.hpp` | assert có kiểm tra trong debug |
| Toán | `math/` | `Vec2/3/4`, `Mat4`, `Quat`, `Rect`, `Color`, easing, random |
| Hình học | `math/bounds2d.hpp`, `bounds3d.hpp`, `primitives2d.hpp` | khối bao, primitive, giao/raycast |
| Bộ nhớ | `memory/` | `FrameAllocator` (arena reset mỗi frame), `ObjectPool` |
| Chuỗi | `string_id.hpp` | chuỗi băm để so sánh bằng số nguyên |
| Dữ liệu | `json.hpp`, `settings.hpp` | parse/ghi JSON, cấu hình lưu xuống đĩa |
| Đo đạc | `profiler.hpp`, `diagnostics.hpp` | vùng profiling, registry `diag::` |

## Hai điều nên biết trước

**Handle không phải con trỏ.** `Handle<T>` là cặp chỉ số + thế hệ. Khi tài nguyên bị huỷ, thế
hệ tăng lên và mọi handle cũ trỏ vào ô đó lập tức **không hợp lệ** thay vì thành con trỏ
treo. Asset, texture, entity đều được định danh kiểu này.

**Có hai hàm ma trận trực giao, rất dễ dùng nhầm:**

| Hàm | Dùng cho | Quy ước |
|---|---|---|
| `Mat4::ortho` | 2D | z là khoá sắp lớp |
| `Mat4::orthoRH` | 3D | đi với `lookAt`, nhìn theo −Z |

Chọn nhầm thì **toàn bộ cảnh bị clip sạch** và ta chỉ thấy khung hình trống — không lỗi,
không cảnh báo.

## `diag::` — đo một lần, đọc mọi nơi

```cpp
diag::add("physics.bodies", world.bodyCount());
```

Một dòng tại chỗ đo là toàn bộ phần tích hợp. Perf overlay, bản dump ra log và test đều đọc
cùng chuỗi số liệu đó. Chuỗi bị tắt thì không ghi gì và không tốn gì.

Xem [examples/diagnostics](../../examples/diagnostics/), [examples/math_primitives](../../examples/math_primitives/).
