# ecs_features

Lát cắt tính năng ECS, headless: các tiện ích mới của `Registry` được chứng minh mà không cần
cửa sổ hay GPU, nên nó kiêm luôn vai trò test hồi quy CI giống [anim_state](../anim_state/).

```bash
cmake --build build/debug --target ecs_features
./build/debug/examples/ecs_features
```

Mỗi giai đoạn dùng một tiện ích đúng theo cách gameplay sẽ dùng, và in ra thứ nó vừa xác
minh; process thoát mã khác 0 ở lời nói dối đầu tiên.

| # | Tiện ích | Nội dung |
|---|---|---|
| 1 | Command buffer | spawn/destroy/add hoãn lại, áp dụng khi `flush` |
| 2 | Lifecycle hook | `onAdd`/`onRemove` bắn khi chèn, gỡ, và khi huỷ entity |
| 3 | Event & observer | `emit()`/`observe()` với payload có kiểu |
| 4 | Lan truyền observer | event nổi lên theo chuỗi `Parent`, và bị chặn lại |
| 5 | Vô hiệu hoá entity | `view()` giấu entity bị disable, `enable()` đưa nó trở lại |
| 6 | Tổ hợp khi duyệt | mọi cặp không thứ tự được thăm **đúng một lần** |
| 7 | Command trễ | một command chỉ chạy sau khi timer của nó hết |
| 8 | Callback | hành vi theo từng entity, chạy khi được gọi |

Giai đoạn 6 đáng chú ý với ai viết code va chạm thủ công: duyệt mọi cặp mà không cẩn thận thì
mỗi cặp bị xử lý hai lần (A–B và B–A), và lực va chạm bị nhân đôi.

Command buffer là thứ giải bài toán "sửa thế giới trong lúc đang duyệt thế giới" — xem
[breakout](../breakout/) để thấy nó dùng thật: một contact physics không thể huỷ entity ngay
giữa lúc solver đang chạy.

Lifecycle hook giải bài toán đối xứng: một component sở hữu tài nguyên ngoài (body physics,
handle audio) cần biết khi nào nó bị gỡ, kể cả khi entity bị huỷ cả cụm.
