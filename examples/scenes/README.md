# scenes

Lưu trữ và nhiều scene: dựng một màn, lưu ra JSON, nạp lại, đóng dấu một prefab vào đó, rồi
chuyển qua lại giữa hai scene lúc chạy.

```bash
cmake --build build/debug --target scenes
./build/debug/examples/scenes
```

Vòng round-trip được kiểm ngay trong tiến trình lúc khởi động: **số entity, một transform lấy
mẫu, và tilemap đều phải quay về giống hệt**, nếu không example sẽ báo.

Đó là phép kiểm đáng có, vì serialize hỏng theo kiểu im lặng: thiếu một trường thì scene vẫn
nạp được, chỉ là vài thứ nằm sai chỗ, và không có gì báo lỗi cả.

## Chuyển scene

```cpp
a.scenes().requestSwitch("menu");
```

Chữ **request** là cố ý. Việc chuyển được **xếp hàng và áp dụng ở đầu frame kế tiếp**, nên
system đang duyệt giữa chừng không bao giờ thấy thế giới đổi dưới chân nó. Đổi scene ngay lập
tức giữa một vòng lặp là cách chắc chắn để có iterator treo.

| Phím | Tác dụng |
|---|---|
| `Tab` | chuyển qua lại giữa scene `main` và `menu` |
| `F5` | lưu scene ra JSON |
| `F9` | nạp scene từ JSON |
| `Esc` | thoát |

| Biến môi trường | Ý nghĩa |
|---|---|
| `VORTEX_MAX_FRAMES` | chạy N frame rồi thoát |
