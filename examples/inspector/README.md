# inspector

Inspector ImGui: duyệt entity đang sống, xem và sửa component ngay lúc chạy, kèm overlay
hiệu năng.

```bash
cmake --build build/debug --target inspector
./build/debug/examples/inspector
```

Ghép ba thứ trong [engine/debug/](../../engine/debug/): `ImGuiLayer` (lớp nền), `Inspector`
(duyệt registry), và `PerfOverlay` (biểu đồ thời gian frame).

Inspector đọc thẳng từ `ecs::Registry`, nên **không có bước đăng ký nào cho từng component**
— thêm một component mới vào game là nó xuất hiện trong inspector. Đó là khác biệt giữa một
công cụ debug được dùng và một công cụ debug bị bỏ quên vì phải bảo trì riêng.

`PerfOverlay` đọc cùng bộ đếm với `diag::` registry (xem [diagnostics](../diagnostics/)) —
đo một lần, đọc ở mọi nơi.

| Phím | Tác dụng |
|---|---|
| `Esc` | thoát |

| Biến môi trường | Ý nghĩa |
|---|---|
| `VORTEX_MAX_FRAMES` | chạy N frame rồi thoát |
