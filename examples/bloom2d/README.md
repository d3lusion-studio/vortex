# bloom2d

Bloom cho 2D, qua `App`.

```bash
cmake --build build/debug --target bloom2d
./build/debug/examples/bloom2d
```

Bật `AppConfig::postProcess` là sprite được render vào một target dạng float, rồi bloom +
tone mapping ACES chạy trên đường ra màn hình.

Việc duy nhất game phải làm khác đi là **tint những sprite muốn phát sáng lên trên 1.0**. Màu
`{6, 3, 1, 1}` sáng gấp sáu lần trắng, và bright pass chính là thứ đi tìm những pixel đó.

Trong example, sprite tối được tint dưới 1 và không hề bloom; sprite sáng được tint trên 1 và
có quầng sáng. Cả hai dùng chung một shader, một pipeline — khác biệt duy nhất là con số.

Đó là lý do HDR phải có mặt: trong một target 8-bit, mọi thứ đều bị kẹp về 1.0 và không có
cách nào để phân biệt "trắng" với "chói".

| Phím | Tác dụng |
|---|---|
| `Space` | bật/tắt bloom |
| `Esc` | thoát |

| Biến môi trường | Ý nghĩa |
|---|---|
| `VORTEX_MAX_FRAMES` | chạy N frame rồi thoát |
