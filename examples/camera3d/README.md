# camera3d

Ba việc một camera 3D phải làm mà [mesh3d](../mesh3d/) không hề cho thấy.

```bash
cmake --build build/debug --target camera3d
./build/debug/examples/camera3d
```

| Chủ đề | Nội dung |
|---|---|
| **Split screen** | cùng một thế giới, hai lần, cạnh nhau, từ hai camera |
| **Orthographic3D** | khung bên phải — đường song song vẫn song song, khoảng cách không làm nhỏ vật thể |
| **Ray casting** | `Camera::viewportToWorld` biến vị trí chuột thành tia world-space, `MeshRenderer::rayCast` cắt tia đó với tam giác thật |

Điểm dễ nhầm nhất nằm ở `Orthographic3D`: **nó không phải camera 2D đem gắn một cảnh 3D vào**.
Nó vẫn là camera 3D đầy đủ với xoay, depth và cull — chỉ có phép chiếu là song song thay vì
phối cảnh. Đó là cái nhìn kiểu CAD hoặc game chiến thuật isometric. Xem `Camera::Mode`.

Có hai hàm ma trận chiếu trực giao và **rất dễ dùng nhầm**: `Mat4::ortho` dành cho 2D (z là
khoá sắp lớp), còn `Mat4::orthoRH` dành cho 3D (đi với `lookAt`, nhìn theo hướng −Z). Chọn
nhầm thì toàn bộ cảnh bị clip sạch và ta chỉ thấy một khung hình trống — không có thông báo
lỗi nào cả.

| Biến môi trường | Ý nghĩa |
|---|---|
| `VORTEX_SCREENSHOT` | ghi frame ra file PPM |
| `VORTEX_MAX_FRAMES` | chạy N frame rồi thoát |

Nhấn `Esc` để thoát.
