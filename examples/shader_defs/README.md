# shader_defs

Shader def và pipeline chuyên biệt hoá, qua RHI.

```bash
cmake --build build/debug --target shader_defs
./build/debug/examples/shader_defs
```

Một file nguồn fragment duy nhất (`effect.frag`) được biên dịch **lúc build** thành bốn biến
thể SPIR-V — hai khối tính năng `WAVE` và `TINT`, mỗi khối bật hoặc tắt — và một
`renderer::PipelineCache` chuyên biệt hoá pipeline cho biến thể nào được yêu cầu, dựng mỗi
cái đúng một lần.

Đây là câu trả lời của engine cho "Shader Defs" + "Specialized Mesh Pipeline" của Bevy, với
một khác biệt then chốt: **cờ def cố định lúc build**, vì kiến trúc này biên dịch GLSL ahead-
of-time chứ không compile trong process. Lúc chạy, engine chỉ chọn và cache biến thể nó cần.

| Phím | Biến thể |
|---|---|
| `1` | không bật gì |
| `2` | chỉ `WAVE` |
| `3` | chỉ `TINT` |
| `4` | cả `WAVE` và `TINT` |
| `Space` | chuyển sang biến thể kế tiếp |
| `Esc` | thoát |

Nếu không bấm gì, example tự đổi biến thể mỗi 120 frame.

| Biến môi trường | Ý nghĩa |
|---|---|
| `VORTEX_SHADERDEFS_CHECK` | chạy chế độ tự kiểm tra rồi thoát |
| `VORTEX_MAX_FRAMES` | chạy N frame rồi thoát |
