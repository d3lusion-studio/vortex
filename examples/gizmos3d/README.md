# gizmos3d

Gizmo 3D: đường debug trong world space — lưới, trục toạ độ, hộp và cầu dạng khung dây — vẽ
qua `renderer::Gizmos3D`.

```bash
cmake --build build/debug --target gizmos3d
./build/debug/examples/gizmos3d
```

Đây là thứ để nhìn thấy transform của một entity, một khối bao, hay một tia — những thứ bản
thân chúng không có hình học nào cả.

## Tự kiểm tra headless

```bash
VORTEX_GIZMOS3D_CHECK=1 ./build/debug/examples/gizmos3d
```

Vẽ gizmo trục toạ độ vào một texture offscreen, đọc ngược pixel về, và khẳng định có tồn tại
pixel đỏ, xanh lá, xanh dương rõ rệt — tức là ba đường X/Y/Z **đã thực sự được rasterise**,
chứ không chỉ được submit. Thoát với mã khác 0 nếu không.

Vẫn cần GPU (khác với các bài kiểm tra thuần CPU như [picking](../picking/)).

| Biến môi trường | Ý nghĩa |
|---|---|
| `VORTEX_GIZMOS3D_CHECK` | chạy chế độ tự kiểm tra rồi thoát |
| `VORTEX_MAX_FRAMES` | chạy N frame rồi thoát |

Bản 2D của cùng ý tưởng nằm ở [debug](../debug/).
