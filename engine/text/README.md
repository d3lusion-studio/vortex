# vortex::text

Font và render chữ.

```cmake
target_link_libraries(mygame PRIVATE Vortex::text)
```

| Header | Nội dung |
|---|---|
| `font.hpp` | `Font` — nạp TTF, atlas glyph, đo chữ, `defaultPath()` |
| `text_renderer.hpp` | `TextRenderer` — dựng glyph thành quad, đi qua `SpriteBatch` |

Chữ không có đường render riêng: `TextRenderer` sinh ra quad và đưa vào cùng
[`SpriteBatch`](../renderer/) mà sprite dùng, nên chữ và hình gộp batch cùng nhau.

## UTF-8 là chuyện nghiêm túc ở đây

Mọi chuỗi trong engine đi qua một bộ giải mã UTF-8, và nó có bài kiểm tra riêng trong CI
([examples/text](../../examples/text/), target `text_check`).

Lý do: **kiểu lỗi này vô hình**. Một decoder làm rơi một byte không crash. Nó render ra
`"Nng tri"` thay vì `"Nông trại"`, và không ai phát hiện cho tới khi có người viết bằng ngôn
ngữ của chính họ. Đó đúng là con bug mà đoạn code hiện tại thay thế.

## Tìm font

```cpp
const std::string path = text::Font::defaultPath(*fs);
```

Quét các thư mục font hệ thống. Đặt `VORTEX_FONT_PATH` để chỉ định thẳng một file TTF và bỏ
qua bước dò — hữu ích trong CI và container, nơi có thể không có font nào.

Nếu không tìm được font nào, hàm trả về chuỗi rỗng; gọi phía trên phải xử lý trường hợp đó
(các example đều báo lỗi rồi thoát).

Xem [examples/ui](../../examples/ui/), [examples/text](../../examples/text/).
