# vortex::renderer

Module lớn nhất (~7 700 dòng): mọi thứ giữa "một cảnh" và "các lệnh RHI".

```cmake
target_link_libraries(mygame PRIVATE Vortex::renderer)
```

## Nội dung

| Nhóm | Header | Nội dung |
|---|---|---|
| 2D | `sprite_batch.hpp`, `sprite_atlas.hpp`, `atlas.hpp` | gộp sprite, đóng gói atlas |
| 2D | `mesh2d.hpp`, `sprite_animation.hpp`, `lighting2d.hpp` | mesh 2D, 9-patch, animation, ánh sáng 2D |
| 2D | `tilemap.hpp` | bản đồ tile, chỉ duyệt phần trong khung nhìn |
| 3D | `mesh.hpp`, `material.hpp`, `skinning.hpp` | mesh, vật liệu PBR, skinning theo xương |
| Camera | `camera.hpp`, `camera2d.hpp`, `camera_controller.hpp` | camera 2D/3D, các rig orbit/fly/follow |
| Pipeline | `render_graph.hpp`, `pipeline_cache.hpp`, `post_process.hpp` | pass và tài nguyên, cache pipeline, bloom/ACES |
| Khác | `culling.hpp`, `particles.hpp`, `gizmos3d.hpp`, `pathfinding.hpp`, `transform_gizmo.hpp` | cull, hạt, gizmo, A* |

## Đường đi của một frame

```
Registry  →  extract  →  danh sách RenderItem  →  batch  →  lệnh RHI
```

Pha **extract** là ranh giới quan trọng nhất trong engine. Nó là nơi duy nhất dữ liệu game
được đọc để render, và chính vì có ranh giới rõ ràng đó mà mô phỏng mới chạy được trên luồng
riêng (xem [app](../app/)).

## Vài điều dễ sai

**Winding của mesh phải là CCW nhìn từ ngoài.** Mesh bị lộn trong ra ngoài **vẫn trông đặc và
bình thường**, nhưng mọi pháp tuyến quay ngược khỏi camera — đủ để giết SSAO và cho ra một
quả cầu đen thui, mà không có lỗi nào được báo.

**Bloom cần HDR.** Muốn sprite phát sáng thì tint nó **trên 1.0** (`{6,3,1,1}` sáng gấp sáu
lần trắng); bright pass tìm chính những pixel đó. Trong target 8-bit mọi thứ bị kẹp về 1.0 và
không thể phân biệt "trắng" với "chói".

**Render graph tự sinh barrier.** Khai báo pass nào đọc/ghi tài nguyên nào, đừng đặt image
barrier bằng tay — quên một cái thì kết quả tuỳ driver: đúng trên máy này, ra rác trên máy kia.

## Kiểm chứng kết quả render

Không chụp được màn hình trong môi trường này. Cách làm là `readTexture` + dump PPM qua
`VORTEX_SCREENSHOT`, rồi so hai ảnh bằng RMSE. Ghim camera bằng `VORTEX_STATIC_CAM` để hai
lần chạy so được với nhau.

Xem [examples/mesh3d](../../examples/mesh3d/), [examples/rendergraph](../../examples/rendergraph/),
[examples/atlas](../../examples/atlas/), [examples/camera_rig](../../examples/camera_rig/).
