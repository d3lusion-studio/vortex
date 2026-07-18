# vortex::jobs

Job system và channel: cách công việc rời khỏi vòng lặp game, và cách kết quả quay về.

```cmake
target_link_libraries(mygame PRIVATE Vortex::jobs)
```

| Header | Nội dung |
|---|---|
| `job_system.hpp` | `JobSystem` — `submit`, `parallelFor`, `JobHandle` |
| `channel.hpp` | `Channel<T>` — hàng đợi an toàn đa luồng, có `tryReceive` |

Module nhỏ nhất trong engine (~266 dòng), nhưng là nền cho phần lớn phần song song hoá.

## Mẫu hình chuẩn

```cpp
auto handle = jobs.submit([&] {
    auto result = expensiveWork();
    channel.send(result);          // gửi từ luồng của job
});

// mỗi frame — KHÔNG chặn:
while (auto r = channel.tryReceive())
    apply(*r);
```

Điểm mấu chốt là `tryReceive`. Nó không bao giờ chặn, nên frame rate không phụ thuộc việc job
đã xong hay chưa. Dùng một `receive()` chặn ở đây sẽ **xoá sạch lợi ích** của việc đưa công
việc sang luồng khác.

## `parallelFor`

```cpp
jobs.parallelFor(count, [&](usize i) { /* xử lý phần tử i */ });
```

Chia dải chỉ số cho các worker. Điều kiện an toàn là mỗi lần lặp **không ghi vào dữ liệu mà
lần lặp khác đọc** — job system không kiểm tra điều này giúp bạn.

Đây là cơ chế đằng sau việc ghi lệnh vẽ đa luồng: mỗi worker lấy một command list thứ cấp
riêng của nó (chia sẻ một command list giữa các luồng là không hợp lệ ở cả Vulkan lẫn
WebGPU).

Xem [examples/async_tasks](../../examples/async_tasks/),
[examples/mt_record](../../examples/mt_record/), [examples/threaded](../../examples/threaded/).
