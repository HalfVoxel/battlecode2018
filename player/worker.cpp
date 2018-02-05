#include "worker.h"
#include "pathfinding.hpp"
#include "view.hpp"
#include "hungarian.h"
#include "maps.h"

using namespace bc;
using namespace std;

int miningSpeed = 3;
int buildSpeed = 5;
int repairSpeed = 10;
int debugRound = -1;
bool devsFixedReplicationBug = false;

struct KarboniteGroup {
    vector<pii> tiles;
    int totalValue;
};

vector<KarboniteGroup> groupKarbonite() {
    vector<vector<bool>> covered(w, vector<bool>(h));
    vector<vector<int>> groupIndices(w, vector<int>(h, -1));
    int maxTimeCostPerGroup = 30;
    vector<KarboniteGroup> groups;

    for (int x = 0; x < w; x++) {
        for (int y = 0; y < h; y++) {
            if (covered[x][y] || karboniteMap.weights[x][y] <= 0) continue;

            KarboniteGroup group;
            queue<pii> que1;
            queue<pii> que2;
            que1.push(pii(x, y));
            int timeCost = 0;
            vector<vector<bool>> covered2(w, vector<bool>(h));
            covered2[x][y] = true;

            while(timeCost < maxTimeCostPerGroup) {
                if (que1.empty()) {
                    swap(que1, que2);
                }
                if (que1.empty()) break;

                int timeToMine = (karboniteMap.weights[x][y] + (miningSpeed-1)) / miningSpeed;
                // Workers can move every second turn so we will have to spend at least 2 turns here
                timeToMine = max(timeToMine, 2);
                timeCost += timeToMine;
                
                pii p = que1.front();
                que1.pop();
                group.tiles.push_back(p);
                covered[p.first][p.second] = true;
                groupIndices[p.first][p.second] = groups.size();

                for (int dx = -1; dx <= 1; dx++) {
                    for (int dy = -1; dy <= 1; dy++) {
                        int nx = p.first + dx;
                        int ny = p.second + dy;
                        if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                        if (covered2[nx][ny] || covered[nx][ny]) continue;

                        // TODO: What about karbonite inside walls? That can be mined in some cases
                        if (isinf(passableMap.weights[nx][ny])) continue;

                        covered2[nx][ny] = true;

                        // Try to avoid adding non-karbonite tiles to the group if possible
                        if (karboniteMap.weights[nx][ny] == 0) {
                            que2.push(pii(nx,ny));
                        } else {
                            que1.push(pii(nx,ny));
                        }
                    }
                }
            }

            groups.push_back(group);
        }
    }

    if ((int)gc.get_round() == debugRound) {
        print({ 0, 0, w - 1, h - 1 }, colorsByID([&](int x, int y) { return groupIndices[x][y] + 1; }), labels([&](int x, int y) { return (int)karboniteMap.weights[x][y]; }));
        print({ 0, 0, w - 1, h - 1 }, colorsByID([&](int x, int y) { return groupIndices[x][y] + 1; }), labels([&](int x, int y) { return max(0, groupIndices[x][y]); }));
    }

    return groups;
}

double greedyWeightedMatching(vector<vector<double>>& costs, vector<int>& assignment) {
    assignment.resize(costs.size());
    vector<tuple<double,int,int>> costOrder;
    vector<bool> usedTargets(costs[0].size());
    for (int i = 0; i < (int)costs.size(); i++) {
        assignment[i] = -1;

        for (int j = 0; j < (int)costs[i].size(); j++) {
            costOrder.push_back(make_tuple(costs[i][j], i, j));
        }
    }
    double totalCost = 0;
    sort(costOrder.begin(), costOrder.end());
    for (auto tup : costOrder) {
        double cost;
        int i, j;
        tie(cost, i, j) = tup;

        if (assignment[i] != -1 || usedTargets[j]) continue;

        usedTargets[j] = true;
        assignment[i] = j;
        totalCost += cost;
    }
    return totalCost;
}

map<pair<int, int>, vector<vector<double> > > cachedTimeMaps;

void matchWorkers() {
    if (planet != Earth) return;

    auto matchWorkersStart = millis();

    // Cluster karbonite
    auto groups = groupKarbonite();
    
    // Find all our workers and structures which can be built or repaired
    vector<BotWorker*> workers;
    vector<Unit*> unitTargets;
    for (auto& u : ourUnits) {
        if (gc.has_unit(u.get_id()) && u.get_location().is_on_map()) {
            if ((u.get_unit_type() == Factory || u.get_unit_type() == Rocket) && u.get_health() < u.get_max_health()) {
                unitTargets.push_back(&u);
            }

            if (u.get_unit_type() == Worker) {
                auto* botWorker = (BotWorker*)unitMap[u.get_id()];
                assert(botWorker != nullptr);
                workers.push_back(botWorker);
            }
        }
    }

    // Note: Hungarian algorithm code will otherwise try to read out of bounds
    if (workers.size() == 0) return;

    HungarianAlgorithm matcher;
    Pathfinder pathfinder;
    int numTargets = groups.size()*3 + unitTargets.size()*5;

    // If there are no targets or if we have very little time
    // then fall back to the previous algorithm
    if (numTargets == 0 || lowTimeRemaining) {
        for (auto* worker : workers) {
            worker->calculatedTargetMap = PathfindingMap();
        }
        return;
    }

    const double INF = 1000000;
    vector<int> timeToReachTarget(numTargets, (int)INF);

    int numIterations = 2;

    auto t0 = millis();
    vector<vector<vector<double>>> distanceMaps(workers.size());
    for (int wi = 0; wi < (int)workers.size(); wi++) {
        auto* worker = workers[wi];
        auto pos = worker->unit.get_map_location();
        auto costMap = worker->getCostMap();
        distanceMaps[wi] = pathfinder.getDistanceToAllTiles(pos.get_x(), pos.get_y(), costMap);
    }
    matchWorkersDijkstraTime2 += millis() - t0;

    for (int it=0; it < numIterations; it++) {
        bool finalIteration = it == numIterations - 1;
        // costMatrix[i][j] = cost for worker i to be assigned target j
        // Note that scores will first be stored here and then the matrix values will be negated to convert them to costs
        vector<vector<double>> costMatrix (workers.size(), vector<double>(numTargets));
        // timeMatrix[i][j] = turns for worker i to reach target j
        vector<vector<int>> timeMatrix (workers.size(), vector<int>(numTargets));

        for (int wi = 0; wi < (int)workers.size(); wi++) {
            auto* worker = workers[wi];
            auto targetMap = worker->getOriginalTargetMap();
            auto pos = worker->unit.get_map_location();
            auto distanceStart = millis();
            auto& distanceMap = distanceMaps[wi];

            auto positionKey = make_pair(pos.get_x(), pos.get_y());
            vector<vector<double> >  timeMap;
            if (cachedTimeMaps.count(positionKey))
                timeMap = cachedTimeMaps[positionKey];
            else {
                // Workers take approximately 2 ticks to move one tile
                // TODO: Can optimize to simply 2 times BFS-distance
                PathfindingMap timeCost(w, h);
                timeCost += 2;
                for (int x = 0; x < w; x++) {
                    for (int y = 0; y < h; y++) {
                        if (isinf(passableMap.weights[x][y])) timeCost.weights[x][y] = passableMap.weights[x][y];
                    }
                }

                timeMap = pathfinder.getDistanceToAllTiles(pos.get_x(), pos.get_y(), timeCost);
                cachedTimeMaps[positionKey] = timeMap;
            }
            matchWorkersDijkstraTime += millis() - distanceStart;
            if ((int)gc.get_round() == debugRound && wi == 0) {
                
                // print({ 0, 0, w - 1, h - 1 }, 0, 60, [&](int x, int y) { return distanceToInitialLocation[0].weights[x][y]; });
                // print({ 0, 0, w - 1, h - 1 }, 0, 60, [&](int x, int y) { return distanceToInitialLocation[1].weights[x][y]; });
                // print({ 0, 0, w - 1, h - 1 }, 0, 60, [&](int x, int y) { return fuzzyKarboniteMap.weights[x][y]; });
                // print({ 0, 0, w - 1, h - 1 }, 0, 1, [&](int x, int y) { return ourStartingPositionMap.weights[x][y]; });

                

                // print({ 0, 0, w - 1, h - 1 }, 0, 150, [&](int x, int y) { return targetMap.weights[x][y]; });
                // print({ 0, 0, w - 1, h - 1 }, 0, 80, [&](int x, int y) { return timeMap[x][y]; });
                // print({ 0, 0, w - 1, h - 1 }, 0, 150, [&](int x, int y) { return distanceMap[x][y]; });
                // if (ourTeam == 1) exit(0);
            }

            for (int i = 0; i < (int)groups.size(); i++) {
                double score = 0;
                double totalKarbonite = 0;
                double minTime = INF;
                for (auto p : groups[i].tiles) {
                    minTime = min(minTime, timeMap[p.first][p.second]);
                    totalKarbonite += karboniteMap.weights[p.first][p.second];
                }
                if (minTime >= INF) {
                    costMatrix[wi][i*3 + 0] = -INF;
                    costMatrix[wi][i*3 + 1] = -INF;
                    costMatrix[wi][i*3 + 2] = -INF;
                    continue;
                }

                assert(totalKarbonite > 0);

                // How many ticks the best worker will have already mined at this spot before we get there
                double previousWork1 = max(0.0, minTime - timeToReachTarget[i*3 + 0]);
                // How many ticks the best and 2nd best worker will have spent here before the 3rd worker (us) gets there
                double previousWork2 = previousWork1 + max(0.0, minTime - timeToReachTarget[i*3 + 1]);

                for (auto p : groups[i].tiles) {
                    score = max(score, targetMap.weights[p.first][p.second] / (1 + distanceMap[p.first][p.second]));
                    minTime = min(minTime, timeMap[p.first][p.second]);
                }


                // TODO: Use for loop instead
                double score0 = score;
                double score1 = score * (totalKarbonite - miningSpeed*previousWork1)/totalKarbonite;
                double score2 = score * (totalKarbonite - miningSpeed*previousWork2)/totalKarbonite;

                score0 = max(score0, 0.0);
                score1 = max(score1, 0.0);
                score2 = max(score2, 0.0);

                costMatrix[wi][i*3 + 0] = score0;
                costMatrix[wi][i*3 + 1] = score1 / 2;
                costMatrix[wi][i*3 + 2] = score2 / 3;
                timeMatrix[wi][i*3 + 0] = minTime;
                timeMatrix[wi][i*3 + 1] = minTime;
                timeMatrix[wi][i*3 + 2] = minTime;

                if ((int)gc.get_round() == debugRound) {
                    cout << "Worker " << wi << " score for group " << i << ": " << score0 << " " << score1 << " " << score2 << endl;
                }
            }
            int offset = groups.size()*3;

            for (int i = 0; i < (int)unitTargets.size(); i++) {
                auto pos2 = unitTargets[i]->get_map_location();
                double score = 0;
                double minTime = INF;
                for (int dx = -1; dx <= 1; dx++) {
                    for (int dy = -1; dy <= 1; dy++) {
                        int nx = pos2.get_x() + dx;
                        int ny = pos2.get_y() + dy;
                        if (nx < 0 || nx < 0 || nx >= w || ny >= h) continue;
                        score = max(score, targetMap.weights[nx][ny] / (1 + distanceMap[nx][ny]));
                        minTime = min(minTime, timeMap[nx][ny]);
                    }
                }

                if (minTime >= INF) {
                    costMatrix[wi][offset + i*5 + 0] = -INF;
                    costMatrix[wi][offset + i*5 + 1] = -INF;
                    costMatrix[wi][offset + i*5 + 2] = -INF;
                    costMatrix[wi][offset + i*5 + 3] = -INF;
                    costMatrix[wi][offset + i*5 + 4] = -INF;
                    continue;
                }

                auto totalHealthToRepair = unitTargets[i]->get_max_health() - unitTargets[i]->get_health();
                // How many ticks the best worker will have already built at this spot before we get there
                double previousWork1 = max(0.0, minTime - timeToReachTarget[offset + i*3 + 0]);
                // How many ticks the best and 2nd best worker will have spent here before the 3rd worker (us) gets there
                double previousWork2 = previousWork1 + max(0.0, minTime - timeToReachTarget[offset + i*3 + 1]);

                // TODO: Slightly incorrect if repairing instead of building, should use repairSpeed
                double score0 = score;
                double score1 = score * (totalHealthToRepair - buildSpeed*previousWork1)/totalHealthToRepair;
                double score2 = score * (totalHealthToRepair - buildSpeed*previousWork2)/totalHealthToRepair;

                score0 = max(score0, 0.0);
                score1 = max(score1, 0.0);
                score2 = max(score2, 0.0);

                costMatrix[wi][offset + i*5 + 0] = score;
                costMatrix[wi][offset + i*5 + 1] = score * 0.8;
                costMatrix[wi][offset + i*5 + 2] = score * 0.6;
                costMatrix[wi][offset + i*5 + 3] = score * 0.4;
                costMatrix[wi][offset + i*5 + 4] = score * 0.2;
                timeMatrix[wi][offset + i*5 + 0] = minTime;
                timeMatrix[wi][offset + i*5 + 1] = minTime;
                timeMatrix[wi][offset + i*5 + 2] = minTime;
                timeMatrix[wi][offset + i*5 + 3] = minTime;
                timeMatrix[wi][offset + i*5 + 4] = minTime;
            }
        }


        // Invert cost matrix
        double mx = 0;
        for (int i = 0; i < (int)costMatrix.size(); i++) {
            for (auto v : costMatrix[i]) mx = max(mx, v);
        }

        for (int i = 0; i < (int)costMatrix.size(); i++) {
            for (auto& v : costMatrix[i]) v = mx - v;
        }

        auto hungarianStart = millis();
        vector<int> assignment;
        if (workers.size() < 30) {
            matcher.Solve(costMatrix, assignment);
        } else {
            greedyWeightedMatching(costMatrix, assignment);
        }
        hungarianTime += millis() - hungarianStart;
        assert(assignment.size() == workers.size());
        

        // Reset
        for (int i = 0; i < (int)timeToReachTarget.size(); i++) timeToReachTarget[i] = (int)INF;

        for (int wi = 0; wi < (int)assignment.size(); wi++) {
            // cout << "Worker " << wi << " goes to " << assignment[wi] << endl;
            auto* worker = workers[wi];
            int target = assignment[wi];

            if (target == -1 || costMatrix[wi][target] >= INF) {
                // No assigned target
                worker->calculatedTargetMap = PathfindingMap();
                continue;
            }

            timeToReachTarget[assignment[wi]] = timeMatrix[wi][assignment[wi]];
            // cout << "Time to reach group " << (assignment[wi]/3) << " + " << (assignment[wi] % 3) << " = " << timeToReachTarget[assignment[wi]] << endl;

            // Performance
            if (!finalIteration) continue;

            assert(target >= 0);
            PathfindingMap mask(w, h);
            int offset = groups.size()*3;

            if (target >= offset) {
                // Move towards a building
                target = (target - offset)/5;
                assert(target < (int)unitTargets.size());
                auto pos2 = unitTargets[target]->get_map_location();

                if ((int)gc.get_round() == debugRound) {
                    cout << "Worker " << wi << " goes to a building at " << pos2.get_x() << " " << pos2.get_y() << endl;
                }

                for (int dx = -1; dx <= 1; dx++) {
                    for (int dy = -1; dy <= 1; dy++) {
                        int nx = pos2.get_x() + dx;
                        int ny = pos2.get_y() + dy;
                        if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                        mask.weights[nx][ny] = 1;
                    }
                }
            } else {
                // Move towards a group
                target /= 3;
                assert(target < (int)groups.size());

                if ((int)gc.get_round() == debugRound) {
                    cout << "Worker " << wi << " goes to group " << target << " with cost " << costMatrix[wi][target] << endl;
                }

                for (auto p : groups[target].tiles) {
                    mask.weights[p.first][p.second] = 1;
                }
            }

            worker->calculatedTargetMap = mask * worker->getOriginalTargetMap();
        }
    }

    matchWorkersTime += millis() - matchWorkersStart;
    // cout << "Done" << endl;
    //if (gc.get_round() == debugRound) exit(0);
    // exit(0);
}

// Returns score for factory placement
// Will be on the order of magnitude of 1 for a well placed factory
// May be negative, but mostly in the [0,1] range
double structurePlacementScore(int x, int y, UnitType unitType) {
    double nearbyTileScore = 1;
    for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
            if (dx == 0 && dy == 0) continue;
            if (x + dx < 0 || x + dx >= w || y + dy <= 0 || y + dy >= h) continue;

            if (!isinf(passableMap.weights[x+dx][y+dy])) {
                // Traversable
                // structureProximityMap is typically 0.4 on the 8 tiles around a factory
                nearbyTileScore += 0.4 / (0.4 + structureProximityMap.weights[x+dx][y+dy]);
            }
        }
    }
    nearbyTileScore /= 16.0f;
    assert(nearbyTileScore <= 1.01f);
    assert(nearbyTileScore >= 0);

    auto pos = MapLocation(Earth, x, y);

    // nearbyTileScore will be approximately 1 if there are 8 free tiles around the factory.

    double score = nearbyTileScore;
    // Score will go to zero when there is more than 50 karbonite on the tile
    score -= karboniteMap.weights[x][y] / 50.0;
    
    score += sqrt(workerAdditiveMap.weights[x][y]) * 0.4;

    auto nearbyUnits = gc.sense_nearby_units(pos, 2);
    double nearbyStructures = 0;
    for (auto& f : nearbyUnits) {
        if (f.get_team() == ourTeam && (f.get_unit_type() == Rocket || f.get_unit_type() == Factory)) 
            nearbyStructures++;
    }

    if (unitType == Rocket) {
        score /= nearbyStructures + 1.0;
    }
    else {
        score /= nearbyStructures * 0.3 + 1.0;
    }

    // We like building factories in clusters (with some spacing)
    //if (nearbyFactories > 1) score *= 1.2f;
    //else if (nearbyFactories > 0) score *= 1.1f;

    // enemyNearbyMap is 1 at enemies and falls off slowly
    score /= enemyNearbyMap.weights[x][y] + 1.0;
    return score;
}

PathfindingMap BotWorker::getTargetMap() {
    if (calculatedTargetMap.weights.size() == 0) return getOriginalTargetMap();
    return calculatedTargetMap;
}

PathfindingMap BotWorker::getOriginalTargetMap() {
    bool isHurt = (unit.get_health() < unit.get_max_health());
    MapReuseObject reuseObject(MapType::Target, unit.get_unit_type(), isHurt);

    PathfindingMap targetMap;
    if (reusableMaps.count(reuseObject)) {
        targetMap = reusableMaps[reuseObject];
    }
    else {
        targetMap = fuzzyKarboniteMap + damagedStructureMap - enemyNearbyMap * 1.0 + 0.01 - structureProximityMap * 0.01;
        if (unit.get_health() < unit.get_max_health()) {
            for (auto& u : ourUnits) {
                if (u.get_unit_type() == Healer) {
                    if (!u.get_location().is_on_map()) {
                        continue;
                    }
                    auto pos = u.get_location().get_map_location();
                    targetMap.addInfluenceMultiple(healerInfluence, pos.get_x(), pos.get_y(), 10);
                }
            }
        }
        targetMap /= rocketHazardMap + 0.1;
        targetMap /= stuckUnitMap + 1.0;
        reusableMaps[reuseObject] = targetMap;
    }

    // Don't enter a rocket while constructing something
    if ((!didBuild || rocketDelay > 10) && gc.get_round() < 600) {
        addRocketTarget(unit, targetMap);
    } else {
        rocketDelay++;
    }

    return targetMap;
}

PathfindingMap BotWorker::getCostMap() {
    MapReuseObject reuseObject(MapType::Cost, unit.get_unit_type(), false);
    if (reusableMaps.count(reuseObject)) {
        return reusableMaps[reuseObject];
    }
    else {
        auto costMap = (((passableMap) * 50.0)/(fuzzyKarboniteMap + 50.0)) + enemyNearbyMap + enemyInfluenceMap + workerProximityMap + structureProximityMap + rocketHazardMap * 50.0;
        reusableMaps[reuseObject] = costMap;
        return costMap;
    }
}

void BotWorker::tick() {
    didBuild = false;

    if (!unit.get_location().is_on_map()) {
        return;
    }

    if (workersMove) {
        hasDoneTick = true;
    }

    auto unitMapLocation = unit.get_location().get_map_location();

    if (workersMove && gc.is_move_ready(unit.get_id())) {
        unitMapLocation = getNextLocation();
        moveToLocation(unitMapLocation);
    }

    if (!unit.get_location().is_on_map()) {
        return;
    }

    unitMapLocation = unit.get_location().get_map_location();

    const auto nearby = gc.sense_nearby_units(unitMapLocation, 2);

    const unsigned id = unit.get_id();

    for (auto& place : nearby) {
        //Building 'em blueprints
        if(gc.can_build(id, place.get_id())) {
            const int& placeId = place.get_id();
            double score = (place.get_health() / (0.0 + place.get_max_health()));
            macroObjects.emplace_back(score, 0, 5 * (!devsFixedReplicationBug), [=]{
                if(gc.can_build(id, placeId)) {
                    //assert(!hasHarvested);
                    didBuild = true;
                    gc.build(id, placeId);
                }
            });
        }
        if(gc.can_repair(id, place.get_id()) && place.get_health() < place.get_max_health()) {
            const int& placeId = place.get_id();
            double score = 2 - (place.get_health() / (0.0 + place.get_max_health()));
            macroObjects.emplace_back(score, 0, 4 * (!devsFixedReplicationBug), [=]{
                if(gc.can_repair(id, placeId)) {
                    gc.repair(id, placeId);
                }
            });
        }
    }

    double bestHarvestScore = -1;
    Direction bestHarvestDirection = Center;
    for (int i = 0; i < 9; ++i) {
        auto d = (Direction) i;
        if (gc.can_harvest(id, d)) {
            auto pos = unitMapLocation.add(d);
            int karbonite = gc.get_karbonite_at(pos);
            double score = karbonite;
            if (score > bestHarvestScore) {
                bestHarvestScore = score;
                bestHarvestDirection = d;
            }
        }
    }
    if (bestHarvestScore > 0) {
        const Direction& dir = bestHarvestDirection;
        macroObjects.emplace_back(1, 0, 3 * (!devsFixedReplicationBug), [=]{
            if (gc.can_harvest(id, dir)) {
                hasHarvested = true;
                gc.harvest(id, dir);
                auto pos = unitMapLocation.add(dir);
                karboniteMap.weights[pos.get_x()][pos.get_y()] = gc.get_karbonite_at(pos);
            }
        });
    }

    if (planet == Earth) {
        for (int i = 0; i < 8; i++) {
            Direction d = (Direction) i;
            // Placing 'em blueprints
            auto newLocation = unitMapLocation.add(d);
            if(isOnMap(newLocation) && gc.can_sense_location(newLocation) && gc.is_occupiable(newLocation)) {
                int x = newLocation.get_x();
                int y = newLocation.get_y();
                double score = state.typeCount[Factory] < 4 ? (1.5 - 0.1 * state.typeCount[Factory]) : 5.0 / (5.0 + state.typeCount[Factory]);
                if (state.typeCount[Factory] >= 5 && state.typeCount[Factory] * 800 > state.remainingKarboniteOnEarth && state.typeCount[Factory] * 200 > (int)gc.get_karbonite())
                    score = 0;
                if (state.typeCount[Factory] < 2)
                    score += 12.0 / (25.0 + initialDistanceToEnemyLocation);

                score *= structurePlacementScore(x, y, Factory);

                macroObjects.emplace_back(score, unit_type_get_blueprint_cost(Factory), 2, [=]{
                    if (lastFactoryBlueprintTurn != (int)gc.get_round() && gc.can_blueprint(id, Factory, d)) {
                        gc.blueprint(id, Factory, d);
                        lastFactoryBlueprintTurn = gc.get_round();
                    }
                });
                auto researchInfo = gc.get_research_info();
                if (researchInfo.get_level(Rocket) >= 1) {
                    double factor = 0.01;
                    if (gc.get_round() > 600) {
                        factor = 0.2;
                    }
                    if (!launchedWorkerCount && !state.typeCount[Rocket]) {
                        factor += 0.5;
                    }
                    // No path to enemy
                    if (mapConnectedness == 0) {
                        factor += 0.2;
                    }
                    // 1 path to the enemy, will be hard to invade
                    if (mapConnectedness == 1) {
                        factor += 0.01;
                    }
                    // 2 paths to the enemy, will be hard to invade
                    if (mapConnectedness == 2) {
                        factor += 0.002;
                    }
                    if (state.typeCount[Ranger]+state.typeCount[Healer]+state.typeCount[Mage] > 120 || (state.typeCount[Ranger]+state.typeCount[Healer] > 80 && averageAttackerSuccessRate < 0.01) || (state.typeCount[Ranger]+state.typeCount[Healer] > 40 && turnsSinceLastFight > 30)) {
                        factor += 0.1;
                    }
                    double score = factor * (state.totalUnitCount - state.typeCount[Worker]*0.9 - state.typeCount[Factory] - 12 * state.typeCount[Rocket]);
                    score -= karboniteMap.weights[x][y] * 0.001;
                    score -= (structureProximityMap.weights[x][y] + rocketProximityMap.weights[x][y] + enemyNearbyMap.weights[x][y] * 0.01) * 0.001;
                    score *= structurePlacementScore(x, y, Rocket);
                    if (gc.get_round() > 650 && state.typeCount[Rocket] == 0)
                        score += 100;
                    score += timesStuck;
                    macroObjects.emplace_back(score, unit_type_get_blueprint_cost(Rocket), 2, [=]{
                        if(lastRocketBlueprintTurn != (int)gc.get_round() && gc.can_blueprint(id, Rocket, d)){
                            gc.blueprint(id, Rocket, d);
                            lastRocketBlueprintTurn = gc.get_round();
                            timesStuck = 0;
                        }
                    });
                }
            }
        }
    }
}

void addWorkerActions () {
    for (int replicateCount = 1; replicateCount <= 7; replicateCount++) {
        int workerCount = state.typeCount[Worker] + replicateCount;
        double karbonitePerWorker = (state.remainingKarboniteOnEarth + 0.0) / workerCount;
        double contestedKarbonitePerWorker = (contestedKarbonite + 0.0) / workerCount;
        double replicateScore = karbonitePerWorker * 0.01 + contestedKarbonitePerWorker * 0.04;

        // If the enemy is close, we probably want quite a lot of workers in any case (repairs, fast construction etc.)
        // However if the enemy is far away it might be better to save those resources
        if (initialDistanceToEnemyLocation < 20) {
            replicateScore += 5.0 / (workerCount + 0.1);
        } else {
            replicateScore += 2.0 / (workerCount + 0.1);
        }

        if (workerCount > 100 || (karbonitePerWorker < 70 && workerCount >= 10 && planet == Earth)) {
            continue;
        }

        // Don't replicate on Mars unless some criteria are met
        if (planet != Earth && state.earthTotalUnitCount > 0 && workerCount >= 8 && gc.get_round() <= 740) {
            continue;
        }

        macroObjects.emplace_back(replicateScore, unit_type_get_replicate_cost(), 2, [=]{
            // Find best worker to replicate
            double bestScore = -10000;
            int bestID = -1;
            Direction bestDirection = North;

            for (auto& u : ourUnits) {
                if(u.get_unit_type() == Worker && u.get_location().is_on_map() && u.get_ability_heat() < 10) {
                    BotWorker* botunit = (BotWorker*)unitMap[u.get_id()];
                    if (botunit != nullptr) {
                        auto unitID = u.get_id();
                        auto unitMapLocation = u.get_map_location();
                        auto nextLocation = botunit->getNextLocation(unitMapLocation, false);
                        auto dir = unitMapLocation.direction_to(nextLocation);
                        double score = botunit->pathfindingScore;
                        if (score > bestScore) {
                            if (nextLocation != unitMapLocation && gc.can_replicate(unitID, dir)) {
                                bestScore = score;
                                bestID = unitID;
                                bestDirection = dir;
                            } else {
                                if (nextLocation != unitMapLocation && gc.get_karbonite() >= 60 && gc.can_sense_location(nextLocation) && gc.is_occupiable(nextLocation) && unitMapLocation.add(dir) == nextLocation && gc.get_unit(unitID).get_ability_heat() < 10) {
                                    devsFixedReplicationBug = true;
                                }
                                // Replicate in the direction with the most karbonite
                                for (int d = 0; d < 8; d++) {
                                    if (gc.can_replicate(unitID, (Direction)d)) {
                                        nextLocation = unitMapLocation.add((Direction)d);
                                        double score2 = score + 0.00001*karboniteMap.weights[nextLocation.get_x()][nextLocation.get_y()];
                                        if (score2 > bestScore) {
                                            bestScore = score2;
                                            bestID = unitID;
                                            bestDirection = (Direction)d;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            if (bestID != -1) {
                gc.replicate(bestID, bestDirection);
                state.typeCount[Worker]++;
            }
        });
    }
}
