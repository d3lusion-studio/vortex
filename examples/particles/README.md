# particles

Hạt: một cột khói đều đặn, một đài phun chịu trọng lực, và một cụm nổ bắn về phía chuột.

```bash
cmake --build build/debug --target particles
./build/debug/examples/particles
```

Mọi emitter đều sống trong `ParticleWorld` của scene, nên vòng lặp `App` cập nhật và vẽ
chúng **mà không cần một dòng code per-frame nào ở đây**.

Đó là điểm thiết kế: hạt không phải thứ game phải nhớ tick mỗi frame. Đăng ký emitter vào
scene một lần, và vòng đời của nó do scene quản.

Ba emitter cũng là ba kiểu hành vi khác nhau:

- **khói** — phát liên tục, tốc độ ổn định, sống lâu
- **đài phun** — phát liên tục nhưng có gia tốc trọng lực
- **nổ** — phát một lần theo sự kiện, tại vị trí chuột

| Phím | Tác dụng |
|---|---|
| chuột | vị trí bắn cụm nổ |
| `Esc` | thoát |

| Biến môi trường | Ý nghĩa |
|---|---|
| `VORTEX_MAX_FRAMES` | chạy N frame rồi thoát |
