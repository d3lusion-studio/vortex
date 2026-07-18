# benchmark

Đo thông lượng sprite: 100 000 sprite mặc định, đi qua toàn bộ đường ECS → extract → batch.

```bash
cmake --build build/debug --target benchmark
./build/debug/examples/benchmark
VORTEX_SPRITES=500000 ./build/debug/examples/benchmark
```

Ghép `Registry`, `FrameAllocator`, `JobSystem` và `SpriteBatch` lại rồi đẩy số lượng lên tới
mức chúng bắt đầu đau, với overlay hiển thị thời gian từng pha.

**Cảnh báo khi đo:** mặc định swapchain chạy ở `PresentMode::Fifo` (V-Sync), nghĩa là con số
sẽ bị chặn ở tần số quét màn hình và **mọi cải tiến đều trông như không có tác dụng**. Muốn
đo thật thì phải chạy ở `PresentMode::Immediate`, nếu không swapchain sẽ nói dối.

`FrameAllocator` ở đây cũng đáng chú ý: danh sách render item được cấp phát từ một arena
được reset mỗi frame, nên không có lượt `malloc`/`free` nào theo từng sprite.

| Phím | Tác dụng |
|---|---|
| `Esc` | thoát |

| Biến môi trường | Ý nghĩa |
|---|---|
| `VORTEX_SPRITES` | số sprite (mặc định `100000`) |
| `VORTEX_MAX_FRAMES` | chạy N frame rồi thoát |

So sánh song song đơn luồng và pipelined: [threaded](../threaded/).
