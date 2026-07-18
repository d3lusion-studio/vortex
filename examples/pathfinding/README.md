# pathfinding

Headless: A* trên lưới ô — tường, góc cua, ngõ cụt, và ngân sách tìm kiếm.

```bash
cmake --build build/debug --target pathfinding
./build/debug/examples/pathfinding
```

Không cửa sổ, không GPU, nên nó kiêm luôn bài kiểm tra CI cho bộ tìm đường.

Pathfinding đúng là loại code **trông có vẻ đúng mà sai một cách tinh vi**:

- một heuristic không admissible vẫn trả về đường đi — chỉ là đường tệ hơn
- một đường chéo cắt góc vẫn trả về đường đi — chỉ là đường **xuyên qua tường**

Cả hai lỗi đều không crash, không cảnh báo, và trong đa số bản đồ test thì kết quả vẫn "trông
hợp lý". Chúng chỉ lộ ra khi người chơi thấy con NPC đi xuyên vách đá.

Mỗi trường hợp trong file này ghim lại đúng một trong những lỗi đó.

Ngân sách tìm kiếm cũng được kiểm: A* phải **bỏ cuộc một cách có kiểm soát** khi vượt giới
hạn số node, chứ không được treo vòng lặp game khi ai đó yêu cầu đường tới một ô bị tường bao
kín.
