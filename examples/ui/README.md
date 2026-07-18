# ui

Text và UI immediate-mode.

```bash
cmake --build build/debug --target ui
./build/debug/examples/ui
```

Ghép ba thứ lại: `text::Font` nạp một font TTF, `text::TextRenderer` dựng glyph thành quad,
và [engine/ui/](../../engine/ui/) vẽ widget lên trên cùng một `SpriteBatch`.

UI ở đây là immediate-mode: không có cây widget nào được giữ giữa các frame. Mỗi frame game
gọi lại hàm dựng UI, và trạng thái duy nhất tồn tại qua frame là những gì game tự giữ. Đổi
lại, không bao giờ có chuyện UI lệch pha với dữ liệu — thứ vẽ ra luôn là dữ liệu của frame
hiện tại.

Font mặc định được tìm qua `text::Font::defaultPath()`, quét các thư mục font hệ thống. Nếu
máy không có font nào, example báo lỗi và thoát.

| Biến môi trường | Ý nghĩa |
|---|---|
| `VORTEX_FONT_PATH` | chỉ định file TTF cụ thể, bỏ qua bước dò font hệ thống |
| `VORTEX_MAX_FRAMES` | chạy N frame rồi thoát |

Nhấn `Esc` để thoát.
