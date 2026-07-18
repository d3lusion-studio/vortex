# app_settings

Cấu hình sống lâu hơn tiến trình.

```bash
cmake --build build/debug --target app_settings
./build/debug/examples/app_settings
```

Luồng thực hiện, và lý do nó phải theo đúng thứ tự này:

1. **Tự đọc file Settings, trước khi `App` tồn tại.**
2. Đổ dữ liệu đó vào `AppConfig` — kích thước cửa sổ, post-processing, bất cứ thứ gì người
   chơi đã chọn.
3. Dựng `App` với config đó, và đưa cho nó cùng tên settings để nó giữ file luôn cập nhật và
   ghi lại lúc tắt.

Thứ tự này bắt buộc vì kích thước cửa sổ được quyết định **lúc `App` được dựng**. Đọc
settings sau khi có `App` thì đã quá muộn — cửa sổ đã mở ở kích thước mặc định rồi, và cách
duy nhất còn lại là mở xong rồi đổi kích thước, người dùng sẽ thấy nó giật một cái.

Chạy lần đầu, đổi cấu hình, thoát, rồi chạy lại: cấu hình phải quay lại y nguyên.

| Biến môi trường | Ý nghĩa |
|---|---|
| `VORTEX_MAX_FRAMES` | chạy N frame rồi thoát |
