#include "LocalSearch.h"
#include <algorithm>
#include <iomanip>
// lance la recherche locale
void LocalSearch::runILS(bool isRepPhase, int maxIterations)
{
  double bestCost = 1.e30;
  for (int it = 0; it < maxIterations; it++)
  {
    // Time-limit check between ILS iterations
    if (params->ticks > 0 && clock() - params->debut > params->ticks * 0.85)
      return;
    if (it > 0)
      shaking();
    runSearchTotal(isRepPhase);
  }
}

void LocalSearch::runSearchTotal(bool isRepPhase)
{
  this->isRepPhase = isRepPhase;
  updateMoves();
  for (int day = 1; day <= params->nbDays; day++) {
    mutationSameDay(day);
  }
  // Time-limit check: skip expensive lot-sizing if time is almost up
  if (params->ticks > 0 && clock() - params->debut > params->ticks * 0.95)
    return;

  mutationDifferentDay();
  updateMoves();
  for (int day = 1; day <= params->nbDays; day++) {
    mutationSameDay(day);
  }
}



void LocalSearch::melangeParcours()
{
  int j, temp;
  for (int k = 0; k <= params->nbDays; k++)
  {
    for (int i = 0; i < (int)ordreParcours[k].size() - 1; i++)
    {
      j = i +
          params->rng->genrand64_int64() % ((int)ordreParcours[k].size() - i);
      temp = ordreParcours[k][i];
      ordreParcours[k][i] = ordreParcours[k][j];
      ordreParcours[k][j] = temp;
      
    }
  }

  for (int i = 0; i < (int)ordreJours.size() - 1; i++)
  {
    j = i + params->rng->genrand64_int64() % ((int)ordreJours.size() - i);
    temp = ordreJours[i];
    ordreJours[i] = ordreJours[j];
    ordreJours[j] = temp;
  }
}

// updates the moves for each node which will be tried in mutationSameDay
void LocalSearch::updateMoves()
{
  int client, client2;
  int size;

  for (int k = 1; k <= params->nbDays; k++)
  {
    // pour chaque client present dans ce jour
    for (int i = 0; i < (int)ordreParcours[k].size(); i++)
    {
      client = ordreParcours[k][i];
      clients[k][client]->moves.clear();
      size = params->cli[client].sommetsVoisins.size();

      for (int a1 = 0; a1 < size; a1++)
      {
        client2 = params->cli[client].sommetsVoisins[a1];
        if (client2 >= params->nbDepots && clients[k][client2]->estPresent)
          clients[k][client]->moves.push_back(client2);
      }
    }
  }

  params->shuffleProches();
  melangeParcours();
}

int LocalSearch::mutationSameDay(int day)
{
  dayCour = day;
  int size = (int)ordreParcours[day].size();
  int size2;
  rechercheTerminee = false;
  int moveEffectue = 0;
  int nbMoves = 0;
  firstLoop = true;
  int crossDepotRestarts = 0;
  const int MAX_CROSS_DEPOT_RESTARTS = 3;
  // Work-budget counter: limits total cross-depot scan attempts per mutationSameDay call
  // independently of wall-clock time so it works in both time-limited and iteration-limited modes.
  int crossDepotWorkDone = 0;
  const int MAX_CROSS_DEPOT_WORK = MAX_CROSS_DEPOT_RESTARTS * params->nbClients; // e.g. 3×25=75
  // Pass-count guard: prevents unbounded convergence loops in iteration-based mode (ticks==0).
  // In time-limited mode the 0.90 time guard fires first. 200 is generous enough for good
  // convergence while keeping each mutationSameDay call O(n * MAX_SAME_DAY_PASSES).
  int loopPass = 0;
  const int MAX_SAME_DAY_PASSES = 50;
  // Total-moves budget: caps combined moves across all passes in this call.
  // Prevents the inner posU loop from running infinitely when a node keeps
  // flipping between depots (each flip strictly improves but by <0.001).
  // 10*nbClients gives ample budget for genuine convergence.
  int totalMoves = 0;
  const int MAX_TOTAL_MOVES = 10 * (params->nbClients + 1);

  while (!rechercheTerminee && loopPass < MAX_SAME_DAY_PASSES)
  {
    loopPass++;
    rechercheTerminee = true;
    moveEffectue = 0;
    // Time-limit check in same-day mutations
    if (params->ticks > 0 && clock() - params->debut > params->ticks * 0.90)
      return nbMoves;
    for (int posU = 0; posU < size; posU++)
    {
      // Granular time guard inside the inner posU loop
      if (params->ticks > 0 && clock() - params->debut > params->ticks * 0.90)
      { rechercheTerminee = true; break; }
      // Total-moves budget guard: prevents infinite re-visits at same posU
      if (totalMoves >= MAX_TOTAL_MOVES)
      { rechercheTerminee = true; break; }
      posU -= moveEffectue; // on retourne sur le dernier noeud si on a modifi�
      nbMoves += moveEffectue;
      totalMoves += moveEffectue;
      moveEffectue = 0;
      noeudU = clients[day][ordreParcours[day][posU]];

      noeudUPred = noeudU->pred;
      x = noeudU->suiv;
      noeudXSuiv = x->suiv;
      xSuivCour = x->suiv->cour;
      routeU = noeudU->route;
      noeudUCour = noeudU->cour;
      noeudUPredCour = noeudUPred->cour;
      xCour = x->cour;

      size2 = (int)noeudU->moves.size();
      for (int posV = 0; posV < size2 && moveEffectue == 0; posV++)
      {
        noeudV = clients[day][noeudU->moves[posV]];
        if (!noeudV->route->nodeAndRouteTested[noeudU->cour] ||
            !noeudU->route->nodeAndRouteTested[noeudU->cour] || firstLoop)
        {
          noeudVPred = noeudV->pred;
          y = noeudV->suiv;
          noeudYSuiv = y->suiv;
          ySuivCour = y->suiv->cour;
          routeV = noeudV->route;
          noeudVCour = noeudV->cour;
          noeudVPredCour = noeudVPred->cour;
          yCour = y->cour;

          if (moveEffectue != 1)
            moveEffectue = mutation1();
          if (moveEffectue != 1)
            moveEffectue = mutation2();
          if (moveEffectue != 1)
            moveEffectue = mutation3();

          // les mutations 4 et 6 (switch) , sont sym�triques
          if (noeudU->cour <= noeudV->cour)
          {
            if (moveEffectue != 1)
              moveEffectue = mutation4();
            if (moveEffectue != 1)
              moveEffectue = mutation6();
          }
          if (moveEffectue != 1)
            moveEffectue = mutation5();

          // mutations 2-opt
          if (moveEffectue != 1)
            moveEffectue = mutation7();
          if (moveEffectue != 1)
            moveEffectue = mutation8();
          if (moveEffectue != 1)
            moveEffectue = mutation9();

          // Cross-depot mutations are handled by the exhaustive scan below,
          // not in the neighbor loop (crossDepotOldCost not yet cached here).

          if (moveEffectue == 1)
          {
            routeU->reinitSingleDayMoves();
            routeV->reinitSingleDayMoves();
          }
        }
      }
      if (params->multiDepot && params->cli[noeudUCour].isBorderline && moveEffectue != 1
          && crossDepotRestarts < MAX_CROSS_DEPOT_RESTARTS)
      {
        // Work guard: cap total cross-depot scan attempts so this is O(n) per call
        // regardless of time mode (fixes hang when ticks==0 / no time limit).
        if (crossDepotWorkDone >= MAX_CROSS_DEPOT_WORK)
          continue;
        // Time guard: also bail early if >80% of global budget spent (time-limited mode only).
        if (params->ticks > 0 && clock() - params->debut > params->ticks * 0.80)
          continue;
        // Cache the current solution cost ONCE for all cross-depot mutation attempts
        crossDepotOldCost = evaluateSolutionCost(true);
        for (int otherClient = params->nbDepots; otherClient < params->nbDepots + params->nbClients && moveEffectue == 0; otherClient++)
        {
          if (otherClient == noeudUCour || !clients[day][otherClient]->estPresent)
            continue;
          // Increment work counter before each attempt; break if budget exhausted.
          if (++crossDepotWorkDone >= MAX_CROSS_DEPOT_WORK) break;
          // Time guard for time-limited mode.
          if (params->ticks > 0 && clock() - params->debut > params->ticks * 0.80)
            break;

          noeudV = clients[day][otherClient];
          if ((!noeudV->route->nodeAndRouteTested[noeudU->cour] || !noeudU->route->nodeAndRouteTested[noeudU->cour] || firstLoop) &&
            noeudV->route->depot->cour != routeU->depot->cour)
          {
            noeudVPred = noeudV->pred;
            y = noeudV->suiv;
            noeudYSuiv = y->suiv;
            ySuivCour = y->suiv->cour;
            routeV = noeudV->route;
            noeudVCour = noeudV->cour;
            noeudVPredCour = noeudVPred->cour;
            yCour = y->cour;

            if (moveEffectue != 1)
              moveEffectue = mutation10();
            if (moveEffectue != 1)
              moveEffectue = mutation11Depot();
            // mutation12 (double swap) is disabled for cross-depot moves
            // because after swapNoeud(U,V), the x/y pointers become stale
            // and the second swapNoeud(x,y) can corrupt the linked list,
            // causing an infinite loop in updateRouteData.

            if (moveEffectue == 1)
            {
              routeU->reinitSingleDayMoves();
              routeV->reinitSingleDayMoves();
              crossDepotRestarts++;
            }
          }
        }
      }

  // c'est un d�pot on tente l'insertion derriere le depot de ce jour
      // si il ya corr�lation
      if (params->isCorrelated1[noeudU->cour][depots[day][0]->cour] &&
          moveEffectue != 1)
        for (int route = 0; route < (int)depots[day].size(); route++)
        {
          noeudV = depots[day][route];
          if (!noeudV->route->nodeAndRouteTested[noeudU->cour] ||
              !noeudU->route->nodeAndRouteTested[noeudU->cour] || firstLoop)
          {
            noeudVPred = noeudV->pred;
            y = noeudV->suiv;
            noeudYSuiv = y->suiv;
            ySuivCour = y->suiv->cour;
            routeV = noeudV->route;
            noeudVCour = noeudV->cour;
            noeudVPredCour = noeudVPred->cour;
            yCour = y->cour;

            if (moveEffectue != 1)
              moveEffectue = mutation1();
            if (moveEffectue != 1)
              moveEffectue = mutation2();
            if (moveEffectue != 1)
              moveEffectue = mutation3();

            if (!noeudV->suiv->estUnDepot)
            {
              if (moveEffectue != 1)
                moveEffectue = mutation8();
              if (moveEffectue != 1)
                moveEffectue = mutation9();
            }

            if (moveEffectue == 1)
            {
              routeU->reinitSingleDayMoves();
              routeV->reinitSingleDayMoves();
            }
          }
        }
      // TODO -- check that memories are working
    }
    firstLoop = false;
  }
  return nbMoves;
}

// pour un noeud, marque que tous les mouvements impliquant ce noeud ont �t�
// test�s pour chaque route du jour day
void LocalSearch::nodeTestedForEachRoute(int cli, int day)
{
  for (int route = 0; route < (int)depots[day].size(); route++)
    routes[day][route]->nodeAndRouteTested[cli] = true;
}

// trying to change the delivery plan (lot sizing for a given customer)
int LocalSearch::mutationDifferentDay()
{
  rechercheTerminee = false;
  int nbMoves = 0;
  int times = 0;
  const int MAX_LOT_SIZING_LOOPS = 3;
  while (!rechercheTerminee && times < MAX_LOT_SIZING_LOOPS) {
    rechercheTerminee = true;
    times++;
    for (int posU = 0; posU < params->nbClients; posU++){
      if (params->ticks > 0 && clock() - params->debut > params->ticks * 0.90)
        return nbMoves;
      nbMoves += mutation11(ordreParcours[0][posU]);
    }
  }
  return nbMoves;
}

// enleve un client de l'ordre de parcours
void LocalSearch::removeOP(int day, int client)
{
  int it = 0;
  int sz = (int)ordreParcours[day].size();
  while (it < sz && ordreParcours[day][it] != client)
  {
    it++;
  }
  // BUG #16 FIX: client not found → skip instead of running past vector end
  // The original unbounded while loop wrote past the vector buffer when the
  // client was absent from ordreParcours[day], silently corrupting heap
  // metadata (STATUS_HEAP_CORRUPTION 0xC0000374, detected ~385 iters later).
  // Trigger: updateRouteData cycle-repair calls removeOP for clients whose
  // route pointer still matches after reset, but were already removed via
  // a prior removeNoeud in the same mutation11 call.
  if (it >= sz)
    return;
  ordreParcours[day][it] =
      ordreParcours[day][sz - 1];
  ordreParcours[day].pop_back();
}

// ajoute un client dans l'ordre de parcours
void LocalSearch::addOP(int day, int client)
{
  int it, temp2;
  if (ordreParcours[day].size() != 0)
  {
    it = (int)params->rng->genrand64_int64() % ordreParcours[day].size();
    temp2 = ordreParcours[day][it];
    ordreParcours[day][it] = client;
    ordreParcours[day].push_back(temp2);
  }
  else
    ordreParcours[day].push_back(client);
}

// change the choices of visit periods and quantity for "client"
int LocalSearch::mutation11(int client)
{
  Noeud *noeudTravail;
  double currentCost;
  // Compute the current lot sizing solution cost (from the model point of view)
  if(params -> isstockout){
    currentCost=evaluateCurrentCost_stockout(client);
  }
  else
    currentCost = evaluateCurrentCost(client);

  // Check if the current delivery plan violates inventory bounds
  bool inventoryFeasible = true;
  {
    double inv = params->cli[client].startingInventory;
    for (int k = 1; k <= params->ancienNbDays; k++) {
      inv += demandPerDay[k][client] - params->cli[client].dailyDemand[k];
      if (inv < params->cli[client].minInventory - 0.001 ||
          inv > params->cli[client].maxInventory + 0.001) {
        inventoryFeasible = false;
        break;
      }
    }
  }

  // Save old state for potential revert
  vector<double> oldDemand(params->ancienNbDays + 1);
  vector<bool> oldPresent(params->ancienNbDays + 1);
  vector<Noeud*> oldPred(params->ancienNbDays + 1, nullptr);
  for (int k = 1; k <= params->ancienNbDays; k++){
    oldDemand[k] = demandPerDay[k][client];
    oldPresent[k] = clients[k][client]->estPresent;
    if (oldPresent[k]) oldPred[k] = clients[k][client]->pred;
  }

  // Remove customer from all routes BEFORE computing insertion costs
  // This ensures insertion costs reflect the route without this customer
  for (int k = 1; k <= params->ancienNbDays; k++)
  {
    noeudTravail = clients[k][client];
    if (noeudTravail->estPresent){
      removeNoeud(noeudTravail);
    }
    demandPerDay[k][client] = 0.;
  }

  // Now compute insertion costs with the customer removed
  for (int k = 1; k <= params->ancienNbDays; k++){
    noeudTravail = clients[k][client];
    computeCoutInsertion(noeudTravail);
  }

  /* Generate the structures of the subproblem */
  vector<vector<Insertion>> insertions = vector<vector<Insertion>>(params->nbDays);
  vector<double> quantities = vector<double>(params->nbDays);
  vector<int> breakpoints = vector<int>(params->nbDays);
  double objective;
  for (int k = 1; k <= params->nbDays; k++)
  {
    insertions[k - 1] = clients[k][client]->allInsertions;
  }
  
  unique_ptr<LotSizingSolver> lotsizingSolver(
      make_unique<LotSizingSolver>(params, insertions, client));

  bool ok = true;
  if(params-> isstockout) ok = lotsizingSolver->solve_stockout();
  else ok = lotsizingSolver->solve();

  if (!ok) {
    // DP solver failed — revert to old state
    for (int k = 1; k <= params->ancienNbDays; k++){
      if (oldPresent[k]) {
        demandPerDay[k][client] = oldDemand[k];
        clients[k][client]->placeInsertion = oldPred[k];
        addNoeud(clients[k][client]);
      }
    }
    return 0;
  }

  objective = lotsizingSolver->objective;
  quantities = lotsizingSolver->quantities;

  if(inventoryFeasible && lt(currentCost,objective-0.01)) {
    // Current solution is cheaper and inventory-feasible, revert
    for (int k = 1; k <= params->ancienNbDays; k++){
      if (oldPresent[k]) {
        demandPerDay[k][client] = oldDemand[k];
        clients[k][client]->placeInsertion = oldPred[k];
        addNoeud(clients[k][client]);
      }
    }
    return 0;
  }

  // Apply the DP solution — insert customer per DP's recommendation
  for (int k = 1; k <= params->ancienNbDays; k++)
  {
    if (quantities[k - 1] > 0.0001 || (lotsizingSolver->breakpoints[k - 1]&&gt(0,lotsizingSolver->breakpoints[k - 1]->detour) ))
    {
      demandPerDay[k][client] = round(quantities[k - 1]);
      clients[k][client]->placeInsertion = lotsizingSolver->breakpoints[k - 1]->place;
      addNoeud(clients[k][client]);
    }
  }

  double tmpCost = 0.0;
  if(params -> isstockout)
     tmpCost = evaluateCurrentCost_stockout(client);
  else
    tmpCost = evaluateCurrentCost(client);

  // Check if the NEW delivery plan achieves inventory feasibility
  bool newPlanFeasible = true;
  {
    double inv = params->cli[client].startingInventory;
    for (int k = 1; k <= params->ancienNbDays; k++) {
      inv += demandPerDay[k][client] - params->cli[client].dailyDemand[k];
      if (inv < params->cli[client].minInventory - 0.001 ||
          inv > params->cli[client].maxInventory + 0.001) {
        newPlanFeasible = false;
        break;
      }
    }
  }

  // Decide whether to keep or revert based on actual costs and inventory feasibility
  bool shouldKeep = false;
  if (!inventoryFeasible && newPlanFeasible) {
    // Old plan violated inventory bounds, new plan doesn't → always keep
    shouldKeep = true;
  } else if (lt(tmpCost, currentCost - 0.01)) {
    // New plan is actually cheaper than old plan → keep
    shouldKeep = true;
  } else if (fabs(tmpCost - objective) <= 0.01 && lt(objective, currentCost - 0.01)) {
    // DP estimate matches reality and predicts improvement → keep (original logic)
    shouldKeep = true;
  }

  if (!shouldKeep)
  {
    // Revert to old state
    for (int k = 1; k <= params->ancienNbDays; k++){
      noeudTravail = clients[k][client];
      if (noeudTravail->estPresent) removeNoeud(noeudTravail);
      demandPerDay[k][client] = 0.;
    }
    for (int k = 1; k <= params->ancienNbDays; k++){
      if (oldPresent[k]) {
        demandPerDay[k][client] = oldDemand[k];
        clients[k][client]->placeInsertion = oldPred[k];
        addNoeud(clients[k][client]);
      }
    }
    return 0;
  }
  if ( currentCost - tmpCost >=0.01 )// An improving move has been found,
                                        // the search is not finished.
  {
    rechercheTerminee = false;
    return 1;
  }
  else
    return 0;
}

int LocalSearch::mutation10()
{
  if (!params->cli[noeudUCour].isBorderline)
    return 0;
  if (routeU->cour == routeV->cour || routeU->depot->cour == routeV->depot->cour)
    return 0;
  if (std::find(params->cli[noeudUCour].candidateDepots.begin(), params->cli[noeudUCour].candidateDepots.end(), routeV->depot->cour) == params->cli[noeudUCour].candidateDepots.end())
    return 0;
  if (noeudUCour == yCour)
    return 0;

  Noeud *oldPred = noeudUPred;
  insertNoeud(noeudU, noeudV);
  double newCost = evaluateSolutionCost(true);
  if (newCost + 0.0001 < crossDepotOldCost)
  {
    rechercheTerminee = false;
    return 1;
  }

  insertNoeud(noeudU, oldPred);
  return 0;
}

int LocalSearch::mutation11Depot()
{
  if (!params->cli[noeudUCour].isBorderline)
    return 0;
  if (routeU->cour == routeV->cour || routeU->depot->cour == routeV->depot->cour)
    return 0;
  if (std::find(params->cli[noeudUCour].candidateDepots.begin(), params->cli[noeudUCour].candidateDepots.end(), routeV->depot->cour) == params->cli[noeudUCour].candidateDepots.end())
    return 0;
  if (noeudUCour == noeudVPredCour || noeudUCour == yCour)
    return 0;

  swapNoeud(noeudU, noeudV);
  double newCost = evaluateSolutionCost(true);
  if (newCost + 0.0001 < crossDepotOldCost)
  {
    rechercheTerminee = false;
    return 1;
  }

  swapNoeud(noeudU, noeudV);
  return 0;
}

int LocalSearch::mutation12()
{
  if (!params->cli[noeudUCour].isBorderline)
    return 0;
  if (routeU->cour == routeV->cour || routeU->depot->cour == routeV->depot->cour)
    return 0;
  if (std::find(params->cli[noeudUCour].candidateDepots.begin(), params->cli[noeudUCour].candidateDepots.end(), routeV->depot->cour) == params->cli[noeudUCour].candidateDepots.end())
    return 0;
  if (x->estUnDepot || y->estUnDepot)
    return 0;
  if (y == noeudUPred || noeudU == y || x == noeudV || noeudV == noeudXSuiv)
    return 0;

  swapNoeud(noeudU, noeudV);
  swapNoeud(x, y);
  double newCost = evaluateSolutionCost(true);
  if (newCost + 0.0001 < crossDepotOldCost)
  {
    rechercheTerminee = false;
    return 1;
  }

  swapNoeud(x, y);
  swapNoeud(noeudU, noeudV);
  return 0;
}

double LocalSearch::evaluateCurrentCost(int client)
{
  
  Noeud *noeudClient;
  double myCost = 0.;
  // Sum up the detour cost, inventory cost, and eventual excess of capacity
  for (int k = 1; k <= params->ancienNbDays; k++)
  {
    noeudClient = clients[k][client];
    if (noeudClient->estPresent)
    {
      // adding the inventory cost (use per-depot cost if multi-depot)
      double depotInvCost = params->inventoryCostSupplier;
      if (params->nbDepots > 1 && noeudClient->route && noeudClient->route->depot)
      {
        int depotIdx = noeudClient->route->depot->cour;
        if (depotIdx < (int)params->inventoryCostPerDepot.size())
          depotInvCost = params->inventoryCostPerDepot[depotIdx];
      }
      myCost +=
          (params->cli[client].inventoryCost - depotInvCost) *
          (double)(params->ancienNbDays + 1 - k) * demandPerDay[k][client];

      // the detour cost
      myCost +=
          params->timeCost[noeudClient->cour][noeudClient->suiv->cour] +
          params->timeCost[noeudClient->pred->cour][noeudClient->cour] -
          params->timeCost[noeudClient->pred->cour][noeudClient->suiv->cour];

      // and the possible excess capacity
      myCost += params->penalityCapa *
                (std::max<double>(0., noeudClient->route->charge -
                                          noeudClient->route->vehicleCapacity) -
                 std::max<double>(0., noeudClient->route->charge -
                                          noeudClient->route->vehicleCapacity -
                                          demandPerDay[k][client]));
    }
  }
  return myCost;
}


// Evaluates the current objective function of the whole solution
double LocalSearch::evaluateSolutionCost(bool penalized)
{
  double myCost = 0.;
  double capacityCosts = 0.;
  double lengthCosts = 0.;
  double timeWindowCosts = 0.;
  double inventoryViolationCosts = 0.;
  vector<vector<int>> depotOfCust(params->ancienNbDays + 1, vector<int>(params->nbDepots + params->nbClients, -1));

  auto depotInventoryCost = [&](int day, int customer) {
    int depotIdx = depotOfCust[day][customer];
    if (depotIdx < 0)
      depotIdx = params->cli[customer].preferredDepot;
    if (params->nbDepots > 1 && depotIdx >= 0 && depotIdx < (int)params->inventoryCostPerDepot.size())
      return params->inventoryCostPerDepot[depotIdx];
    return params->inventoryCostSupplier;
  };

  for (int k = 1; k <= params->ancienNbDays; k++)
  {
    for (int r = 0; r < params->nombreVehicules[k]; r++)
    {
      myCost += routes[k][r]->distance;
      capacityCosts += params->penalityCapa * std::max<double>(routes[k][r]->charge - routes[k][r]->vehicleCapacity, 0.);
      lengthCosts += params->penalityLength * std::max<double>(routes[k][r]->temps - routes[k][r]->maxRouteTime, 0.);
      timeWindowCosts += params->penalityTimeWindow * routes[k][r]->timeWindowViolation;

      if (params->nbDepots > 1 && routes[k][r]->depot)
      {
        Noeud *n = routes[k][r]->depot->suiv;
        int _cyc = 0;
        while (n && !n->estUnDepot)
        {
          depotOfCust[k][n->cour] = routes[k][r]->depot->cour;
          n = n->suiv;
          if (++_cyc > 500) break;
        }
      }
    }
  }

  if (params->isstockout)
  {
    vector<double> inventory(params->nbDepots + params->nbClients, 0.);
    for (int i = params->nbDepots; i < params->nbDepots + params->nbClients; i++)
      inventory[i] = params->cli[i].startingInventory;

    myCost += params->objectiveConstant_stockout;
    for (int k = 1; k <= params->ancienNbDays; k++)
      for (int i = params->nbDepots; i < params->nbDepots + params->nbClients; i++)
      {
        double endInventory = std::max<double>(0., inventory[i] + demandPerDay[k][i] - params->cli[i].dailyDemand[k]);
        double stockout = std::max<double>(0., params->cli[i].dailyDemand[k] - demandPerDay[k][i] - inventory[i]);
        myCost += endInventory * params->cli[i].inventoryCost;
        myCost += stockout * params->cli[i].stockoutCost;
        myCost -= demandPerDay[k][i] * (params->ancienNbDays + 1 - k) * depotInventoryCost(k, i);
        inventory[i] = endInventory;

        if (penalized)
        {
          if (inventory[i] < params->cli[i].minInventory)
            inventoryViolationCosts += params->cli[i].minInventory - inventory[i];
          if (inventory[i] > params->cli[i].maxInventory)
            inventoryViolationCosts += inventory[i] - params->cli[i].maxInventory;
        }
      }
  }
  else
  {
    for (int k = 1; k <= params->ancienNbDays; k++)
      for (int i = params->nbDepots; i < params->nbDepots + params->nbClients; i++)
        myCost += demandPerDay[k][i] * (params->ancienNbDays + 1 - k) * (params->cli[i].inventoryCost - depotInventoryCost(k, i));

    myCost += params->objectiveConstant;

    if (penalized)
    {
      for (int i = params->nbDepots; i < params->nbDepots + params->nbClients; i++)
      {
        double inventory = params->cli[i].startingInventory;
        for (int k = 1; k <= params->nbDays; k++)
        {
          inventory += demandPerDay[k][i];
          inventory -= params->cli[i].dailyDemand[k];
          if (inventory < params->cli[i].minInventory)
            inventoryViolationCosts += params->cli[i].minInventory - inventory;
          if (inventory > params->cli[i].maxInventory)
            inventoryViolationCosts += inventory - params->cli[i].maxInventory;
        }
      }
    }
  }

  if (!penalized)
    return myCost;

  return myCost + capacityCosts + lengthCosts + timeWindowCosts + params->penalityInventory * inventoryViolationCosts;
}



// Evaluates the current objective function of the whole solution
void LocalSearch::printInventoryLevels(std::ostream& file,bool add)
{
  double inventoryClientCosts = 0.;
  double inventorySupplyCosts = 0.;
  double stockClientCosts = 0;
  double stockClientAmount=0;
  double routeCosts = 0.;
  double routeDuration = 0.;
  double loadCosts = 0.;
  double lengthCosts = 0.;
  double timeWindowCosts = 0.;
  vector<vector<int>> depotOfCust(params->nbDays + 1, vector<int>(params->nbDepots + params->nbClients, -1));

  auto timeToHours = [&](double value) {
    return params->speedKmh > 0.0 ? value / params->speedKmh : value;
  };

  // Summing distance and load penalty
  for (int k = 1; k <= params->ancienNbDays; k++)
  {
    for (int r = 0; r < params->nombreVehicules[k]; r++)
    {
      routeCosts += routes[k][r]->distance;
      routeDuration += routes[k][r]->temps;
      loadCosts += params->penalityCapa * std::max<double>(routes[k][r]->charge - routes[k][r]->vehicleCapacity, 0.);
      lengthCosts += params->penalityLength * std::max<double>(routes[k][r]->temps - routes[k][r]->maxRouteTime, 0.);
      timeWindowCosts += params->penalityTimeWindow * routes[k][r]->timeWindowViolation;

      if (params->nbDepots > 1 && routes[k][r]->depot)
      {
        Noeud *assigned = routes[k][r]->depot->suiv;
        while (assigned && !assigned->estUnDepot)
        {
          depotOfCust[k][assigned->cour] = routes[k][r]->depot->cour;
          assigned = assigned->suiv;
        }
      }
      
      if(!add) {
        int depotId = routes[k][r]->depot ? routes[k][r]->depot->cour : -1;
        double departureTime = 0.;
        if (params->hasTimeWindows && routes[k][r]->depot && !routes[k][r]->depot->suiv->estUnDepot)
        {
          Noeud *firstCustomer = routes[k][r]->depot->suiv;
          double travelToFirst = params->timeCost[depotId][firstCustomer->cour];
          double serviceStartFirst = std::max(travelToFirst, params->cli[firstCustomer->cour].earliestTime);
          departureTime = std::max(0.0, serviceStartFirst - travelToFirst);
        }

        file <<"day["<<k<<"] route["<<r<<"] depot["<<depotId<<"]: distance = "<<routes[k][r]->distance
             <<" duration = "<<routes[k][r]->temps;
        if (params->hasTimeWindows)
          file << " depart = " << std::fixed << std::setprecision(3) << timeToHours(departureTime) << "h";
        file << std::defaultfloat << std::setprecision(6)
             <<" load = "<<routes[k][r]->charge;
        if (params->hasTimeWindows)
          file << " TW_viol = " << routes[k][r]->timeWindowViolation;
        file << endl;
        if (routes[k][r]->depot)
        {
          Noeud *n = routes[k][r]->depot->suiv;
          int prev = depotId;
          double currentTime = departureTime;
          file << " depot[" << depotId << "]";
          if (params->hasTimeWindows)
            file << "(depart=" << std::fixed << std::setprecision(3) << timeToHours(departureTime) << "h)";
          while (n && !n->estUnDepot)
          {
            double arrival = currentTime + params->cli[prev].serviceDuration + params->timeCost[prev][n->cour];
            double serviceStart = params->hasTimeWindows ? std::max(arrival, params->cli[n->cour].earliestTime) : arrival;
            double departure = serviceStart + params->cli[n->cour].serviceDuration;
            file << " -> c" << n->cour << "(del=" << demandPerDay[k][n->cour];
            if (params->hasTimeWindows)
            {
              file << ",arr=" << timeToHours(arrival) << "h,start=" << timeToHours(serviceStart) << "h,dep=" << timeToHours(departure)
                   << "h,tw=[" << timeToHours(params->cli[n->cour].earliestTime) << "," << timeToHours(params->cli[n->cour].latestTime) << "]";
            }
            file << ")";
            currentTime = serviceStart;
            prev = n->cour;
            n = n->suiv;
          }
          file << std::defaultfloat << std::setprecision(6) << " -> depot[" << depotId << "]" << endl;
        }
      }
    }
  }

  // Printing customer inventory and computing customer inventory cost
  if(params->isstockout){

    double inventoryClient;
    for (int i = params->nbDepots; i < params->nbDepots + params->nbClients;
         i++)
    {
      inventoryClient = params->cli[i].startingInventory;
      if(!add) file  << "CUSTOMER " << i << " bounds (" << params->cli[i].minInventory
           << "," << params->cli[i].maxInventory << ") ";
      for (int k = 1; k <= params->nbDays; k++)
      {
        // print the level in the morning
        if(!add) file << "[morning: " << inventoryClient;
        // print the level after receiving inventory
        inventoryClient += demandPerDay[k][i];
        if(!add) file  << " ,replinishment: " << demandPerDay[k][i];
        // print the level after consumption
        double stock = std::max<double>(0,params->cli[i].dailyDemand[k]-inventoryClient);
        inventoryClient = std::max<double>(0,inventoryClient-params->cli[i].dailyDemand[k]);
        
        if(!add) file  << ", everning: " << inventoryClient << "] ";

        inventoryClientCosts += inventoryClient * params->cli[i].inventoryCost ;
        stockClientCosts += stock*params->cli[i].stockoutCost;
        stockClientAmount += stock;
      }
      if(!add) file  << endl;
    }
  }
  else{
    double inventoryClient;
    for (int i = params->nbDepots; i < params->nbDepots + params->nbClients; i++)
    {
      inventoryClient = params->cli[i].startingInventory;
      if(!add) file  << "CUSTOMER " << i << " bounds (" << params->cli[i].minInventory
           << "," << params->cli[i].maxInventory << ") ";
      for (int k = 1; k <= params->nbDays; k++)
      {
        // print the level in the morning
        if(!add) file  << "[" << inventoryClient;
        // print the level after receiving inventory
        inventoryClient += demandPerDay[k][i];
        if(!add) file  << "," << inventoryClient;
        // print the level after consumption
        inventoryClient -= params->cli[i].dailyDemand[k];
        if(!add) file << "," << inventoryClient << "] ";

        // Only count positive inventory for holding cost (negative = stockout, not negative holding)
        inventoryClientCosts += std::max<double>(0, inventoryClient) * params->cli[i].inventoryCost;
      }
      if(!add) file  << endl;
    }
  }
  

  double inventorySupply = 0;
  if (params->multiDepot && !params->availableSupplyPerDepot.empty()) {
    // Per-depot supplier inventory tracking
    for (int d = 0; d < params->nbDepots; d++) {
      double depotInv = 0;
      if(!add) file << "DEPOT " << d << "    ";
      for (int k = 1; k <= params->nbDays; k++) {
        depotInv += params->availableSupplyPerDepot[d][k];
        if(!add) file << "[" << depotInv << ",";
        for (int i = params->nbDepots; i < params->nbDepots + params->nbClients; i++) {
          if (depotOfCust[k][i] == d)
            depotInv -= demandPerDay[k][i];
        }
        if(!add) file << depotInv << "] ";
        inventorySupplyCosts += depotInv * params->inventoryCostPerDepot[d];
      }
      if(!add) file << endl;
    }
  } else {
    if(!add) file  << "SUPPLIER    ";
    for (int k = 1; k <= params->nbDays; k++)
    {
      inventorySupply += params->availableSupply[k];
      if(!add) file  << "[" << inventorySupply << ",";
      for (int i = params->nbDepots; i < params->nbDepots + params->nbClients; i++)
        inventorySupply -= demandPerDay[k][i];
      if(!add) file  << inventorySupply << "] ";
      inventorySupplyCosts += inventorySupply * params->inventoryCostSupplier;
    }
    if(!add) file  << endl;
  }

  file  << "ROUTING COST: " << routeCosts << endl;
  file  << "ROUTE DURATION: " << routeDuration << endl;
  file << "CAPACITY PENALTY: " << loadCosts << endl;
  file << "ROUTE LENGTH PENALTY: " << lengthCosts << endl;
  file << "TIME WINDOW PENALTY: " << timeWindowCosts << endl;
  file << "DEPOT HOLDING: " << inventorySupplyCosts << endl;
  file << "RETAILER HOLDING: " << inventoryClientCosts << endl;
  file << "CLIENT STOCKOUT: " << stockClientCosts<<endl;
  file << "CLIENT STOCKOUT Amount: " << stockClientAmount<<endl;
  file  << "COST SUMMARY : OVERALL "
       << routeCosts + inventorySupplyCosts + inventoryClientCosts + stockClientCosts
       << endl;
  file  << "REFORMULATED COST : " << evaluateSolutionCost(true) << endl;

  // Print delivery quantity per retailer per depot
  if(!add && params->multiDepot) {
    file << endl << "=== CUSTOMER DEPOT ASSIGNMENT BY DAY ===" << endl;
    for (int i = params->nbDepots; i < params->nbDepots + params->nbClients; i++) {
      file << "Customer " << i << ": ";
      for (int k = 1; k <= params->nbDays; k++) {
        if (demandPerDay[k][i] > 0.0001)
          file << "day" << k << "->D" << depotOfCust[k][i] << " ";
      }
      if (params->cli[i].isBorderline)
        file << "[borderline]";
      file << endl;
    }

    file << endl << "=== DELIVERY QUANTITY PER RETAILER PER DEPOT ===" << endl;
    for (int d = 0; d < params->nbDepots; d++) {
      file << "DEPOT " << d << ":" << endl;
      for (int i = params->nbDepots; i < params->nbDepots + params->nbClients; i++) {
        double totalDel = 0;
        file << "  Customer " << i << ": ";
        for (int k = 1; k <= params->nbDays; k++) {
          if (depotOfCust[k][i] == d && demandPerDay[k][i] > 0.0001) {
            file << "day" << k << "=" << demandPerDay[k][i] << " ";
            totalDel += demandPerDay[k][i];
          }
        }
        if (totalDel > 0)
          file << "(total=" << totalDel << ")" << endl;
        else
          file << "(none)" << endl;
      }
    }
  }
}

// supprime le noeud
void LocalSearch::removeNoeud(Noeud *U)
{
  // mettre a jour les noeuds
  U->pred->suiv = U->suiv;
 
  U->suiv->pred = U->pred;

  U->route->updateRouteData();

  // on g�re les autres structures de donn�es
  removeOP(U->jour, U->cour);
  U->estPresent = false;

  // signifier que les insertions sur cette route ne sont plus bonnes
  U->route->initiateInsertions();

  // signifier que pour ce jour les insertions de noeuds ne sont plus bonnes
  for (int i = params->nbDepots; i < params->nbDepots + params->nbClients; i++)
    clients[U->jour][i]->coutInsertion = 1.e30;

}

void LocalSearch::addNoeud(Noeud *U)
{
  if (U->placeInsertion == nullptr) return; // safety: no valid insertion point (e.g. route had a cycle)
  U->placeInsertion->suiv->pred = U;
  U->pred = U->placeInsertion;
  U->suiv = U->placeInsertion->suiv;
  U->placeInsertion->suiv = U;

  // et mettre a jour les routes
  U->route = U->placeInsertion->route;
  U->route->cycleRepairHappened = false; // reset before calling updateRouteData
  U->route->updateRouteData();

  // BUG #17 FIX: if updateRouteData's cycle-repair fired, the route was reset
  // (depot→fin) and U's insertion was undone.  U's pred/suiv are stale — do
  // NOT mark U as present or add it to ordreParcours, or the next removeNoeud
  // call will use the stale pointers to corrupt the chain of other clients.
  if (U->route->cycleRepairHappened) {
    U->estPresent = false;
    return;
  }

  // on gère les autres structures de données
  addOP(U->jour, U->cour);
  U->estPresent = true;

  // signifier que les insertions sur cette route ne sont plus bonnes
  U->route->initiateInsertions();

  // signifier que pour ce jour les insertions de noeuds ne sont plus bonnes
  for (int i = params->nbDepots; i < params->nbDepots + params->nbClients; i++)
    clients[U->jour][i]->coutInsertion = 1.e30;
}

// calcule pour un jour donn� et un client donn� (repr�sent� par un noeud)
// les couts d'insertion dans les differentes routes constituant ce jour
void LocalSearch::computeCoutInsertion(Noeud *client)
{
  Route *myRoute;
  client->allInsertions.clear();
  int currentDepot = (client->estPresent && client->route && client->route->depot) ? client->route->depot->cour : -1;
  vector<int> allowedDepots;

  if (params->multiDepot && params->nbDepots > 1)
  {
    if (params->cli[client->cour].isBorderline && !params->cli[client->cour].candidateDepots.empty())
      allowedDepots = params->cli[client->cour].candidateDepots;
    else
      allowedDepots.push_back(params->cli[client->cour].preferredDepot);

    if (currentDepot >= 0 && std::find(allowedDepots.begin(), allowedDepots.end(), currentDepot) == allowedDepots.end())
      allowedDepots.push_back(currentDepot);
  }

  // for each route of this day
  for (int r = 0; r < (int)routes[client->jour].size(); r++){
    // later on we can simply retrieve
    // calculate the best insertion point as well as its load

    myRoute = routes[client->jour][r];
	if (!allowedDepots.empty())
	{
		int routeDepot = (myRoute->depot != nullptr) ? myRoute->depot->cour : -1;
		if (std::find(allowedDepots.begin(), allowedDepots.end(), routeDepot) == allowedDepots.end())
			continue;
	}
    myRoute->evalInsertClient(client);
    client->allInsertions.push_back(myRoute->bestInsertion[client->cour]);
  }

	if (client->allInsertions.empty())
	{
		for (int r = 0; r < (int)routes[client->jour].size(); r++)
		{
			myRoute = routes[client->jour][r];
			myRoute->evalInsertClient(client);
			client->allInsertions.push_back(myRoute->bestInsertion[client->cour]);
		}
	}

  // eliminate dominated insertions
  client->removeDominatedInsertions(params->penalityCapa);
}

double LocalSearch::evaluateCurrentCost_stockout(int client)
{
  Noeud *noeudClient;
  double myCost = 0.;
  double I = params->cli[client].startingInventory;
  // Sum up the detour cost, inventory cost, and eventual excess of capacity
  for (int k = 1; k <= params->ancienNbDays; k++)
  {
    noeudClient = clients[k][client];
    if (noeudClient->estPresent){
      // adding the inventory cost
        myCost +=
          params->cli[client].inventoryCost * 
          std::max<double> (0., I+demandPerDay[k][client]-params->cli[client].dailyDemand[k]);
      //stockout
        myCost +=
          params->cli[client].stockoutCost * std::max<double> (0., -I-demandPerDay[k][client]+params->cli[client].dailyDemand[k]);
      
      //-supplier *q[] (per-depot if available)
        double depotInvCostSO = params->inventoryCostSupplier;
        if (params->nbDepots > 1 && noeudClient->route && noeudClient->route->depot)
        {
          int depotIdx = noeudClient->route->depot->cour;
          if (depotIdx < (int)params->inventoryCostPerDepot.size())
            depotInvCostSO = params->inventoryCostPerDepot[depotIdx];
        }
        myCost -=  depotInvCostSO *
            (double)(params->ancienNbDays + 1 - k) * demandPerDay[k][client];

      // the detour cost
        myCost +=
            params->timeCost[noeudClient->cour][noeudClient->suiv->cour] +
            params->timeCost[noeudClient->pred->cour][noeudClient->cour] -
            params->timeCost[noeudClient->pred->cour][noeudClient->suiv->cour];

      // and the possible excess capacity, the privous penalty are calculated already.
        double x1 = noeudClient->route->charge -  noeudClient->route->vehicleCapacity;
        if(eq(x1,0)) x1 = 0;
        double x2=noeudClient->route->charge -
                  noeudClient->route->vehicleCapacity - demandPerDay[k][client];
        if(eq(x2,0)) x2 = 0;
        myCost += params->penalityCapa *(std::max<double>(0., x1) - std::max<double>(0., x2));
        myCost += 1000000*std::max<double> (0.,I+demandPerDay[k][client]-params->cli[client].maxInventory);
       
        I = std::max<double> (0., I+demandPerDay[k][client]-params->cli[client].dailyDemand[k]);
      }
      else{ 
        myCost += params->cli[client].inventoryCost *  std::max<double>(0., I-params->cli[client].dailyDemand[k]);
        myCost += params->cli[client].stockoutCost * std::max<double>  (0., -I+params->cli[client].dailyDemand[k]);

        I = std::max<double> (0., I-params->cli[client].dailyDemand[k]);
        
      }
  }
  return myCost;
}



void LocalSearch::shaking()
{
  updateMoves(); // shuffles the order of the customers in each day in the
  // table "ordreParcours"

  // For each day, perform one random swap
  int nbRandomSwaps = 1;
  for (int k = 1; k <= params->nbDays; k++)
  {
    if (ordreParcours[k].size() > 2) // if there are more than 2 customers in the day
    {
      for (int nSwap = 0; nSwap < nbRandomSwaps; nSwap++)
      {
        // Select the two customers to be swapped
        int client1 = ordreParcours[k][params->rng->genrand64_int63() %
                                       ordreParcours[k].size()];
        int client2 = client1;
        while (client2 == client1)
          client2 = ordreParcours[k][params->rng->genrand64_int63() %
                                     ordreParcours[k].size()];

        // Perform the swap
        Noeud *noeud1 = clients[k][client1];
        Noeud *noeud2 = clients[k][client2];

        // If the nodes are not identical or consecutive (TODO : check why
        // consecutive is a problem in the function swap)
        if (client1 != client2 &&
            !(noeud1->suiv == noeud2 || noeud1->pred == noeud2))
        {
          swapNoeud(noeud1, noeud2);
        }
      }
    }
  }

  // Take one customer, and put it back to the days corresponding to the best
  // lot sizing (without detour cost consideration)
  // NOTE: The lot-sizing model (ModelLotSizingPI) is commented out.
  // Skip the lot-sizing re-insertion to avoid accessing empty vectors.
  // Instead, just keep the random swap shaking above.
}

// constructeur
LocalSearch::LocalSearch(void) {}

// constructeur 2
LocalSearch::LocalSearch(Params *params, Individu *individu)
    : params(params), individu(individu)
{
  vector<Noeud *> tempNoeud; 
  vector<Route *> tempRoute;

  vector<bool> tempB2;
  vector<vector<bool>> tempB;
  vector<vector<int>> temp;
  vector<int> temp2;
  vector<vector<paireJours>> tempPair;
  vector<paireJours> tempPair2;
  Noeud *myDepot;
  Noeud *myDepotFin;
  Route *myRoute;

  clients.push_back(tempNoeud);
  depots.push_back(tempNoeud);
  depotsFin.push_back(tempNoeud);
  routes.push_back(tempRoute);

  for (int kk = 1; kk <= params->nbDays; kk++)
  {
    clients.push_back(tempNoeud);
    depots.push_back(tempNoeud);
    depotsFin.push_back(tempNoeud);
    routes.push_back(tempRoute);
    // dimensionnement du champ noeuds a la bonne taille
    for (int i = 0; i < params->nbDepots; i++)
      clients[kk].push_back(NULL);
    for (int i = params->nbDepots; i < params->nbClients + params->nbDepots;
         i++)
      clients[kk].push_back(
          new Noeud(false, i, kk, false, NULL, NULL, NULL, 0));

    // dimensionnement du champ depots et routes � la bonne taille

    for (int i = 0; i < params->nombreVehicules[kk]; i++)
    {
      myDepot = new Noeud(true, params->ordreVehicules[kk][i].depotNumber, kk,
                          false, NULL, NULL, NULL, 0);
      myDepotFin = new Noeud(true, params->ordreVehicules[kk][i].depotNumber,
                             kk, false, NULL, NULL, NULL, 0);
      myRoute = new Route(
          i, kk, myDepot, 0, 0, params->ordreVehicules[kk][i].maxRouteTime,
          params->ordreVehicules[kk][i].vehicleCapacity, params, this);
      myDepot->route = myRoute;
      myDepotFin->route = myRoute;
      routes[kk].push_back(myRoute);
      depots[kk].push_back(myDepot);
      depotsFin[kk].push_back(myDepotFin);
    }
  }

  // initialisation de la structure ordreParcours 
  for (int day = 0; day <= params->nbDays; day++)
    ordreParcours.push_back(temp2);

  for (int i = params->nbDepots; i < params->nbDepots + params->nbClients; i++)
    ordreParcours[0].push_back(i);

  // initialisation de la structure ordreJours
  for (int day = 1; day <= params->nbDays; day++)
    ordreJours.push_back(day);
}


// destructeur
LocalSearch::~LocalSearch(void)
{
  if (!clients.empty())
    for (int i = 0; i < (int)clients.size(); i++)
      if (!clients[i].empty())
        for (int j = 0; j < (int)clients[i].size(); j++)
          delete clients[i][j];

  if (!routes.empty())
    for (int i = 0; i < (int)routes.size(); i++)
      if (!routes[i].empty())
        for (int j = 0; j < (int)routes[i].size(); j++)
          delete routes[i][j];

  if (!depots.empty())
    for (int i = 0; i < (int)depots.size(); i++)
      if (!depots[i].empty())
        for (int j = 0; j < (int)depots[i].size(); j++)
          delete depots[i][j];
}
