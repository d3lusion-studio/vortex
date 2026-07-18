# game

Một vòng lặp game 2D hoàn chỉnh: sprite có animation, camera có cull, và camera bám nhân
vật — không một dòng boilerplate window/device/swapchain nào.

```bash
cmake --build build/debug --target game_example
./build/debug/examples/game_example
```

> Lưu ý: thư mục là `game/` nhưng **target tên là `game_example`** (`game` đã bị dùng cho
> mục đích khác trong CMake).

Đây là example ngắn nhất mà vẫn ra hình một trò chơi thực sự. Đối chiếu độ dài của nó với
[mesh3d](../mesh3d/) là cách nhanh nhất để thấy `App` tiết kiệm được bao nhiêu.

Ba thứ đang chạy cùng lúc:

- **sprite animation** — chuyển frame theo thời gian, không theo số frame render
- **cull theo camera** — chỉ những gì trong khung nhìn mới được extract và vẽ
- **camera bám** — nội suy độc lập với frame rate, nên tốc độ bám không đổi theo FPS

| Phím | Tác dụng |
|---|---|
| `WASD` | di chuyển nhân vật |
| `Esc` | thoát |

| Biến môi trường | Ý nghĩa |
|---|---|
| `VORTEX_MAX_FRAMES` | chạy N frame rồi thoát |

Game hoàn chỉnh hơn: [breakout](../breakout/), [games/farm_rpg](../../games/farm_rpg/),
[games/roller](../../games/roller/).
