#include "Params.h"
#include <algorithm>

namespace {

bool coordLess(const couple &lhs, const couple &rhs)
{
	if (lhs.x != rhs.x)
		return lhs.x < rhs.x;
	return lhs.y < rhs.y;
}

double crossProduct(const couple &origin, const couple &pointA, const couple &pointB)
{
	return (pointA.x - origin.x) * (pointB.y - origin.y) - (pointA.y - origin.y) * (pointB.x - origin.x);
}

double squaredDistance(const couple &lhs, const couple &rhs)
{
	double dx = lhs.x - rhs.x;
	double dy = lhs.y - rhs.y;
	return dx * dx + dy * dy;
}

double computeClusterDiameter(const vector<couple> &points)
{
	if (points.size() <= 1)
		return 0.;

	vector<couple> sortedPoints = points;
	std::sort(sortedPoints.begin(), sortedPoints.end(), coordLess);

	vector<couple> hull;
	hull.reserve(sortedPoints.size() * 2);
	for (const couple &point : sortedPoints)
	{
		while (hull.size() >= 2 && crossProduct(hull[hull.size() - 2], hull[hull.size() - 1], point) <= 0.)
			hull.pop_back();
		hull.push_back(point);
	}

	size_t lowerHullSize = hull.size();
	for (int idx = (int)sortedPoints.size() - 2; idx >= 0; idx--)
	{
		const couple &point = sortedPoints[idx];
		while (hull.size() > lowerHullSize && crossProduct(hull[hull.size() - 2], hull[hull.size() - 1], point) <= 0.)
			hull.pop_back();
		hull.push_back(point);
	}

	if (hull.size() > 1)
		hull.pop_back();

	double bestSquaredDistance = 0.;
	for (size_t i = 0; i < hull.size(); i++)
		for (size_t j = i + 1; j < hull.size(); j++)
			bestSquaredDistance = std::max(bestSquaredDistance, squaredDistance(hull[i], hull[j]));

	return sqrt(bestSquaredDistance);
}

}

// creating the parameters from the instance file
Params::Params(string nomInstance, string nomSolution, int type, int nbVeh, string nomBKS, int seedRNG, int rou,bool stockout) : 
	type(type), nbVehiculesPerDep(nbVeh)
{
	seed = seedRNG;

	if (seed == 0)
		rng = new Rng((unsigned long long)time(NULL));
	else
		rng = new Rng((unsigned long long)(seed));

	pathToInstance = nomInstance;
	pathToSolution = nomSolution;
	pathToBKS = nomBKS;

	debut = clock();
	ticks = 0; // will be set by main before Genetic::evolve
	//PItime = 0;
	nbVehiculesPerDep = nbVeh;

	// ouverture du fichier en lecture
	fichier.open(nomInstance.c_str());

	// parsing des donn�es
	// Initialize speedKmh before parsing, since getClient() uses it for TW conversion
	speedKmh = 40.0;
	if (fichier.is_open())
		preleveDonnees(nomInstance, rou, stockout);
	else
	{
		cout << "Unable to open file : " << nomInstance << endl;
		throw string(" Unable to open file ");
	}
	cout << "Read file done" << endl;

	// Setting the parameters
	setMethodParams();
	
	// Simply compute the distances from the coordinates
	computeDistancierFromCoords();

	// calcul des structures
	calculeStructures();

	// Compute the constant value in the objective function
	if(stockout) computeConstant_stockout();
	else computeConstant();
	
}

Params::Params(string nomInstance, string nomSolution, int type, int nbVeh, string nomBKS, int seedRNG) : 
	type(type), nbVehiculesPerDep(nbVeh)
{
	seed = seedRNG;

	if (seed == 0)
		rng = new Rng((unsigned long long)time(NULL));
	else
		rng = new Rng((unsigned long long)(seed));

	pathToInstance = nomInstance;
	pathToSolution = nomSolution;
	pathToBKS = nomBKS;

	debut = clock();
	ticks = 0;
	nbVehiculesPerDep = nbVeh;

	// ouverture du fichier en lecture
	fichier.open(nomInstance.c_str());

	// parsing des donn�es
	speedKmh = 40.0; // Initialize before parsing (getClient uses it for TW conversion)
	if (fichier.is_open())
		preleveDonnees(nomInstance, 0,0);
	else
	{
		cout << "Unable to open file : " << nomInstance << endl;
		throw string(" Unable to open file ");
	}
	cout << "read file done" << endl;

	// Setting the parameters
	setMethodParams();

	// Simply compute the distances from the coordinates
	computeDistancierFromCoords();

	// calcul des structures
	calculeStructures();

	// Compute the constant value in the objective function
	computeConstant();

	// for instances 27-32 of the PVRP, there is a small patch to avoid errors because the objective function
	// is of a very different magnitude, need to give a more adapted starting value for the penalties
	
}

void Params::computeConstant()
{
	objectiveConstant = 0;


	if (isInventoryRouting)
	{

		// Removing the customer inventory cost once the product has been consumed (CONSTANT IN OBJECTIVE)
		for (int k = 1; k <= ancienNbDays; k++)
			for (int i = nbDepots; i < nbDepots + nbClients; i++)
				objectiveConstant -= cli[i].dailyDemand[k] * (ancienNbDays + 1 - k) * cli[i].inventoryCost;

		// Adding the cost of the initial inventory at the customer location (CONSTANT IN OBJECTIVE)
		for (int i = nbDepots; i < nbDepots + nbClients; i++)
			objectiveConstant += cli[i].startingInventory * (ancienNbDays + 1 - 1) * cli[i].inventoryCost;

		// Adding the total cost for supplier inventory over the planning horizon (CONSTANT IN OBJECTIVE)
		if (multiDepot && !availableSupplyPerDepot.empty())
		{
			for (int d = 0; d < nbDepots; d++)
				for (int k = 1; k <= ancienNbDays; k++)
					objectiveConstant += availableSupplyPerDepot[d][k] * (ancienNbDays + 1 - k) * inventoryCostPerDepot[d];
		}
		else
		{
			for (int k = 1; k <= ancienNbDays; k++)
				objectiveConstant += availableSupply[k] * (ancienNbDays + 1 - k) * inventoryCostSupplier;
		}

	}
	
}

void Params::computeDistancierFromCoords()
{
	double d;
	vector<double> dist;

	// on remplit les distances dans timeCost
	for (int i = 0; i < nbClients + nbDepots; i++)
	{
		dist.clear();
		for (int j = 0; j < nbClients + nbDepots; j++)
		{
			if (type == 39) // Haversine distance for lat/long coordinates, result in km
			{
				double lat1 = cli[i].coord.y * M_PI / 180.0;
				double lat2 = cli[j].coord.y * M_PI / 180.0;
				double dLat = (cli[j].coord.y - cli[i].coord.y) * M_PI / 180.0;
				double dLon = (cli[j].coord.x - cli[i].coord.x) * M_PI / 180.0;
				double a = sin(dLat/2) * sin(dLat/2) +
				           cos(lat1) * cos(lat2) * sin(dLon/2) * sin(dLon/2);
				double c = 2 * atan2(sqrt(a), sqrt(1-a));
				d = 6371.0 * c; // Earth radius in km
			}
			else
			{
				d = sqrt((cli[i].coord.x - cli[j].coord.x) * (cli[i].coord.x - cli[j].coord.x) +
						 (cli[i].coord.y - cli[j].coord.y) * (cli[i].coord.y - cli[j].coord.y));
			}

			// integer rounding
			if (isRoundingInteger) // to be able to deal with other rounding conventions
			{
				d += 0.5;
				d = (double)(int)d;
			}
			else if (isRoundingTwoDigits)
			{
				d = d * 100.0;
				d += 0.5;
				d = (double)(int)d;
				d = d * 0.01;
			}

			dist.push_back((double)d);
		}
		timeCost.push_back(dist);
	}
}

void Params::setMethodParams()
{
	// parameters related to how the problem is treated
	conversionToPVRP = true;
	triCentroides = false;	  // is the chromosome ordered by barycenter of the routes (CVRP case, c.f. OR2012 paper)
	isRoundingInteger = true; // using the rounding (for now set to true, because currently testing on the instances of Uchoa)
	isRoundingTwoDigits = false;

	// parameters of the population
	el = 3;					// ***
	mu = 12;				// *
	lambda = 25;			// *
	//PItime=0;
	nbCountDistMeasure = 5; // o
	rho = 0.30;				// o
	delta = 0.001;			// o

	// parameters of the mutation
	maxLSPhases = 10000; // number of LS phases, here RI-PI-RI  // ***
	prox = 40;			 // granularity parameter (expressed as a percentage of the problem size -- 35%) // ***
	proxCst = 1000000;	 // granularity parameter (expressed as a fixed maximum)
	prox2Cst = 1000000;	 // granularity parameter on PI
	pRep = 0.5;			 // probability of repair // o

	// param�tres li�s aux p�nalit�s adaptatives
	// penalityCapa = max(10,seed*50);	// Initial penalty values // o
	penalityCapa = 50;
	penalityLength = 10; // Initial penalty values // o
	minValides = 0.2;	// Target range for the number of feasible solutions // **
	maxValides = 0.25;	// Target range for the number of feasible solutions // **
	distMin = 0.01;		// Distance in terms of objective function under which the solutions are considered to be the same // o
	borneSplit = 2.0;	// Split parameter (how far to search beyond the capacity limit) // o
	borderlineFactor = 0.15;

	// necessary adjustments for the CVRP (cf. OR2012)
	if (!isInventoryRouting)
	{
		maxLSPhases = 1;
		triCentroides = true;
		proxCst = 20;
		prox2Cst = 200;
	}

	// Default time window penalty
	if (!hasTimeWindows)
		penalityTimeWindow = 0;

	// Default inventory penalty (for IRP)
	penalityInventory = 100;

	// Type 39 uses Haversine distances - no integer rounding
	if (type == 39)
	{
		isRoundingInteger = false;
		isRoundingTwoDigits = false;
	}

	// Speed for time-distance conversion (type 39)
	speedKmh = 40.0;
}

Params::~Params(void) {}

// effectue le prelevement des donnees du fichier
void Params::preleveDonnees(string nomInstance, int rou, bool stockout)
{
	// variables temporaires utilisees dans la fonction
	vector<Client> cTemp;
	vector<Vehicle> tempI;
	Client client;
	double rt, vc;
	string contenu;
	string useless2;
	multiDepot = false;
	periodique = false;
	isInventoryRouting = false;
	isstockout = false;
	hasTimeWindows = false;
	penalityTimeWindow = 1000;
	//C. Archetti, L. Bertazzi, G. Laporte, and M.G. Speranza. A branch-and-cut algorithm for a vendor-managed inventory-routing problem. Transportation Science, 41:382-391, 2007 Instances

	if (type == 38) // IRP format of Archetti http://or-brescia.unibs.it/instances 
	{
		cout << "path: " << nomInstance << endl;
		isInventoryRouting = true;
		isstockout = stockout;
		cout<<"isstockout "<<isstockout<<endl;
		// nbVehiculesPerDep = 2;
		if (nbVehiculesPerDep == -1)
		{
			cout << "ERROR : Need to specify fleet size" << endl;
			throw string("ERROR : Need to specify fleet size");
		}
		fichier >> nbClients;
		nbClients--; // the given number of nodes also counts the supplier/depot
		fichier >> nbDays;
		fichier >> vc; //vehicle capacityx
		rt = 1000000;
		nbDepots = 1;
		ancienNbDays = nbDays;

		ordreVehicules.push_back(tempI);
		nombreVehicules.push_back(0);
		dayCapacity.push_back(0);
		for (int kk = 1; kk <= nbDays; kk++)
		{
			ordreVehicules.push_back(tempI);
			dayCapacity.push_back(0);
			nombreVehicules.push_back(nbDepots * nbVehiculesPerDep);
			for (int i = 0; i < nbDepots; i++)
				for (int j = 0; j < nbVehiculesPerDep; j++)
					ordreVehicules[kk].push_back(Vehicle(-1, -1, -1));
		}

		for (int kk = 1; kk <= nbDays; kk++)
		{
			for (int j = 0; j < nbVehiculesPerDep; j++)
			{
				ordreVehicules[kk][j].depotNumber = 0;
				ordreVehicules[kk][j].maxRouteTime = rt;
				ordreVehicules[kk][j].vehicleCapacity = vc;
				dayCapacity[kk] += vc;
			}
		}
	}

	// Multi-depot IRP with Time Windows format
	else if (type == 39)
	{
		cout << "path: " << nomInstance << endl;
		isInventoryRouting = true;
		multiDepot = true;
		hasTimeWindows = true;
		isstockout = stockout;
		cout << "isstockout " << isstockout << endl;
		isRoundingInteger = false;
		isRoundingTwoDigits = false;

		// Line 1: nbRetailers nbDays vehicleCapacity nbDepots
		fichier >> nbClients;
		fichier >> nbDays;
		double vc_read;
		fichier >> vc_read;
		fichier >> nbDepots;
		ancienNbDays = nbDays;
		rt = 1000000;

		if (nbVehiculesPerDep == -1)
		{
			cout << "ERROR : Need to specify fleet size (vehicles per depot)" << endl;
			throw string("ERROR : Need to specify fleet size");
		}

		int totalVehicles = nbDepots * nbVehiculesPerDep;
		ordreVehicules.push_back(tempI);
		nombreVehicules.push_back(0);
		dayCapacity.push_back(0);
		for (int kk = 1; kk <= nbDays; kk++)
		{
			ordreVehicules.push_back(tempI);
			dayCapacity.push_back(0);
			nombreVehicules.push_back(totalVehicles);
			for (int i = 0; i < nbDepots; i++)
				for (int j = 0; j < nbVehiculesPerDep; j++)
					ordreVehicules[kk].push_back(Vehicle(-1, -1, -1));
		}

		for (int kk = 1; kk <= nbDays; kk++)
		{
			for (int i = 0; i < nbDepots; i++)
			{
				for (int j = 0; j < nbVehiculesPerDep; j++)
				{
					int vIdx = i * nbVehiculesPerDep + j;
					ordreVehicules[kk][vIdx].depotNumber = i;
					ordreVehicules[kk][vIdx].maxRouteTime = rt;
					ordreVehicules[kk][vIdx].vehicleCapacity = vc_read;
					dayCapacity[kk] += vc_read;
				}
			}
		}
	}

	// format classique de VRP Cordeau
	else if (type == 0)
	{
		// premiere ligne: description du probleme
		fichier >> type >> nbVehiculesPerDep >> nbClients;
		fichier >> nbDays;
		nbDepots = 1;
		ancienNbDays = nbDays;

		ordreVehicules.push_back(tempI);
		nombreVehicules.push_back(0);
		dayCapacity.push_back(0);
		for (int kk = 1; kk <= nbDays; kk++)
		{
			ordreVehicules.push_back(tempI);
			dayCapacity.push_back(0);
			nombreVehicules.push_back(nbDepots * nbVehiculesPerDep);
			for (int i = 0; i < nbDepots; i++)
				for (int j = 0; j < nbVehiculesPerDep; j++)
					ordreVehicules[kk].push_back(Vehicle(-1, -1, -1));
		}

		// caracteristiques des vehicules , dans l'ordre depot1*day1 ... depot1*day2 ...
		for (int i = 0; i < nbDepots; i++)
		{
			for (int kk = 1; kk <= nbDays; kk++)
			{
				fichier >> rt >> vc;
				if (rt == 0)
					rt = 100000;
				for (int j = 0; j < nbVehiculesPerDep; j++)
				{
					ordreVehicules[kk][nbVehiculesPerDep * i + j].depotNumber = i;
					ordreVehicules[kk][nbVehiculesPerDep * i + j].maxRouteTime = rt;
					ordreVehicules[kk][nbVehiculesPerDep * i + j].vehicleCapacity = vc;
					dayCapacity[kk] += vc;
				}
			}
		}

		// Filling the data of the IRP to be able to solve the CVRP as a special case
		availableSupply.push_back(0.);
		availableSupply.push_back(1.e30); // all supply needed on day 1
		inventoryCostSupplier = 0.;
	}
	
	// Liste des clients
	for (int i = 0; i < nbClients + nbDepots; i++)
	{
		Client client = getClient(i,rou);
		
		cli.push_back(client);
	}
}

// calcule les autres structures du programme
void Params::calculeStructures()
{
	int temp;
	vector<bool> tempB2;
	vector<vector<bool>> tempB;
	vector<double> dist;
	float distanceMax = 0;

	// initializing the correlation matrix
	for (int i = 0; i < nbClients + nbDepots; i++)
	{
		isCorrelated1.push_back(tempB2);
		isCorrelated2.push_back(tempB2);
		for (int j = 0; j < nbClients + nbDepots; j++)
		{
			isCorrelated1[i].push_back(false);
			isCorrelated2[i].push_back(false);
		}
	}

	for (int i = 0; i < nbClients + nbDepots; i++)
	{
		cli[i].ordreProximite.clear();
		cli[i].sommetsVoisins.clear();
	}

	// on remplit la liste des plus proches pour chaque client
	for (int i = nbDepots; i < nbClients + nbDepots; i++)
	{
		for (int j = 0; j < nbClients + nbDepots; j++)
			if (i != j)
				cli[i].ordreProximite.push_back(j); 

		// et on la classe
		for (int a1 = 0; a1 < nbClients + nbDepots - 1; a1++)
			for (int a2 = 0; a2 < nbClients + nbDepots - a1 - 2; a2++)
				if (timeCost[i][cli[i].ordreProximite[a2]] > timeCost[i][cli[i].ordreProximite[a2 + 1]])
				{
					temp = cli[i].ordreProximite[a2 + 1];
					cli[i].ordreProximite[a2 + 1] = cli[i].ordreProximite[a2];
					cli[i].ordreProximite[a2] = temp;
				}

		// on remplit les x% plus proches
		for (int j = 0; j < (prox * (int)cli[i].ordreProximite.size()) / 100 && j < proxCst; j++)
		{
			cli[i].sommetsVoisins.push_back(cli[i].ordreProximite[j]);
			isCorrelated1[i][cli[i].ordreProximite[j]] = true;
		}
		for (int j = 0; j < (int)cli[i].ordreProximite.size() && j < prox2Cst; j++)
			isCorrelated2[i][cli[i].ordreProximite[j]] = true;
	}

	if (multiDepot && nbDepots > 1)
	{
		vector<vector<couple>> clusterPoints(nbDepots);

		for (int i = nbDepots; i < nbClients + nbDepots; i++)
		{
			vector<int> depotOrder;
			for (int d = 0; d < nbDepots; d++)
				depotOrder.push_back(d);
			std::sort(depotOrder.begin(), depotOrder.end(), [&](int lhs, int rhs) {
				return timeCost[i][lhs] < timeCost[i][rhs];
			});

			cli[i].distanceToDepot.assign(nbDepots, 0.);
			cli[i].candidateDepots.clear();
			for (int rank = 0; rank < nbDepots; rank++)
				cli[i].distanceToDepot[depotOrder[rank]] = timeCost[i][depotOrder[rank]];

			cli[i].preferredDepot = depotOrder.front();
			cli[i].candidateDepots.push_back(cli[i].preferredDepot);
			cli[i].isBorderline = false;
			clusterPoints[cli[i].preferredDepot].push_back(cli[i].coord);
		}

		vector<double> hullDiagonal(nbDepots, 0.);
		for (int d = 0; d < nbDepots; d++)
			hullDiagonal[d] = computeClusterDiameter(clusterPoints[d]);

		for (int i = nbDepots; i < nbClients + nbDepots; i++)
		{
			vector<int> depotOrder;
			for (int d = 0; d < nbDepots; d++)
				depotOrder.push_back(d);
			std::sort(depotOrder.begin(), depotOrder.end(), [&](int lhs, int rhs) {
				return cli[i].distanceToDepot[lhs] < cli[i].distanceToDepot[rhs];
			});

			double threshold = std::max(1.0, borderlineFactor * std::max(1.0, hullDiagonal[cli[i].preferredDepot]));
			for (int rank = 1; rank < nbDepots; rank++)
			{
				int depotIdx = depotOrder[rank];
				if (cli[i].distanceToDepot[depotIdx] - cli[i].distanceToDepot[cli[i].preferredDepot] <= threshold)
				{
					cli[i].candidateDepots.push_back(depotIdx);
					cli[i].isBorderline = true;
				}
			}
		}
	}

	// on melange les proches
	shuffleProches();

	// on calcule les tableaux de pattern dynamiques
	for (int i = 0; i < (int)cli.size(); i++)
	{
		cli[i].computeVisitsDyn();
		cli[i].computeJourSuiv();
	}
}

// sous routine du prelevement de donnees
Client Params::getClient(int i,int rou)
{
	struct couple coordonnees;
	Client client;
	int nbPattern;
	pattern p;
	client.preferredDepot = 0;
	client.isBorderline = false;

	// file format of Cordeau et al.
	if (type == 0)
	{
		fichier >> client.custNum >> coordonnees.x >> coordonnees.y >> client.serviceDuration >> client.demand >> client.freq >> nbPattern;

		// Default time window (unconstrained)
		client.earliestTime = 0;
		client.serviceTime = client.serviceDuration;
		client.latestTime = 1.e20;

		// Filling the IRP fields to be able to preserve the CVRP as a special case of the IRP solver
		client.dailyDemand.push_back(0.);
		client.dailyDemand.push_back(client.demand);
		client.inventoryCost = 0.;
		client.startingInventory = 0.;
		client.minInventory = 0.;
		client.maxInventory = client.demand;

		client.coord = coordonnees;
		client.nbJours = nbDays;

		p.dep = 0;
		for (int j = 0; j < nbPattern; j++)
		{
			fichier >> p.pat;
			client.visits.push_back(p);
			client.visitsOrigin.push_back(p);
		}
	}
	else if (type == 38) // IRP format of Archetti et al.
	{
		fichier >> client.custNum;
		client.custNum--;
		fichier >> coordonnees.x >> coordonnees.y;
		client.serviceDuration = 0;
		client.coord = coordonnees;
		client.nbJours = nbDays;

		if (client.custNum == 0) // information of the supplier
		{
			client.freq = 0;
			double initInventory;
			double dailyProduction;
			fichier >> initInventory;
			fichier >> dailyProduction;
			availableSupply = vector<double>(nbDays + 1, 0.); // days are indexed from 1 ... t
			for (int t = 1; t <= nbDays; t++)
				availableSupply[t] = dailyProduction;
			availableSupply[1] += initInventory;
			fichier >> inventoryCostSupplier;
			client.earliestTime = 0;
			client.serviceTime = 0;
			client.latestTime = 1.e20;
		}
		else //information of each customer
		{
			client.freq = 1;
			fichier >> client.startingInventory;
			fichier >> client.maxInventory;
			fichier >> client.minInventory;
			double myDailyDemand;
			fichier >> myDailyDemand;
			client.dailyDemand = vector<double>(nbDays + 1, myDailyDemand);
			fichier >> client.inventoryCost;
			client.stockoutCost = client.inventoryCost*rou;
			client.earliestTime = 0;
			client.serviceTime = 0;
			client.latestTime = 1.e20;
		}

		client.demand = 1.e20; // Just to make sure I never rely on this field for the IRP for now (later on it will totally disappear)
		p.dep = 0;
		p.pat = 1;
		client.visits.push_back(p);
		client.visitsOrigin.push_back(p);
	}
	else if (type == 39) // Multi-depot IRP with time windows
	{
		fichier >> client.custNum;
		client.custNum--;
		fichier >> coordonnees.x >> coordonnees.y;
		client.coord = coordonnees;
		client.nbJours = nbDays;

		if (client.custNum < nbDepots) // depot information
		{
			client.freq = 0;
			client.serviceDuration = 0;
			double initInventory;
			double dailyProduction;
			double depotHoldingCost;
			fichier >> initInventory;
			fichier >> dailyProduction;
			fichier >> depotHoldingCost;

			// Initialize per-depot supply data
			if (availableSupplyPerDepot.empty())
			{
				availableSupplyPerDepot.resize(nbDepots);
				inventoryCostPerDepot.resize(nbDepots, 0.);
				// Also initialize the aggregate supply for backward compatibility
				availableSupply = vector<double>(nbDays + 1, 0.);
				inventoryCostSupplier = 0.;
			}

			int depotIdx = client.custNum;
			availableSupplyPerDepot[depotIdx] = vector<double>(nbDays + 1, 0.);
			for (int t = 1; t <= nbDays; t++)
				availableSupplyPerDepot[depotIdx][t] = dailyProduction;
			availableSupplyPerDepot[depotIdx][1] += initInventory;
			inventoryCostPerDepot[depotIdx] = depotHoldingCost;

			// Aggregate supply across depots for backward compatibility
			for (int t = 1; t <= nbDays; t++)
				availableSupply[t] += availableSupplyPerDepot[depotIdx][t];
			// Use the average depot holding cost as the aggregate
			inventoryCostSupplier = 0.;
			for (int d = 0; d <= depotIdx; d++)
				inventoryCostSupplier += inventoryCostPerDepot[d];
			inventoryCostSupplier /= (depotIdx + 1);

			// Depots don't have time windows
			client.earliestTime = 0;
			client.serviceTime = 0;
			client.latestTime = 1.e20;
		}
		else // retailer information
		{
			client.freq = 1;
			fichier >> client.startingInventory;
			fichier >> client.maxInventory;
			fichier >> client.minInventory;
			double myDailyDemand;
			fichier >> myDailyDemand;
			client.dailyDemand = vector<double>(nbDays + 1, myDailyDemand);
			fichier >> client.inventoryCost;
			client.stockoutCost = client.inventoryCost * rou;

			// Time window: a_i, s_i, b_i (in hours, convert to km-equivalent using speed)
			fichier >> client.earliestTime;
			fichier >> client.serviceTime;
			fichier >> client.latestTime;

			// Convert hours to km-equivalent: time_km = time_hours * speed_kmh
			// This ensures TW constraints are comparable with Haversine distances (in km)
			client.earliestTime *= speedKmh;
			client.serviceTime *= speedKmh;
			client.latestTime *= speedKmh;

			client.serviceDuration = client.serviceTime;
		}

		client.demand = 1.e20;
		p.dep = 0;
		p.pat = 1;
		client.visits.push_back(p);
		client.visitsOrigin.push_back(p);
	}
	return client;
}

void Params::shuffleProches()
{
	int temp, temp2;

	// on introduit du d�sordre dans la liste des plus proches pour chaque client
	for (int i = nbDepots; i < nbClients + nbDepots; i++)
	{
		// on introduit du desordre
		for (int a1 = 0; a1 < (int)cli[i].sommetsVoisins.size() - 1; a1++)
		{
			temp2 = a1 + rng->genrand64_int64() % ((int)cli[i].sommetsVoisins.size() - a1);
			temp = cli[i].sommetsVoisins[a1];
			cli[i].sommetsVoisins[a1] = cli[i].sommetsVoisins[temp2];
			cli[i].sommetsVoisins[temp2] = temp;
		}
	}
}

Params::Params(Params *params, int decom, int *serieVisites, Vehicle **serieVehicles, int *affectDepots, int *affectPatterns, int depot, int jour, int nbVisites, int nbVeh)
{
	debut = clock();
	rng = params->rng;
	type = params->type;
	seed = params->seed;

	/* For now I just kept the CVRP decomposition */
	// if (decom == 2) decomposeDepots(params,affectDepots,depot);
	// else if (decom == 1) decomposeDays(params,affectPatterns,jour);
	if (decom == 0)
		decomposeRoutes(params, serieVisites, serieVehicles, nbVisites, nbVeh);
	else
		cout << "Error : Transformation not available actually" << endl;

	periodique = params->periodique;
	multiDepot = params->multiDepot;
	isInventoryRouting = params->isInventoryRouting;
	if (decom == 2)
		multiDepot = false;
	if (decom == 1)
		periodique = false;

	// affectation des autres parametres
	setMethodParams();
	penalityCapa = params->penalityCapa;
	penalityLength = params->penalityLength;

	mu = params->mu;
	lambda = params->lambda;
	el = params->el;
	nbCountDistMeasure = params->nbCountDistMeasure;

	// calcul des distances
	// on remplit les distances dans timeCost
	for (int i = 0; i < nbClients + nbDepots; i++)
	{
		timeCost.push_back(vector<double>());
		for (int j = 0; j < nbClients + nbDepots; j++)
			timeCost[i].push_back(params->timeCost[correspondanceTable[i]][correspondanceTable[j]]);
	}

	// calcul des structures
	calculeStructures();
}

void Params::decomposeRoutes(Params *params, int *serieVisites, Vehicle **serieVehicles, int nbVisites, int nbVeh)
{
	vector<Vehicle> temp;

	if (params->multiDepot || params->periodique)
		cout << "Attention decomposition VRP incorrecte" << endl;

	correspondanceTable2.clear();
	for (int i = 0; i < params->nbClients + params->nbDepots; i++)
		correspondanceTable2.push_back(-1);

	correspondanceTable.push_back(0);

	for (int i = 0; i < nbVisites; i++)
	{
		correspondanceTable.push_back(serieVisites[i]);
		correspondanceTable2[serieVisites[i]] = (int)correspondanceTable.size() - 1;
	}

	nbVehiculesPerDep = nbVeh;
	nbClients = correspondanceTable.size() - 1;
	nbDepots = 1;
	nbDays = 1;
	ancienNbDays = 1;
	borneSplit = 1.5;
	type = params->type;

	// on place les donn�es sur les v�hicules
	ordreVehicules.clear();
	nombreVehicules.clear();
	dayCapacity.clear();
	ordreVehicules.push_back(temp);
	nombreVehicules.push_back(0);
	dayCapacity.push_back(0);
	ordreVehicules.push_back(temp);
	nombreVehicules.push_back(nbVeh);
	dayCapacity.push_back(0);
	for (int v = 0; v < nbVeh; v++)
	{
		ordreVehicules[1].push_back(*serieVehicles[v]);
		dayCapacity[1] += serieVehicles[v]->vehicleCapacity;
	}

	// on met les bons clients
	// ils ont toujours les anciens num�ros
	for (int i = 0; i < nbDepots + nbClients; i++)
		cli.push_back(params->cli[correspondanceTable[i]]);
}

void Params::computeConstant_stockout()
{
	objectiveConstant_stockout = 0.;

	if (isInventoryRouting)
	{
		// Adding the total cost for supplier inventory over the planning horizon (CONSTANT IN OBJECTIVE)
		if (multiDepot && !availableSupplyPerDepot.empty())
		{
			for (int d = 0; d < nbDepots; d++)
				for (int k = 1; k <= ancienNbDays; k++)
					objectiveConstant_stockout += availableSupplyPerDepot[d][k] * (ancienNbDays + 1 - k) * inventoryCostPerDepot[d];
		}
		else
		{
			for (int k = 1; k <= ancienNbDays; k++)
				objectiveConstant_stockout += availableSupply[k] * (ancienNbDays + 1 - k) * inventoryCostSupplier;
		}
	}
	
}