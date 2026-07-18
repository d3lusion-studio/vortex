# ui_anim

UI động, đường cong easing, và không gian màu — cùng một `anim::Curve` dùng để pose bộ
xương, giờ chĩa vào một hình chữ nhật.

```bash
cmake --build build/debug --target ui_anim
./build/debug/examples/ui_anim
```

Đó là luận điểm đáng kiểm chứng: **ở đây không có "hệ thống animation cho UI"**. Một
`Curve<f32>` không biết và không cần biết số float nó sinh ra là góc quay của một khớp, độ
mờ của một panel, hay tiêu cự của camera.

Cần một hệ thống riêng cho từng thứ đó chính là cách một engine kết thúc với bốn hệ thống
animation bất đồng ý nhau về ý nghĩa của "ease-in".

Example còn cho thấy vì sao nội suy màu nên làm trong không gian màu tuyến tính chứ không
phải sRGB — nội suy sai không gian màu khiến đoạn chuyển giữa hai màu bị tối lại ở giữa.

| Biến môi trường | Ý nghĩa |
|---|---|
| `VORTEX_SCREENSHOT` | ghi frame ra file PPM tại đường dẫn chỉ định |
| `VORTEX_FONT_PATH` | chỉ định file TTF cụ thể |
| `VORTEX_MAX_FRAMES` | chạy N frame rồi thoát |

Nhấn `Esc` để thoát.
