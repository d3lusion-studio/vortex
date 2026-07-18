# triangle

Tam giác "hello world" của đồ hoạ, vẽ trực tiếp qua RHI — không có `App`, không
`SpriteBatch`, không renderer.

```bash
cmake --build build/debug --target triangle
./build/debug/examples/triangle
```

Đây là example ở mức thấp nhất còn vẽ được ra màn hình, nên nó là chỗ để đọc khi muốn hiểu
[engine/rhi/](../../engine/rhi/) thực sự yêu cầu những gì:

1. `rhi::createDevice(window)` — chọn adapter, tạo device và queue
2. `device->createSwapchain(...)` với `PresentMode::Fifo`
3. nạp hai file SPIR-V đã biên dịch sẵn lúc build (GLSL được dịch ahead-of-time, không
   compile trong process)
4. một vertex buffer gồm 3 đỉnh, mỗi đỉnh là vị trí clip-space + màu
5. tạo pipeline, rồi mỗi frame: acquire → begin pass → bind → `draw(3)` → end → present

Màu nội suy giữa ba đỉnh là do rasteriser làm, không phải shader — đó là cách nhanh nhất để
xác nhận vertex input layout được khai báo đúng.

| Biến môi trường | Ý nghĩa |
|---|---|
| `VORTEX_MAX_FRAMES` | chạy đúng N frame rồi thoát sạch (dùng cho smoke test CI) |
| `VORTEX_RHI_API` | `vulkan` (mặc định) hoặc `webgpu` |

Nhấn `Esc` để thoát.
