# vortex::audio

Audio, bọc quanh miniaudio. Một header, một interface.

```cmake
target_link_libraries(mygame PRIVATE Vortex::audio)
```

| Khả năng | API |
|---|---|
| nạp và phát | `load` + `play` |
| điều khiển | pause/resume, seek, pitch, pan trên âm đang chạy |
| tổng hợp tone | `createWaveform` — phát một cao độ, quét tần số lúc chạy |
| âm thanh thủ tục | `createProcedural` — **callback chính là âm thanh** |

## Mặc định **không** phải spatial

Đây là điều dễ nhầm nhất. Âm thanh mặc định phát không theo vị trí; muốn âm thanh 3D theo
khoảng cách và hướng thì phải **bật rõ ràng**. Chờ đợi hành vi spatial rồi thấy mọi âm thanh
phát đều nhau ở hai loa là triệu chứng của việc này.

## Nguồn âm không nhất thiết là dữ liệu

`createProcedural` nhận một callback sinh mẫu theo thời gian thực và coi nó là một nguồn hợp
lệ, đi qua đúng đường trộn và điều khiển như một file WAV. Tổng hợp FM, tiếng ồn động, âm
thanh sinh theo trạng thái game — tất cả đều đi lối này.

## Máy không có thiết bị âm thanh

Engine báo rõ và chạy tiếp thay vì sập, nên CI và máy ảo không cần cấu hình gì thêm.

## Tạo lười

Qua `App`, hệ audio chỉ được dựng khi lần đầu được hỏi tới. Game không dùng âm thanh thì
không trả chi phí nào.

Xem [examples/audio_demo](../../examples/audio_demo/) — tham quan từng khả năng một, có console
thuyết minh.
