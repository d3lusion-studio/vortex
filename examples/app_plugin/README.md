# app_plugin

Plugin và nhóm plugin.

```bash
cmake --build build/debug --target app_plugin
./build/debug/examples/app_plugin
```

Điều đáng chứng minh ở đây là điều trước kia **không thể xảy ra**: ba plugin, mỗi cái đăng ký
một hook update, và **cả ba đều chạy**.

Dưới bản `App` cũ — nơi `onUpdate()` *thay thế* callback đã đăng ký trước đó thay vì *nối
thêm* — plugin đăng ký sau cùng sẽ âm thầm xoá mất hai cái kia. Triệu chứng duy nhất là hai
hệ thống lặng lẽ ngừng hoạt động: không crash, không cảnh báo, không log.

Đó chính là loại lỗi mà một API đúng phải làm cho không thể viết ra được, và là lý do
`onUpdate` giờ nối thêm chứ không ghi đè.

| Biến môi trường | Ý nghĩa |
|---|---|
| `VORTEX_MAX_FRAMES` | chạy N frame rồi thoát |
| `VORTEX_LOG` | mức log (đọc bởi engine, không riêng example này) |
