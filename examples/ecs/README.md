# ecs

Registry, transform phân cấp cha-con, và bước extract → batch.

```bash
cmake --build build/debug --target ecs
./build/debug/examples/ecs
```

Example này giới thiệu kiến trúc mà mọi game trong repo đều dùng lại. Thế giới là một
`ecs::Registry`; sprite không tự vẽ chính mình. Mỗi frame có hai pha tách bạch:

1. **cập nhật** — system chạy trên component (ở đây: `Spin` quay các entity)
2. **extract** — duyệt registry, sinh ra danh sách `RenderItem` phẳng, rồi đưa cho batcher

Việc tách hai pha này là điều kiện để có được [threaded](../threaded/): pha extract là ranh
giới duy nhất giữa luồng game và luồng render.

Transform cha-con cũng ở đây — entity vệ tinh gắn vào một entity gốc, và di chuyển gốc thì
cả cụm đi theo, vì transform toàn cục được tính từ chuỗi cha trước khi extract.

| Phím | Tác dụng |
|---|---|
| `WASD` / phím mũi tên | pan camera |
| `Esc` | thoát |

| Biến môi trường | Ý nghĩa |
|---|---|
| `VORTEX_SATELLITES` | số cụm entity spawn ra |
| `VORTEX_MAX_FRAMES` | chạy N frame rồi thoát |
