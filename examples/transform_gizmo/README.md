# transform_gizmo

Gizmo transform (tịnh tiến) tương tác, headless: tia chuột bắt vào trục nào, và kéo thì mục
tiêu di chuyển dọc đúng trục đó ra sao.

```bash
cmake --build build/debug --target transform_gizmo
./build/debug/examples/transform_gizmo
```

Tương tác này là toán thuần — trong ứng dụng thật, `Camera::viewportToWorld` sẽ cung cấp tia
— nên nó xác minh được mà không cần cửa sổ hay GPU.

| # | Giai đoạn | Nội dung |
|---|---|---|
| 1 | Hover | tia đi gần một tay cầm thì chọn trục đó; đi xa thì không chọn gì |
| 2 | Kéo | kéo làm mục tiêu di chuyển dọc trục đã bắt |
| 3 | Khoá trục | chuyển động con trỏ lệch khỏi trục **không rò rỉ** sang trục khác |

Giai đoạn 3 là giai đoạn quan trọng. Chuột di chuyển trong mặt phẳng 2D còn trục thì nằm
trong không gian 3D, nên phép chiếu phải loại bỏ toàn bộ thành phần vuông góc với trục. Làm
sai thì kéo theo X cũng khẽ dịch cả Y và Z — vừa đủ nhỏ để không ai nhận ra cho tới khi cảnh
đã lệch mất.

Là test hồi quy CI như các bài khác — thoát mã khác 0 ở lỗi đầu tiên.

Gizmo chỉ để nhìn (không tương tác): [gizmos3d](../gizmos3d/).
