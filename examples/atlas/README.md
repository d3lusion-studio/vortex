# atlas

Atlas thực sự dùng để làm gì: gộp draw call.

```bash
cmake --build build/debug --target atlas
./build/debug/examples/atlas
```

Cùng 64 sprite được vẽ, hai lần mỗi giây đổi qua lại giữa hai cách dựng **trông giống hệt
nhau trên màn hình**:

| Chế độ | Cách dựng | Số draw call |
|---|---|---|
| `LOOSE` | 64 device texture riêng biệt | 64 |
| `ATLAS` | cùng 64 ảnh đó, đóng gói chung | 1 |

Đó là toàn bộ luận điểm. Đổi texture buộc batcher phải cắt batch và phát một lệnh vẽ mới,
nên chi phí không nằm ở số sprite mà ở **số lần đổi texture**. Gói chung vào một atlas thì
64 sprite chỉ còn đúng một lệnh vẽ, trong khi hình ảnh không đổi một pixel nào.

| Phím | Tác dụng |
|---|---|
| `Space` | giữ nguyên chế độ hiện tại (dừng tự đổi qua lại) |
| `Esc` | thoát |

| Biến môi trường | Ý nghĩa |
|---|---|
| `VORTEX_MAX_FRAMES` | chạy N frame rồi thoát |
