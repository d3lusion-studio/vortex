# input

Action, không phải phím.

```bash
cmake --build build/debug --target input
./build/debug/examples/input
```

Game bên dưới **không bao giờ hỏi "phím W có đang được nhấn không"** — nó hỏi "`moveY` có
dương không". Và cùng một đoạn code đó được điều khiển bởi bàn phím, cần analog của gamepad,
hay d-pad, mà không cần biết đó là cái nào.

Đó là toàn bộ luận điểm. Đọc phím trực tiếp trong code gameplay có nghĩa là mọi màn hình gán
phím, mọi hỗ trợ gamepad, và mọi bố cục bàn phím không phải QWERTY đều trở thành một đợt sửa
xuyên suốt toàn bộ codebase.

Bấm `R` để gán lại action `fire`, và **cả bảng ánh xạ round-trip qua JSON** — đó chính xác
là tất cả những gì một màn hình cài đặt phím cần.

| Phím | Tác dụng |
|---|---|
| `WASD` / phím mũi tên | các action di chuyển |
| `Space`, `E`, `F`, `Q` | các action khác |
| `R` | gán lại action `fire`, rồi lưu/nạp lại bảng qua JSON |
| `Esc` | thoát |

| Biến môi trường | Ý nghĩa |
|---|---|
| `VORTEX_MAX_FRAMES` | chạy N frame rồi thoát |
