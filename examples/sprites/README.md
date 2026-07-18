# sprites

`SpriteBatch` với nhiều texture, và một camera 2D pan/zoom được.

```bash
cmake --build build/debug --target sprites
./build/debug/examples/sprites
```

Bước tiếp theo sau [triangle](../triangle/): thay vì tự dựng pipeline, ta đưa sprite cho
`renderer::SpriteBatch` và nó lo phần gộp draw call. Texture ở đây được sinh trong code
(khối đặc, hình tròn có alpha, ô bàn cờ) nên example không phụ thuộc file asset nào.

Điều đáng nhìn là số draw call: batcher gom các sprite **liên tiếp dùng chung texture** vào
một lệnh vẽ, nên thứ tự submit ảnh hưởng trực tiếp tới hiệu năng. Đó chính là vấn đề mà
[atlas](../atlas/) giải quyết triệt để.

| Phím | Tác dụng |
|---|---|
| `WASD` / phím mũi tên | pan camera |
| `Esc` | thoát |

| Biến môi trường | Ý nghĩa |
|---|---|
| `VORTEX_SPRITES` | số sprite spawn ra (mặc định trong source) |
| `VORTEX_MAX_FRAMES` | chạy N frame rồi thoát |
