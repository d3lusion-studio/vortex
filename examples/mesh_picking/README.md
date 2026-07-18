# mesh_picking

Chọn mesh 3D, headless: phần toán tia, chọn vật gần nhất qua transform, pick chính xác tới
tam giác, và backend điều khiển bằng camera nuôi đúng cái `PickingSystem` đang chạy picking
2D — tất cả trên CPU.

```bash
cmake --build build/debug --target mesh_picking
./build/debug/examples/mesh_picking
```

| # | Giai đoạn | Nội dung |
|---|---|---|
| 1 | Toán tia | tia với AABB / cầu / tam giác, cả trúng và trượt |
| 2 | Che khuất | mesh gần nhất dọc theo tia thắng |
| 3 | Transform | xoay một entity thì tia trúng thứ khác |
| 4 | Pick chính xác | proxy tam giác từ chối tia mà AABB thô sẽ chấp nhận |
| 5 | Backend camera | click theo toạ độ màn hình chọn đúng mesh dưới con trỏ |

Giai đoạn 4 là khác biệt giữa "chọn được" và "chọn đúng": AABB của một cái thang hay một
thanh kiếm chéo chứa rất nhiều khoảng trống, và pick theo AABB sẽ trúng khi con trỏ còn cách
vật thể khá xa.

Giai đoạn 5 nối lại với [picking](../picking/) — cùng một `PickingSystem`, chỉ khác backend.

Là test hồi quy CI — thoát mã khác 0 ở lỗi đầu tiên.
