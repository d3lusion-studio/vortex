# vortex::physics

Physics 2D, bọc quanh Box2D.

```cmake
target_link_libraries(mygame PRIVATE Vortex::physics)
```

| Header | Nội dung |
|---|---|
| `physics_world.hpp` | `PhysicsWorld` — bước mô phỏng, raycast, callback contact |
| `components.hpp` | `RigidBody2D`, `Collider2D`, joint, sensor |

## Qua `App` thì không phải dựng gì

```cpp
app.physics();   // tạo world cho scene đang hoạt động, nếu chưa có
```

Vòng lặp bước nó **bên trong cùng một fixed update với gameplay**. Chi tiết này quan trọng:
nếu physics bước ở một nhịp còn gameplay chạy ở nhịp khác, code đọc vị trí sẽ thấy trạng thái
cũ hoặc mới tuỳ frame, và bug sinh ra sẽ không tái hiện được.

## Ai là nguồn sự thật

Body của Box2D và `Transform2D` của ECS là **hai nguồn dữ liệu khác nhau**:

| Loại body | Nguồn sự thật |
|---|---|
| động (dynamic) | **physics** — transform được đồng bộ *từ* body sau mỗi bước |
| kinematic / tĩnh | **code** — ghi vào body, physics không tự đổi nó |

Ghi thẳng vào transform của một body động sẽ bị bước tiếp theo ghi đè — im lặng.

## Huỷ entity từ trong callback contact

Không được. Contact bắn ra từ **bên trong** solver, nơi huỷ một body là không an toàn. Ghi
lại vào `CommandBuffer` rồi flush sau khi bước xong — xem
[examples/breakout](../../examples/breakout/).

## Không phải lúc nào cũng cần physics

Va chạm với tile chỉ là một phép tra mảng cờ tile, rẻ hơn nhiều so với nhét 32 000 collider
tĩnh vào Box2D. Xem [examples/tilemap](../../examples/tilemap/).

Xem [examples/physics_app](../../examples/physics_app/) (qua `App`) và
[examples/physics](../../examples/physics/) (tự dựng vòng lặp).
