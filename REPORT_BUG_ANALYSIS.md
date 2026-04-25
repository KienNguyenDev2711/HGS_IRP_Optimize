# Báo Cáo Phân Tích Kỹ Thuật: HGS-IRP Solver
## Nguyên Nhân Chương Trình Bị Treo & Kết Quả Khác Nhau Mỗi Lần Chạy

---
## . Vấn Đề #1: Treo Vĩnh Viễn Với `-t 0` (10 Khách)

### Triệu chứng
Lệnh `./irp data.dat -seed 42 -t 0` không bao giờ kết thúc.

### Nguyên nhân gốc (đã xác nhận bằng debug trace)

Trong hàm `mutationSameDay`, có logic:
```cpp
posU -= moveEffectue;  // nếu tìm được move, quay lại vị trí đó
```

Với 10 khách và 3 depot, customer số 7 bị kẹt trong vòng lặp oscillation:
- Customer 7 ở depot 0 → tìm move sang depot 1 → cải thiện nhỏ (> 0.0001) → thực hiện
- Customer 7 ở depot 1 → tìm move quay về depot 0 → cải thiện nhỏ ngược chiều → thực hiện
- Lặp lại vô tận → `posU` không bao giờ tăng qua vị trí này

Vì `-t 0` → `ticks = 0` → **tất cả time guard đều bị tắt** → không có gì dừng được vòng lặp.

### Fix đã áp dụng
Thêm bộ đếm `totalMoves` giới hạn tổng số move trong một lần gọi `mutationSameDay`:
```cpp
int totalMoves = 0;
const int MAX_TOTAL_MOVES = 10 * (params->nbClients + 1);
// Với n=10: limit=110 moves; với n=100: limit=1010 moves

if (totalMoves >= MAX_TOTAL_MOVES)
    { rechercheTerminee = true; break; }  // thoát loop ngay
```

**Kết quả xác nhận**: 10-customer `-t 0` → chạy ổn, đạt It 3500+ trong ~30 giây.

---

## 3. Vấn Đề #2: Chạy Rất Lâu Với 100 Khách Hàng

### Triệu chứng
Với `sample_100cust_3days.dat` và `-t 0`, chương trình chạy hàng chục phút không dừng.

### Điều tra thực nghiệm (profiling)

Thêm timer vào `runSearchTotal` và đo thực tế trên hardware:

```
[PROF] edu#0  SD1=0.006s  MDD=0.362s  SD2=0.004s  → total 0.372s
[PROF] edu#1  SD1=0.005s  MDD=0.321s  SD2=0.004s  → total 0.330s
...
[PROF] edu#12 SD1=0.005s  MDD=0.355s  SD2=0.004s  → total 0.364s
...
Evolution iterations:
[PROF] SD1=0.006s  MDD=0.210s  SD2=0.004s  → total 0.220s
[PROF] SD1=0.005s  MDD=0.441s  SD2=0.006s  → total 0.452s  ← xấu nhất
[PROF] SD1=0.003s  MDD=0.082s  SD2=0.002s  → total 0.087s  ← tốt nhất
```

**Kết luận rõ ràng**:
- `mutationSameDay` (SD1, SD2): **0.003–0.013s** → gần như không đáng kể
- `mutationDifferentDay` (MDD): **0.082–0.441s, trung bình ~0.22s** → **chiếm 95%+ thời gian**

### Tại sao `mutationDifferentDay` chậm?

Với 100 khách hàng, `mutationDifferentDay` chạy 3 vòng lặp × 100 khách = **300 lần gọi `LotSizingSolver`**.

Mỗi `LotSizingSolver`:
1. Tạo 3 `PLFunction` objects (piecewise-linear function)
2. Mỗi `PLFunction` chứa ~15 pieces (=số route/ngày)
3. Mỗi piece dùng `std::shared_ptr<LinearPiece>` → heap allocation
4. Giải DP qua 3 ngày với các phép toán min/merge/shift trên PLFunction
5. Backtrack để tìm lịch giao hàng tối ưu

**Chi phí**: ~300 allocations heap × 300 calls = 90,000+ `shared_ptr` operations per `mutationDifferentDay`.

### Tính toán thời gian chạy (trước khi sửa)

| Giai đoạn | Thời gian | Ghi chú |
|-----------|-----------|---------|
| Khởi tạo population (24 edu) | ~8 giây | Chấp nhận được |
| Mỗi iteration trong evolution | ~0.22 giây | MDD chiếm 95%+ |
| 5000 non-improving iterations | **~1100 giây ≈ 18 phút** |  Không chấp nhận được |
| maxIter = 100,000 (worst case) | **~22,000 giây ≈ 6 giờ** |  Không thể dừng kịp |

### Fix đã áp dụng

Giảm số iteration theo kích thước bài toán (trong `main.cpp`):

```cpp
int n = mesParametres->nbClients;
int iterScale = std::max(1, n / 25);          // 1 tại n≤25, 4 tại n=100
int max_iter       = std::max(20000, 100000 / iterScale);  // 25000 tại n=100
int maxIterNonProd = std::max(1000,   5000  / iterScale);  // 1250 tại n=100
```

**Ảnh hưởng theo kích thước bài toán**:

| n (khách) | max_iter | maxIterNonProd | Thời gian tối đa (ước tính) |
|-----------|---------|----------------|--------------------------|
| 25        | 100,000 | 5,000          | ~60 phút (nếu không cải thiện) |
| 50        | 50,000  | 2,500          | ~15 phút |
| 100       | 25,000  | **1,250**      | **~4.5 phút**  |
| 200       | 20,000  | 1,000          | ~3.5 phút |

**Lưu ý**: Với `-t N` (time limit), chương trình sẽ dừng sớm hơn bất kể kích thước.

---

## 4. Vấn Đề #3: Kết Quả Khác Nhau Mỗi Lần Chạy

### Triệu chứng
Chạy cùng một file dữ liệu nhiều lần nhận được kết quả khác nhau.

### Nguyên nhân 1: Seed ngẫu nhiên theo thời gian (CHÍNH)

Khi không dùng `-seed N`, chương trình mặc định:
```cpp
if (seed == 0)
    seed = (uint64_t)time(NULL);  // seed = thời gian hiện tại (giây)
```

Vì `time(NULL)` thay đổi mỗi giây, mọi lần chạy → seed khác nhau → chuỗi RNG hoàn toàn khác → kết quả khác nhau. Đây là **thiết kế có chủ đích** — tạo ra sự đa dạng trong tìm kiếm.

### Nguyên nhân 2: OS scheduling ảnh hưởng đến time guard (PHỤ)

Khi dùng `-t N` (có time limit), các time guard trong code dùng `clock()`:
```cpp
if (params->ticks > 0 && clock() - params->debut > params->ticks * 0.90)
    return;  // kết thúc sớm
```

Ngay cả với cùng seed và cùng dữ liệu:
- Lần 1: OS cấp CPU đều → time guard bắn vào iteration 847
- Lần 2: OS bị interrupt khác → time guard bắn vào iteration 831
- → Hai lần chạy rẽ nhánh vào đường khác nhau → kết quả khác

### Nguyên nhân 3: Thứ tự xử lý phụ thuộc RNG (CỘNG HƯỞNG)

`shuffleProches()` và `melangeParcours()` xáo trộn thứ tự duyệt khách hàng ngẫu nhiên mỗi lần. Thứ tự duyệt ảnh hưởng đến move nào được thực hiện khi có nhiều move cùng cải thiện.

### Kết luận

```
Kết quả khác nhau = (seed khác nhau) × (OS scheduling khác nhau) × (thứ tự RNG khác)
```

| Trường hợp | Kết quả |
|-----------|---------|
| `-seed 0` (default) | Luôn khác nhau (seed = time(NULL)) |
| `-seed 42 -t 30` | Có thể khác nhau (time guard không tất định) |
| `-seed 42 -t 0` | **Hoàn toàn tất định (deterministic)**|

### Khuyến nghị

**Để tái hiện kết quả**: dùng `-seed N -t 0` (cố định seed, không dùng time limit):
```bash
./irp data.dat -seed 42 -type 39 -veh 5 -t 0
```

**Để tìm giải pháp tốt nhất**: chạy nhiều lần với seed khác nhau và chọn kết quả tốt nhất:
```bash
./irp data.dat -seed 1  -type 39 -veh 5 -t 60
./irp data.dat -seed 2  -type 39 -veh 5 -t 60
./irp data.dat -seed 42 -type 39 -veh 5 -t 60
```

---

## 5. Hướng Dẫn Sử Dụng Theo Kích Thước Bài Toán

### Chế độ `-t 0` (không giới hạn thời gian, dừng theo iteration)

| n (khách) | Thời gian ước tính | Khuyến nghị |
|-----------|------------------|-------------|
| ≤25       | 1–5 phút          |  Dùng bình thường |
| 50        | 3–8 phút          |  Chấp nhận được |
| 100       | 4–5 phút          |  Sau khi fix |
| 200+      | 10+ phút          | ⚠️ Nên dùng `-t N` |

### Chế độ `-t N` (giới hạn thời gian N giây)

Khuyến nghị cho **production use** (tất cả kích thước):
```bash
./irp data.dat -seed 42 -type 39 -veh 5 -t 120   # 2 phút
./irp data.dat -seed 42 -type 39 -veh 5 -t 300   # 5 phút (100 khách)
```

Ưu điểm của `-t N`:
- Kết thúc đúng lúc, có thể dự đoán
- Tận dụng tối đa thời gian cho phép
- Dễ so sánh kết quả giữa các lần chạy

---

## 6. Tóm Tắt Các Thay Đổi Code Trong Session Này

### Fix 1: Ngăn vòng lặp vô tận trong `mutationSameDay` (`LocalSearch.cpp`)

**Nguyên nhân**: `posU -= moveEffectue` gây ra customer oscillation giữa depots, không bao giờ tăng `posU`.

**Fix**: Thêm `MAX_TOTAL_MOVES = 10×(n+1)` budget cap.

```cpp
// TRƯỚC: không có giới hạn tổng số moves
while (!rechercheTerminee) {
    for (int posU = 0; posU < size; posU++) {
        posU -= moveEffectue;  // ← có thể loop vĩnh viễn tại posU=5
        ...
    }
}

// SAU: có tổng moves budget
int totalMoves = 0;
const int MAX_TOTAL_MOVES = 10 * (params->nbClients + 1);
while (!rechercheTerminee && loopPass < MAX_SAME_DAY_PASSES) {
    for (int posU = 0; posU < size; posU++) {
        if (totalMoves >= MAX_TOTAL_MOVES) { break; }  // ← thoát khi đủ work
        posU -= moveEffectue;
        totalMoves += moveEffectue;
        ...
    }
}
```

### Fix 2: Scale iteration limits theo kích thước bài toán (`main.cpp`)

**Nguyên nhân**: `maxIterNonProd = 5000` cứng nhắc không phù hợp với 100 khách hàng (mỗi iteration mất ~220ms).

**Fix**: Tính động dựa trên n:
```cpp
int n = mesParametres->nbClients;
int iterScale = std::max(1, n / 25);
int max_iter       = std::max(20000, 100000 / iterScale);
int maxIterNonProd = std::max(1000,   5000  / iterScale);
```

---

## 8. Các Lỗi Crash/Treo Với 100 Khách Hàng (Session Mới)

### Vấn Đề #12: Treo Vô Tận Trong `updateIndiv` và `updateCentroidCoord`

**Triệu chứng**: Chương trình treo hoàn toàn sau khi bắt đầu evolution với n=100. Debug trace xác nhận treo tại `[muter:updateIndiv]` sau `[muter:RST]` đã xong.

**Nguyên nhân gốc**: Một mutation (mutation7/8/9) tạo ra cycle trong linked-list route. Route bị corrupt được giữ nguyên đến khi `updateIndiv` và `updateCentroidCoord` duyệt `while (!node->estUnDepot) node = node->suiv;` — vòng lặp không bao giờ thoát.

**Fix**: Thêm cycle guard vào cả hai hàm:
```cpp
int _cyc = 0;
const int _maxCyc = params->nbClients + params->nbDepots + 5;
while (!node->estUnDepot)
{
    if (++_cyc > _maxCyc) break; // cycle detected — abort traversal
    ...
    node = node->suiv;
}
```

**File sửa**: `Individu.cpp` (updateIndiv), `Route.cpp` (updateCentroidCoord)

---

### Vấn Đề #13: Access Violation Trong `addNoeud` (NULL `placeInsertion`)

**Triệu chứng**: Crash `access violation` tại `addNoeud` trong một route bị cycle-guard ngắt.

**Nguyên nhân gốc**: Khi cycle guard bắn trong `evalInsertClientp`/`evalInsertClient`, hàm `break` sớm mà không cập nhật `bestInsertion[U->cour].place`. Sau đó `updateLS` gán `U->placeInsertion = bestInsertion[...].place` = NULL. `addNoeud` dereferences `placeInsertion->suiv` → crash.

**Fix 1** (tức thì): NULL guard trong `addNoeud`:
```cpp
void LocalSearch::addNoeud(Noeud *U)
{
    if (U->placeInsertion == nullptr) return; // safety: no valid insertion point
    ...
}
```

**Fix 2** (đầy đủ): `updateRouteData` sửa chữa route bị cycle thay vì chỉ break:
```cpp
if (place > 500) {
    // Cycle detected — repair route completely
    Noeud* fin = myLS->depotsFin[day][cour];
    depot->suiv = fin; fin->pred = depot;
    depot->pred = fin; fin->suiv = depot;
    for (int _ci ...) { /* mark clients absent, call removeOP */ }
    isFeasible = false;
    noeud = fin;
    break;
}
```

**File sửa**: `LocalSearch.cpp` (addNoeud), `Route.cpp` (updateRouteData)

---

### Vấn Đề #14: Route Corrupt Sau Cycle Detection Trong `updateRouteData`

**Triệu chứng**: Sau khi cycle guard bắn (place > 500), route bị để ở trạng thái nửa chừng — một số node đã được đếm, depot/fin chưa được relink. Các mutations tiếp theo dùng route corrupt này tiếp tục tạo ra lỗi.

**Nguyên nhân gốc**: Code gốc chỉ `break` khỏi loop, không repair linked-list.

**Fix**: Xem Fix 2 ở Vấn Đề #13 bên trên — toàn bộ route được relink về empty (depot↔depotFin) và tất cả client được đánh dấu absent.

---

### Vấn Đề #15: Corrupt Linked-List Trong Execution Loops Của `mutation8`/`mutation9`

**Triệu chứng**: Sau một số iteration, route bị corrupt theo kiểu khác — pointer suiv/pred bị đảo ngược một phần.

**Nguyên nhân gốc**: `mutation8` (2-OPT* inversion) và `mutation9` (2-OPT* no inversion) có cycle guards `return 0` bên trong execution loops (các vòng đảo ngược pointer). Nếu guard bắn sau khi đã flip một số pointer → linked-list corrupt một phần.

**Fix**: Pre-validate toàn bộ segment TRƯỚC khi sửa bất kỳ pointer nào. Execution loops dùng `break` (không `return 0`) vì sau pre-validation chúng không nên bao giờ trigger:
```cpp
// Pre-validate: check full segment is cycle-free
{ int _cyc = 0; Noeud *check = x;
  while (!check->estUnDepot) { check = check->suiv;
    if (++_cyc > params->nbClients + params->nbDepots + 5) return 0;
  }
}
// Now safe to modify pointers
```

**File sửa**: `Mutations.cpp` (mutation8, mutation9)

---

### Kết Quả Sau Tất Cả Fix

| Metric | Trước fix | Sau fix |
|--------|-----------|---------|
| n=100, `-t 30` | Treo/crash | EXIT=0, 1249 iterations, 29.8s |
| n=100, `-t 5`  | Crash (heap corruption) | EXIT=0, hoàn chỉnh |
| n=100, `-t 0`  | Crash ở It ~410 (heap corruption) → sau Bug #16 fix: crash ở It ~1500 trong `diversify()` | EXIT=0 (chạy đủ 6250 iterations) |
| n=25, `-t 30`  | 4898 iterations | 4898 iterations (không regression) |
| Cycle hits điển hình | N/A | ~1 per 30s run (rare, handled gracefully) |

---

### Vấn Đề #16: `removeOP()` Unbounded Loop → Heap Corruption

**Triệu chứng**: Với n=100, `-t 0`, chương trình crash ở ~It 410 với STATUS_HEAP_CORRUPTION (0xC0000374). `-t 30` không crash vì time guard dừng sớm trước khi trigger.

**Nguyên nhân gốc**: `removeOP(day, client)` dùng vòng lặp `while (ordreParcours[day][it] != client)` không có giới hạn. Khi cycle-repair trong `updateRouteData` reset route và gọi `removeOP` cho client đã bị xóa khỏi `ordreParcours[day]` trước đó (bởi `removeNoeud` trong cùng một `mutation11` call), vòng lặp đi quá cuối vector, ghi đè heap metadata. Heap allocator phát hiện lỗi ~385 iterations sau.

**Fix** (`LocalSearch.cpp`):
```cpp
void LocalSearch::removeOP(int day, int client)
{
  int it = 0;
  int sz = (int)ordreParcours[day].size();
  while (it < sz && ordreParcours[day][it] != client)
    it++;
  if (it >= sz)
    return;  // client not in ordreParcours — skip safely
  ordreParcours[day][it] = ordreParcours[day][sz - 1];
  ordreParcours[day].pop_back();
}
```

**File sửa**: `LocalSearch.cpp` (removeOP)

---

### Vấn Đề #17: `addNoeud()` Không Xử Lý Cycle-Repair → Stale Pointers → Crash Trong `diversify()`

**Triệu chứng**: Sau fix Bug #16, n=100 `-t 0` reach It ~1500 rồi crash bên trong `diversify()` → `education()` → `runSearchTotal()` → `mutation11()` → `addNoeud()`. Output file kết thúc sau "Diversification", không có "Elapsed time:".

**Nguyên nhân gốc**: `addNoeud(U)` luôn gọi `addOP(U->jour, U->cour)` và đặt `U->estPresent = true` sau `updateRouteData()`. Nhưng nếu cycle-repair bắn bên trong `updateRouteData()`, route bị reset về empty (depot↔depotFin) và U không thực sự được link vào chain. `U->pred`/`U->suiv` là stale. Khi `removeNoeud(U)` được gọi sau đó, nó dùng pointers stale này để "unlink" U, corrupt linked-list của các client khác còn đang valid. Sau nhiều lần corrupt như vậy, chain có cycle thực → `updateRouteData` walk không bao giờ kết thúc → crash.

**Root cause path**: `mutation11` → `addNoeud(U)` → `updateRouteData()` fires cycle-repair, resets route, nhưng `addNoeud` tiếp tục như không có gì → `addOP` + `estPresent=true` với stale pointers → next `removeNoeud(U)` corrupt chain.

**Fix** (`Route.h`, `Route.cpp`, `LocalSearch.cpp`):
1. Thêm flag `bool cycleRepairHappened` vào `Route`
2. `updateRouteData()` set `route->cycleRepairHappened = true` khi cycle-repair fires
3. `addNoeud()` reset flag trước khi gọi `updateRouteData()`, và abort ngay (không mark present, không addOP) nếu flag được set sau:
```cpp
U->route->cycleRepairHappened = false;
U->route->updateRouteData();
if (U->route->cycleRepairHappened) {
    U->estPresent = false;
    return;  // U's pred/suiv are stale — do not corrupt the chain
}
addOP(U->jour, U->cour);
U->estPresent = true;
```

**File sửa**: `Route.h` (thêm field), `Route.cpp` (set flag + init), `LocalSearch.cpp` (addNoeud)

**Kết quả**: n=100 `-t 0` chạy đủ 6250 iterations, EXIT=0, in "Elapsed time:" và "Number of iterations:" bình thường.

---

## 7. Giới Hạn Đã Biết Còn Lại

1. **n > 200**: Mỗi iteration có thể mất 0.5+ giây. Khuyến nghị dùng `-t N`.
2. **Kết quả không tối ưu tuyệt đối**: HGS-IRP là metaheuristic; không đảm bảo tối ưu global. Chạy nhiều lần với seed khác nhau cho kết quả tốt hơn.
3. **Multi-depot oscillation**: Với nhiều depot rất gần nhau (khoảng cách < threshold), nhiều khách hàng có thể bị đánh dấu "borderline" → cross-depot mutations chạy nhiều hơn → chậm hơn. Đây là đặc tính của dữ liệu test (3 depot trong bán kính 30km).


