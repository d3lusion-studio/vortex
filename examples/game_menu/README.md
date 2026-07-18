# game_menu

Lớp vỏ ngoài của một game: màn hình loading chờ asset nạp thật, menu chính dựng bằng UI
immediate-mode, và một trạng thái "đang chơi" nhỏ để vào ra.

```bash
cmake --build build/debug --target game_menu
./build/debug/examples/game_menu
```

Nó phủ hai thứ mà mọi game cần trước khi cần bất cứ thứ gì khác — **Loading Screen** và
**Game Menu** — dựng trên vòng lặp `App`, `AssetManager`, và module [ui](../../engine/ui/).

Điểm đáng chú ý là màn hình loading **chờ việc nạp asset thật**, không phải một thanh tiến
trình giả chạy theo đồng hồ. Tiến độ đến từ barrier của `AssetManager`, nên nó phản ánh đúng
số asset đã xong — và nếu một asset nạp lỗi, màn hình loading biết điều đó.

| Phím | Tác dụng |
|---|---|
| `Esc` | quay lại menu / thoát |

| Biến môi trường | Ý nghĩa |
|---|---|
| `VORTEX_GAMEMENU_CHECK` | tự động đi qua cả ba trạng thái rồi thoát (dùng cho CI) |
| `VORTEX_FONT_PATH` | chỉ định file TTF cụ thể |
| `VORTEX_MAX_FRAMES` | chạy N frame rồi thoát |
