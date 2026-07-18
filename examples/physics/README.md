# physics

Physics 2D qua Box2D, tự dựng vòng lặp: `PhysicsWorld`, collider, và âm thanh va chạm.

```bash
cmake --build build/debug --target physics
./build/debug/examples/physics
```

Bản này dựng `PhysicsWorld` bằng tay và tự gọi `step()`. Muốn xem bản gọn hơn chạy qua `App`
thì xem [physics_app](../physics_app/) — cùng hệ physics, ít boilerplate hơn nhiều.

Điểm cần nắm: **body của Box2D và `Transform2D` của ECS là hai nguồn dữ liệu khác nhau**, và
phải rõ ai là nguồn sự thật. Với body động, physics là chủ và transform được đồng bộ *từ*
nó sau mỗi bước; ghi thẳng vào transform của một body động sẽ bị bước tiếp theo ghi đè.

Example còn tự sinh một file WAV tiếng "beep" rồi phát nó khi có va chạm, nên nó cũng là
minh hoạ cho việc nối callback contact vào [audio](../../engine/audio/).

| Phím | Tác dụng |
|---|---|
| `Esc` | thoát |

| Biến môi trường | Ý nghĩa |
|---|---|
| `VORTEX_MAX_FRAMES` | chạy N frame rồi thoát |
