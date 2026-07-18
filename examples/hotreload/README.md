# hotreload

Hot reload code gameplay: sửa `game.cpp`, build lại, và game đang chạy nhận code mới **mà
không khởi động lại**.

```bash
cmake --build build/debug --target hotreload
./build/debug/examples/hotreload
```

Sau đó, trong lúc nó vẫn đang chạy, sửa [game.cpp](game.cpp) rồi:

```bash
cmake --build build/debug --target game        # build lại thư viện gameplay
```

Cửa sổ nhận thay đổi ngay.

## Cách hoạt động

Ba file, và ranh giới giữa chúng mới là điều đáng đọc:

| File | Vai trò |
|---|---|
| [game_api.h](game_api.h) | **hợp đồng** — struct `GameApi` gồm các con trỏ hàm, cộng số phiên bản |
| [game.cpp](game.cpp) | gameplay, build thành thư viện chia sẻ (`libgame.so`) |
| [host.cpp](host.cpp) | chương trình chính: mở cửa sổ, nạp thư viện, theo dõi file |

Host theo dõi thời gian sửa của file `.so`. Khi nó đổi, host **copy ra một đường dẫn tạm rồi
mới nạp** — nạp thẳng file gốc sẽ khoá nó và lần build kế tiếp thất bại.

Hai ràng buộc khiến mô hình này chạy được:

1. **Trạng thái game sống ở phía host**, không phải trong thư viện. Thư viện bị unload thì
   trạng thái vẫn còn, nên reload không mất tiến trình chơi.
2. **`VORTEX_GAME_API_VERSION` phải khớp.** Đổi bố cục `GameApi` mà quên tăng số phiên bản
   thì host sẽ gọi con trỏ hàm theo bố cục cũ — và đó là crash không thể lần ra.

| Biến môi trường | Ý nghĩa |
|---|---|
| `VORTEX_MAX_FRAMES` | chạy N frame rồi thoát |

| Phím | Tác dụng |
|---|---|
| `WASD` / phím mũi tên | di chuyển (hành vi do `game.cpp` quyết định — thử sửa nó) |
| `Esc` | thoát |
