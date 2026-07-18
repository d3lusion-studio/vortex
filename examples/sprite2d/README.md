# sprite2d

Toàn bộ những gì đường render sprite 2D làm được, trên một màn hình.

```bash
cmake --build build/debug --target sprite2d
./build/debug/examples/sprite2d
```

| Hàng | Nội dung |
|---|---|
| 1 | **lọc** — cùng một sheet 16×16 phóng to, linear so với nearest |
| 2 | **tiling** — một quad duy nhất, UV vượt quá 1.0, với sampler `Repeat` |
| 3 | **lật** — một glyph bất đối xứng được mirror theo từng trục |
| 4 | **anchor** — cùng một sprite quay quanh ba điểm neo khác nhau |
| 5 | **vẽ bằng CPU** — một texture có pixel được ghi lại mỗi frame |

Hàng 1 là hàng đáng chú ý nhất với pixel art: `Linear` làm nhoè sprite khi phóng to, còn
`Nearest` giữ cạnh sắc. Sai lựa chọn này là lý do phổ biến nhất khiến pixel art trông "mềm"
một cách khó hiểu.

Hàng 5 chứng minh texture không nhất thiết đến từ file — pixel ghi từ CPU rồi upload lại mỗi
frame vẫn đi qua đúng đường đó.

| Biến môi trường | Ý nghĩa |
|---|---|
| `VORTEX_MAX_FRAMES` | chạy N frame rồi thoát |

Nhấn `Esc` để thoát.
