# assets

`AssetManager` nạp PNG, và **hot reload**.

```bash
cmake --build build/debug --target assets
./build/debug/examples/assets
```

Bốn ảnh PNG được nạp qua `AssetManager` và vẽ ra, mỗi ảnh giữ đúng tỉ lệ khung hình gốc.

Phần đáng thử là hot reload: **sửa một file PNG trong `assets/` khi chương trình đang chạy**
rồi lưu lại — ảnh trên màn hình đổi theo ngay, không cần khởi động lại. `AssetManager` theo
dõi thời gian sửa file và nạp lại những asset đã đổi, còn handle mà game đang giữ thì vẫn
nguyên; chỉ nội dung phía sau handle được thay.

Đó là lý do asset được tham chiếu qua handle chứ không phải con trỏ — xem [hello](../hello/).

Thư mục asset được cố định lúc build qua define `VORTEX_ASSET_DIR` (không phải biến môi
trường), nên chạy binary từ đâu cũng tìm thấy ảnh.

| Biến môi trường | Ý nghĩa |
|---|---|
| `VORTEX_MAX_FRAMES` | chạy N frame rồi thoát |

Nhấn `Esc` để thoát.
