# vortex::rhi

Render Hardware Interface: một API đồ hoạ duy nhất, nhiều backend phía dưới.

```cmake
target_link_libraries(mygame PRIVATE Vortex::rhi)
```

```bash
VORTEX_RHI_API=webgpu ./build/debug/examples/mesh3d    # mặc định: vulkan
```

Đổi backend **chỉ là đổi biến môi trường** — không sửa một dòng renderer hay gameplay nào.

| Header | Nội dung |
|---|---|
| `device.hpp` | `IDevice` — tạo buffer, texture, pipeline, bind group |
| `swapchain.hpp` | `ISwapchain` — acquire/present, present mode |
| `command_list.hpp` | `ICommandList` — begin pass, bind, draw, push constant |
| `rhi_types.hpp`, `rhi_enums.hpp`, `rhi_handle.hpp` | struct mô tả, enum, handle |

## Hai ngân sách cứng cần nhớ

**Push constant ≤ 128 byte**, theo giới hạn WebGPU. Push block của pipeline mesh 3D hiện tại
**đúng 128 byte và đã đầy** — thêm gì nữa thì phải chuyển sang uniform buffer.

**Tối đa 4 descriptor set.** Pipeline mesh 3D dùng cả bốn; shadow map phải nằm chung trong
set của scene chứ không có set riêng.

Hai con số này là lý do một số thứ trong renderer trông vòng vo hơn cần thiết. Chúng là ràng
buộc thật, không phải lựa chọn thẩm mỹ.

## Khác biệt giữa hai backend

Vulkan và WebGPU không cùng mô hình, và chỗ lệch nhau đã được xử lý trong lớp này:

- **sampler tách rời** — WebGPU tách sampler khỏi texture, Vulkan thì gộp được
- **command buffer thứ cấp hoãn lại** — mô hình ghi đa luồng của WebGPU khác Vulkan

## Đo hiệu năng

Mặc định `PresentMode::Fifo` (V-Sync). Khi đo tốc độ phải chuyển sang
**`PresentMode::Immediate`**, nếu không swapchain sẽ chặn con số ở tần số quét màn hình và
mọi cải tiến trông như vô tác dụng.

Xem [examples/triangle](../../examples/triangle/) (mức thấp nhất còn vẽ được),
[examples/instancing](../../examples/instancing/), [examples/mt_record](../../examples/mt_record/).
