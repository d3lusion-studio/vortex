# tilemap

Một màn chơi dạng tile: lớp sao parallax, lớp đất đặc, và một nhân vật va chạm với tile bằng
cách hỏi cờ của tile — **không có engine physics nào tham gia**.

```bash
cmake --build build/debug --target tilemap
./build/debug/examples/tilemap
```

Bản đồ là 512×64 tile (32 000 ô). Chỉ đúng phần màn hình dưới camera là được duyệt, nên đi
ngang hết cả màn chơi **tốn đúng bằng đứng yên**.

Đó là điểm chính. Một bản đồ tile không phải 32 000 entity; nó là một mảng, và render nó là
việc duyệt một cửa sổ chữ nhật trong mảng đó. Biến mỗi ô thành một entity là cách nhanh nhất
để giết hiệu năng của một thứ vốn rất rẻ.

Va chạm cũng vậy: hỏi "tile ở ô này có cờ `Solid` không" là một phép tra mảng, rẻ hơn nhiều
so với đưa 32 000 collider tĩnh vào Box2D.

| Phím | Tác dụng |
|---|---|
| `A` / `D` | chạy |
| `Space` | nhảy |
| `Esc` | thoát |

| Biến môi trường | Ý nghĩa |
|---|---|
| `VORTEX_MAX_FRAMES` | chạy N frame rồi thoát |

Cùng loại màn chơi nhưng dữ liệu đến từ file Tiled: [tiled](../tiled/).
