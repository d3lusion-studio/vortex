# math_primitives

Lát cắt thư viện toán 2D, headless: khối bao, phép kiểm giao/raycast, số đo của primitive,
lấy mẫu đều, và một primitive **do game tự định nghĩa** cắm vào cùng những thuật toán generic
đó.

```bash
cmake --build build/debug --target math_primitives
./build/debug/examples/math_primitives
```

| # | Giai đoạn | Nội dung |
|---|---|---|
| 1 | Giao khối bao | AABB/đường tròn chồng nhau + ray cast |
| 2 | Primitive | diện tích / chu vi / `contains` |
| 3 | Lấy mẫu | lấy điểm ngẫu nhiên phân bố **đều** trên hình |
| 4 | Primitive tuỳ biến | hình do game định nghĩa, dùng chung thuật toán |

Giai đoạn 3 tinh tế hơn vẻ ngoài: lấy mẫu đều trên một đường tròn bằng cách random góc và
random bán kính độc lập sẽ cho kết quả **dồn về tâm**, không hề đều. Đây là lỗi trông đúng
cho tới khi ai đó vẽ 10 000 điểm ra và thấy cụm ở giữa.

Giai đoạn 4 là điểm kiến trúc: thuật toán viết theo concept chứ không theo danh sách kiểu
đóng, nên hình dạng riêng của game được hưởng nguyên bộ mà không phải sửa engine.

Là test hồi quy CI như [anim_state](../anim_state/) — mỗi giai đoạn in ra thứ nó xác minh,
thoát mã khác 0 ở lỗi đầu tiên.
