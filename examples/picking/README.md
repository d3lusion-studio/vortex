# picking

Chọn đối tượng bằng con trỏ, headless: hover, click, hit-test theo lớp, kéo, kéo-thả, và một
backend tuỳ biến — tất cả điều khiển bằng input con trỏ tổng hợp và xác minh qua event, không
cần cửa sổ hay GPU.

```bash
cmake --build build/debug --target picking
./build/debug/examples/picking
```

| # | Giai đoạn | Nội dung |
|---|---|---|
| 1 | Hover | `Over` khi vào, `Out` khi ra |
| 2 | Click | `Down`/`Up`/`Click` khi nhấn-thả trên một entity |
| 3 | Phân lớp | entity trên cùng trong các pickable chồng nhau thắng |
| 4 | Vô hiệu hoá | entity bị disable thì không pick được |
| 5 | Kéo | `DragStart`, `Drag`, `DragEnd` trên một entity kéo được |
| 6 | Kéo & thả | `Drop` báo cái gì được thả và thả lên ai |
| 7 | Backend tuỳ biến | thay hit-test nhưng vẫn sinh đúng bộ event đó |

Giai đoạn 7 là điểm kiến trúc: `PickingSystem` không biết hình học. Nó nhận kết quả hit-test
từ một backend và sinh event, nên cùng một hệ thống phục vụ được sprite 2D, mesh 3D
([mesh_picking](../mesh_picking/)), và bất cứ quy tắc riêng nào game muốn.

Là test hồi quy CI — thoát mã khác 0 ở lỗi đầu tiên.

Bản trực quan hoá có GPU: [picking_debug](../picking_debug/).
