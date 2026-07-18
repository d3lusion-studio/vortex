# assets_demo

Hệ thống asset, và tám việc mà bản cũ không làm được.

```bash
cmake --build build/debug --target assets_demo
./build/debug/examples/assets_demo
```

| # | Khả năng | Nội dung |
|---|---|---|
| 1 | asset tuỳ biến | loader cho định dạng engine chưa từng biết (`.pal`) |
| 2 | async + barrier | việc nạp chạy trên luồng IO; một barrier báo tiến độ |
| 3 | asset sinh trong code | texture dựng bằng code, đăng ký như mọi asset khác |
| 4 | tuỳ chọn texture | repeat/clamp, sRGB/UNORM — quyết định **lúc nạp** |
| 5 | lưu asset | ghi **qua loader**, nên kiểu nào cũng ghi được, không riêng texture |
| 6 | subasset | `"file.pal#warm"` — một file, nhiều asset, mỗi cái cache riêng |

Điểm 1 là điểm quan trọng nhất về mặt kiến trúc: định dạng riêng của game được đăng ký từ
phía game, engine không cần biết trước. Điểm 6 đi kèm quy ước URI `scheme://` + `#subasset`,
dùng chung một đường `fetchBytes()` cho file trên đĩa, dữ liệu nhúng và HTTP.

| Biến môi trường | Ý nghĩa |
|---|---|
| `VORTEX_WEB_ASSET_URL` | URL để thử nạp asset qua HTTP (bỏ qua giai đoạn này nếu không đặt) |

Example này chạy headless và tự in ra từng bước đã kiểm chứng.
