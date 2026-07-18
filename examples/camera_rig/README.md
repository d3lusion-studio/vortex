# camera_rig

Camera controller, headless: mọi rig đều là toán thuần, nên mọi lời hứa của nó đều kiểm được
mà không cần cửa sổ.

```bash
cmake --build build/debug --target camera_rig
./build/debug/examples/camera_rig
```

| Rig | Bất biến được kiểm |
|---|---|
| **Orbit** | xoay/dolly/pan quanh một mục tiêu; bán kính là bất biến |
| **Fly** | WASD + mouse-look; đi ngang vẫn giữ thăng bằng, "lên" nghĩa là lên theo world |
| **Pan 2D** | kéo thì thế giới dính chặt vào con trỏ; lăn chuột zoom quanh con trỏ |
| **Follow2D** | làm mượt độc lập frame rate + deadzone kiểu platformer |
| **Shake** | dựa trên "trauma": tắt dần về **đúng bằng 0**, offset mượt chứ không phải nhiễu trắng |
| **Zoom** | zoom phép chiếu mang cùng ý nghĩa ở cả perspective lẫn orthographic |

Những bất biến này là lý do file test tồn tại. "Bán kính là bất biến" nghĩa là xoay quanh mục
tiêu không được lén thay đổi khoảng cách — sai số dồn qua vài nghìn frame sẽ khiến camera
trôi dần vào trong hoặc ra xa mà không ai chỉ ra được lúc nào nó bắt đầu.

Tương tự với shake: tắt dần về *gần* 0 nghĩa là camera rung vĩnh viễn ở mức không nhìn thấy
được nhưng vẫn đủ làm hỏng ảnh chụp và test so ảnh.

Thoát mã khác 0 ở lời hứa đầu tiên bị phá vỡ.
