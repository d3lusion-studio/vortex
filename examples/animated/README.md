# animated

Shader động: một hiệu ứng full-screen được điều khiển bằng thời gian từ lúc khởi động, đẩy
lên GPU qua push constant mỗi frame.

```bash
cmake --build build/debug --target animated
./build/debug/examples/animated
```

Đây là example "dữ liệu động trong shader" đơn giản nhất, và mọi hiệu ứng theo thời gian
khác đều là một biến thể của nó.

Push constant được chọn ở đây vì nó là con đường rẻ nhất để đưa vài byte vào shader mỗi
frame — không cần buffer, không cần descriptor set, không cần đồng bộ. Đổi lại là ngân sách
rất nhỏ: **tối đa 128 byte**, theo giới hạn của WebGPU (xem
[docs/api_reference.md](../../docs/api_reference.md)).

## Tự kiểm tra headless

```bash
VORTEX_ANIMATED_CHECK=1 ./build/debug/examples/animated
```

Render hiệu ứng vào texture offscreen ở **hai mốc thời gian khác nhau**, đọc ngược cả hai
về, và khẳng định chúng khác nhau — bằng chứng animation thực sự là hàm của thời gian chứ
không phải một ảnh tĩnh. Thoát mã khác 0 nếu không. Cần GPU.

| Biến môi trường | Ý nghĩa |
|---|---|
| `VORTEX_ANIMATED_CHECK` | chạy chế độ tự kiểm tra rồi thoát |
| `VORTEX_MAX_FRAMES` | chạy N frame rồi thoát |

Nhấn `Esc` để thoát.
