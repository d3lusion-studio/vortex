# breakout

Breakout kinh điển — và là ví dụ hoàn chỉnh về việc cả stack 2D hợp lại: vòng lặp `App`,
physics Box2D, render sprite, và các tiện ích ECS.

```bash
cmake --build build/debug --target breakout
./build/debug/examples/breakout
```

Hai chi tiết đáng chỉ ra:

**`CommandBuffer` hoãn việc phá gạch.** Một contact bắn ra từ *bên trong* bước physics, nơi
mà huỷ một entity (và body của nó) giữa lúc đang solve là không an toàn. Nên cú va chạm được
*ghi lại*, rồi mới áp dụng trong fixed update sau khi bước physics kết thúc. Đây đúng là bài
toán thứ tự mà command buffer sinh ra để giải.

**Fixed update.** Vật lý chạy ở bước thời gian cố định, tách khỏi tốc độ render — nên quỹ đạo
bóng giống hệt nhau ở 60 FPS và 144 FPS. Buộc physics vào frame rate là cách nhanh nhất để
có một game chạy khác nhau trên từng máy.

| Phím | Tác dụng |
|---|---|
| `A` / `D` hoặc `←` / `→` | di chuyển thanh đỡ |
| `Space` | phát bóng |
| `Esc` | thoát |

```bash
VORTEX_BREAKOUT_CHECK=1 ./build/debug/examples/breakout   # tự chơi, lỗi nếu không thắng
```

| Biến môi trường | Ý nghĩa |
|---|---|
| `VORTEX_BREAKOUT_CHECK` | tự chơi tới khi thắng; thoát mã khác 0 nếu không thắng được |
| `VORTEX_MAX_FRAMES` | chạy N frame rồi thoát |
