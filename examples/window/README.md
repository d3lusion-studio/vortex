# window

Mở một cửa sổ và đọc input — lớp platform, tách hẳn khỏi phần render.

```bash
cmake --build build/debug --target window
./build/debug/examples/window
```

Không có RHI, không có device, không có swapchain trong file này. Nó chỉ dùng ba interface
của [engine/platform/](../../engine/platform/): `IWindow`, `IInputProvider`, `IClock`. Vòng
lặp gọi `clock->tick()`, `input->newFrame()`, `window->pollEvents()` theo đúng thứ tự đó —
đó là thứ tự mà mọi vòng lặp khác trong repo, kể cả `App`, đều dùng lại.

Toạ độ chuột được log ra console mỗi khi nó đổi (làm tròn theo pixel), kèm scroll delta và
delta time của frame.

| Phím | Tác dụng |
|---|---|
| `Esc` | thoát |
| `Space` / `Enter` / `F1` | log ra console để kiểm tra việc nhận phím |

Trên Wayland, GLFW được ép chạy qua X11/XWayland — xem log lúc khởi động. Đây là chủ ý,
không phải lỗi.
