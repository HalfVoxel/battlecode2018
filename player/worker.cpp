#include "worker.h"
#include "pathfinding.hpp"
#include "view.hpp"
#include "hungarian.h"

using namespace bc;
using namespace std;

struct KarboniteGroup {
    vector<pii> tiles;
    int totalValue;
};

void printMap (vector<vector<int>> values) {
    for (int y = 0; y < values[0].size(); y++) {
        for (int x = 0; x < values[0].size(); x++) {
        }
    }
}

vector<KarboniteGroup> groupKarbonite() {
    vector<vector<bool>> covered(w, vector<bool>(h));
    vector<vector<int>> groupIndices(w, vector<int>(h, -1));
    int maxTimeCostPerGroup = 80;
    int miningSpeed = 3;
    vector<KarboniteGroup> groups;

    for (int x = 0; x < w; x++) {
        for (int y = 0; y < h; y++) {
            if (covered[x][y] || karboniteMap.weights[x][y] == 0) continue;

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

    // print({ 0, 0, w - 1, h - 1 }, colorsByID([&](int x, int y) { return groupIndices[x][y] + 1; }), labels([&](int x, int y) { return (int)karboniteMap.weights[x][y]; }));
    // print({ 0, 0, w - 1, h - 1 }, colorsByID([&](int x, int y) { return groupIndices[x][y] + 1; }), labels([&](int x, int y) { return max(0, groupIndices[x][y]); }));

    return groups;
}

void matchWorkers() {
    if (planet != Earth || ourTeam != 0) return;

    auto groups = groupKarbonite();
    cout << endl;
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

    HungarianAlgorithm matcher;
    Pathfinder pathfinder;
    int numTargets = groups.size()*3 + unitTargets.size()*3;
    if (numTargets == 0) {
        for (auto* worker : workers) {
            worker->calculatedTargetMap = PathfindingMap();
        }
        return;
    }


    vector<vector<double>> costMatrix (workers.size(), vector<double>(numTargets));
    for (int wi = 0; wi < workers.size(); wi++) {
        auto* worker = workers[wi];
        auto targetMap = worker->getOriginalTargetMap();
        auto costMap = worker->getCostMap();
        auto pos = worker->unit.get_map_location();
        auto distanceMap = pathfinder.getDistanceToAllTiles(pos.get_x(), pos.get_y(), costMap);
        for (int i = 0; i < groups.size(); i++) {
            auto score = 0;
            for (auto p : groups[i].tiles) {
                score += targetMap.weights[p.first][p.second] / (1 + distanceMap[p.first][p.second]);
            }
            // cout << "Score for " << wi << " " << i << ": " << score << endl;
            costMatrix[wi][i*3 + 0] = score;
            costMatrix[wi][i*3 + 1] = score/2;
            costMatrix[wi][i*3 + 2] = score/4;
        }
        int offset = groups.size()*3;

        for (int i = 0; i < unitTargets.size(); i++) {
            auto pos2 = unitTargets[i]->get_map_location();
            double score = 0;
            for (int dx = -1; dx <= 1; dx++) {
                for (int dy = -1; dy <= 1; dy++) {
                    int nx = pos2.get_x() + dx;
                    int ny = pos2.get_y() + dy;
                    if (nx < 0 || nx < 0 || nx >= w || ny >= h) continue;
                    score = max(score, targetMap.weights[nx][ny] / (1 + distanceMap[nx][ny]));
                }
            }

            costMatrix[wi][offset + i*3 + 0] = score;
            costMatrix[wi][offset + i*3 + 1] = score/2;
            costMatrix[wi][offset + i*3 + 2] = score/4;
        }
    }


    // Invert cost matrix
    double mx = 0;
    for (int i = 0; i < costMatrix.size(); i++) {
        for (auto v : costMatrix[i]) mx = max(mx, v);
    }

    for (int i = 0; i < costMatrix.size(); i++) {
        for (auto& v : costMatrix[i]) v = mx - v;
    }

    vector<int> assignment;
    double cost = matcher.Solve(costMatrix, assignment);
    assert(assignment.size() == workers.size());
    for (int wi = 0; wi < assignment.size(); wi++) {
        // cout << "Worker " << wi << " goes to " << assignment[wi] << endl;
        auto* worker = workers[wi];
        int target = assignment[wi];

        if (target == -1) {
            // No assigned target
            worker->calculatedTargetMap = PathfindingMap();
            continue;
        }

        assert(target >= 0);
        PathfindingMap mask(w, h);
        int offset = groups.size()*3;

        if (target >= offset) {
            // Move towards a building
            target = (target - offset)/3;
            assert(target < unitTargets.size());

            auto pos2 = unitTargets[target]->get_map_location();
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
            assert(target < groups.size());

            for (auto p : groups[target].tiles) {
                mask.weights[p.first][p.second] = 1;
            }
        }

        worker->calculatedTargetMap = mask * worker->getOriginalTargetMap();
    }
    // cout << "Done" << endl;
    // if (gc.get_round() == 63) exit(0);
}

// Returns score for factory placement
// Will be on the order of magnitude of 1 for a well placed factory
// May be negative, but mostly in the [0,1] range
double structurePlacementScore(int x, int y, UnitType unitType) {
    double nearbyTileScore = 0;
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
    nearbyTileScore /= 8.0f;
    assert(nearbyTileScore <= 1.01f);
    assert(nearbyTileScore >= 0);

    auto pos = MapLocation(Earth, x, y);

    // nearbyTileScore will be approximately 1 if there are 8 free tiles around the factory.

    double score = nearbyTileScore;
    // Score will go to zero when there is more than 50 karbonite on the tile
    score -= karboniteMap.weights[x][y] / 50.0;

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

    // enemyNearbyMap is 1 at enemies and falls of slowly
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
            macroObjects.emplace_back(score, 0, 1, [=]{
                if(gc.can_build(id, placeId)) {
                    cout << "Building" << endl;
                    didBuild = true;
                    gc.build(id, placeId);
                }
            });
        }
        if(gc.can_repair(id, place.get_id()) && place.get_health() < place.get_max_health()) {
            const int& placeId = place.get_id();
            double score = 2 - (place.get_health() / (0.0 + place.get_max_health()));
            macroObjects.emplace_back(score, 0, 1, [=]{
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
        macroObjects.emplace_back(1, 0, 0, [=]{
            if (gc.can_harvest(id, dir)) {
                gc.harvest(id, dir);
                auto pos = unitMapLocation.add(dir);
                karboniteMap.weights[pos.get_x()][pos.get_y()] = gc.get_karbonite_at(pos);
            }
        });
    }

    double karbonitePerWorker = (state.remainingKarboniteOnEarth + 0.0) / state.typeCount[Worker];
    double replicateScore = karbonitePerWorker * 0.008 + 2.5 / (state.typeCount[Worker] + 0.1);
    if (state.typeCount[Worker] > 100 || (karbonitePerWorker < 70 && state.typeCount[Worker] >= 10 && planet == Earth)) {
        replicateScore = -1;
    }

    if (planet == Earth) {
        for (int i = 0; i < 8; i++) {
            Direction d = (Direction) i;
            // Placing 'em blueprints
            auto newLocation = unitMapLocation.add(d);
            if(isOnMap(newLocation) && gc.can_sense_location(newLocation) && gc.is_occupiable(newLocation)) {
                int x = newLocation.get_x();
                int y = newLocation.get_y();
                double score = state.typeCount[Factory] < 4 ? (2.3 - 0.4 * state.typeCount[Factory]) : 5.0 / (5.0 + state.typeCount[Factory]);
                if (state.typeCount[Factory] >= 5 && state.typeCount[Factory] * 800 > state.remainingKarboniteOnEarth)
                    score = 0;
                if (state.typeCount[Factory] < 2)
                    score += 12.0 / (5.0 + initialDistanceToEnemyLocation);

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
                    macroObjects.emplace_back(score, unit_type_get_blueprint_cost(Rocket), 2, [=]{
                        if(lastRocketBlueprintTurn != (int)gc.get_round() && gc.can_blueprint(id, Rocket, d)){
                            gc.blueprint(id, Rocket, d);
                            lastRocketBlueprintTurn = gc.get_round();
                        }
                    });
                }
            }
        }
    }

    if(unit.get_ability_heat() < 10 && unit.get_location().is_on_map() && (planet == Earth || state.earthTotalUnitCount == 0 || state.typeCount[Worker] < 8 || gc.get_round() > 740) && replicateScore > bestMacroObjectScore - 0.1) {
        auto nextLocation = getNextLocation(unitMapLocation, false);

        if (nextLocation != unitMapLocation) {
            auto d = unitMapLocation.direction_to(nextLocation);
            double score = replicateScore + 0.001 * log(1.1 + pathfindingScore);
            macroObjects.emplace_back(score, unit_type_get_replicate_cost(), 2, [=]{
                if(gc.can_replicate(id, d)) {
                    gc.replicate(id, d);
                }
            });
        }
    }
}
