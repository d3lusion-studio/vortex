# vortex::platform

Mọi thứ thuộc về hệ điều hành, sau một lớp interface: cửa sổ, input, thời gian, file, thư
viện động.

```cmake
target_link_libraries(mygame PRIVATE Vortex::platform)
```

| Header | Interface | Nội dung |
|---|---|---|
| `window.hpp` | `IWindow` | tạo cửa sổ, kích thước framebuffer, poll sự kiện |
| `input.hpp` | `IInputProvider` | trạng thái phím/chuột theo từng frame |
| `input_map.hpp` | `InputMap` | **action**, không phải phím — xem bên dưới |
| `clock.hpp` | `IClock` | delta time, thời gian từ lúc chạy |
| `filesystem.hpp` | `IFileSystem` | đọc/ghi file, thời gian sửa (dùng cho hot reload) |
| `dynamic_library.hpp` | `DynamicLibrary` | nạp `.so`/`.dll` lúc chạy |

Cài đặt hiện tại dùng GLFW, nhưng game không thấy GLFW — nó chỉ thấy `IWindow`.

## Thứ tự trong vòng lặp

```cpp
clock->tick();
input->newFrame();      // chốt trạng thái frame trước
window->pollEvents();   // nạp sự kiện mới
```

Đúng thứ tự này, vì `newFrame()` phải chạy **trước** khi sự kiện mới tới thì "vừa nhấn" mới
phân biệt được với "đang giữ".

## `InputMap`: action, không phải phím

```cpp
if (map.axis("moveY") > 0.0f) ...     // không phải isKeyPressed(Key::W)
```

Cùng một đoạn code chạy được với bàn phím, cần analog hay d-pad mà không cần biết đó là cái
nào. Cả bảng ánh xạ round-trip qua JSON — đó là tất cả những gì một màn hình gán phím cần.

Đọc phím trực tiếp trong gameplay khiến mọi hỗ trợ gamepad và mọi bố cục bàn phím không phải
QWERTY trở thành một đợt sửa xuyên suốt codebase.

## Ghi chú Wayland

Trên phiên KDE Wayland, GLFW bị **ép chạy qua X11/XWayland** (xem log khởi động). Đây là chủ
ý cho tới khi swapchain Vulkan cho Wayland hoàn tất.

Xem [examples/window](../../examples/window/), [examples/input](../../examples/input/),
[examples/hotreload](../../examples/hotreload/).
