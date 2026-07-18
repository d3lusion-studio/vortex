# vortex::app

Vòng lặp game. Module ở tầng cao nhất — nó nối tất cả những module còn lại lại với nhau.

```cmake
target_link_libraries(mygame PRIVATE Vortex::app)
```

| Header | Nội dung |
|---|---|
| `app.hpp` | `App`, `AppConfig` — vòng lặp và cấu hình |
| `plugin.hpp` | plugin và nhóm plugin |
| `scene_manager.hpp` | nhiều scene, chuyển scene có xếp hàng |
| `model_loader.hpp` | `loadModel` — glTF thành entity `Transform3D` + `MeshComp` |

## Ba dòng là đủ

```cpp
app::AppConfig config;
app::App app(config);
return app.run();
```

Bấy nhiêu cho ta: cửa sổ, device Vulkan + swapchain, sprite batcher, job system, asset
manager, scene ECS kèm camera 2D, input map, particle world, audio và physics tạo lười — và
một vòng lặp **fixed timestep cho mô phỏng, variable timestep cho render**, không rơi vào
vòng xoáy chết khi một frame chạy quá lâu.

## Đường 2D và đường 3D

```cpp
config.render3D = true;   // App dựng MeshRenderer, depth buffer, mặt trời + shadow pass
```

Sau đó game 3D spawn entity **y hệt cách game 2D spawn `SpriteComp`**:

```cpp
reg.emplace<ecs::Transform3D>(e, {.position = p, .scale = s});
reg.emplace<ecs::MeshComp>(e, {.mesh = ballMesh, .color = c, .roughness = 0.35f});
```

Không chạm tay vào `MeshRenderer`, render graph hay shadow pass. So sánh với
[examples/mesh3d](../../examples/mesh3d/), nơi tất cả những thứ đó phải viết tay.

## Plugin nối thêm, không ghi đè

`onUpdate()` **nối thêm** callback chứ không thay thế cái đã đăng ký. Đây từng là lỗi ngược
lại, và triệu chứng duy nhất là hai hệ thống lặng lẽ ngừng chạy: không crash, không cảnh báo.

## Mô phỏng trên luồng riêng

```cpp
config.threadedSimulation = true;
```

Mô phỏng chạy trước renderer một frame; thời gian mỗi frame trở thành `max(sim, render)` thay
vì `sim + render`. Điều khoản duy nhất nó đòi ở game: **không đụng vào GPU từ một hook
update**.

Khi đo tốc độ, dùng `presentMode = Immediate` — nếu không V-Sync sẽ chặn cả hai chế độ ở cùng
một con số.

## Cấu hình lưu xuống đĩa

Đọc file Settings **trước khi dựng `App`**, đổ vào `AppConfig`, rồi mới dựng. Kích thước cửa
sổ được quyết định lúc `App` được dựng — đọc settings sau đó là đã muộn.

Xem [examples/app_empty](../../examples/app_empty/), [app_plugin](../../examples/app_plugin/),
[app_settings](../../examples/app_settings/), [threaded](../../examples/threaded/),
[scene_viewer](../../examples/scene_viewer/), và [games/roller](../../games/roller/).
