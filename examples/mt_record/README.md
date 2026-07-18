# mt_record

Ghi lệnh vẽ đa luồng: nhiều luồng cùng ghi vào command buffer thứ cấp, rồi gộp lại thành một
lần submit.

```bash
cmake --build build/debug --target mt_record
./build/debug/examples/mt_record
VORTEX_QUADS=200000 ./build/debug/examples/mt_record
```

Đây là dạng song song khác với [threaded](../threaded/). Ở đó, mô phỏng và render chạy lệch
pha nhau theo thời gian; ở đây, **bản thân việc ghi lệnh vẽ được chia cho nhiều luồng cùng
lúc**, mỗi luồng lấy một khoảng quad và ghi vào buffer riêng của nó.

Việc này chỉ an toàn vì mỗi luồng ghi vào command buffer riêng — chia sẻ một buffer giữa các
luồng thì không hợp lệ ở cả Vulkan lẫn WebGPU. Đó cũng là lý do backend WebGPU phải hoãn
(defer) các command buffer thứ cấp, vì mô hình của nó khác Vulkan ở điểm này.

| Phím | Tác dụng |
|---|---|
| `Esc` | thoát |

| Biến môi trường | Ý nghĩa |
|---|---|
| `VORTEX_QUADS` | số quad — vặn lên để kiểm việc ghi đa luồng |
| `VORTEX_MAX_FRAMES` | chạy N frame rồi thoát |
