# skinned

glTF có skin và animation, trọn vẹn từ đầu tới cuối.

```bash
cmake --build build/debug --target skinned
./build/debug/examples/skinned
```

Đường đi của dữ liệu:

```
assets/models/cato/cato.gltf  ->  assets::loadGltf
  -> renderer::MeshData mỗi primitive (vị trí, UV, chỉ số joint, trọng số)
  -> anim::Skeleton  (41 joint: cha, inverse bind, bind pose)
  -> anim::Clip x7   (Idle, Walk, Run, Think, ...)
```

Mỗi frame: player đẩy đồng hồ tiến lên và sample clip thành một local pose, pose đó được
biến đổi theo chuỗi cha thành ma trận skinning, rồi đẩy lên GPU.

Chuyển clip đi qua `cross.play(clip, fadeTime)` — **fade, không snap**. Đây là điểm dễ làm
sai: nhảy thẳng sang clip mới khiến nhân vật giật một frame, còn cross-fade thì nội suy giữa
hai pose trong khoảng `fadeTime`.

| Phím | Tác dụng |
|---|---|
| `1`–`7` | chọn clip (số lượng tuỳ model, tối đa 7) |
| `M` | bật/tắt mask — chỉ áp animation lên một nhánh của bộ xương |
| `Esc` | thoát |

| Biến môi trường | Ý nghĩa |
|---|---|
| `VORTEX_CLIP` | clip khởi đầu |
| `VORTEX_SWITCH_TO` / `VORTEX_SWITCH_AT` | tự chuyển sang clip khác ở frame thứ N |
| `VORTEX_FADE` | thời gian cross-fade, giây (mặc định `0.25`) |
| `VORTEX_MASK` | bật mask ngay từ đầu |
| `VORTEX_SCREENSHOT` | ghi frame ra file PPM |
| `VORTEX_MAX_FRAMES` | chạy N frame rồi thoát |

Giới hạn hiện tại: tối đa **255 xương**, joint phải theo thứ tự cha-trước-con, và **chưa hỗ
trợ `.glb`** (chỉ `.gltf` kèm file rời).

Máy trạng thái animation chạy trên cùng nền tảng này nằm ở [anim_state](../anim_state/).
