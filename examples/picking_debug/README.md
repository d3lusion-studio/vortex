# picking_debug

Công cụ debug cho picking: overlay vẽ tia pick, điểm trúng, và viền của entity đang nằm dưới
con trỏ.

```bash
cmake --build build/debug --target picking_debug
./build/debug/examples/picking_debug
```

Nó dùng lại **đúng đường pick thật**, nên nó cũng là một phép kiểm rằng picking và phần trực
quan hoá của nó đồng ý với nhau. Một overlay vẽ theo logic riêng thì khi hai bên lệch nhau,
overlay sẽ nói dối đúng vào lúc ta cần nó nhất.

## Tự kiểm tra headless

```bash
VORTEX_PICKINGDEBUG_CHECK=1 ./build/debug/examples/picking_debug
```

Nhắm con trỏ vào một cái hộp rồi ra ngoài nó, khẳng định kết quả hit đúng **và** overlay thực
sự sinh ra hình học (qua đọc ngược pixel từ target offscreen). Thoát mã khác 0 nếu không.

| Phím | Tác dụng |
|---|---|
| chuột | di chuyển con trỏ pick |
| `Esc` | thoát |

| Biến môi trường | Ý nghĩa |
|---|---|
| `VORTEX_PICKINGDEBUG_CHECK` | chạy chế độ tự kiểm tra rồi thoát |
| `VORTEX_MAX_FRAMES` | chạy N frame rồi thoát |

Phần logic thuần: [picking](../picking/), [mesh_picking](../mesh_picking/).
