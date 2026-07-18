# vortex::asset

Nạp, cache, hot reload, và ghi asset.

```cmake
target_link_libraries(mygame PRIVATE Vortex::asset)
```

| Header | Nội dung |
|---|---|
| `asset_manager.hpp` | `AssetManager` — nạp, cache, hot reload, barrier tiến độ |
| `asset_loader.hpp` | interface loader — **điểm mở rộng cho định dạng riêng** |
| `asset_types.hpp` | handle và kiểu asset |
| `image.hpp`, `texture_asset.hpp`, `cooked_texture.hpp` | PNG và texture |
| `gltf.hpp` | model glTF: mesh, bộ xương, clip |
| `tiled.hpp` | bản đồ `.tmj` xuất từ Tiled |

## Asset được tham chiếu bằng handle

Không phải con trỏ. Khi hot reload thay nội dung một asset, **handle mà game đang giữ vẫn
nguyên** — chỉ dữ liệu phía sau handle được thay. Đó là điều kiện để hot reload không đòi hỏi
game phải đăng ký lại thứ gì.

## URI: `scheme://` + `#subasset`

```
file://textures/hero.png
embedded://default_white
http://cdn.example.com/pack.bin
palette.pal#warm            # một file, nhiều asset, mỗi cái cache riêng
```

Cả ba scheme đi qua **một hàm `fetchBytes()` duy nhất**, nên loader không cần biết dữ liệu
đến từ đĩa, từ binary hay từ mạng.

Khi viết loader riêng, `canLoad` **phải xét cả `basePath`**, không chỉ phần đuôi file.

## Nạp bất đồng bộ

Việc nạp chạy trên luồng IO; một barrier báo tiến độ. Đây là thứ để dựng màn hình loading
**phản ánh đúng số asset đã xong**, thay vì một thanh tiến trình giả chạy theo đồng hồ — và
nếu một asset lỗi, màn hình loading biết điều đó. Xem
[examples/game_menu](../../examples/game_menu/).

## Định dạng riêng của game

Engine không cần biết trước. Đăng ký loader từ phía game và định dạng đó dùng được như mọi
asset khác — kể cả việc **ghi**, vì lưu asset cũng đi qua loader.

Xem [examples/assets](../../examples/assets/) (hot reload) và
[examples/assets_demo](../../examples/assets_demo/) (toàn bộ khả năng).
