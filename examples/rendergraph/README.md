# rendergraph

Render graph: khai báo các pass và tài nguyên chúng đọc/ghi, rồi để graph tự lo thứ tự và
chuyển trạng thái tài nguyên.

```bash
cmake --build build/debug --target rendergraph
./build/debug/examples/rendergraph
```

Ở đây có một color pass vẽ cảnh vào một offscreen target, rồi một post pass đọc target đó và
ghi ra swapchain.

Cái mà graph loại bỏ là **barrier viết tay**. Khi một pass ghi vào một texture rồi pass sau
đọc nó, GPU cần một image barrier để chuyển layout và đảm bảo thứ tự. Quên barrier đó thì
kết quả tuỳ driver: chạy đúng trên máy này, ra rác trên máy kia. Graph biết ai đọc ai ghi
nên nó tự sinh barrier.

Bấm `P` để tắt post pass lúc chạy — graph dựng lại với một pass ít hơn và nối color pass
thẳng vào swapchain, không cần code đặc biệt cho trường hợp đó.

| Phím | Tác dụng |
|---|---|
| `P` | bật/tắt post pass (in trạng thái ra console) |
| `Esc` | thoát |

| Biến môi trường | Ý nghĩa |
|---|---|
| `VORTEX_RG_NO_POST` | khởi động với post pass đã tắt sẵn |
| `VORTEX_MAX_FRAMES` | chạy N frame rồi thoát |
