# vortex::ecs

Entity-Component-System: thế giới game sống ở đây.

```cmake
target_link_libraries(mygame PRIVATE Vortex::ecs)
```

| Header | Nội dung |
|---|---|
| `registry.hpp` | `Registry` — tạo/huỷ entity, thêm/gỡ component, `view()` |
| `entity.hpp` | `Entity` — chỉ số + thế hệ, như `Handle` |
| `components.hpp` | component dựng sẵn: `Transform2D`, `Transform3D`, `SpriteComp`, `MeshComp`, `Parent`… |
| `scene.hpp` | `Scene` — registry + camera + tilemap + particle world |
| `commands.hpp` | `CommandBuffer` — thao tác hoãn lại |
| `callback.hpp` | hành vi theo từng entity |
| `serialize.hpp` | lưu/nạp scene ra JSON |
| `systems.hpp` | system dựng sẵn (transform phân cấp, extract…) |
| `picking.hpp`, `picking3d.hpp`, `picking_debug.hpp` | chọn đối tượng bằng con trỏ |

## `CommandBuffer`: bài toán thứ tự

Sửa thế giới **trong lúc đang duyệt thế giới** là hành vi không xác định. Ví dụ điển hình:
một contact physics bắn ra từ bên trong bước solver, và huỷ entity (cùng body của nó) ngay
lúc đó là không an toàn.

```cpp
cmd.destroy(brick);   // ghi lại
// ...
cmd.flush(registry);  // áp dụng, sau khi bước physics xong
```

Xem [examples/breakout](../../examples/breakout/) để thấy nó dùng thật.

## Lifecycle hook

`onAdd`/`onRemove` bắn khi chèn, gỡ, **và khi entity bị huỷ cả cụm**. Đây là cách một
component sở hữu tài nguyên ngoài (body physics, handle audio) biết lúc nào phải dọn dẹp.

## Chuyển scene được xếp hàng

```cpp
scenes.requestSwitch("menu");
```

Chữ **request** là cố ý: việc chuyển được áp dụng ở **đầu frame kế tiếp**, nên system đang
duyệt giữa chừng không bao giờ thấy thế giới đổi dưới chân nó.

## Duyệt theo cặp

`Registry` cung cấp cách duyệt mọi cặp không thứ tự **đúng một lần**. Tự viết vòng lặp lồng
nhau mà không cẩn thận thì mỗi cặp bị xử lý hai lần (A–B và B–A) và lực va chạm bị nhân đôi.

Xem [examples/ecs](../../examples/ecs/), [examples/ecs_features](../../examples/ecs_features/),
[examples/scenes](../../examples/scenes/).
