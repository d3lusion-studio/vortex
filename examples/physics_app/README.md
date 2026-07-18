# physics_app

Physics qua `App`: không phải dựng `PhysicsWorld`, không phải gọi `step()`.

```bash
cmake --build build/debug --target physics_app
./build/debug/examples/physics_app
```

Gọi `app.physics()` là world được tạo cho scene đang hoạt động, và vòng lặp bước nó **bên
trong cùng một fixed update với gameplay**.

Chi tiết "cùng một fixed update" mới là điều quan trọng. Nếu physics bước ở một nhịp còn
gameplay chạy ở nhịp khác, thì code gameplay đọc vị trí sẽ thấy trạng thái cũ hoặc mới tuỳ
frame, và bug sinh ra sẽ không tái hiện được.

Nội dung minh hoạ:

- collider dạng hộp và hình tròn
- một con lắc nối bằng revolute joint
- raycast bắn từ vị trí chuột
- callback contact begin/end
- một sensor đếm những gì rơi vào nó

Sensor và raycast là hai thứ hầu hết gameplay cần trước cả khi cần va chạm thật: vùng kích
hoạt, tầm nhìn của địch, kiểm tra "có đất dưới chân không".

| Phím | Tác dụng |
|---|---|
| chuột | điểm bắn raycast |
| `Esc` | thoát |

| Biến môi trường | Ý nghĩa |
|---|---|
| `VORTEX_MAX_FRAMES` | chạy N frame rồi thoát |
