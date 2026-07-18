# mesh3d

Sân chơi 3D của engine: mesh có ánh sáng, depth/cull, overlay sprite 2D, và gần như mọi
tính năng render nâng cao — bật bằng biến môi trường.

```bash
cmake --build build/debug --target mesh3d
./build/debug/examples/mesh3d
```

Đây là example 3D lớn nhất và cũng là cái tự dựng vòng lặp render riêng (swapchain, render
graph, shadow pass đều viết tay). Nếu muốn xem đường 3D **qua `App`** thay vì viết tay, xem
[scene_viewer](../scene_viewer/) hoặc [games/roller](../../games/roller/).

## Chế độ render

Mặc định là forward. Đặt `VORTEX_DEFERRED` để chuyển sang deferred — và **các hiệu ứng bên
dưới chỉ có tác dụng khi deferred đang bật**, vì chúng cần G-buffer:

```bash
VORTEX_DEFERRED=1 VORTEX_SSAO=1 ./build/debug/examples/mesh3d
```

| Biến môi trường | Yêu cầu | Ý nghĩa |
|---|---|---|
| `VORTEX_DEFERRED` | — | chuyển sang deferred shading |
| `VORTEX_SSAO` | deferred | ambient occlusion theo không gian màn hình |
| `VORTEX_MOTION_BLUR` | deferred | motion blur |
| `VORTEX_CONTACT` | deferred | contact shadow |
| `VORTEX_DECALS` | deferred | decal chiếu lên G-buffer |
| `VORTEX_VOLUMETRIC` | deferred | ánh sáng thể tích |
| `VORTEX_LIGHTMAP` | — | dùng lightmap nướng sẵn |
| `VORTEX_STATIC_CAM` | — | ghim camera một chỗ (để so ảnh giữa các lần chạy) |
| `VORTEX_SCREENSHOT` | — | ghi frame ra file PPM tại đường dẫn chỉ định |
| `VORTEX_MAX_FRAMES` | — | chạy N frame rồi thoát |

`VORTEX_STATIC_CAM` + `VORTEX_SCREENSHOT` + `VORTEX_MAX_FRAMES` là bộ ba dùng để kiểm chứng
kết quả render: chụp cùng một khung hình tĩnh trước và sau khi sửa, rồi so hai file PPM.

Nhấn `Esc` để thoát.
