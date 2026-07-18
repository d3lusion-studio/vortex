# vortex::anim

Animation: đường cong, clip, bộ xương, pose, blend, và máy trạng thái.

```cmake
target_link_libraries(mygame PRIVATE Vortex::anim)
```

| Header | Nội dung |
|---|---|
| `curve.hpp` | `Curve<T>` — keyframe + nội suy, **kiểu bất kỳ** |
| `clip.hpp` | `Clip` — tập curve theo track, kèm event |
| `skeleton.hpp` | `Skeleton` — cây joint, inverse bind, bind pose |
| `pose.hpp` | `Pose` — transform cục bộ/toàn cục của một tư thế |
| `graph.hpp` | blend tree, cross-fade, mask theo nhánh xương |
| `state_machine.hpp` | `StateMachine` — trạng thái, điều kiện chuyển, exit time |

## `Curve<T>` không biết nó đang animate cái gì

Đây là điểm thiết kế trung tâm. Một `Curve<f32>` không biết và không cần biết số float nó
sinh ra là góc quay của một khớp, độ mờ của một panel UI, hay tiêu cự của camera.

Cần một hệ thống riêng cho từng thứ đó chính là cách một engine kết thúc với bốn hệ thống
animation bất đồng ý nhau về ý nghĩa của "ease-in". Xem
[examples/ui_anim](../../examples/ui_anim/) — UI động dùng đúng `Curve` này.

## Cross-fade, đừng snap

```cpp
cross.play(&clip, fadeTime);   // KHÔNG phải gán clip trực tiếp
```

Nhảy thẳng sang clip mới làm nhân vật giật một frame. Cross-fade nội suy giữa hai pose trong
khoảng `fadeTime` (mặc định hợp lý: 0.25 s).

## Root motion

Chuyển động do animation sinh ra được rút ra thành độ dịch chuyển của nhân vật, thay vì nhân
vật trượt độc lập với chân đang bước ("foot sliding").

## Giới hạn hiện tại

- **tối đa 255 xương**
- joint phải theo thứ tự **cha trước con**
- **chưa hỗ trợ `.glb`** — chỉ `.gltf` kèm file rời

Xem [examples/skinned](../../examples/skinned/) (có GPU, model thật) và
[examples/anim_state](../../examples/anim_state/) (headless, chạy được trong CI).
