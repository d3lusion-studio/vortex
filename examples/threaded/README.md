# threaded

Vòng lặp game dạng pipeline: mô phỏng chạy trên luồng riêng, đi trước renderer một frame.

```bash
cmake --build build/debug --target threaded
./build/debug/examples/threaded
VORTEX_THREADED=0 ./build/debug/examples/threaded    # để so sánh
```

```
đơn luồng:  [sim N][render N][sim N+1][render N+1] ...   frame = sim + render
pipeline:   [sim N+1        ]                            frame = max(sim, render)
            [render N       ]
```

Đặt `AppConfig::threadedSimulation` là xong; phần còn lại vòng lặp lo. Điều khoản duy nhất nó
đòi lại từ phía game dài đúng một dòng: **không đụng vào GPU từ một hook update**.

Để so sánh có ý nghĩa, example checksum thế giới ở một **tick cố định** (`kCheckTick = 200`)
chứ không phải ở cuối lần chạy — vì pipeline mô phỏng xa hơn một frame so với cái nó render,
nên tổng số tick hai chế độ đạt tới vốn không cần bằng nhau. So ở một tick cố định là phép so
duy nhất có ý nghĩa, và nó cũng chứng minh việc song song hoá **không làm đổi kết quả mô
phỏng**.

**Khi đo tốc độ**, dùng `presentMode = Immediate` — nếu không, V-Sync sẽ chặn cả hai chế độ ở
cùng một con số và pipeline trông như vô dụng.

| Biến môi trường | Mặc định | Ý nghĩa |
|---|---|---|
| `VORTEX_THREADED` | `1` | `0` để chạy đơn luồng |
| `VORTEX_ENTITIES` | `20000` | số entity mô phỏng |
| `VORTEX_PARALLEL_EXTRACT` | `1` | song song hoá pha extract |
| `VORTEX_WORKERS` | — | số luồng worker |
| `VORTEX_POST` | — | bật post-processing |
| `VORTEX_MAX_FRAMES` | — | chạy N frame rồi thoát |
