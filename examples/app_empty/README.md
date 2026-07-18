# app_empty

Thứ nhỏ nhất mà vẫn là một game.

```bash
cmake --build build/debug --target app_empty
./build/debug/examples/app_empty
```

Ba dòng. Và đây là những gì ba dòng đó cho ta:

- một cửa sổ, một device Vulkan và swapchain
- một sprite batcher
- một job system
- một asset manager
- một scene ECS kèm camera 2D
- một input map
- một particle world
- audio và physics, tạo lười (chỉ dựng khi lần đầu được hỏi tới)

Và một vòng lặp điều phối tất cả những thứ đó với **fixed timestep cho mô phỏng** và
**variable timestep cho render** — đồng thời không rơi vào vòng xoáy chết khi một frame chạy
quá lâu.

Ý nghĩa của file này là làm rõ ranh giới: mọi thứ ở trên là thứ `App` cho không, còn mọi
example tự dựng vòng lặp riêng ([triangle](../triangle/), [mesh3d](../mesh3d/),
[rendergraph](../rendergraph/)) là đang từ chối phần đó để đổi lấy quyền kiểm soát.

Xem [app_plugin](../app_plugin/) và [app_settings](../app_settings/) để biết cách mở rộng.
