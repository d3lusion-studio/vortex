# alien_cake_addict

Một game 3D nhỏ trên lưới ô vuông. Một khối lập phương nhảy quanh bàn cờ, và mỗi lần nó đáp
trúng cái bánh thì điểm tăng lên còn bánh mới xuất hiện ở chỗ khác. Mỗi cái bánh ăn được
cũng thả một khối xuống một cái tháp thưởng đang cao dần.

```bash
cmake --build build/debug --target alien_cake_addict
./build/debug/examples/alien_cake_addict
```

Đây là ví dụ hoàn chỉnh của đường 3D — render mesh, mặt trời đổ bóng, HDR có tone mapping —
ghép với gameplay lưới ô thuần tuý.

Điểm kiến trúc đáng học: **gameplay là một value type nhỏ (`Game`) không chứa một dòng
rendering nào**. Chính vì thế bài tự kiểm tra mới xác minh được luật chơi mà không cần GPU:

```bash
VORTEX_ALIENCAKE_CHECK=1 ./build/debug/examples/alien_cake_addict
```

Chế độ này tự động đi về phía bánh và kiểm tra điểm số lên đúng như luật. Tách logic khỏi
render không phải chuyện thẩm mỹ — nó là điều kiện để test được.

| Phím | Tác dụng |
|---|---|
| `WASD` / phím mũi tên | di chuyển trên lưới |
| `Esc` | thoát |

| Biến môi trường | Ý nghĩa |
|---|---|
| `VORTEX_ALIENCAKE_CHECK` | tự chơi và kiểm tra luật, không cần GPU |
| `VORTEX_MAX_FRAMES` | chạy N frame rồi thoát |
