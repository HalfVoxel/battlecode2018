#include "rocket.h"
#include "pathfinding.hpp"
using namespace bc;
using namespace std;

int launchedWorkerCount;
int countRocketsSent = 0;

vector<vector<double>> mars_karbonite_map(int time) {
    auto& marsMap = gc.get_starting_planet(Mars);
    int w = marsMap.get_width();
    int h = marsMap.get_height();
    vector<vector<double>> res (w, vector<double>(h, 0.0));

    // Just in case some karbonite actually exists at mars at start
    for (int x = 0; x < w; x++) {
        for (int y = 0; y < h; y++) {
            res[x][y] += marsMap.get_initial_karbonite_at(MapLocation(Mars, x, y));
        }
    }

    auto& asteroids = gc.get_asteroid_pattern();
    for (int t = 0; t < time; t++) {
        if (asteroids.has_asteroid_on_round(t)) {
            auto asteroid = asteroids.get_asteroid_on_round(t);
            auto loc = asteroid.get_map_location();
            // Note: Assumes mars and earth have the same size
            if (isOnMap(loc)) {
                res[loc.get_x()][loc.get_y()] += asteroid.get_karbonite();
            }
        }
    }

    return res;
}

bool reasonableTimeToLaunchRocket () {
    auto& orbit = gc.get_orbit_pattern();
    double derivative = orbit.get_amplitude() * (2*M_PI / orbit.get_period()) * cos(gc.get_round() * (2*M_PI / orbit.get_period()));
    // Only launch if the travel time will not be reduced by more than 1 turn by simply waiting 1 turn.
    return derivative >= -1;
}

map<int, int> visitedMarsRegions;
PathfindingMap dontLandSpots(MAX_MAP_SIZE,MAX_MAP_SIZE);

tuple<bool,MapLocation,int> find_best_landing_spot() {
    cout << "Finding landing spot" << endl;
    auto& marsMap = gc.get_starting_planet(Mars);
    
    int w = marsMap.get_width();
    int h = marsMap.get_height();
    auto karb = mars_karbonite_map(gc.get_round() + 100);
    vector<vector<bool>> searched (w, vector<bool>(h, false));

    float bestScore = -1;
    int bestRegion = 0;
    MapLocation bestLandingSpot = MapLocation(Earth, 0, 0);

    int region = 0;
    for (int x = 0; x < w; x++) {
        for (int y = 0; y < h; y++) {
            if (searched[x][y]) continue;

            if (marsMap.is_passable_terrain_at(MapLocation(Mars, x, y))) {
                // Note: assumes that regions never change!!
                // Otherwise the region IDs will get messed up
                region++;

                stack<pii> que;
                que.push(pii(x,y));
                searched[x][y] = true;
                int totalResources = 0;
                int totalArea = 0;

                pii bestInRegion = pii(x, y);
                float inRegionScore = -1000000;
                float inRegionWeight = 0;

                while(!que.empty()) {
                    auto p = que.top();
                    que.pop();
                    totalResources += karb[p.first][p.second];
                    totalArea += 1;

                    float nodeScore = -karb[p.first][p.second] - dontLandSpots.weights[p.first][p.second];
                    if (nodeScore > inRegionScore) {
                        inRegionScore = nodeScore;
                        bestInRegion = p;
                        inRegionWeight = 1;
                    } else if (nodeScore == inRegionScore) {
                        float weight = 1;
                        inRegionWeight += weight;
                        if (((rand() % 100000)/100000.0f) * inRegionWeight < weight) {
                            bestInRegion = p;
                        }
                    }

                    for (int dx = -1; dx <= 1; dx++) {
                        for (int dy = -1; dy <= 1; dy++) {
                            int nx = p.first + dx;
                            int ny = p.second + dy;
                            if (nx >= 0 && ny >= 0 && nx < w && ny < h && !searched[nx][ny]) {
                                searched[nx][ny] = true;
                                if (marsMap.is_passable_terrain_at(MapLocation(Mars, nx, ny))) {
                                    que.push(pii(nx, ny));
                                }
                            }
                        }
                    }
                }

                int timesVisitedPreviously = visitedMarsRegions[region];
                float score = (totalResources + totalArea * 0.1f) / ((dontLandSpots.weights[bestInRegion.first][bestInRegion.second] + 1) * (1 + timesVisitedPreviously));
                cout << "Area: " << totalArea << " Resources: " << totalResources << " best spot " << bestInRegion.first << " " << bestInRegion.second << " with score " << inRegionScore <<  " => " << score << endl;
                if (score > bestScore) {
                    bestScore = score;
                    bestRegion = region;
                    bestLandingSpot = MapLocation(Mars, bestInRegion.first, bestInRegion.second);
                }
            }
        }
    }
    
    cout << "Found " << bestLandingSpot.get_x() << " " << bestLandingSpot.get_y() << endl;
    return make_tuple(bestScore > 0, bestLandingSpot, bestRegion);
}

void BotRocket::tick() {
    if (!unit.get_location().is_on_map()) {
        return;
    }
    if (!unit.structure_is_built()) {
        return;
    }
    hasDoneTick = true;
    if(gc.get_planet() == Mars) {
        auto garrison = unit.get_structure_garrison();
        for (size_t i = 0; i < garrison.size(); i++) {
            if (!unloadFrontUnit()) {
                break;
            }
        }
    } else {
        int workerCount = 0;
        for (auto u : unit.get_structure_garrison()) {
            if (gc.get_unit(u).get_unit_type() == Worker) {
                ++workerCount;
            }
        }
        unsigned int countInGarrison = unit.get_structure_garrison().size();
        if ((reasonableTimeToLaunchRocket() && (countInGarrison == unit.get_structure_max_capacity() || (workerCount && !launchedWorkerCount))) || gc.get_round() == 749 || (countInGarrison > 0 && unit.get_health() < unit.get_max_health()*0.6)) {
            bool anyLandingSpot;
            MapLocation landingSpot;
            int marsRegion;
            tie(anyLandingSpot, landingSpot, marsRegion) = find_best_landing_spot();
            if (anyLandingSpot) {
                if (gc.can_launch_rocket(unit.get_id(), landingSpot)) {
                    visitedMarsRegions[marsRegion]++;
                    // Heavily discourage ladning in the same 3x3 region as this rocket.
                    double reductionFactor = 10;
                    dontLandSpots.addInfluence(vector<vector<double>>(3, vector<double>(3, reductionFactor)), landingSpot.get_x(), landingSpot.get_y());
                    gc.launch_rocket(unit.get_id(), landingSpot);
                    invalidate_units();
                    launchedWorkerCount += workerCount;
                }
            }
        }
    }

}
