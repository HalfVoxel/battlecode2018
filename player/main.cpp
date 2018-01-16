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
double bestMacroObjectScore;



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

bool isOnMap(MapLocation location) {
   return location.get_x() >= 0 && location.get_y() >= 0 && location.get_x() < w && location.get_y() < h;
}

struct BotWorker : BotUnit {
    BotWorker(const Unit& unit) : BotUnit(unit) {}


    PathfindingMap getTargetMap() {
        bool isHurt = (unit.get_health() < unit.get_max_health());
        MapReuseObject reuseObject(MapType::Target, unit.get_unit_type(), isHurt);

        PathfindingMap targetMap;
        if (reusableMaps.count(reuseObject)) {
            targetMap = reusableMaps[reuseObject];
        }
        else {
            targetMap = fuzzyKarboniteMap + damagedStructureMap - enemyNearbyMap * 1.0 + 0.01;
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
            reusableMaps[reuseObject] = targetMap;
        }

        for (auto rocketId : unitShouldGoToRocket[unit.get_id()]) {
            auto unit = gc.get_unit(rocketId);
            if(!unit.get_location().is_on_map()) {
                continue;
            }
            auto rocketLocation = unit.get_location().get_map_location();
            targetMap.weights[rocketLocation.get_x()][rocketLocation.get_y()] += 100;
        }

        return targetMap;
    }

    PathfindingMap getCostMap() {
        MapReuseObject reuseObject(MapType::Cost, unit.get_unit_type(), false);
        if (reusableMaps.count(reuseObject)) {
            return reusableMaps[reuseObject];
        }
        else {
            auto costMap = ((passableMap * 50.0)/(fuzzyKarboniteMap + 50.0)) + enemyNearbyMap + enemyInfluenceMap + workerProximityMap;
            reusableMaps[reuseObject] = costMap;
            return costMap;
        }
    }

    void tick() {

        if (!unit.get_location().is_on_map()) {
            return;
        }

        hasDoneTick = true;

        const auto locus = unit.get_location().get_map_location();
        const auto nearby = gc.sense_nearby_units(locus, 2);

        auto unitMapLocation = unit.get_location().get_map_location();

        const unsigned id = unit.get_id();

        for (auto& place : nearby) {
            //Building 'em blueprints
            if(gc.can_build(id, place.get_id())) {
                const int& placeId = place.get_id();
                double score = (place.get_health() / (0.0 + place.get_max_health()));
                macroObjects.emplace_back(score, 0, 1, [=]{
                    if(gc.can_build(id, placeId)) {
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
                }
            });
        }

        double karbonitePerWorker = (state.remainingKarboniteOnEarth + 0.0) / state.typeCount[Worker];
        double replicateScore = karbonitePerWorker * 0.008 + 2.5 / state.typeCount[Worker];

        if (planet == Earth) {
            for (int i = 0; i < 8; i++) {
                Direction d = (Direction) i;
                // Placing 'em blueprints
                auto newLocation = unitMapLocation.add(d);
                if(isOnMap(newLocation) && gc.can_sense_location(newLocation) && gc.is_occupiable(newLocation)) {
                    double score = state.typeCount[Factory] < 3 ? (3 - state.typeCount[Factory]) : 5.0 / (5.0 + state.typeCount[Factory]);
                    macroObjects.emplace_back(score, unit_type_get_blueprint_cost(Factory), 2, [=]{
                        if(gc.can_blueprint(id, Factory, d)){
                            gc.blueprint(id, Factory, d);
                        }
                    });
                    auto researchInfo = gc.get_research_info();
                    if (researchInfo.get_level(Rocket) >= 1) {
                        double factor = 0.01;
                        if (gc.get_round() > 600) {
                            factor = 0.5;
                        }
                        if (!launchedWorkerCount && !state.typeCount[Rocket]) {
                            factor += 0.5;
                        }
                        double score = factor * (state.totalUnitCount - state.typeCount[Factory] - 12 * state.typeCount[Rocket]);
                        macroObjects.emplace_back(score, unit_type_get_blueprint_cost(Rocket), 2, [=]{
                            if(gc.can_blueprint(id, Rocket, d)){
                                gc.blueprint(id, Rocket, d);
                            }
                        });
                    }
                }
            }
        }

        auto nextLocation = unitMapLocation;

        if (gc.is_move_ready(unit.get_id())) {
            auto nextLocation = getNextLocation();
            moveToLocation(nextLocation);
        }

        if(unit.get_ability_heat() < 10 && unit.get_location().is_on_map() && (planet == Earth || state.earthTotalUnitCount == 0 || state.typeCount[Worker] < 8) && replicateScore > bestMacroObjectScore - 0.1) {
            unitMapLocation = nextLocation;
            nextLocation = getNextLocation(unitMapLocation, false);

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
            targetMap = PathfindingMap(w, h);
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
            targetMap /= (enemyNearbyMap + 1.0);
            reusableMaps[reuseObject] = targetMap;
        }

        for (auto rocketId : unitShouldGoToRocket[unit.get_id()]) {
            auto unit = gc.get_unit(rocketId);
            if(!unit.get_location().is_on_map()) {
                continue;
            }
            auto rocketLocation = unit.get_location().get_map_location();
            targetMap.weights[rocketLocation.get_x()][rocketLocation.get_y()] += 100;
        }


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

            auto costMap = passableMap + healerProximityMap + enemyInfluenceMap;
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
            invalidate_units();
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
                invalidate_units();
                succeededHealing = true;
            }
        }
        double interpolationFactor = 0.99;
        averageHealerSuccessRate = averageHealerSuccessRate * interpolationFactor + succeededHealing * (1-interpolationFactor);
    }

    void doOvercharge() {
        if (hasOvercharge && unit.get_ability_heat() < 10 && unit.get_location().is_on_map()) {
            const auto nearby = gc.sense_nearby_units(unit.get_location().get_map_location(), unit.get_ability_range());

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
                invalidate_units();
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
        if (gc.can_produce_robot(id, Ranger)){
            double score = 2;
            macroObjects.emplace_back(score, unit_type_get_factory_cost(Ranger), 2, [=] {
                if (gc.can_produce_robot(id, Ranger)) {
                    gc.produce_robot(id, Ranger);
                }
            });
        }
        if (gc.can_produce_robot(id, Worker)){
            double score = 0;
            auto researchInfo = gc.get_research_info();
            if (state.typeCount[Worker] == 0 && (researchInfo.get_level(Rocket) >= 1 || state.typeCount[Factory] < 3)) {
                score += 10;
            }
            macroObjects.emplace_back(score, unit_type_get_factory_cost(Worker), 2, [=] {
                if (gc.can_produce_robot(id, Worker)) {
                    gc.produce_robot(id, Worker);
                }
            });
        }
        if (gc.can_produce_robot(id, Mage)){
            auto location = unit.get_location().get_map_location();
            double score = enemyInfluenceMap.weights[location.get_x()][location.get_y()] * 0.4;
            macroObjects.emplace_back(score, unit_type_get_factory_cost(Mage), 2, [=] {
                if (gc.can_produce_robot(id, Mage)) {
                    gc.produce_robot(id, Mage);
                }
            });
        }
        if (gc.can_produce_robot(id, Healer)){
            double score = 0.0;
            if (state.typeCount[Ranger] > 6) {
                score += 2.5;
            }
            if (state.typeCount[Ranger] > 10) {
                score += 1.5;
            }
            if (state.typeCount[Ranger] > 14) {
                score += 1.5;
            }
            if (state.totalRobotDamage > 200) {
                score += 3.0;
            }
            score /= state.typeCount[Healer];
            score += averageHealerSuccessRate * 1.8;
            macroObjects.emplace_back(score, unit_type_get_factory_cost(Healer), 2, [=] {
                if (gc.can_produce_robot(id, Healer)) {
                    gc.produce_robot(id, Healer);
                }
            });
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
        if (u.get_unit_type() == Worker && state.typeCount[Worker] <= 3) {
            continue;
        }
        if (u.get_location().is_on_map()) {
            auto uLocation = u.get_location().get_map_location();
            int dx = uLocation.get_x() - unitLocation.get_x();
            int dy = uLocation.get_y() - unitLocation.get_y();
            candidates.emplace_back(dx*dx + dy*dy, u.get_id());
        }
    }
    sort(candidates.begin(), candidates.end());
    for (int i = 0; i < min((int) candidates.size(), remainingTravellers); i++) {
        if (gc.get_unit(candidates[i].second).get_unit_type() == Worker) {
            if (hasWorker) {
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
        switch(researchInfo.get_level(Ranger)) {
            case 0:
                scores[Ranger] = 10 + 0.1 * state.typeCount[Ranger];
                break;
            case 1:
                scores[Ranger] = 5 + 0.1 * state.typeCount[Ranger];
                break;
            case 2:
                scores[Ranger] = 3 + 0.1 * state.typeCount[Ranger];
                break;
        }
        switch(researchInfo.get_level(Healer)) {
            case 0: 
                scores[Healer] = 9 + 2.0 * state.typeCount[Healer];
                break;
            case 1: 
                scores[Healer] = 6 + 1.0 * state.typeCount[Healer];
                break;
            case 2: 
                scores[Healer] = 1.0 * state.typeCount[Healer];
                break;
        }
        switch(researchInfo.get_level(Worker)) {
            case 0:
                scores[Worker] = 5;
                break;
            case 1:
                scores[Worker] = 3;
                break;
            case 2:
                scores[Worker] = 3;
                break;
            case 3:
                scores[Worker] = 6;
                break;
        }
        switch(researchInfo.get_level(Rocket)) {
            case 0:
                scores[Rocket] = 7;
                break;
            case 1:
                scores[Rocket] = 6;
                break;
            case 2:
                scores[Rocket] = 6;
                break;
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
                    if (x >= 0 && y >= 0 && x < w && y < w) {
                        if (planetMap->is_passable_terrain_at(MapLocation(gc.get_planet(), x, y))){
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
                    if (x >= 0 && y >= 0 && x < w && y < w) {
                        double w;
                        if (planetMap->is_passable_terrain_at(MapLocation(gc.get_planet(), x, y))){
                            w = weight2;
                        }
                        else {
                            w = 0;
                        }
                        newEnemyPositionMap.weights[x][y] += w * enemyPositionMap.weights[i][j];
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

// NOTE: this call also updates enemy position map for some reason
void updateKarboniteMap() {
    for (int i = 0; i < w; i++) {
        for (int j = 0; j < h; j++) {
            auto location = MapLocation(gc.get_planet(), i, j);
            if (gc.can_sense_location(location)) {
                if (karboniteMap.weights[i][j]) {
                    int karbonite = gc.get_karbonite_at(location);
                    karboniteMap.weights[i][j] = karbonite;
                }
                enemyPositionMap.weights[i][j] = 0;
            }
        }
    }

    fuzzyKarboniteMap = PathfindingMap(w, h);
    for (int i = 0; i < w; i++) {
        for (int j = 0; j < h; j++) {
            for (int k = -1; k <= 1; k++) {
                for (int l = -1; l <= 1; l++) {
                    int x = i+k;
                    int y = j+l;
                    if (x >= 0 && y >= 0 && x < w && y < w){
                        double kar = karboniteMap.weights[x][y];
                        kar = log(kar + 1);
                        if (k != 0 || l != 0)
                            kar *= 0.9;
                        fuzzyKarboniteMap.weights[i][j] = max(fuzzyKarboniteMap.weights[i][j], kar);
                    }
                }
            }
        }
    }
}

void updateEnemyInfluenceMaps(){
    enemyInfluenceMap = PathfindingMap(w, h);
    enemyNearbyMap = PathfindingMap(w, h);
    for (auto& u : enemyUnits) {
        if (u.get_location().is_on_map()) {
            auto pos = u.get_location().get_map_location();
            if (u.get_unit_type() == Ranger) {
                enemyInfluenceMap.addInfluence(enemyRangerTargetInfluence, pos.get_x(), pos.get_y());
            }
            if (u.get_unit_type() == Mage) {
                enemyInfluenceMap.addInfluence(mageTargetInfluence, pos.get_x(), pos.get_y());
            }
            if (u.get_unit_type() == Knight) {
                enemyInfluenceMap.addInfluence(knightTargetInfluence, pos.get_x(), pos.get_y());
            }
            enemyNearbyMap.maxInfluence(wideEnemyInfluence, pos.get_x(), pos.get_y());
            enemyPositionMap.weights[pos.get_x()][pos.get_y()] += 1.0;
        }
    }

    auto&& initial_units = gc.get_starting_planet(Earth).get_initial_units();
    for (auto& enemy : initial_units) {
        if (enemy.get_team() == enemyTeam && enemy.get_location().is_on_map()) {
            auto pos = enemy.get_location().get_map_location();
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

void updateWorkerProximityMap() {
    workerProximityMap = PathfindingMap(w, h);
    for (auto& u : ourUnits) {
        if (u.get_location().is_on_map()) {
            if (u.get_unit_type() == Worker) {
                auto pos = u.get_location().get_map_location();
                workerProximityMap.maxInfluence(workerProximityInfluence, pos.get_x(), pos.get_y());
            }
            if (u.get_unit_type() == Factory && u.structure_is_built()) {
                auto pos = u.get_location().get_map_location();
                workerProximityMap.maxInfluence(factoryProximityInfluence, pos.get_x(), pos.get_y());
            }
            if (u.get_unit_type() == Rocket && u.structure_is_built()) {
                auto pos = u.get_location().get_map_location();
                workerProximityMap.maxInfluence(rocketProximityInfluence, pos.get_x(), pos.get_y());
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
            for (int i = 0; i < 8; i++) {
                Direction d = (Direction) i;
                auto location = unit.get_location().get_map_location().add(d);
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
                        damagedStructureMap.weights[x][y] = max(damagedStructureMap.weights[x][y], 3 * (1.5 + remainingLife));
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
                passableMap.weights[unitMapLocation.get_x()][unitMapLocation.get_y()] = 1.5;
            }
        }
    }
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

bool tickUnits(bool firstIteration) {
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
            if (!botunit->hasDoneTick) {
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
}

int main() {
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

    enemyPositionMap = PathfindingMap(w, h);

    double totalTurnTime = 0.0;
    // loop through the whole game.
    mapComputationTime = 0;
    pathfindingTime = 0;
    while (true) {
        time_t t0 = millis();
        unsigned round = gc.get_round();
        printf("Round: %d\n", round);

        updateResearchStatus();
        findUnits();

        reusableMaps.clear();
        updateAsteroids();
        updateEnemyPositionMap();
        updateNearbyFriendMap();

        // NOTE: this call also updates enemy position map for some reason
        updateKarboniteMap();
        updateEnemyInfluenceMaps();
        updateWorkerProximityMap();
        updateDamagedStructuresMap();


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

        auto t1 = millis();
        cout << "Preprocessing: " << (t1-t0) << endl;

        bool firstIteration = true;
        while (true) {
            auto t2 = millis();
            createUnits();
            bool anyTickDone = tickUnits(firstIteration);
            if (hasOvercharge) doOvercharge();
            auto t3 = millis();
            cout << "Iteration: " << (t3 - t2) << endl;

            if (!anyTickDone) break;

            findUnits();
            firstIteration = false;
            executeMacroObjects();
            auto t4 = millis();
            cout << "Execute: " << (t4 - t3) << endl;
        }

        auto t5 = millis();
        cout << "All iterations: " << std::round(t5 - t1) << endl;
        updateResearch();

        auto t6 = millis();
        cout << "Research: " << (t6 - t5) << endl;

        cout << "Map computation time: " << std::round(mapComputationTime) << endl;
        cout << "Pathfinding time: " << std::round(pathfindingTime) << endl;
        cout << "Invalidation time: " << std::round(unitInvalidationTime) << endl;
        for (auto it : timeUsed) {
            cout << unitTypeToString[it.first] << ": " << std::round(it.second) << endl;
        }

        double turnTime = millis() - t0;
        cout << "Turn time: " << turnTime << endl;
        totalTurnTime += turnTime;
        cout << "Average: " << std::round(totalTurnTime/gc.get_round()) << endl;

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
