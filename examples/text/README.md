# text

Headless: bộ giải mã UTF-8 mà mọi chuỗi trong engine đều đi qua.

```bash
cmake --build build/debug --target text_check
./build/debug/examples/text_check
```

> Lưu ý: thư mục là `text/` nhưng **target tên là `text_check`**.

Không cửa sổ, không GPU, nên nó chạy được trong CI như một bài kiểm tra hồi quy.

Chỗ này đáng được ghim lại vì kiểu lỗi của nó vô hình: một decoder làm rơi mất một byte
**không hề crash**. Nó render ra `"Nng tri"` thay vì `"Nông trại"`, và không ai phát hiện ra
cho tới khi có người viết bằng ngôn ngữ của chính họ.

Đó đúng là con bug mà đoạn code này thay thế.

Process thoát với mã khác 0 ngay ở trường hợp sai đầu tiên.
