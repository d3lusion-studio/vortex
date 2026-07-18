# async_tasks

Task bất đồng bộ và channel, headless: công việc rời khỏi vòng lặp game bằng cách nào, và
kết quả quay về ra sao.

```bash
cmake --build build/debug --target async_tasks
./build/debug/examples/async_tasks
```

Mẫu hình mà file này tồn tại để minh hoạ:

1. Vòng lặp gặp một việc đắt (ở đây: đếm số nguyên tố trong các khoảng lớn).
2. Nó submit việc đó cho `JobSystem` và giữ lại một `JobHandle` — **frame không chờ**.
3. Job gửi kết quả vào một `Channel` khi xong, trên luồng của chính nó.
4. Mỗi frame, vòng lặp rút channel bằng `tryReceive` — lấy những gì đã tới, không block.

Bước 4 là bước quan trọng. `tryReceive` không bao giờ chặn, nên frame rate không phụ thuộc
vào việc job đã xong hay chưa. Dùng một `receive()` chặn ở đây sẽ xoá sạch lợi ích của việc
đưa công việc sang luồng khác.

Không cửa sổ, không GPU — chạy được trong CI.

Xem thêm [threaded](../threaded/) cho mô hình song song ở cấp toàn vòng lặp, và
[mt_record](../mt_record/) cho việc ghi lệnh vẽ đa luồng.
