# HGS-IRP Multi-Depot with Time Windows (Type 39) — Output Reference

## Build & Run

### Build (Windows / MSVC)
```
cl /nologo /EHsc /std:c++14 /O2 *.cpp /Fe:irp.exe
```

### Run
```
irp.exe <data_file> -seed <N> -type 39 -veh <vehicles_per_depot> -t <time_limit_sec>
```
Example:
```
irp.exe Data\Custom\sample_multi_depot_tw.dat -seed 1 -type 39 -veh 2 -t 60
```

### Parameters
| Flag | Description |
|------|-------------|
| `-type 39` | Multi-depot IRP with time windows |
| `-veh N` | Number of vehicles **per depot** |
| `-t T` | Time limit in seconds |
| `-seed S` | Random seed (0 = system time) |

---

## Input Data Format (Type 39)

### Line 1 — Problem dimensions
```
nbRetailers  nbDays  vehicleCapacity  nbDepots
```

### Depot lines (one per depot)
```
id  longitude  latitude  initialInventory  dailyProduction  holdingCost
```

### Customer lines (one per customer)
```
id  longitude  latitude  startingInventory  maxInventory  minInventory  dailyDemand  inventoryCost  earliestTime  serviceTime  latestTime
```

- **Time values** are specified in **hours** and internally converted to km-equivalent by multiplying with speed (default 40 km/h).
- **Distances** are computed using the Haversine formula (output in km).

---

## Console Output

### Iteration log format
```
It <iter> | Sol <bestFeasible> <bestInfeasible> | Moy <avgFeas> <avgInfeas> | Div <divFeas> <divInfeas> | Val <cap> <len> <tw> | Pen <penCap> <penLen> <penTW> | Pop <nValid> <nInvalid>
```

| Field | Meaning |
|-------|---------|
| `Sol` | Best solution cost (feasible / infeasible subpopulations) |
| `Moy` | Average cost per subpopulation |
| `Div` | Diversity measure per subpopulation |
| `Val` | Fraction of population feasible for: capacity, route length, time windows |
| `Pen` | Current adaptive penalty weights |
| `Pop` | Population size: valid / invalid individuals |

`NEW BEST FEASIBLE <cost>` is printed each time a better feasible solution is found.

---

## Solution Output Files

Two files are written to `<data_directory>/diff/`:

1. **STsol-...** — Appended best solution from each run
2. **STbks-...** — Best known solution (detailed, overwritten if improved)

### BKS File Structure

#### Route detail block (per day, per vehicle)
```
day[k] route[r] depot[d]: travel time = T  load = L  TW_viol = V
 depot[d](depart=Hh) -> c<id>(del=Q, arr=Ah, start=Sh, dep=Dh, tw=[a,b]) -> ... -> depot[d]
```

| Field | Meaning |
|-------|---------|
| `day[k]` | Planning day (1-indexed) |
| `route[r]` | Vehicle/route index |
| `depot[d]` | Depot node serving this route |
| `distance` | Total routing distance used in the operational objective |
| `duration` | Total route duration including travel, waiting, and service |
| `depart=Hh` | Vehicle departure time from depot in hours |
| `load` | Total delivery quantity on this route |
| `TW_viol` | Time window violation amount (0 = feasible) |
| `del=Q` | Delivery quantity to this customer |
| `arr=Ah` | Raw arrival time at this customer in hours |
| `start=Sh` | Service start time after waiting for the time window opening |
| `dep=Dh` | Departure time after service |
| `tw=[a,b]` | Customer time window in hours |

¹ To convert arrival time to hours: divide by speed (40 km/h).  
  Example: `arr=80.0` = 80/40 = 2.0 hours.

#### Customer inventory block
```
CUSTOMER <id> bounds (min, max) [day1_morning, day1_afterDelivery, day1_afterConsumption] [day2...] [day3...]
```

For each day:
- **morning**: Inventory level at start of day
- **afterDelivery**: morning + delivery quantity
- **afterConsumption**: afterDelivery − dailyDemand

#### Depot inventory block
```
DEPOT <d>  [startOfDay, afterShipments] [...]
```

For each day:
- **startOfDay**: cumulative production up to this day
- **afterShipments**: after subtracting deliveries to customers served by this depot

#### Cost summary
```
ROUTING COST: <totalRoutingDistance>
ROUTE DURATION: <totalRouteDuration>
CAPACITY PENALTY: <capacityViolationPenalty>
ROUTE LENGTH PENALTY: <routeLengthPenalty>
TIME WINDOW PENALTY: <timeWindowPenalty>
DEPOT HOLDING: <depotInventoryHoldingCost>
RETAILER HOLDING: <customerInventoryHoldingCost>
CLIENT STOCKOUT: <customerStockoutCost>
COST SUMMARY : OVERALL <routingCost + depotHolding + retailerHolding + stockout>
REFORMULATED COST : <operational cost + penalties>
```

- **ROUTING COST** is now the pure travel distance, not the route duration.
- **ROUTE DURATION** is reported separately for time-window analysis.
- **COST SUMMARY** is the original-space operational objective value.
- **REFORMULATED COST** is the penalized objective used by the metaheuristic.

#### Route export lines
```
<depot> <day> <routeIndex> <travelTime> <load>  <nNodes> <node1> <node2> ... <nodeN>
```

---

## Feasibility Criteria

A solution is considered **valid** when all three conditions hold:
1. **Capacity**: Total load on each route ≤ vehicle capacity
2. **Route length**: Total route time ≤ max route time
3. **Time windows**: Arrival at each customer ≤ customer's latest service time

Note: Inventory feasibility (inventory ≥ minInventory) is enforced through the cost function (reformulated objective) rather than as a hard constraint on validity.

---

## Key Implementation Notes

- **Multi-depot split**: Uses `splitLF` which correctly assigns each vehicle to its designated depot for distance/time calculations.
- **Time windows**: Hours → km-equiv conversion ensures TW constraints are comparable with Haversine distances.
- **Lot-sizing DP** (mutation11): Decides optimal delivery quantities per customer per day, considering per-depot inventory costs and route insertion costs.
- **Adaptive penalties**: Capacity, route length, and TW penalties self-adjust to maintain ~20-25% feasible solutions in the population.
