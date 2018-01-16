#include "rocket.h"

using namespace bc;
using namespace std;

int launchedWorkerCount;

vector<vector<double>> mars_karbonite_map(int time) {
    auto& marsMap = gc.get_starting_planet(Mars);
    int w = marsMap.get_width();
    int h = marsMap.get_height();
    vector<vector<double>> res (w, vector<double>(h, 0.0));

    // Just in case some karbonite actually exists at mars at start
    for (int x = 0; x < w; x++) {
        for (int y = 0; y < w; y++) {
            res[x][y] += marsMap.get_initial_karbonite_at(MapLocation(Mars, x, y));
        }
    }

    auto& asteroids = gc.get_asteroid_pattern();
    for (int t = 0; t < time; t++) {
        if (asteroids.has_asteroid_on_round(t)) {
            auto asteroid = asteroids.get_asteroid_on_round(t);
            auto loc = asteroid.get_map_location();
            res[loc.get_x()][loc.get_y()] += asteroid.get_karbonite();
        }
    }

    return res;
}

pair<bool,MapLocation> find_best_landing_spot() {
    cout << "Finding landing spot" << endl;
    auto& marsMap = gc.get_starting_planet(Mars);
    
    int w = marsMap.get_width();
    int h = marsMap.get_height();
    auto karb = mars_karbonite_map(gc.get_round() + 100);
    vector<vector<bool>> searched (w, vector<bool>(h, false));

    float bestScore = -1;
    MapLocation bestLandingSpot = MapLocation(Earth, 0, 0);

    for (int x = 0; x < w; x++) {
        for (int y = 0; y < w; y++) {
            if (searched[x][y]) break;

            if (marsMap.is_passable_terrain_at(MapLocation(Mars, x, y))) {
                stack<pii> que;
                que.push(pii(x,y));
                int totalResources = 0;
                int totalArea = 0;

                pii bestInRegion = pii(x, y);
                float inRegionScore = -1000000;
                float inRegionWeight = 0;

                while(!que.empty()) {
                    auto p = que.top();
                    que.pop();
                    totalResources += karb[x][y];
                    totalArea += 1;

                    float nodeScore = -karb[x][y];
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
                            int nx = x + dx;
                            int ny = y + dy;
                            if (nx >= 0 && ny >= 0 && nx < w && ny < h && !searched[nx][ny]) {
                                searched[nx][ny] = true;
                                if (marsMap.is_passable_terrain_at(MapLocation(Mars, nx, ny))) {
                                    que.push(pii(nx, ny));
                                }
                            }
                        }
                    }
                }

                float score = totalResources + totalArea * 0.001f;
                if (score > bestScore) {
                    bestScore = score;
                    bestLandingSpot = MapLocation(Mars, bestInRegion.first, bestInRegion.second);
                }
            }
        }
    }
    
    cout << "Found " << bestLandingSpot.get_x() << " " << bestLandingSpot.get_y() << endl;
    return make_pair(bestScore > 0, bestLandingSpot);
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
        if (unit.get_structure_garrison().size() == unit.get_structure_max_capacity() || gc.get_round() == 749 || (workerCount && !launchedWorkerCount)) {
            auto res = find_best_landing_spot();
            if (res.first) {
                if (gc.can_launch_rocket(unit.get_id(), res.second)) {
                    gc.launch_rocket(unit.get_id(), res.second);
                    invalidate_units();
                    launchedWorkerCount += workerCount;
                }
            }
        }
    }

}
