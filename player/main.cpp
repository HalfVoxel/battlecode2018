#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>
#include <random>
#include <functional>
#include <iostream>
#include <map>
#include <stack>

#include "common.h"
#include "pathfinding.hpp"
#include "influence.h"
#include "rocket.h"
#include "maps.h"

using namespace bc;
using namespace std;


double averageHealerSuccessRate;
map<UnitType, double> timeConsumptionByUnit;
map<UnitType, string> unitTypeToString;
bool hasOvercharge;
bool hasBlink;
double bestMacroObjectScore;
bool existsPathToEnemy;
int lastFactoryBlueprintTurn = -1;
int turnsSinceLastFight;
bool workersMove;

// True if mars has any landing spots (i.e does mars have any traversable ground)
bool anyReasonableLandingSpotOnInitialMars;

// Sort of how many distinct ways there are to get to the enemy
// If the enemy is behind a wall, then this is 0
// If there is a choke point of size 1 then this is 1.
// If the player starts in 2 places, each of them can reach the enemy in 1 way, then this is 2.
// All paths to the enemy must thouch distinct nodes.
// Maximum value is 6 to avoid wasting too much time calculating it.
int mapConnectedness;

// On average how many units there are around an enemy unit that we can see
// This is the expected damage multiplier for mages
float splashDamagePotential = 0;


static_assert((int)Worker == 0, "");
static_assert((int)Rocket == 6, "");


struct State {
    map<UnitType, int> typeCount;
    double remainingKarboniteOnEarth;
    int totalRobotDamage;
    int totalUnitCount;
    int earthTotalUnitCount;
} state;

struct MacroObject {
    double score;
    unsigned cost;
    int priority;
    int rnd;
    function<void()> lambda;

    MacroObject(double _score, unsigned _cost, int _priority, function<void()> _lambda) {
        score = _score;
        cost = _cost;
        priority = _priority;
        lambda = _lambda;
        rnd = rand();
    }

    void execute() {
        lambda();
    }

    bool operator<(const MacroObject& other) const {
        if (priority != other.priority) {
            return priority < other.priority;
        }
        if (score != other.score) {
            return score < other.score;
        }
        return rnd < other.rnd;
    }
};

vector<MacroObject> macroObjects;

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

struct BotWorker : BotUnit {
    int rocketDelay = 0;
    bool didBuild = false;
    BotWorker(const Unit& unit) : BotUnit(unit) {}


    PathfindingMap getTargetMap() {
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

    PathfindingMap getCostMap() {
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

    void tick() {
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
        double replicateScore = karbonitePerWorker * 0.015 + 2.5 / state.typeCount[Worker];
        if (karbonitePerWorker < 70 && state.typeCount[Worker] >= 10 && planet == Earth) {
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
                    double score = state.typeCount[Factory] < 3 ? (3 - state.typeCount[Factory]) : 5.0 / (5.0 + state.typeCount[Factory]);
                    if (state.typeCount[Factory] >= 5 && state.typeCount[Factory] * 400 > state.remainingKarboniteOnEarth)
                        score = 0;

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
                        if (state.typeCount[Ranger] > 100 || (state.typeCount[Ranger] > 70 && averageAttackerSuccessRate < 0.02)) {
                            factor += 0.1;
                        }
                        double score = factor * (state.totalUnitCount - state.typeCount[Worker]*0.9 - state.typeCount[Factory] - 12 * state.typeCount[Rocket]);
                        score -= karboniteMap.weights[x][y] * 0.001;
                        score -= (structureProximityMap.weights[x][y] + rocketProximityMap.weights[x][y] + enemyNearbyMap.weights[x][y] * 0.01) * 0.001;
                        score *= structurePlacementScore(x, y, Rocket);
                        macroObjects.emplace_back(score, unit_type_get_blueprint_cost(Rocket), 2, [=]{
                            if(gc.can_blueprint(id, Rocket, d)){
                                gc.blueprint(id, Rocket, d);
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
};

struct BotKnight : BotUnit {
    BotKnight(const Unit& unit) : BotUnit(unit) {}

    PathfindingMap getTargetMap() {
        return defaultMilitaryTargetMap();
    }

    PathfindingMap getCostMap() {
        return defaultMilitaryCostMap();
    }

    void tick() {
        if (!unit.get_location().is_on_map()) return;

        hasDoneTick = true;

        default_military_behaviour();
    }
};

struct BotRanger : BotUnit {
    BotRanger(const Unit& unit) : BotUnit(unit) {}

    PathfindingMap getTargetMap() {
        return defaultMilitaryTargetMap();
    }

    PathfindingMap getCostMap() {
        return defaultMilitaryCostMap();
    }

    void tick() {
        if (!unit.get_location().is_on_map()) return;

        hasDoneTick = true;

        default_military_behaviour();
    }
};

struct BotMage : BotUnit {
    BotMage(const Unit& unit) : BotUnit(unit) {}

    PathfindingMap getTargetMap() {
        return defaultMilitaryTargetMap();
    }

    PathfindingMap getCostMap() {
        return defaultMilitaryCostMap();
    }

    void tick() {
        if (!unit.get_location().is_on_map()) return;

        hasDoneTick = true;

        default_military_behaviour();
    }
};


struct BotHealer : BotUnit {
    BotHealer(const Unit& unit) : BotUnit(unit) {}

    PathfindingMap getTargetMap() {
        MapReuseObject reuseObject(MapType::Target, unit.get_unit_type(), false);

        PathfindingMap targetMap;
        if (reusableMaps.count(reuseObject)) {
            targetMap = reusableMaps[reuseObject];
        }
        else {
            targetMap = enemyNearbyMap * 0.0001 + 0.001 - structureProximityMap * 0.001;
            for (auto& u : ourUnits) {
                if (!u.get_location().is_on_map()) {
                    continue;
                }
                if (is_robot(u.get_unit_type())) {
                    if (u.get_id() == id) {
                        continue;
                    }
                    double remainingLife = u.get_health() / (u.get_max_health() + 0.0);
                    if (remainingLife == 1.0) {
                        continue;
                    }
                    auto uMapLocation = u.get_location().get_map_location();

                    targetMap.maxInfluenceMultiple(healerTargetInfluence, uMapLocation.get_x(), uMapLocation.get_y(), 15 * (2.0 - remainingLife));
                }
            }
            /*for (auto& u : ourUnits) {
                if (!u.get_location().is_on_map()) {
                    continue;
                }
                if (u.get_unit_type() == Mage) {
                    auto uMapLocation = u.get_location().get_map_location();
                    targetMap.addInfluenceMultiple(healerTargetInfluence, uMapLocation.get_x(), uMapLocation.get_y(), 10);
                }
            }*/
            if (hasOvercharge) {
                targetMap += healerOverchargeMap * 10;
            }
            targetMap /= (enemyNearbyMap + stuckUnitMap + 1.0);
            targetMap /= rocketHazardMap + 0.1;
            reusableMaps[reuseObject] = targetMap;
        }

        addRocketTarget(unit, targetMap);

        return targetMap;
    }

    PathfindingMap getCostMap() {
        MapReuseObject reuseObject(MapType::Cost, unit.get_unit_type(), false);
        if (reusableMaps.count(reuseObject)) {
            return reusableMaps[reuseObject];
        }
        else {
            PathfindingMap healerProximityMap(w, h);
            for (auto& u : ourUnits) {
                if (!u.get_location().is_on_map()) {
                    continue;
                }
                if (is_robot(u.get_unit_type())) {
                    if (u.get_id() == id) {
                        continue;
                    }
                    double remainingLife = u.get_health() / (u.get_max_health() + 0.0);
                    if (remainingLife == 1.0) {
                        continue;
                    }

                    auto uMapLocation = u.get_location().get_map_location();

                    if (u.get_unit_type() == Healer) {
                        healerProximityMap.addInfluence(healerProximityInfluence, uMapLocation.get_x(), uMapLocation.get_y());
                    }
                }
            }

            auto costMap = passableMap + healerProximityMap + enemyInfluenceMap + rocketHazardMap * 10.0;
            reusableMaps[reuseObject] = costMap;
            return costMap;
        }
    }

    void tick() {
        if (!unit.get_location().is_on_map()) return;

        hasDoneTick = true;

        auto unitMapLocation = unit.get_location().get_map_location();
        bool succeededHealing = false;
        int bestTargetId = -1;
        double bestTargetRemainingLife = 1.0;
        for (auto& u : ourUnits) {
            if (!u.get_location().is_on_map()) {
                continue;
            }
            if (is_robot(u.get_unit_type())) {
                if (u.get_id() == id) {
                    continue;
                }
                double remainingLife = u.get_health() / (u.get_max_health() + 0.0);
                if (remainingLife == 1.0) {
                    continue;
                }

                if (gc.can_heal(id, u.get_id()) && gc.is_heal_ready(id)) {
                    if (remainingLife < bestTargetRemainingLife) {
                        bestTargetRemainingLife = remainingLife;
                        bestTargetId = u.get_id();
                    }
                }
            }
        }
        if (bestTargetId != -1) {
            gc.heal(id, bestTargetId);
            invalidate_unit(bestTargetId);
            invalidate_unit(id);
            succeededHealing = true;
        }

        auto nextLocation = getNextLocation();
        moveToLocation(nextLocation);

        if(!succeededHealing) {
            for (auto& u : ourUnits) {
                if (!u.get_location().is_on_map()) {
                    continue;
                }
                if (is_robot(u.get_unit_type())) {
                    if (u.get_id() == id) {
                        continue;
                    }
                    double remainingLife = u.get_health() / (u.get_max_health() + 0.0);
                    if (remainingLife == 1.0) {
                        continue;
                    }

                    if (gc.can_heal(id, u.get_id()) && gc.is_heal_ready(id)) {
                        if (remainingLife < bestTargetRemainingLife) {
                            bestTargetRemainingLife = remainingLife;
                            bestTargetId = u.get_id();
                        }
                    }
                }
            }
            if (bestTargetId != -1) {
                gc.heal(id, bestTargetId);
                invalidate_unit(bestTargetId);
                invalidate_unit(id);
                succeededHealing = true;
            }
        }
        double interpolationFactor = 0.99;
        averageHealerSuccessRate = averageHealerSuccessRate * interpolationFactor + succeededHealing * (1-interpolationFactor);
    }

    void doOvercharge() {
        if (hasOvercharge && unit.get_ability_heat() < 10 && unit.get_location().is_on_map()) {
            const auto& location = unit.get_location().get_map_location();
            if (mageNearbyMap.weights[location.get_x()][location.get_y()] > 0)
                return;
            const auto nearby = gc.sense_nearby_units(location, unit.get_ability_range());

            const Unit* best_unit = nullptr;
            double best_unit_score = 0;

            for (auto& place : nearby) {
                if (place.get_health() <= 0) continue;
                if (place.get_unit_type() != Ranger && place.get_unit_type() != Mage) continue;
                if (place.get_team() != gc.get_team()) continue;
                if (place.get_attack_heat() < 20) continue;
                double score = place.get_attack_heat();
                if (score > best_unit_score) {
                    best_unit_score = score;
                    best_unit = &place;
                }
            }
            if (best_unit != nullptr) {
                int otherUnitId = best_unit->get_id();
                gc.overcharge(unit.get_id(), best_unit->get_id());
                invalidate_unit(best_unit->get_id());
                invalidate_unit(id);
                unitMap[otherUnitId]->tick();
            }
        }
    }
};

struct BotFactory : BotUnit {
    BotFactory(const Unit& unit) : BotUnit(unit) {}

    void tick() {

        hasDoneTick = true;
        auto garrison = unit.get_structure_garrison();
        for (size_t i = 0; i < garrison.size(); i++) {
            if (!unloadFrontUnit()) {
                break;
            }
        }

        if (gc.get_round() >= 742) {
            // Don't produce any more units as they won't have time to get into a rocket
            return;
        }

        if (!unit.is_factory_producing()) {
            if (existsPathToEnemy){
                double score = 1;
                const auto& location = unit.get_location().get_map_location();
                double nearbyEnemiesWeight = enemyNearbyMap.weights[location.get_x()][location.get_y()];
                if (distanceToInitialLocation[enemyTeam].weights[location.get_x()][location.get_y()] < 15 && gc.get_round() < 50)
                    score += 20;
                score /= state.typeCount[Knight] + 1.0;
                if (nearbyEnemiesWeight > 0.7)
                    score += 0.1;
                if (nearbyEnemiesWeight > 0.8)
                    score += 0.4;
                if (nearbyEnemiesWeight > 0.9) {
                    score += 1;
                    if (gc.get_round() < 100)
                        score += 1;
                }
                score += 30 * enemyFactoryNearbyMap.weights[location.get_x()][location.get_y()];
                macroObjects.emplace_back(score, unit_type_get_factory_cost(Knight), 2, [=] {
                    if (gc.can_produce_robot(id, Knight)) {
                        gc.produce_robot(id, Knight);
                    }
                });
            }
            {
                double score = 2 + 20.0 / (10.0 + state.typeCount[Ranger]);
                macroObjects.emplace_back(score, unit_type_get_factory_cost(Ranger), 2, [=] {
                    if (gc.can_produce_robot(id, Ranger)) {
                        gc.produce_robot(id, Ranger);
                    }
                });
            }
            {
                double score = 0;
                auto researchInfo = gc.get_research_info();
                if (state.typeCount[Worker] == 0 && (researchInfo.get_level(Rocket) >= 1 || state.typeCount[Factory] < 3)) {
                    score += 10;
                }
                if (gc.get_round() > 600 && state.typeCount[Worker] < 10) {
                    score += 500;
                }
                if (state.typeCount[Rocket] > 5) {
                    score += 10;
                }
                macroObjects.emplace_back(score, unit_type_get_factory_cost(Worker), 2, [=] {
                    if (gc.can_produce_robot(id, Worker)) {
                        gc.produce_robot(id, Worker);
                    }
                });
            }
            {
                auto location = unit.get_location().get_map_location();
                // Not even sure about this, but yeah. If a mage hit will on average hit 4 enemies, go for it (compare to ranger score of 2)
                double score = splashDamagePotential * 0.5; // enemyInfluenceMap.weights[location.get_x()][location.get_y()] * 0.4;
                if (hasOvercharge) {
                    score += state.typeCount[Healer] * 2;
                }
                score /= state.typeCount[Mage] + 1.0;
                macroObjects.emplace_back(score, unit_type_get_factory_cost(Mage), 2, [=] {
                    if (gc.can_produce_robot(id, Mage)) {
                        gc.produce_robot(id, Mage);
                    }
                });
            }
            {
                int otherMilitary = state.typeCount[Ranger] + state.typeCount[Mage] + state.typeCount[Knight];
                // Never have more healers than the combined total of other military units
                if (otherMilitary > state.typeCount[Healer]) {
                    double score = 0.0;
                    if (state.typeCount[Ranger] > 6) {
                        score += 4.5;
                    }
                    if (state.typeCount[Ranger] > 10) {
                        score += state.typeCount[Ranger] * 0.7 + state.typeCount[Mage] * 0.7;
                    }
                    score /= state.typeCount[Healer];
                    score += averageHealerSuccessRate * 1.8;
                    if (hasOvercharge)
                        score += 1.0;
                    macroObjects.emplace_back(score, unit_type_get_factory_cost(Healer), 2, [=] {
                        if (gc.can_produce_robot(id, Healer)) {
                            gc.produce_robot(id, Healer);
                        }
                    });
                }
            }
        }
    }
};

void selectTravellersForRocket(Unit& unit) {
    if (!unit.get_location().is_on_map()) {
        return;
    }
    if (unit.get_location().get_map_location().get_planet() == Mars) {
        return;
    }
    if (!unit.structure_is_built()) {
        return;
    }
    bool hasWorker = false;
    for (auto id : unit.get_structure_garrison()) {
        auto u = gc.get_unit(id);
        if (u.get_unit_type() == Worker) {
            hasWorker = true;
        }
    }
    int remainingTravellers = unit.get_structure_max_capacity() - unit.get_structure_garrison().size();
    auto unitLocation = unit.get_location().get_map_location();
    vector<pair<double, unsigned> > candidates;
    for (auto& u : ourUnits) {
        if (u.get_unit_type() == Rocket || u.get_unit_type() == Factory) {
            continue;
        }
        if (u.get_unit_type() != Worker && candidates.size() == unit.get_structure_max_capacity() - 1 && !launchedWorkerCount) {
            continue;
        }
        if (u.get_unit_type() == Worker && state.typeCount[Worker] <= 1) {
            continue;
        }
        if (u.get_location().is_on_map()) {
            auto uLocation = u.get_location().get_map_location();
            int dx = uLocation.get_x() - unitLocation.get_x();
            int dy = uLocation.get_y() - unitLocation.get_y();
            double penalty = dx*dx + dy*dy;
            if (u.get_unit_type() == Worker) {
                if (launchedWorkerCount) {
                    penalty += 5000;
                }
                else {
                    penalty /= 4;
                }
            }
            candidates.emplace_back(penalty, u.get_id());
        }
    }
    sort(candidates.begin(), candidates.end());
    for (int i = 0; i < min((int) candidates.size(), remainingTravellers); i++) {
        if (gc.get_unit(candidates[i].second).get_unit_type() == Worker) {
            if (hasWorker && launchedWorkerCount) {
                continue;
            }
            hasWorker = true;
        }
        unitShouldGoToRocket[candidates[i].second].push_back(unit.get_id());
    }
}

struct Researcher {
    UnitType getBestResearch() {
        map<UnitType, double> scores;

        auto researchInfo = gc.get_research_info();
        switch(researchInfo.get_level(Knight)) {
            case 0:
                scores[Knight] = 2 + 0.1 * state.typeCount[Knight];
                break;
            case 1:
                scores[Knight] = 1 + 0.01 * state.typeCount[Knight];
                break;
            case 2:
                scores[Knight] = 1 + 0.01 * state.typeCount[Knight];
                break;
        }
        switch(researchInfo.get_level(Mage)) {
            case 0:
                scores[Mage] = 3 + 1.0 * state.typeCount[Mage];
                if (hasOvercharge)
                    scores[Mage] += 50;
                break;
            case 1:
                scores[Mage] = 2.5 + 1.0 * state.typeCount[Mage];
                break;
            case 2:
                scores[Mage] = 2 + 1.0 * state.typeCount[Mage];
                break;
            case 3:
                scores[Mage] = 3 + 1.0 * state.typeCount[Mage];
                break;
        }
        if (researchInfo.get_level(Mage) <= 3 && researchInfo.get_level(Healer) >= 3) {
            scores[Mage] += 5;
        }
        switch(researchInfo.get_level(Ranger)) {
            case 0:
                scores[Ranger] = 4 + 0.05 * state.typeCount[Ranger];
                if (hasOvercharge)
                    scores[Ranger] += 1;
                break;
            case 1:
                scores[Ranger] = 1 + 0.01 * state.typeCount[Ranger];
                if (hasOvercharge)
                    scores[Ranger] += 3;
                break;
            case 2:
                scores[Ranger] = 0.5 + 0.01 * state.typeCount[Ranger];
                break;
        }
        switch(researchInfo.get_level(Healer)) {
            case 0:
                scores[Healer] = 9 + 2.0 * state.typeCount[Healer];
                break;
            case 1:
                scores[Healer] = 8 + 1.0 * state.typeCount[Healer];
                break;
            case 2:
                scores[Healer] = 5 + 1.0 * state.typeCount[Healer];
                break;
        }
        switch(researchInfo.get_level(Worker)) {
            case 0:
                scores[Worker] = 2;
                break;
            case 1:
                scores[Worker] = 1;
                break;
            case 2:
                scores[Worker] = 1;
                break;
            case 3:
                scores[Worker] = 1;
                break;
        }
        switch(researchInfo.get_level(Rocket)) {
            case 0:
                if (!anyReasonableLandingSpotOnInitialMars) {
                    // Ha! We cannot even LAND on mars, why should we get there?
                    scores[Rocket] = 0;
                    break;
                }

                scores[Rocket] = 7;
                // if (state.typeCount[Knight] + state.typeCount[Mage] + state.typeCount[Ranger] > 150)
                if (state.typeCount[Ranger] > 100) {
                    scores[Rocket] += 200;
                }
                if (gc.get_round() > 150 && (averageAttackerSuccessRate < 0.001 || turnsSinceLastFight > 20)) {
                    scores[Rocket] += 200;
                }
                if (!existsPathToEnemy) {
                    scores[Rocket] += 1000;
                }
                if (gc.get_round() > 500) {
                    scores[Rocket] += 30;
                }
                // Few paths to the enemy. Will be hard to invade on earth
                if (mapConnectedness == 1) {
                    scores[Rocket] += 10;
                }
                // Few paths to the enemy. Will be hard to invade on earth
                if (mapConnectedness == 2) {
                    scores[Rocket] += 1.5;
                }
                break;
            case 1:
                scores[Rocket] = 6;
                if (gc.get_round() > 630)
                    scores[Rocket] = 0.01;
                break;
            case 2:
                scores[Rocket] = 6;
                if (gc.get_round() > 630)
                    scores[Rocket] = 0.01;
                break;
        }

        // Don't get rockets if we won't have time to use them before the flood anyway
        if (gc.get_round() > 750 - 50 - 5) {
            scores[Rocket] = 0.01;
        }

        UnitType bestType = Mage;
        double bestScore = 0;
        for (auto it : scores) {
            if (it.second > bestScore) {
                bestType = it.first;
                bestScore = it.second;
            }
        }
        return bestType;
    }
};

void findUnits() {
    ourUnits = gc.get_my_units();
    auto planet = gc.get_planet();
    ourTeam = gc.get_team();
    enemyTeam = (Team)(1 - (int)gc.get_team());

    allUnits.clear();
    enemyUnits.clear();
    for (int team = 0; team < 2; team++) {
        if (team != ourTeam) {
            auto u = gc.sense_nearby_units_by_team(MapLocation(planet, 0, 0), 1000000, (Team)team);
            for (auto& unit : u)
                enemyUnits.emplace_back(move(unit));
        }
    }
    for (auto& unit : ourUnits)
        allUnits.push_back(unit.clone());
    for (auto& unit : enemyUnits)
        allUnits.push_back(unit.clone());
}

void updateAsteroids() {
    if (gc.get_planet() == Mars) {
        auto asteroidPattern = gc.get_asteroid_pattern();
        if (asteroidPattern.has_asteroid_on_round(gc.get_round())) {
            auto strike = asteroidPattern.get_asteroid_on_round(gc.get_round());
            auto location = strike.get_map_location();
            karboniteMap.weights[location.get_x()][location.get_y()] += strike.get_karbonite();
        }
    }
}

void updateEnemyPositionMap() {
    PathfindingMap newEnemyPositionMap(w, h);
    for (int i = 0; i < w; i++) {
        for (int j = 0; j < h; j++) {
            double cnt = 0;
            for (int k = -1; k <= 1; k++) {
                for (int l = -1; l <= 1; l++) {
                    if (!k && !l) {
                        continue;
                    }
                    int x = i+k;
                    int y = j+l;
                    if (x >= 0 && y >= 0 && x < w && y < h) {
                        if (passableMap.weights[x][y] <= 1000){
                            ++cnt;
                        }
                    }
                }
            }
            double weight1 = 0.8;
            double weight2 = cnt > 0 ? 0.15 / cnt : 0;
            for (int k = -1; k <= 1; k++) {
                for (int l = -1; l <= 1; l++) {
                    if (!k && !l) {
                        newEnemyPositionMap.weights[i][j] += weight1 * enemyPositionMap.weights[i][j];
                        continue;
                    }
                    int x = i+k;
                    int y = j+l;
                    if (x >= 0 && y >= 0 && x < w && y < h) {
                        double we;
                        if (passableMap.weights[x][y] <= 1000){
                            we = weight2;
                        }
                        else {
                            we = 0;
                        }
                        newEnemyPositionMap.weights[x][y] += we * enemyPositionMap.weights[i][j];
                    }
                }
            }
        }
    }
    enemyPositionMap = newEnemyPositionMap;
}

void updateNearbyFriendMap() {
    nearbyFriendMap = PathfindingMap(w, h);

    for (auto& u : ourUnits) {
        if (u.get_unit_type() == Ranger) {
            if (!u.get_location().is_on_map()) {
                continue;
            }
            auto pos = u.get_location().get_map_location();
            nearbyFriendMap.addInfluence(rangerProximityInfluence, pos.get_x(), pos.get_y());
        }
    }
}

void initKarboniteMap() {
    karboniteMap = PathfindingMap(w, h);
    for (int i = 0; i < w; i++) {
        for (int j = 0; j < h; j++) {
            auto location = MapLocation(gc.get_planet(), i, j);
            int karbonite = planetMap->get_initial_karbonite_at(location);
            karboniteMap.weights[i][j] = karbonite;
        }
    }
}

void updateCanSenseLocation() {
    canSenseLocation = vector<vector<bool>>(w, vector<bool>(h));
    for (int i = 0; i < w; i++) {
        for (int j = 0; j < h; j++) {
            auto location = MapLocation(gc.get_planet(), i, j);
            canSenseLocation[i][j] = gc.can_sense_location(location);
        }
    }
}

void updateDiscoveryMap() {
    for (int i = 0; i < w; i++) {
        for (int j = 0; j < h; j++) {
            if (canSenseLocation[i][j]) {
                discoveryMap.weights[i][j] = 0.0;
            }
            else {
                discoveryMap.weights[i][j] = min(1.0, discoveryMap.weights[i][j] + 0.005);
            }
        }
    }
}

void computeDistancesToInitialLocations() {
    assert(planet == Earth);
    auto&& initial_units = gc.get_starting_planet(Earth).get_initial_units();
    for (int team = 0; team < 2; ++team) {
        distanceToInitialLocation[team] = PathfindingMap(w, h);
        distanceToInitialLocation[team] += 1000;
        queue<pair<int, int> > bfsQueue;
        for (auto& unit : initial_units) {
            if (!unit.get_location().is_on_map())
                continue;
            if (unit.get_team() == team) {
                auto pos = unit.get_location().get_map_location();
                int x = pos.get_x();
                int y = pos.get_y();
                distanceToInitialLocation[team].weights[x][y] = 0;
                bfsQueue.push(make_pair(x, y));

            }
        }
        while(!bfsQueue.empty()) {
            auto cur = bfsQueue.front();
            bfsQueue.pop();
            int x = cur.first;
            int y = cur.second;
            for (int dx = -1; dx <= 1; dx++) {
                for (int dy = -1; dy <= 1; dy++) {
                    int nx = x + dx;
                    int ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                        continue;
                    if (passableMap.weights[nx][ny] > 1000)
                        continue;
                    int newDis = distanceToInitialLocation[team].weights[x][y] + 1;
                    if (newDis < distanceToInitialLocation[team].weights[nx][ny]) {
                        distanceToInitialLocation[team].weights[nx][ny] = newDis;
                        bfsQueue.push(make_pair(nx, ny));
                    }
                }
            }
        }
    }
}

// NOTE: this call also updates enemy position map for some reason
void updateKarboniteMap() {
    for (int i = 0; i < w; i++) {
        for (int j = 0; j < h; j++) {
            if (canSenseLocation[i][j]) {
                if (karboniteMap.weights[i][j]) {
                    const MapLocation location(planet, i, j);
                    int karbonite = gc.get_karbonite_at(location);
                    karboniteMap.weights[i][j] = karbonite;
                    if (planet == Earth && distanceToInitialLocation[ourTeam].weights[i][j] > 200) {
                        // The karbonite is pretty much unreachable, so let's ignore it
                        karboniteMap.weights[i][j] = 0.01;
                    }
                }
                enemyPositionMap.weights[i][j] = 0;
            }
        }
    }
}

void updateFuzzyKarboniteMap() {
    fuzzyKarboniteMap = PathfindingMap(w, h);
    for (int i = 0; i < w; i++) {
        for (int j = 0; j < h; j++) {
            for (int k = -1; k <= 1; k++) {
                for (int l = -1; l <= 1; l++) {
                    int x = i+k;
                    int y = j+l;
                    if (x >= 0 && y >= 0 && x < w && y < h){
                        double kar = karboniteMap.weights[x][y];
                        kar = log(kar + 1);
                        if (k != 0 || l != 0)
                            kar *= 0.9;
                        fuzzyKarboniteMap.weights[i][j] = max(fuzzyKarboniteMap.weights[i][j], kar);
                    }
                }
            }
            if (planet == Earth) {
                int disDiff = distanceToInitialLocation[enemyTeam].weights[i][j] - distanceToInitialLocation[ourTeam].weights[i][j];
                if (disDiff <= 6) {
                    fuzzyKarboniteMap.weights[i][j] *= 1.4;
                }
                if (disDiff <= 0) {
                    fuzzyKarboniteMap.weights[i][j] *= 1.2;
                }
                if (disDiff <= -3) {
                    fuzzyKarboniteMap.weights[i][j] *= 1.1;
                }
                fuzzyKarboniteMap.weights[i][j] /= 1.0 + workerProximityMap.weights[i][j];
            }
        }
    }
    fuzzyKarboniteMap /= ourStartingPositionMap;
}

void updateEnemyInfluenceMaps(){
    enemyInfluenceMap = PathfindingMap(w, h);
    enemyNearbyMap = PathfindingMap(w, h);
    enemyFactoryNearbyMap = PathfindingMap(w, h);
    healerOverchargeMap = PathfindingMap(w, h);
    enemyExactPositionMap = PathfindingMap(w, h);
    for (auto& u : enemyUnits) {
        if (u.get_location().is_on_map()) {
            auto pos = u.get_location().get_map_location();
            if (u.get_unit_type() == Ranger) {
                enemyInfluenceMap.addInfluence(enemyRangerTargetInfluence, pos.get_x(), pos.get_y());
            }
            if (u.get_unit_type() == Mage) {
                enemyInfluenceMap.addInfluenceMultiple(enemyMageTargetInfluence, pos.get_x(), pos.get_y(), 2.0);
            }
            if (u.get_unit_type() == Knight) {
                enemyInfluenceMap.addInfluenceMultiple(enemyKnightTargetInfluence, pos.get_x(), pos.get_y(), 3.0);
            }
            enemyNearbyMap.maxInfluence(wideEnemyInfluence, pos.get_x(), pos.get_y());
            enemyPositionMap.weights[pos.get_x()][pos.get_y()] += 1.0;
            enemyExactPositionMap.weights[pos.get_x()][pos.get_y()] = 1;
            healerOverchargeMap.maxInfluence(healerOverchargeInfluence, pos.get_x(), pos.get_y());
            enemyFactoryNearbyMap.maxInfluence(enemyFactoryNearbyInfluence, pos.get_x(), pos.get_y());
        }
    }

    if (planet == Earth) {
        auto&& initial_units = gc.get_starting_planet(Earth).get_initial_units();
        for (auto& unit : initial_units) {
            if (!unit.get_location().is_on_map())
                continue;
            if (unit.get_team() == enemyTeam) {
                auto pos = unit.get_location().get_map_location();
                for (int x = 0; x < w; ++x) {
                    for (int y = 0; y < h; ++y) {
                        int dx = pos.get_x() - x;
                        int dy = pos.get_y() - y;
                        enemyNearbyMap.weights[x][y] += 0.01 / (dx * dx + dy * dy + 5);
                    }
                }
            }
        }
    }
}

void computeOurStartingPositionMap() {
    ourStartingPositionMap = PathfindingMap(w, h);
    if (planet == Earth) {
        auto&& initial_units = gc.get_starting_planet(Earth).get_initial_units();
        for (auto& unit : initial_units) {
            if (!unit.get_location().is_on_map())
                continue;
            if (unit.get_team() == ourTeam) {
                auto pos = unit.get_location().get_map_location();
                for (int x = 0; x < w; ++x) {
                    for (int y = 0; y < h; ++y) {
                        int dx = pos.get_x() - x;
                        int dy = pos.get_y() - y;
                        ourStartingPositionMap.weights[x][y] = max(ourStartingPositionMap.weights[x][y], 200.0 / (dx * dx + dy * dy + 200.0));
                    }
                }
            }
        }
    }
    else {
        ourStartingPositionMap += 1;
    }
}

void updateWorkerMaps() {
    workerProximityMap = PathfindingMap(w, h);
    for (auto& u : ourUnits) {
        if (u.get_location().is_on_map()) {
            if (u.get_unit_type() == Worker) {
                auto pos = u.get_location().get_map_location();
                workerProximityMap.maxInfluence(workerProximityInfluence, pos.get_x(), pos.get_y());
            }
        }
    }
    workersNextToMap = PathfindingMap(w, h);
    for (auto& u : ourUnits) {
        if (u.get_location().is_on_map()) {
            if (u.get_unit_type() == Worker) {
                auto pos = u.get_location().get_map_location();
                int x = pos.get_x();
                int y = pos.get_y();
                for (int dx = -1; dx <= 1; ++dx) {
                    for (int dy = -1; dy <= 1; ++dy) {
                        int nx = x + dx;
                        int ny = y + dy;
                        if (nx >= 0 && ny >= 0 && nx < w && ny < h) {
                            workersNextToMap.weights[nx][ny]++;
                        }
                    }
                }
            }
        }
    }
}

void updateMageNearbyMap() {
    mageNearbyMap = PathfindingMap(w, h);
    mageNearbyFuzzyMap = PathfindingMap(w, h);
    for (auto& u : ourUnits) {
        if (u.get_unit_type() == Mage && u.get_location().is_on_map()) {
            auto pos = u.get_location().get_map_location();
            mageNearbyMap.maxInfluence(mageProximityInfluence, pos.get_x(), pos.get_y());
            mageNearbyFuzzyMap.maxInfluence(mageNearbyFuzzyInfluence, pos.get_x(), pos.get_y());
        }
    }
}

void updateStructureProximityMap() {
    structureProximityMap = PathfindingMap(w, h);
    rocketProximityMap = PathfindingMap(w, h);
    for (auto& u : ourUnits) {
        if (u.get_location().is_on_map()) {
            if (u.get_unit_type() == Factory && u.structure_is_built()) {
                auto pos = u.get_location().get_map_location();
                structureProximityMap.maxInfluence(factoryProximityInfluence, pos.get_x(), pos.get_y());
            }
            if (u.get_unit_type() == Rocket) {
                auto pos = u.get_location().get_map_location();
                rocketProximityMap.maxInfluence(rocketProximityInfluence, pos.get_x(), pos.get_y());
                if (u.structure_is_built()) {
                    structureProximityMap.maxInfluence(rocketProximityInfluence, pos.get_x(), pos.get_y());
                }
            }
        }
    }
}

void updateDamagedStructuresMap() {
    damagedStructureMap = PathfindingMap(w, h);
    for (auto& unit : ourUnits) {
        if (unit.get_unit_type() == Factory || unit.get_unit_type() == Rocket) {
            double remainingLife = unit.get_health() / (unit.get_max_health() + 0.0);
            if (remainingLife == 1.0) {
                continue;
            }
            auto unitLocation = unit.get_location().get_map_location();
            int unitX = unitLocation.get_x();
            int unitY = unitLocation.get_y();
            for (int i = 0; i < 8; i++) {
                Direction d = (Direction) i;
                auto location = unitLocation.add(d);
                int x = location.get_x();
                int y = location.get_y();
                if (x >= 0 && x < w && y >= 0 && y < h) {
                    if (gc.has_unit_at_location(location) && is_structure(gc.sense_unit_at_location(location).get_unit_type())) {
                        continue;
                    }
                    if (unit.structure_is_built()) {
                        damagedStructureMap.weights[x][y] = max(damagedStructureMap.weights[x][y], 2 * (2.0 - remainingLife));
                    }
                    else {
                        double score = 3 * (1.5 + remainingLife);
                        if (workersNextToMap.weights[unitX][unitY] >= 5) {
                            score /= 1 + 0.05 + workersNextToMap.weights[unitX][unitY];
                        }
                        damagedStructureMap.weights[x][y] = max(damagedStructureMap.weights[x][y], 6 * (1.5 + 0.5 * remainingLife));
                    }
                }
            }
        }
    }
}

void updatePassableMap() {
    passableMap = PathfindingMap(w, h);
    for (int i = 0; i < w; i++) {
        for (int j = 0; j < h; j++) {
            auto location = MapLocation(gc.get_planet(), i, j);
            if (planetMap->is_passable_terrain_at(location)) {
                passableMap.weights[i][j] = 1.0;
            }
            else {
                passableMap.weights[i][j] = numeric_limits<double>::infinity();
            }
        }
    }

    for (const auto& unit : enemyUnits) {
        auto unitMapLocation = unit.get_location().get_map_location();
        passableMap.weights[unitMapLocation.get_x()][unitMapLocation.get_y()] = 1000;
    }

    for (const auto& unit : ourUnits) {
        if (unit.get_location().is_on_map()) {
            auto unitMapLocation = unit.get_location().get_map_location();
            if (is_robot(unit.get_unit_type())) {
                passableMap.weights[unitMapLocation.get_x()][unitMapLocation.get_y()] = 1000;
            }
            else {
                if (unit.structure_is_built()) {
                    passableMap.weights[unitMapLocation.get_x()][unitMapLocation.get_y()] = 1.5;
                }
                else {
                    passableMap.weights[unitMapLocation.get_x()][unitMapLocation.get_y()] = 1000;
                }
            }
        }
    }
}

void updateStuckUnitMap() {
    stuckUnitMap = PathfindingMap(w, h);
    for (const auto& unit : ourUnits) {
        if (!unit.get_location().is_on_map())
            continue;
        const auto& location = unit.get_location().get_map_location();
        int x = location.get_x();
        int y = location.get_y();
        int moveDirections = 0;
        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                int nx = x+dx;
                int ny = y+dy;
                if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                    continue;
                if (passableMap.weights[nx][ny] <= 1) {
                    ++moveDirections;
                }
            }
        }
        double score = 5.0 / (1.0 + moveDirections * moveDirections);
        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                int nx = x+dx;
                int ny = y+dy;
                if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                    continue;
                stuckUnitMap.weights[nx][ny] += score;
            }
        }
    }
}

void updateRocketHazardMap() {
    rocketHazardMap = PathfindingMap(w, h);
    if (planet == Mars) {
        auto rocketLandingInfo = gc.get_rocket_landings();
        for (unsigned int round = gc.get_round(); round < gc.get_round() + 10; ++round) {
            const auto rocketLandings = rocketLandingInfo.get_landings_on_round(round);
            for (const auto& landing : rocketLandings) {
                const auto& destination = landing.get_destination();
                int x = destination.get_x();
                int y = destination.get_y();
                for (int dx = -1; dx <= 1; ++dx) {
                    for (int dy = -1; dy <= 1; ++dy) {
                        int nx = x + dx;
                        int ny = y + dy;
                        if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                            continue;
                        rocketHazardMap.weights[nx][ny] += 0.5;
                    }
                }
                rocketHazardMap.weights[x][y] += 1;
            }
        }
    }
    else {
        for (auto& u : ourUnits) {
            if (u.get_location().is_on_map()) {
                if (u.get_unit_type() == Rocket) {
                    if (u.structure_is_built()) {
                        auto pos = u.get_location().get_map_location();
                        int x = pos.get_x();
                        int y = pos.get_y();
                        for (int dx = -1; dx <= 1; ++dx) {
                            for (int dy = -1; dy <= 1; ++dy) {
                                if (dx == 0 && dy == 0)
                                    continue;
                                int nx = x + dx;
                                int ny = y + dy;
                                if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                                    continue;
                                rocketHazardMap.weights[nx][ny] += 1;
                            }
                        }
                    }
                }
            }
        }
    }
}

void updateRocketAttractionMap() {
    rocketAttractionMap = PathfindingMap(w, h);
    for (auto& u : ourUnits) {
        if (u.get_location().is_on_map()) {
            if (u.get_unit_type() == Rocket && u.structure_is_built() && u.get_structure_garrison().size() < u.get_structure_max_capacity()) {
                auto pos = u.get_location().get_map_location();
                rocketAttractionMap.weights[pos.get_x()][pos.get_y()] = 10;
            }
        }
    }
}

void analyzeEnemyPositions () {
    splashDamagePotential = 0;
    float weight = 0;
    for (const auto& unit : enemyUnits) {
        auto nearby = gc.sense_nearby_units_by_team(unit.get_map_location(), 2, enemyTeam);
        splashDamagePotential += nearby.size();
        weight += 1;
    }
    if (weight > 0) splashDamagePotential /= weight;

    // Assume at least a multiplier of 2.
    // Important in the beginning of the game when we cannot see any enemies
    splashDamagePotential = max(splashDamagePotential, 2.0f);
    cerr << "Splash damage potential: " << splashDamagePotential << endl;

}
void createUnits() {
    for (const auto& unit : ourUnits) {
        assert(gc.has_unit(unit.get_id()));
        const unsigned id = unit.get_id();
        BotUnit* botUnitPtr = nullptr;

        if (unitMap.find(id) == unitMap.end()) {
            switch(unit.get_unit_type()) {
                case Worker: botUnitPtr = new BotWorker(unit); break;
                case Knight: botUnitPtr = new BotKnight(unit); break;
                case Ranger: botUnitPtr = new BotRanger(unit); break;
                case Mage: botUnitPtr = new BotMage(unit); break;
                case Healer: botUnitPtr = new BotHealer(unit); break;
                case Factory: botUnitPtr = new BotFactory(unit); break;
                case Rocket: botUnitPtr = new BotRocket(unit); break;
                default:
                    cerr << "Unknown unit type!" << endl;
                    exit(1);
            }
            if (unit.get_unit_type() == Worker && state.typeCount[Worker] > 100) {
                botUnitPtr->isRocketFodder = true;
            }
            unitMap[id] = botUnitPtr;
        } else {
            botUnitPtr = unitMap[id];
        }
        botUnitPtr->unit = unit.clone();
    }
}

map<UnitType, double> timeUsed;

bool tickUnits(bool firstIteration, int unitTypes = -1) {
    bool anyTickDone = false;
    for (int iteration = 0; iteration < 2; ++iteration) {
        for (const auto& unit : ourUnits) {
            auto unitType = unit.get_unit_type();
            if (iteration == 0) {
                if (unitType != Healer) {
                    continue;
                }
            }
            else {
                if (unitType == Healer) {
                    continue;
                }
            }
            auto botunit = unitMap[unit.get_id()];
            if (botunit == nullptr) {
                continue;
            }
            if (firstIteration) {
                botunit->hasDoneTick = false;
            }
            if (!botunit->hasDoneTick && (1 << (int)unit.get_unit_type()) & unitTypes) {
                double start = millis();
                botunit->tick();
                double dt = millis() - start;
                timeUsed[unitType] += dt;
                anyTickDone |= botunit->hasDoneTick;
            }
        }
    }
    return anyTickDone;
}

void doOvercharge() {
    for (const auto& unit : ourUnits) {
        auto botunit = unitMap[unit.get_id()];
        if (botunit == nullptr) {
            continue;
        }
        if (botunit->unit.get_unit_type() != Healer) {
            continue;
        }
        double start = millis();
        ((BotHealer*)botunit)->doOvercharge();
        double dt = millis() - start;
        timeUsed[botunit->unit.get_unit_type()] += dt;
    }
}

void executeMacroObjects() {
    sort(macroObjects.rbegin(), macroObjects.rend());
    bestMacroObjectScore = 0;
    bool failedPaying = false;
    for (auto& macroObject : macroObjects) {
        if (macroObject.score <= 0) {
            continue;
        }
        if (failedPaying && macroObject.cost) {
            continue;
        }
        if (gc.get_karbonite() >= macroObject.cost) {
            macroObject.execute();
        } else {
            failedPaying = true;
            bestMacroObjectScore = macroObject.score;
        }
    }
    macroObjects.clear();
}

Researcher researcher;
void updateResearch() {
    auto researchInfo = gc.get_research_info();
    if (researchInfo.get_queue().size() == 0) {
        auto type = researcher.getBestResearch();
        gc.queue_research(type);
    }
}

void updateResearchStatus() {
    auto researchInfo = gc.get_research_info();
    if (researchInfo.get_level(Healer) >= 3) {
        hasOvercharge = true;
    }
    if (researchInfo.get_level(Mage) >= 4) {
        hasBlink = true;
    }
}

int computeConnectedness() {
    PathfindingMap targetMap(w, h);

    int connectedness = 0;
    Pathfinder pathfinder;
    auto& initial_units = gc.get_starting_planet(Earth).get_initial_units();
    for (auto& enemy : initial_units) {
        if (enemy.get_team() == enemyTeam && enemy.get_location().is_on_map()) {
            targetMap.addInfluence(1000, enemy.get_map_location());
        }
    }

    PathfindingMap passableMap2 = passableMap;

    bool changed = true;
    while(connectedness < 5 && changed) {
        changed = false;
        for (auto& unit : initial_units) {
            if (unit.get_team() != enemyTeam && unit.get_location().is_on_map()) {
                auto pos = unit.get_location().get_map_location();
                auto path = pathfinder.getPath(pos, targetMap, passableMap2);
                cout << "Found path of length " << path.size() << endl;
                if (path.size() > 1) {
                    changed = true;
                    connectedness++;
                    for (size_t i = 1; i < path.size() - 1; i++) {
                        passableMap2.addInfluence(numeric_limits<double>::infinity(), MapLocation(pos.get_planet(), path[i].x, path[i].y));
                    }
                }
            }
        }
    }

    cout << "Map is " << connectedness << " connected" << endl;
    return connectedness;
}

double mageCoordinationTime;

void coordinateMageAttacks() {
    if (!hasOvercharge || !state.typeCount[Mage]) {
        return;
    }
    auto start = millis();
    while (true) {
        PathfindingMap canShootAtMap(w, h);
        PathfindingMap shootMap(w, h);
        PathfindingMap healerMap(w, h);
        for (auto& unit : allUnits) {
            if (!unit.get_location().is_on_map())
                continue;
            const auto& mapLocation = unit.get_location().get_map_location();
            int x = mapLocation.get_x();
            int y = mapLocation.get_y();
            double multiplier = unit.get_team() == gc.get_team() ? -1 : 1;
            switch (unit.get_unit_type()) {
                case Ranger:
                    multiplier *= 1;
                    break;
                case Healer:
                    multiplier *= 1.3;
                    break;
                case Knight:
                    multiplier *= 0.8;
                    break;
                case Mage:
                    multiplier *= 1.3;
                    break;
                case Worker:
                    multiplier *= 0.25;
                    break;
                case Factory:
                    multiplier *= 0.9;
                    break;
                case Rocket:
                    if (planet == Earth)
                        multiplier *= 1.1;
                    else
                        multiplier *= 0.01;
                    break;
            }
            canShootAtMap.weights[x][y] = 1;
            for (int dx = -1; dx <= 1; dx++) {
                for (int dy = -1; dy <= 1; dy++) {
                    int nx = x+dx;
                    int ny = y+dy;
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                        continue;
                    shootMap.weights[nx][ny] += multiplier;
                }
            }
            if (unit.get_team() == gc.get_team() && unit.get_unit_type() == Healer && unit.get_ability_heat() < 10) {
                healerMap.addInfluence(healerTargetInfluence, x, y);
            }
        }
        PathfindingMap targetMap(w, h);
        for (int x = 0; x < w; ++x) {
            for (int y = 0; y < h; ++y) {
                const MapLocation location(planet, x, y);
                if (shootMap.weights[x][y] > 0 && canShootAtMap.weights[x][y] > 0) {
                    targetMap.maxInfluenceMultiple(mageTargetInfluence, x, y, shootMap.weights[x][y]);
                }
            }
        }
        PathfindingMap distanceToMage(w, h);
        vector<vector<int> > minHealerSum(w, vector<int>(h));
        vector<vector<pair<int, int> > > distanceToMageParent(w, vector<pair<int, int>>(h, make_pair(-1, -1)));
        distanceToMage += 1000;
        queue<pair<int, int> > bfsQueue;
        for (auto& unit : ourUnits) {
            if (unit.get_unit_type() != Mage) {
                continue;
            }
            if (!unit.get_location().is_on_map())
                continue;
            const auto& mapLocation = unit.get_location().get_map_location();
            int x = mapLocation.get_x();
            int y = mapLocation.get_y();
            distanceToMage.weights[x][y] = 0;
            if (unit.get_movement_heat() < 10) {
                distanceToMage.weights[x][y] -= 1;
                if (hasBlink && unit.get_ability_heat() < 10) {
                    distanceToMage.weights[x][y] -= 1;
                    bfsQueue.push(make_pair(x, y));
                }
            }
            minHealerSum[x][y] = healerMap.weights[x][y];
        }
        for (int x = 0; x < w; ++x) {
            for (int y = 0; y < h; ++y) {
                if (distanceToMage.weights[x][y] == -1) {
                    bfsQueue.push(make_pair(x, y));
                }
            }
        }
        for (int x = 0; x < w; ++x) {
            for (int y = 0; y < h; ++y) {
                if (distanceToMage.weights[x][y] == 0) {
                    bfsQueue.push(make_pair(x, y));
                }
            }
        }
        while(!bfsQueue.empty()) {
            auto cur = bfsQueue.front();
            bfsQueue.pop();
            int x = cur.first;
            int y = cur.second;
            int d = distanceToMage.weights[x][y];
            if (2 * minHealerSum[x][y] <= d) {
                continue;
            }
            int D = d+1;
            if (d%2 == 0)
                ++D;
            for (int dx = -1; dx <= 1; dx++) {
                for (int dy = -1; dy <= 1; dy++) {
                    int nx = x+dx;
                    int ny = y+dy;
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                        continue;
                    if (passableMap.weights[nx][ny] > 1)
                        continue;
                    if (D < distanceToMage.weights[nx][ny]) {
                        distanceToMage.weights[nx][ny] = D;
                        bfsQueue.push(make_pair(nx, ny));
                        distanceToMageParent[nx][ny] = cur;
                        minHealerSum[nx][ny] = -1;
                    }
                    if (D == distanceToMage.weights[nx][ny]) {
                        int healerSum = min(minHealerSum[x][y], (int)(healerMap.weights[nx][ny] + (distanceToMage.weights[nx][ny])/2));
                        if (healerSum > minHealerSum[nx][ny]) {
                            minHealerSum[nx][ny] = healerSum;
                            distanceToMageParent[nx][ny] = cur;
                        }
                    }
                }
            }
            if (d%2 == 0 && hasBlink) {
                D = d+1;
                for (int dx = -2; dx <= 2; dx++) {
                    for (int dy = -2; dy <= 2; dy++) {
                        int nx = x+dx;
                        int ny = y+dy;
                        if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                            continue;
                        if (passableMap.weights[nx][ny] > 1)
                            continue;
                        if (D < distanceToMage.weights[nx][ny]) {
                            distanceToMage.weights[nx][ny] = D;
                            bfsQueue.push(make_pair(nx, ny));
                            distanceToMageParent[nx][ny] = cur;
                            minHealerSum[nx][ny] = -1;
                        }
                        if (D == distanceToMage.weights[nx][ny]) {
                            int healerSum = min(minHealerSum[x][y], (int)(healerMap.weights[nx][ny] + (distanceToMage.weights[nx][ny]+1)/2));
                            if (healerSum > minHealerSum[nx][ny]) {
                                minHealerSum[nx][ny] = healerSum;
                                distanceToMageParent[nx][ny] = cur;
                            }
                        }
                    }
                }
            }
        }
        vector<pair<double, pair<int, int> > > bestTargets;
        for (int x = 0; x < w; ++x) {
            for (int y = 0; y < h; ++y) {
                if (targetMap.weights[x][y] > 0 && distanceToMage.weights[x][y] > 0 && distanceToMage.weights[x][y] < 1000) {
                    double score = targetMap.weights[x][y] / (distanceToMage.weights[x][y]);
                    if (healerMap.weights[x][y] > 0)
                        score *= 1.2;
                    if (healerMap.weights[x][y] > 1)
                        score *= 1.1;
                    if (score < 0.6)
                        continue;
                    bestTargets.emplace_back(score, make_pair(x, y));
                }
            }
        }
        if (!bestTargets.size()) {
            break;
        }
        sort(bestTargets.rbegin(), bestTargets.rend());
        bool anyOvercharge = false;
        for (auto& target : bestTargets) {
            vector<pair<int, int> > path;
            auto curNode = target.second;
            while(curNode.first != -1) {
                path.push_back(curNode);
                curNode = distanceToMageParent[curNode.first][curNode.second];
            }
            reverse(path.begin(), path.end());
            for (auto& node : path) {
                cout << node.first << " " << node.second << " - " << healerMap.weights[node.first][node.second] << " " << distanceToMage.weights[node.first][node.second] << " " << minHealerSum[node.first][node.second] << endl;
            }
            MapLocation location(planet, path[0].first, path[0].second);
            if (!canSenseLocation[path[0].first][path[0].second]) {
                cout << "Error! Couldn't sense location!" << endl;
                continue;
            }
            if (!gc.has_unit_at_location(location)) {
                cout << "Error! Doesn't have unit at location!" << endl;
                continue;
            }
            auto mage = gc.sense_unit_at_location(location);
            auto& botUnit = unitMap[mage.get_id()];
            for (size_t i = 0; i < path.size()-1; ++i) {
                mage_attack(botUnit->unit);
                if (botUnit->unit.get_health() <= 0) {
                    cout << "Warning! The attacking mage died" << endl;
                    break;
                }
                if (!botUnit->unit.get_location().is_on_map()) {
                    cout << "Warning! Attacking mage entered a structure" << endl;
                    break;
                }
                auto location = botUnit->unit.get_location().get_map_location();
                bool hasDoneAnything = false;
                if (botUnit->unit.get_movement_heat() >= 10) {
                    auto nearbyUnits = gc.sense_nearby_units_by_team(location, 30, gc.get_team());
                    double bestScore = -1;
                    unsigned int bestUnitId = 0;
                    //int bestUnitX=0;
                    //int bestUnitY=0;
                    for (const auto& unit : nearbyUnits) {
                        if (unit.get_unit_type() == Healer && unit.get_ability_heat() < 10) {
                            const auto& location = unit.get_location().get_map_location();
                            int x = location.get_x();
                            int y = location.get_y();
                            int lastOverchargeChance = i;
                            for (size_t j = i+1; j+1 < path.size(); ++j) {
                                int dx = x - path[j].first;
                                int dy = y - path[j].second;
                                int dis2 = dx*dx + dy*dy;
                                if (dis2 <= 30) {
                                    lastOverchargeChance = j;
                                }
                            }
                            double score = 100 - lastOverchargeChance + 1.0 / (enemyNearbyMap.weights[x][y] + 1.0);
                            if (score > bestScore) {
                                bestScore = score;
                                bestUnitId = unit.get_id();
                                //bestUnitX = x;
                                //bestUnitY = y;
                            }
                        }
                    }
                    if (bestScore > 0) {
                        gc.overcharge(bestUnitId, botUnit->unit.get_id());
                        invalidate_unit(botUnit->unit.get_id());
                        invalidate_unit(bestUnitId);
                        //assert(botUnit->unit.get_attack_heat() == 0);
                        //assert(botUnit->unit.get_movement_heat() == 0);
                        anyOvercharge = true;
                        hasDoneAnything = true;
                    }
                }
                if (botUnit->unit.get_movement_heat() >= 10) {
                    cout << "Warning! Attacking mage couldn't make it to target spot" << endl;
                    //assert(0);
                    break;
                }
                if (hasBlink && botUnit->unit.get_ability_heat() < 10) {
                    int j = min(i+1, path.size()-1);
                    const MapLocation blinkTo(planet, path[j].first, path[j].second);
                    if (gc.can_begin_blink(botUnit->unit.get_id(), blinkTo)) {
                        passableMap.weights[location.get_x()][location.get_y()] = 1;
                        gc.blink(botUnit->unit.get_id(), blinkTo);
                        invalidate_unit(botUnit->unit.get_id());
                        mage_attack(botUnit->unit);
                        i = j;
                        anyOvercharge = true;
                        location = blinkTo;
                        hasDoneAnything = true;
                        passableMap.weights[location.get_x()][location.get_y()] = 1000;
                    }
                }
                if (i < path.size()-1) {
                    botUnit->moveToLocation(MapLocation(planet, path[i+1].first, path[i+1].second));
                    mage_attack(botUnit->unit);
                    location = botUnit->unit.get_location().get_map_location();
                    if (location.get_x() == path[i].first && location.get_y() == path[i].second) {
                        if (!hasDoneAnything) {
                            cout << "Warning! Attacking mage couldn't make it to target spot" << endl;
                            //assert(0);
                            break;
                        }
                        --i;
                    }
                    else {
                        anyOvercharge = true;
                    }
                }
            }
            if (anyOvercharge)
                break;
            mage_attack(botUnit->unit);
        }
        if (!anyOvercharge)
            break;
        invalidate_units();
        findUnits();
        createUnits();
    }
    mageCoordinationTime += millis()-start;
}

#ifdef CUSTOM_BACKTRACE
void* __libc_stack_end;
#endif

int main() {
#ifdef CUSTOM_BACKTRACE
    __libc_stack_end = __builtin_frame_address(0);
#endif
    setup_signal_handlers();
    srand(time(0));

    printf("Player C++ bot starting\n");
    printf("Connecting to manager...\n");

    unitTypeToString[Worker]="Worker";
    unitTypeToString[Ranger]="Ranger";
    unitTypeToString[Mage]="Mage";
    unitTypeToString[Knight]="Knight";
    unitTypeToString[Healer]="Healer";
    unitTypeToString[Factory]="Factory";
    unitTypeToString[Rocket]="Rocket";

    // std::default_random_engine generator;
    // std::uniform_int_distribution<int> distribution (0,8);
    // auto dice = std::bind ( distribution , generator );
    // Most methods return pointers; methods returning integers or enums are the only exception.

    if (bc_has_err()) {
        // If there was an error creating gc, just die.
        printf("Failed, dying.\n");
        exit(1);
    }
    printf("Connected!\n");

    planet = gc.get_planet();
    planetMap = &gc.get_starting_planet(gc.get_planet());
    w = planetMap->get_width();
    h = planetMap->get_height();
    initKarboniteMap();
    initInfluence();

    computeOurStartingPositionMap();
    updatePassableMap();
    discoveryMap = PathfindingMap(w, h);
    if (planet == Earth) {
        computeDistancesToInitialLocations();
        mapConnectedness = computeConnectedness();
        existsPathToEnemy = mapConnectedness > 0;
        if (!existsPathToEnemy) {
            cout << "There doesn't exist a path to the enemy!" << endl;
        }
        else {
            cout << "There exists a path to the enemy!" << endl;
        }
    } else {
        // Mars
        // Whatever
        mapConnectedness = 10;
        existsPathToEnemy = true;
    }

    anyReasonableLandingSpotOnInitialMars = get<0>(find_best_landing_spot());

    enemyPositionMap = PathfindingMap(w, h);
    // Write that we have no rockets in the beginning.
    gc.write_team_array(1, 0);

    // Note: Used for tournament script to show statistics
    if (planet == Earth) cout << "MAP IS " << w << "x" << h << endl;

    double preprocessingComputationTime = 0;
    double totalTurnTime = 0.0;
    mapComputationTime = 0;
    targetMapComputationTime = 0;
    costMapComputationTime = 0;
    attackComputationTime = 0;
    pathfindingTime = 0;

    turnsSinceLastFight = 0;
    // loop through the whole game.
    while (true) {
        time_t t0 = millis();
        unsigned round = gc.get_round();
        printf("Round: %d\n", round);
        int timeLeft = gc.get_time_left_ms();
        // If less than 0.5 seconds left, then enter low power mode
        lowTimeRemaining = timeLeft < 500;
        if (lowTimeRemaining) {
            printf("LOW TIME REMAINING\n");
        }
        printf("Time remaining: %d\n", timeLeft);

        ++turnsSinceLastFight;
        updateResearchStatus();
        findUnits();

        updateCanSenseLocation();
        updateDiscoveryMap();
        reusableMaps.clear();
        updateAsteroids();
        updateEnemyPositionMap();
        updateNearbyFriendMap();

        // NOTE: this call also updates enemy position map for some reason
        updateWorkerMaps();
        updateKarboniteMap();
        updateFuzzyKarboniteMap();
        updateEnemyInfluenceMaps();
        updateMageNearbyMap();
        updateStructureProximityMap();
        updateDamagedStructuresMap();

        updateRocketHazardMap();
        if (planet == Earth) {
            updateRocketAttractionMap();
        }

        if (!lowTimeRemaining) analyzeEnemyPositions();

        unitShouldGoToRocket.clear();

        for (auto& unit : ourUnits) {
            if (unit.get_unit_type() == Rocket) {
                selectTravellersForRocket(unit);
            }
        }

        macroObjects.clear();
        state = State();
        state.totalRobotDamage = 0;
        for (auto& unit : ourUnits) {
            state.typeCount[unit.get_unit_type()]++;
            state.totalUnitCount++;
            if (is_robot(unit.get_unit_type())) {
                state.totalRobotDamage += unit.get_max_health() - unit.get_health();
            }
        }
        state.remainingKarboniteOnEarth = karboniteMap.sum();
        state.earthTotalUnitCount = gc.get_team_array(Earth)[0];

        updatePassableMap();
        updateStuckUnitMap();

        auto t1 = millis();
        preprocessingComputationTime += t1-t0;

        bool firstIteration = true;
        workersMove = false;
        while (true) {
            auto t2 = millis();
            createUnits();
            cout << "We have " << ourUnits.size() << " units" << endl;
            coordinateMageAttacks();
            bool anyTickDone = tickUnits(firstIteration, 1 << (int)Healer);

            anyTickDone |= tickUnits(false);
            if (hasOvercharge) doOvercharge();
            auto t3 = millis();
            cout << "Iteration: " << (t3 - t2) << endl;
            
            executeMacroObjects();

            if (firstIteration) {
                workersMove = true;
                updateFuzzyKarboniteMap();
                findUnits();
                createUnits();
                updateDamagedStructuresMap();
                MapReuseObject reuseObject(MapType::Target, Worker, false);
                reusableMaps.erase(reuseObject);
                reuseObject.isHurt = true;
                reusableMaps.erase(reuseObject);
                anyTickDone |= tickUnits(false, 1 << (int)Worker);
                firstIteration = false;
            }

            if (!anyTickDone) break;

            findUnits();
            // auto t4 = millis();
            // cout << "Execute: " << (t4 - t3) << endl;
        }

        auto t5 = millis();
        cout << "All iterations: " << std::round(t5 - t1) << endl;
        updateResearch();

        double turnTime = millis() - t0;
        totalTurnTime += turnTime;

        if (!lowTimeRemaining) {
            auto t6 = millis();
            cout << "Research: " << (t6 - t5) << endl;

            cout << "Map computation time: " << std::round(mapComputationTime) << endl;
            cout << "   Target map computation time: " << std::round(targetMapComputationTime) << endl;
            cout << "   Cost map computation time: " << std::round(costMapComputationTime) << endl;
            cout << "Pathfinding time: " << std::round(pathfindingTime) << endl;
            cout << "Attack computation time: " << std::round(attackComputationTime) << endl;
            cout << "Mage coordination time: " << std::round(mageCoordinationTime) << endl;
            cout << "Invalidation time: " << std::round(unitInvalidationTime) << endl;
            cout << "Preprocessing time: " << std::round(preprocessingComputationTime) << endl;
            for (auto it : timeUsed) {
                cout << unitTypeToString[it.first] << ": " << std::round(it.second) << endl;
            }

            cout << "Turn time: " << turnTime << endl;
            
            cout << "Average: " << std::round(totalTurnTime/gc.get_round()) << endl;
            cout << "Total: " << std::round(totalTurnTime) << endl;
            cout << "Attacker success rate: " << averageAttackerSuccessRate << endl;
            cout << "Average healer success rate: " << averageHealerSuccessRate << endl;
        }

        gc.write_team_array(0, state.totalUnitCount);

        // this line helps the output logs make more sense by forcing output to be sent
        // to the manager.
        // it's not strictly necessary, but it helps.
        // pause and wait for the next turn.
        fflush(stdout);
        gc.next_turn();
    }
    // I'm convinced C++ is the better option :)
}
