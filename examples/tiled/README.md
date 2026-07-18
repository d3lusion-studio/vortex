# tiled

Cùng loại màn chơi như [tilemap](../tilemap/), chỉ khác ở chỗ **không thứ gì trong đó nằm
trong file code này**.

```bash
cmake --build build/debug --target tiled
./build/debug/examples/tiled
```

Tile, tileset, cờ va chạm và điểm spawn của nhân vật đều đến từ `level.tmj` — vẽ bằng
[Tiled](https://www.mapeditor.org/).

Thứ mà code vẫn giữ là **hành vi**: trọng lực, cách nhảy, "nhặt đồ" nghĩa là gì.

Sự phân chia đó chính là toàn bộ ý nghĩa của example:

> **dữ liệu nói thế giới nằm ở đâu, code nói thế giới làm gì.**

Hệ quả thực tế là người thiết kế màn chơi sửa `level.tmj` và xem kết quả mà không cần build
lại, không cần trình biên dịch, và không cần hỏi lập trình viên.

| Phím | Tác dụng |
|---|---|
| `A` / `D` | chạy |
| `Space` | nhảy |
| `Esc` | thoát |

| Biến môi trường | Ý nghĩa |
|---|---|
| `VORTEX_MAX_FRAMES` | chạy N frame rồi thoát |
