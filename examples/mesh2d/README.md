# mesh2d

Đường mesh 2D và 9-patch, đặt cạnh nhau.

```bash
cmake --build build/debug --target mesh2d
./build/debug/examples/mesh2d
```

| Hàng | Nội dung |
|---|---|
| 1 | **hình** — tròn, lục giác, tam giác, đa giác lồi |
| 2 | **cung** — hình quạt, viên phân, cung có nét, vòng xoay |
| 3 | **màu theo đỉnh** — một mesh, mỗi đỉnh một màu (quad không làm được) |
| 4 | **blend mode** — cùng một hình vẽ ở `Opaque`, `Blend`, `Additive` trên nền |
| 5 | **9-patch** — một panel bo góc 32×32 kéo ra ba kích thước khác nhau |

Hàng 3 là lý do mesh 2D tồn tại song song với sprite: một quad có đúng một màu tint, còn
mesh thì nội suy màu qua từng đỉnh.

Hàng 5 là so sánh trực tiếp đáng nhìn nhất — 9-patch giữ nguyên bốn góc và chỉ kéo giãn phần
giữa, còn sprite phóng to một cách ngây thơ thì góc bị bóp méo theo. Mọi khung UI co giãn
được đều dựa trên cơ chế này.

| Phím | Tác dụng |
|---|---|
| `Space` | hàng 5: đổi giữa 9-patch và phóng to ngây thơ |
| `Esc` | thoát |
