# instancing

Instancing qua RHI: cả một lưới quad vẽ trong **một** draw call.

```bash
cmake --build build/debug --target instancing
./build/debug/examples/instancing
```

Bốn góc của quad được sinh trong shader; offset và màu của từng instance lấy từ một vertex
buffer per-instance duy nhất. `draw(4, N)` phát ra N instance cùng lúc — không có công việc
CPU nào theo từng quad, và không có draw call nào theo từng quad.

Đây là cấp độ sau [atlas](../atlas/): atlas gộp draw call bằng cách bỏ việc đổi texture,
còn instancing thì bỏ luôn việc lặp trên CPU.

## Tự kiểm tra headless

```bash
VORTEX_INSTANCING_CHECK=1 ./build/debug/examples/instancing
```

Gán cho mỗi instance một màu riêng biệt, render lưới vào target offscreen, đọc về, rồi đếm
số màu khác nền phân biệt được. Không bật MSAA nên cạnh sắc, và số màu đếm được phải đúng
bằng N — tức là **cả N instance đều thực sự được vẽ**, không thiếu cái nào. Cần GPU.

| Biến môi trường | Ý nghĩa |
|---|---|
| `VORTEX_INSTANCING_CHECK` | chạy chế độ tự kiểm tra rồi thoát |
| `VORTEX_MAX_FRAMES` | chạy N frame rồi thoát |

Nhấn `Esc` để thoát.
