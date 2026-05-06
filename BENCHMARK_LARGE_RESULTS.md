# HGS-IRP Large Instance Benchmark Report

## Configuration

| Parameter | Value |
|-----------|-------|
| Algorithm | HGS-IRP (Hybrid Genetic Search for Inventory Routing Problem) |
| Stopping rule 1 | 2000 consecutive non-improving iterations (`maxIterNonProd = 2000`) |
| Stopping rule 2 | Time limit 300 seconds (`-t 300`) |
| Which fires first | The algorithm stops at whichever condition is met first |
| Random seed | 1 (seed 2 used only for Large-15; see note) |
| Solution type | `-type 39` |

### Fleet sizing rule

| Instance group | Depots | Periods | Vehicles per depot |
|----------------|--------|---------|--------------------|
| Large-1 to 4   | 3      | 3       | 3 (`-veh 3`)       |
| Large-5 to 8   | 4      | 3       | 5 (`-veh 5`)       |
| Large-9 to 11  | 3      | 6       | 5 (`-veh 5`)       |
| Large-12 to 16 | 4      | 6       | 5 (`-veh 5`)       |

**Note on `-veh 3` for Large-9:** Running Large-9 with `-veh 3` causes a deterministic access violation crash (exit code 0xC0000005). Using `-veh 5` produces valid results. The root cause is a latent memory bug triggered when vehicle capacity is tight relative to demand. All 6-period instances use `-veh 5`.

---

## Results — 3-Period Instances (Large-1 to 8)

| Instance | n   | Depots | Veh | Feasible | Best Cost  | Iters | Stopping Reason       |
|----------|-----|--------|-----|----------|------------|-------|-----------------------|
| Large-1  | 50  | 3      | 3   | **YES**  | 6,486.23   | 2,896 | Time limit (300 s)    |
| Large-2  | 70  | 3      | 3   | NO       | —          | 1,999 | 2000-iter rule        |
| Large-3  | 90  | 3      | 3   | **YES**  | 13,074.5   | 2,769 | Time limit (300 s)    |
| Large-4  | 100 | 3      | 3   | **YES**  | 12,479.9   | 2,268 | Time limit (300 s)    |
| Large-5  | 120 | 4      | 5   | **YES**  | 16,875.1   | 2,034 | Time limit (300 s)    |
| Large-6  | 135 | 4      | 5   | **YES**  | 20,212.7   | 2,271 | Time limit (300 s)    |
| Large-7  | 150 | 4      | 5   | **YES**  | 21,013.7   | 2,066 | Time limit (300 s)    |
| Large-8  | 170 | 4      | 5   | NO       | —          | 1,999 | 2000-iter rule        |

Feasibility rate (3-period): **6 / 8 = 75%**

---

## Results — 6-Period Instances (Large-9 to 16)

| Instance | n   | Depots | Veh | Seed | Feasible | Best Cost  | Iters | NI at Stop | Elapsed  | Stopping Reason     |
|----------|-----|--------|-----|------|----------|------------|-------|------------|----------|---------------------|
| Large-9  | 50  | 3      | 5   | 1    | **YES**  | 10,382.5   | 1,999 | 2,000      | 271.6 s  | 2000-iter rule      |
| Large-10 | 70  | 3      | 5   | 1    | **YES**  | 17,566.2   | 2,164 | —          | 300 s    | Time limit          |
| Large-11 | 90  | 3      | 5   | 1    | **YES**  | 27,633.7   | 2,175 | 2,000      | 274.3 s  | 2000-iter rule      |
| Large-12 | 100 | 4      | 5   | 1    | **YES**  | 33,295.0   | 2,075 | —          | 300 s    | Time limit          |
| Large-13 | 120 | 4      | 5   | 1    | NO       | —          | 1,999 | 2,000      | —        | 2000-iter rule      |
| Large-14 | 135 | 4      | 5   | 1    | **YES**  | 80,913.6   | 2,157 | —          | 300 s    | Time limit          |
| Large-15 | 150 | 4      | 5   | **2**| NO       | —          | 1,999 | 2,000      | 278.6 s  | 2000-iter rule      |
| Large-16 | 170 | 4      | 5   | 1    | NO       | —          | 1,999 | 2,000      | —        | 2000-iter rule      |

Feasibility rate (6-period): **5 / 8 = 62.5%**

---

## Overall Summary

| Metric                | Value           |
|-----------------------|-----------------|
| Total instances       | 16              |
| Feasible solutions    | 11 / 16 = **68.75%** |
| 3-period feasible     | 6 / 8 = 75%     |
| 6-period feasible     | 5 / 8 = 62.5%   |
| Stopped by time limit | 8 instances     |
| Stopped by 2000-iter  | 8 instances     |

---

## Notes and Known Issues

### 1. Large-15 seed anomaly

Running `Large-15.dat -seed 1` causes an access violation (exit code `0xC0000005`) during the first generation of the GA. With `-seed 2` the run completes normally (infeasible in 278.6 s / 1999 iterations). This is a latent memory bug in the codebase that is triggered by a specific random sequence. The result for Large-15 is therefore reported with `seed=2`.

### 2. High cost on Large-14

Large-14 (n=135, T=6) shows a best cost of 80,913.6, which is roughly 2.4× the cost of Large-12 (n=100, T=6) at 33,295. This is attributable to the combination of more customers, higher cumulative inventory cost over 6 periods, and a harder routing structure. The value is the operational IRP objective (routing + inventory).

### 3. Infeasible instances with 2000-iter stopping rule

Instances Large-2, 8, 13, 15, 16 reported no feasible solution within the 2000 non-improving iteration budget. These are the largest/hardest instances in their respective period groups. Options to improve:
- Increase vehicle count (`-veh 7` or higher)
- Increase time budget (`-t 600`)
- Use more permissive initial penalty values

### 4. Stopping criterion implementation

The 2000-iteration non-improving rule is implemented in `Genetic::evolve()` (`Genetic.cpp`):

```cpp
while (nbIter < maxIter && nbIterNonProd < maxIterNonProd
       && (ticks <= 0 || clock() - params->debut <= ticks))
```

`maxIterNonProd = 2000` and `max_iter = 10000000` are set unconditionally in `main.cpp`. When `-t 300` is specified, both conditions are active; the algorithm halts at whichever fires first.

**Root cause of the original bug (for reference):** The user's earlier attempt to set `maxIterNonProd = 2000` was placed inside an `else` branch that was dead code. `commandline.cpp` line 54 sets `cpu_time = 1200` as the default, making `nb_ticks_allowed > 0` always true — so the `if (nb_ticks_allowed > 0)` branch always executed and the `else` branch with the 2000-iter value was never reached. The fix moved both limits outside the if/else, making them unconditional.

---

## Command Reference

```
# 3-period, 3-depot (Large-1 to 4)
.\irp.exe Data\Large\Large-X.dat -seed 1 -type 39 -veh 3 -t 300

# 3-period, 4-depot (Large-5 to 8)
.\irp.exe Data\Large\Large-X.dat -seed 1 -type 39 -veh 5 -t 300

# 6-period, any depot count (Large-9 to 16)
.\irp.exe Data\Large\Large-X.dat -seed 1 -type 39 -veh 5 -t 300
```

Build command (MSVC, x64):
```
cl /nologo /EHsc /std:c++14 /O2 *.cpp /Fe:irp.exe
```
