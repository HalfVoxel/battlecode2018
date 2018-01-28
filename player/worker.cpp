#include "worker.h"
#include "pathfinding.hpp"
using namespace bc;
using namespace std;


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
                double score = state.typeCount[Factory] < 4 ? (2.5 - 0.5 * state.typeCount[Factory]) : 5.0 / (5.0 + state.typeCount[Factory]);
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
