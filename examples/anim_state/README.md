# anim_state

Máy trạng thái animation, chạy headless.

```bash
cmake --build build/debug --target anim_state
./build/debug/examples/anim_state
```

Một rig hai khớp tổng hợp và ba clip tổng hợp (Idle; Walk 1.2 m/s có event tiếng bước chân;
Jump không lặp) điều khiển một `StateMachine` đúng theo cách gameplay sẽ làm:

```
speed > 0.5      ->  Idle fade sang Walk
trigger("jump")  ->  bất cứ đâu cũng fade sang Jump
Jump kết thúc    ->  exit time 1.0 trả về lại Idle
```

Không cửa sổ, không GPU — nên đây là **bài kiểm tra hồi quy CI** cho hệ animation, giống
[text](../text/) và [pathfinding](../pathfinding/).

Ngoài chuyển trạng thái, nó còn kiểm root motion: chuyển động do animation sinh ra được rút
ra thành độ dịch chuyển của nhân vật, thay vì nhân vật trượt độc lập với chân đang bước
("foot sliding").

Mỗi giai đoạn in ra thứ nó vừa xác minh, và process thoát mã khác 0 ở lời nói dối đầu tiên.

Bản có GPU và model thật: [skinned](../skinned/).
