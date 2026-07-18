# diagnostics

Chẩn đoán, headless: đo một lần, đọc ở mọi nơi.

```bash
cmake --build build/debug --target diagnostics
./build/debug/examples/diagnostics
```

Ba việc example này chứng minh:

- **Chỉ số tuỳ biến** — `diag::add("physics.bodies", n)` ngay tại chỗ đo là toàn bộ phần tích
  hợp. Overlay, bản dump ra log, và bài test này đều đọc **cùng một chuỗi số liệu**.
- **Bật/tắt lúc chạy** — một chuỗi bị tắt thì không ghi gì và không tốn gì, và khi bật lại
  thì tiếp tục từ chỗ nó dừng.
- **Consumer dạng log** — `logEvery()` in mọi chuỗi đang bật ra qua log của engine.

Điểm đầu tiên là điểm đáng giá nhất. Một dòng tại chỗ đo là đủ; không cần khai báo trước,
không cần nối dây tới overlay. Nếu việc thêm một chỉ số tốn nhiều hơn thế, người ta sẽ không
thêm, và hệ thống chẩn đoán sẽ chỉ đo đúng những gì tác giả của nó nghĩ ra từ đầu.

Không cửa sổ, không GPU — chạy được trong CI.

Bản có giao diện: [inspector](../inspector/).
