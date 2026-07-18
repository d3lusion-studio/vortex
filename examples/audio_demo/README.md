# audio_demo

Audio, dưới dạng một chuyến tham quan có hướng dẫn: mọi khả năng của `IAudioEngine`, từng
giai đoạn một, với console thuyết minh việc loa đang phải làm gì.

```bash
cmake --build build/debug --target audio_demo
./build/debug/examples/audio_demo
```

Không có cửa sổ — audio cần **thời gian**, không cần pixel. Trên máy không có thiết bị âm
thanh, nó nói vậy rồi thoát sạch, nên an toàn khi chạy trong CI.

| # | Giai đoạn | Nội dung |
|---|---|---|
| 1 | load + play | một file WAV tổng hợp đọc từ đĩa (example tự ghi ra asset của nó) |
| 2 | control | pause/resume, seek, pitch, pan trên một âm đang lặp |
| 3 | tone | `createWaveform`: phát một cao độ, quét tần số ngay lúc chạy |
| 4 | procedural | `createProcedural`: **callback chính là âm thanh** (tổng hợp FM) |

Giai đoạn 4 là giai đoạn đáng chú ý nhất về mặt kiến trúc: nguồn âm không nhất thiết là dữ
liệu. Một hàm sinh mẫu theo thời gian thực cũng là một nguồn hợp lệ, đi qua đúng đường trộn
và điều khiển như một file WAV.

Lưu ý: audio trong Vortex **mặc định không phải là spatial** — muốn âm thanh theo vị trí thì
phải bật rõ ràng.
