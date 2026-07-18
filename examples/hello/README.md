# hello

Khởi tạo tối thiểu: log, kiểu toán học, và `Handle`. Không mở cửa sổ, không đụng GPU.

```bash
cmake --build build/debug --target hello
./build/debug/examples/hello
```

Đây là example duy nhất không cần driver Vulkan, nên nó là thứ đầu tiên nên chạy sau khi
build xong — nếu `hello` chạy được thì phần core của engine đã link đúng.

Nó in ra ba thứ:

- phiên bản engine, qua `VORTEX_VERSION_STRING`
- `length({3,4}) = 5.0`, kiểm tra thư viện toán trong [engine/core/](../../engine/core/)
- một `Handle<T>` rỗng là **không hợp lệ**, còn `Handle{index=7, generation=1}` thì hợp lệ

Điểm cuối đáng chú ý hơn vẻ ngoài của nó. Handle trong Vortex là chỉ số + thế hệ
(generation), không phải con trỏ: khi một tài nguyên bị huỷ, thế hệ tăng lên, và mọi handle
cũ trỏ tới ô đó lập tức trở thành không hợp lệ thay vì thành con trỏ treo. Cả asset,
texture và entity đều được định danh theo cách này.
