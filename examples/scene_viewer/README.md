# scene_viewer

Nạp một model glTF và cho camera quay quanh nó — bản chạy trên vòng lặp `App`, và là bằng
chứng rằng một model có thể đi thẳng vào một game 3D dựng trên `App`.

```bash
cmake --build build/debug --target scene_viewer
./build/debug/examples/scene_viewer
```

Model được spawn thành các entity `Transform3D` + `MeshComp` bởi `app::loadModel`; vòng lặp
`render3D` vẽ chúng — đúng con đường mà các khối hình primitive của
[games/roller](../../games/roller/) đi qua.

Điều đáng nói nằm ở chỗ **không có gì trong file này chạm tay vào `MeshRenderer`, render
graph hay shadow pass**. So sánh với [mesh3d](../mesh3d/), nơi toàn bộ những thứ đó phải
viết tay: đó là khác biệt giữa đường `App` và đường RHI thô.

| Phím | Tác dụng |
|---|---|
| `A` / `D` hoặc `←` / `→` | quay camera quanh model |
| `Esc` | thoát |

| Biến môi trường | Ý nghĩa |
|---|---|
| `VORTEX_SCENEVIEWER_CHECK` | tự kiểm tra: nạp model, khẳng định import ra được hình học, rồi thoát |
| `VORTEX_SCREENSHOT` | ghi frame ra file PPM |
| `VORTEX_SHOT_FRAME` | chụp ở frame thứ mấy |
| `VORTEX_MAX_FRAMES` | chạy N frame rồi thoát |

Model có bộ xương và animation thì xem [skinned](../skinned/).
