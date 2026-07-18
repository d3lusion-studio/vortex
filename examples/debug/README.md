# debug

Debug draw: vẽ hình, lưới và nhãn ngay trong world space, phân theo **category** bật/tắt
được lúc chạy.

```bash
cmake --build build/debug --target debug_draw
./build/debug/examples/debug_draw
```

> Lưu ý: thư mục là `debug/` nhưng **target tên là `debug_draw`**.

`debug::DebugDraw` là thứ để nhìn thấy những gì không có hình hài — một bounding box, một
tia raycast, hướng của một vector. Điểm thiết kế đáng chú ý là category: mỗi lệnh vẽ thuộc
về một nhóm, và tắt nhóm đó thì lệnh vẽ **không tốn gì cả**, nên code debug có thể nằm lại
vĩnh viễn trong game thay vì bị comment ra rồi comment vào.

| Phím | Tác dụng |
|---|---|
| `1` | bật/tắt category lưới |
| `2` | bật/tắt category hình khối |
| `3` | bật/tắt category nhãn chữ |
| `Esc` | thoát |

| Biến môi trường | Ý nghĩa |
|---|---|
| `VORTEX_MAX_FRAMES` | chạy N frame rồi thoát |

Bản 3D của cùng ý tưởng này nằm ở [gizmos3d](../gizmos3d/).
