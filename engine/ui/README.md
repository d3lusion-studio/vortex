# vortex::ui

UI immediate-mode. Một header, ~255 dòng.

```cmake
target_link_libraries(mygame PRIVATE Vortex::ui)
```

## Immediate mode nghĩa là gì

**Không có cây widget nào được giữ giữa các frame.** Mỗi frame, game gọi lại hàm dựng UI từ
đầu:

```cpp
if (ui.button("Chơi", {x, y, w, h})) startGame();
```

Trạng thái duy nhất tồn tại qua frame là những gì game tự giữ.

Đổi lại là một tính chất đáng giá: **UI không bao giờ lệch pha với dữ liệu**. Thứ vẽ ra luôn
là dữ liệu của frame hiện tại, nên không có chuyện thanh máu hiển thị con số của ba frame
trước vì ai đó quên gọi hàm cập nhật.

Cái giá là UI được dựng lại mỗi frame — chấp nhận được với UI của game (menu, HUD, màn hình
cài đặt), nhưng không phải lựa chọn đúng cho giao diện dày đặc kiểu công cụ. Đó là lý do
inspector của [debug](../debug/) dùng ImGui thay vì module này.

## Vẽ bằng gì

Widget đi qua cùng `SpriteBatch` và `TextRenderer` mà mọi thứ khác dùng — không có đường
render riêng cho UI. Khung co giãn dùng **9-patch** (giữ nguyên bốn góc, chỉ kéo phần giữa);
phóng to sprite một cách ngây thơ sẽ làm méo góc.

## Animation cho UI

Không có "hệ thống animation UI" ở đây. Dùng thẳng `anim::Curve` trong [anim](../anim/) —
cùng đường cong dùng để pose bộ xương. Xem [examples/ui_anim](../../examples/ui_anim/).

Xem [examples/ui](../../examples/ui/), [examples/game_menu](../../examples/game_menu/).
