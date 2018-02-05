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
#include <set>

#include "common.h"
#include "pathfinding.hpp"
#include "influence.h"
#include "rocket.h"
#include "worker.h"
#include "maps.h"

using namespace bc;
using namespace std;


double averageHealerSuccessRate;
map<UnitType, double> timeConsumptionByUnit;
map<UnitType, string> unitTypeToString;
bool hasOvercharge;
bool hasBlink;
bool enemyHasRangers;
bool enemyHasMages;
bool enemyHasKnights;
bool hasUnstuckUnit;
bool hasBuiltMage;
double estimatedEnemyKnights = 0;
double estimatedEnemyRangers = 0;

int turnsSinceLastFight;
int timesStuck = 0;

// On average how many units there are around an enemy unit that we can see
// This is the expected damage multiplier for mages
float splashDamagePotential = 0;


static_assert((int)Worker == 0, "");
static_assert((int)Rocket == 6, "");

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

vector<Unit*> getUnitsInDisk(MapLocation center, int squaredRadius) {
    int r = floor(sqrt(squaredRadius));
    int x = center.get_x();
    int y = center.get_y();
    vector<Unit*> ret;
    for (int dx = -r; dx <= r; ++dx) {
        for (int dy = -r; dy <= r; ++dy) {
            int dis2 = dx * dx + dy * dy;
            if (dis2 > squaredRadius)
                continue;
            int nx = x + dx;
            int ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                continue;
            if (unitAtLocation[nx][ny] != nullptr)
                ret.push_back(unitAtLocation[nx][ny]);
        }
    }
    return ret;
}


struct BotHealer : BotUnit {
    BotHealer(const Unit& unit) : BotUnit(unit) {}

    PathfindingMap getTargetMap() {
        MapReuseObject reuseObject(MapType::Target, unit.get_unit_type(), false);

        PathfindingMap targetMap;
        if (reusableMaps.count(reuseObject)) {
            targetMap = reusableMaps[reuseObject];
        }
        else {
            targetMap = PathfindingMap(w, h) + 0.001;
            for (auto& u : enemyUnits) {
                if (!u.get_location().is_on_map()) {
                    continue;
                }
                if (is_robot(u.get_unit_type())) {
                    auto uMapLocation = u.get_location().get_map_location();
                    targetMap.maxInfluence(healerSafetyInfluence, uMapLocation.get_x(), uMapLocation.get_y());
                }
            }
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

                    double factor = 1;
                    switch (u.get_unit_type()) {
                        case Worker:
                            factor = 0.1;
                            break;
                        case Mage:
                            factor = 1.6;
                            break;
                        case Ranger:
                            factor = 1.0;
                            break;
                        case Healer:
                            factor = 1.2;
                            break;
                        case Knight:
                            factor = 1.1;
                            break;
                        default:
                            break;
                    }
                    remainingLife -= factor;

                    targetMap.maxInfluenceMultiple(healerTargetInfluence, uMapLocation.get_x(), uMapLocation.get_y(), 12 * (1.2 - remainingLife));
                }
            }
            targetMap += enemyNearbyMap * 0.0001 - structureProximityMap * 0.001;
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
            targetMap /= (enemyInfluenceMap * 10.0 + stuckUnitMap + 1.0);
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

    bool healUnits() {
        if (gc.is_heal_ready(id) && unit.get_location().is_on_map()) {
            int bestTargetId = -1;
            double bestTargetRemainingLife = 1000;
            int healAmount = -unit.get_damage();
            for (auto u : getUnitsInDisk(unit.get_location().get_map_location(), 30)) {
                if (u->get_team() != ourTeam) {
                    continue;
                }
                if (!u->get_location().is_on_map()) {
                    continue;
                }
                if (is_robot(u->get_unit_type())) {
                    double remainingLife = u->get_health() / (u->get_max_health() + 0.0);
                    if (remainingLife == 1.0) {
                        continue;
                    }
                    
                    if (!gc.can_heal(id, u->get_id()))
                        continue;

                    double factor = 1;
                    switch (u->get_unit_type()) {
                        case Worker:
                            factor = 0.1;
                            break;
                        case Mage:
                            factor = 1.3;
                            break;
                        case Ranger:
                            factor = 1.0;
                            break;
                        case Healer:
                            factor = 1.2;
                            break;
                        case Knight:
                            factor = 1.1;
                            break;
                        default:
                            break;
                    }
                    remainingLife -= factor;

                    int canHealAmount = min(healAmount, (int)u->get_max_health() - (int)u->get_health());
                    remainingLife *= canHealAmount;

                    if (remainingLife < bestTargetRemainingLife) {
                        bestTargetRemainingLife = remainingLife;
                        bestTargetId = u->get_id();
                    }
                }
            }
            if (bestTargetId != -1) {
                gc.heal(id, bestTargetId);
                invalidate_unit(bestTargetId);
                invalidate_unit(id);
                return true;
            }
        }
        return false;
    }

    void tick() {
        if (!unit.get_location().is_on_map()) return;
        
        auto unitMapLocation = unit.get_location().get_map_location();

        hasDoneTick = true;

        bool succeededHealing = false;

        succeededHealing = healUnits();

        if (veryLowTimeRemaining) {
            return;
        }

        auto nextLocation = getNextLocation();
        moveToLocation(nextLocation);

        if(!succeededHealing) {
            succeededHealing = healUnits();
        }
        double interpolationFactor = 0.99;
        averageHealerSuccessRate = averageHealerSuccessRate * interpolationFactor + succeededHealing * (1-interpolationFactor);
    }

    void doOvercharge() {
        return;
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

        if (gc.get_round() >= 737) {
            // Don't produce any more units as they won't have time to get into a rocket
            return;
        }

        if (!unit.is_factory_producing()) {
            const auto& location = unit.get_location().get_map_location();
            double nearbyEnemiesWeight = enemyNearbyMap.weights[location.get_x()][location.get_y()];
            auto researchInfo = gc.get_research_info();
            if (existsPathToEnemy){
                double score = 1;
                if (distanceToInitialLocation[enemyTeam].weights[location.get_x()][location.get_y()] < 14 && gc.get_round() < 80)
                    score += 20;
                if (distanceToInitialLocation[enemyTeam].weights[location.get_x()][location.get_y()] < 18 && gc.get_round() < 100)
                    score += 5;
                if (state.typeCount[Factory] >= 3)
                    score *= 0.7;
                score /= state.typeCount[Knight] + 1.0;
                if (nearbyEnemiesWeight > 0.7)
                    score += 0.3;
                if (nearbyEnemiesWeight > 0.8)
                    score += 1.0;
                if (nearbyEnemiesWeight > 0.9) {
                    score += 2.0;
                    if (gc.get_round() < 120)
                        score += 1;
                }
                if (nearbyEnemiesWeight > 0.95) {
                    score += 15.0;
                }
                score += 10 * enemyFactoryNearbyMap.weights[location.get_x()][location.get_y()];
                if (gc.get_round() < 90)
                    score += 15 * enemyFactoryNearbyMap.weights[location.get_x()][location.get_y()];

                if (enemyHasMages)
                    score *= 0.7;
				score *= 4.0 / (4.0 + state.typeCount[Ranger] + estimatedEnemyRangers);
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
                if (state.typeCount[Worker] == 0 && (researchInfo.get_level(Rocket) >= 1 || state.typeCount[Factory] < 3)) {
                    score += 10;
                }
                if (gc.get_round() > 600 && state.typeCount[Worker] < 5) {
                    score += 500;
                }
                if (state.typeCount[Rocket] > 5) {
                    score += 20 / (state.typeCount[Worker] + 1.0);
                }
                macroObjects.emplace_back(score, unit_type_get_factory_cost(Worker), 2, [=] {
                    if (gc.can_produce_robot(id, Worker)) {
                        gc.produce_robot(id, Worker);
                    }
                });
            }
            {
                // Not even sure about this, but yeah. If a mage hit will on average hit 4 enemies, go for it (compare to ranger score of 2)
                double score = splashDamagePotential * 0.5; // enemyInfluenceMap.weights[location.get_x()][location.get_y()] * 0.4;
                if (hasOvercharge) {
                    score += state.typeCount[Healer] * (researchInfo.get_level(Mage) * 0.6 + 1.0);
                }
                if (gc.get_round() < 120) {
                    score += estimatedEnemyKnights * 3.0 / (estimatedEnemyRangers + 1.0);
                }
                score /= state.typeCount[Mage] + 1.0;
                if (enemyHasKnights && !enemyHasRangers && !hasBuiltMage && nearbyEnemiesWeight < 0.75)
                    score += 30;
                macroObjects.emplace_back(score, unit_type_get_factory_cost(Mage), 2, [=] {
                    if (gc.can_produce_robot(id, Mage)) {
                        hasBuiltMage = true;
                        gc.produce_robot(id, Mage);
                    }
                });
            }
            {
                int otherMilitary = state.typeCount[Ranger] + state.typeCount[Mage] + state.typeCount[Knight];
                // Never have more healers than the combined total of other military units
                if (otherMilitary > state.typeCount[Healer]) {
                    double score = 0.0;
                    if (state.typeCount[Ranger] >= 2) {
                        score += 3;
                    }
                    if (state.typeCount[Ranger] >= 3) {
                        score += 3;
                    }
                    if (state.typeCount[Ranger] >= 5) {
                        score += 4;
                    }
                    if (state.typeCount[Ranger] > 6) {
                        score += state.typeCount[Ranger] * 1.2 + state.typeCount[Mage] * 0.7;
                    }
                    score /= state.typeCount[Healer] + 1.0;
                    score += averageHealerSuccessRate * 1.5;
                    if (hasOvercharge)
                        score += 0.8;
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
    int hasHealers = 0;
    for (auto id : unit.get_structure_garrison()) {
        auto u = gc.get_unit(id);
        if (u.get_unit_type() == Worker) {
            hasWorker = true;
        }
        if (u.get_unit_type() == Healer) {
            ++hasHealers;
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
        auto unitType = gc.get_unit(candidates[i].second).get_unit_type();
        if (unitType == Worker) {
            if (hasWorker && launchedWorkerCount) {
                ++remainingTravellers;
                continue;
            }
            hasWorker = true;
        }
        if (unitType == Healer) {
            if (hasHealers >= 3) {
                ++remainingTravellers;
                continue;
            }
            ++hasHealers;
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
                scores[Healer] = 7 + 1.0 * state.typeCount[Healer];
                break;
        }
        switch(researchInfo.get_level(Worker)) {
            case 0:
                scores[Worker] = 0.5;
                break;
            case 1:
                scores[Worker] = 0.5;
                break;
            case 2:
                scores[Worker] = 0.5;
                break;
            case 3:
                scores[Worker] = 0.5;
                break;
        }
        switch(researchInfo.get_level(Rocket)) {
            case 0:
                if (!anyReasonableLandingSpotOnInitialMars) {
                    // Ha! We cannot even LAND on mars, why should we get there?
                    scores[Rocket] = 0;
                    break;
                }

                scores[Rocket] = 4;
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
                    scores[Rocket] += 3.5;
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
    unitAtLocation = vector<vector<Unit*> >(w, vector<Unit*>(h, nullptr));
    ourUnits = gc.get_my_units();
    sort(ourUnits.begin(), ourUnits.end(), [](const Unit& a, const Unit& b) -> bool
    { 
        double scoreA = 0;
        double scoreB = 0;
        if (a.get_location().is_on_map()) {
            const auto locationA = a.get_location().get_map_location();
            scoreA = rangerCanShootEnemyCountMap.weights[locationA.get_x()][locationA.get_y()];
        }
        if (b.get_location().is_on_map()) {
            const auto locationB = b.get_location().get_map_location();
            scoreB = rangerCanShootEnemyCountMap.weights[locationB.get_x()][locationB.get_y()];
        }
        return scoreA < scoreB;
    });
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
    for (auto& unit : ourUnits) {
        allUnits.push_back(unit.clone());
        if (unit.get_location().is_on_map()) {
            const auto location = unit.get_location().get_map_location();
            unitAtLocation[location.get_x()][location.get_y()] = &unit;
        }
    }
    for (auto& unit : enemyUnits)
        allUnits.push_back(unit.clone());
}

void updateEnemyHasRangers() {
    double newEstimate = 0;
    for (auto& u : enemyUnits) {
        if (u.get_unit_type() == Ranger) {
            enemyHasRangers = true;
            ++newEstimate;
        }
    }
    double factor = 0.05;
    estimatedEnemyRangers = newEstimate * factor + estimatedEnemyRangers * (1 - factor);
}

void updateEnemyHasMages() {
    if (enemyHasMages)
        return;
    for (auto& u : enemyUnits) {
        if (u.get_unit_type() == Mage)
            enemyHasMages = true;
    }
}

void updateEnemyHasKnights() {
    double newEstimate = 0;
    for (auto& u : enemyUnits) {
        if (u.get_unit_type() == Knight) {
            enemyHasKnights = true;
            ++newEstimate;            
        }
    }
    double factor = 0.05;
    estimatedEnemyKnights = newEstimate * factor + estimatedEnemyKnights * (1 - factor);
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
            if (team == 1) {
                initialDistanceToEnemyLocation = min(initialDistanceToEnemyLocation, (int)(distanceToInitialLocation[0].weights[x][y] + distanceToInitialLocation[1].weights[x][y]));
            }
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
    contestedKarbonite = 0;
    fuzzyKarboniteMap = PathfindingMap(w, h);
    for (int i = 0; i < w; i++) {
        for (int j = 0; j < h; j++) {
            double karbs = 0;
            for (int k = -1; k <= 1; k++) {
                for (int l = -1; l <= 1; l++) {
                    int x = i+k;
                    int y = j+l;
                    if (x >= 0 && y >= 0 && x < w && y < h){
                        double kar = karboniteMap.weights[x][y];
                        kar = log(kar + 1);
                        if (k != 0 || l != 0)
                            kar *= 0.9;
                        karbs = max(karbs, kar);
                    }
                }
            }
            if (planet == Earth) {
                int disDiff = distanceToInitialLocation[enemyTeam].weights[i][j] - distanceToInitialLocation[ourTeam].weights[i][j];
                // 0 when karbonite is very close to us, 1 when close to enemy, 0.5 when equally close
                float relativeDiff = distanceToInitialLocation[ourTeam].weights[i][j] / (distanceToInitialLocation[enemyTeam].weights[i][j] + distanceToInitialLocation[ourTeam].weights[i][j]);
                if (disDiff <= 4 && disDiff >= -4) {
                    contestedKarbonite += karboniteMap.weights[i][j];
                } else if (disDiff <= 5 && disDiff >= -5) {
                    contestedKarbonite += karboniteMap.weights[i][j] * 0.5f;
                }

                if (disDiff <= 6) {
                    karbs *= 1.2;
                }

                karbs *= 1 / (1 + 0.7/2);
                if (relativeDiff < 0.7) {
                    karbs *= 1 + 4*relativeDiff;
                }
                if (distanceToInitialLocation[enemyTeam].weights[i][j] < 5) {
                    karbs *= 0.7;
                }
                karbs /= 1.0 + workerProximityMap.weights[i][j];
            }

            fuzzyKarboniteMap.weights[i][j] = karbs;
        }
    }
#ifndef NDEBUG
    cout << "ContestedKarbonite: " << contestedKarbonite << endl;
#endif
    fuzzyKarboniteMap /= ourStartingPositionMap;
}

void updateEnemyInfluenceMaps(){
    enemyInfluenceMap = PathfindingMap(w, h);
    enemyNearbyMap = PathfindingMap(w, h);
    enemyFactoryNearbyMap = PathfindingMap(w, h);
    healerOverchargeMap = PathfindingMap(w, h);
    enemyExactPositionMap = PathfindingMap(w, h);
    rangerCanShootEnemyCountMap = PathfindingMap(w, h);
    enemyKnightNearbyMap = PathfindingMap(w, h);
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
                enemyKnightNearbyMap.addInfluenceMultiple(mageHideFromKnightInfluence, pos.get_x(), pos.get_y(), 3.0);
            }
            if (u.get_unit_type() == Factory) {
				enemyFactoryNearbyMap.maxInfluence(enemyFactoryNearbyInfluence, pos.get_x(), pos.get_y());
			}
			if (u.get_unit_type() != Worker) {
                enemyNearbyMap.maxInfluence(wideEnemyInfluence, pos.get_x(), pos.get_y());
            }
            rangerCanShootEnemyCountMap.addInfluence(rangerTargetInfluence, pos.get_x(), pos.get_y());
            enemyPositionMap.weights[pos.get_x()][pos.get_y()] += 1.0;
            enemyExactPositionMap.weights[pos.get_x()][pos.get_y()] = 1;
            healerOverchargeMap.maxInfluence(healerOverchargeInfluence, pos.get_x(), pos.get_y());
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

    workerAdditiveMap = PathfindingMap(w, h);
    for (auto& u : ourUnits) {
        if (u.get_location().is_on_map()) {
            if (u.get_unit_type() == Worker) {
                auto pos = u.get_location().get_map_location();
                workerAdditiveMap.addInfluence(workerAdditiveInfluence, pos.get_x(), pos.get_y());
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
                        damagedStructureMap.weights[x][y] = max(damagedStructureMap.weights[x][y], 5 * (2.0 - remainingLife));
                    }
                    else {
                        double score = 3 * (1.5 + remainingLife);
                        if (workersNextToMap.weights[unitX][unitY] >= 5) {
                            score /= 1 + 0.05 + workersNextToMap.weights[unitX][unitY];
                        }
                        damagedStructureMap.weights[x][y] = max(damagedStructureMap.weights[x][y], 15 * (1.5 + 0.5 * remainingLife));
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
    hasUnstuckUnit = false;
    for (const auto& unit : ourUnits) {
        if (!unit.get_location().is_on_map())
            continue;
        if (is_structure(unit.get_unit_type()) && !unit.structure_is_built())
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
        if (moveDirections)
            hasUnstuckUnit = true;
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

void updateWithinRangeMap() {
    withinRangeMap = PathfindingMap(w, h);
    for (auto& u : ourUnits) {
        if (u.get_location().is_on_map() && u.get_unit_type() == Ranger) {
            const auto location = u.get_location().get_map_location();
            withinRangeMap.addInfluence(rangerTargetInfluence, location.get_x(), location.get_y());
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
#ifndef NDEBUG
    cout << "Splash damage potential: " << splashDamagePotential << endl;
#endif

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
#ifndef NDEBUG
                    cout << "Unknown unit type!" << endl;
#endif
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
                botunit->hasHarvested = false;
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
#ifndef NDEBUG
                cout << "Found path of length " << path.size() << endl;
#endif
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

#ifndef NDEBUG
    cout << "Map is " << connectedness << " connected" << endl;
#endif
    return connectedness;
}

double mageCoordinationTime;

void coordinateRangerAttacks() {
    if (!hasOvercharge || !state.typeCount[Ranger]) {
        return;
    }
    map<unsigned int, vector<unsigned int> > rangerTargets;
    for (auto& unit : ourUnits) {
        if (unit.get_unit_type() == Ranger && unit.get_location().is_on_map()) {
            int attackRange = unit.get_attack_range();
            const auto locus = unit.get_location().get_map_location();
            int x = locus.get_x();
            int y = locus.get_y();
            if (!exists_enemy_in_range(x, y, attackRange)) {
                continue;
            }
            const auto nearby = gc.sense_nearby_units(locus, attackRange);
            vector<unsigned int> targets;
            for (const auto& enemy : nearby) {
                if (enemy.get_team() != unit.get_team() && enemy.get_unit_type() != Worker)
                    targets.push_back(enemy.get_id());
            }
            rangerTargets[unit.get_id()] = targets;
        }
    }
    map<unsigned int, set<int> > targetedBy;
    map<pair<int, int>, int> shooter;
    auto researchInfo = gc.get_research_info();
    for (auto& unit : ourUnits) {
        if (unit.get_unit_type() == Healer && unit.get_location().is_on_map()) {
            if (unit.get_ability_heat() >= 10)
                continue;
            int attackRange = unit.get_ability_range();
            const auto locus = unit.get_location().get_map_location();
            if (mageNearbyMap.weights[locus.get_x()][locus.get_y()] > 0 && researchInfo.get_level(Mage) >= 3)
                continue;
            const auto nearby = gc.sense_nearby_units(locus, attackRange);
            for (const auto& ranger : nearby) {
                if (ranger.get_team() == unit.get_team() && ranger.get_unit_type() == Ranger) {
                    for (const unsigned int enemyId : rangerTargets[ranger.get_id()]) {
                        targetedBy[enemyId].insert(unit.get_id());
                        shooter[make_pair(enemyId, unit.get_id())] = ranger.get_id();
                    }
                }
            }
        }
    }
    for (auto it : targetedBy) {
        if (!gc.can_sense_unit(it.first))
            continue;
        Unit unit = gc.get_unit(it.first);
        unsigned int hitsRequired = ceil(unit.get_health() / 30.0);
        if (it.second.size() >= hitsRequired) {
            for (const auto& healerId : it.second) {
                if (!gc.can_sense_unit(it.first))
                    continue;
                if (!gc.can_sense_unit(healerId))
                    continue;
                int rangerId = shooter[make_pair(it.first, healerId)];
                if (!gc.can_sense_unit(rangerId))
                    continue;
                const auto healer = gc.get_unit(healerId);
                if (!healer.get_location().is_on_map())
                    continue;
                const auto healerLocation = healer.get_location().get_map_location();
                const auto ranger = gc.get_unit(rangerId);
                if (!ranger.get_location().is_on_map())
                    continue;
                const auto rangerLocation = ranger.get_location().get_map_location();
                int x1 = healerLocation.get_x();
                int y1 = healerLocation.get_y();
                int x2 = rangerLocation.get_x();
                int y2 = rangerLocation.get_y();
                int dx = x1-x2;
                int dy = y1-y2;
                if (dx*dx + dy*dy > 30)
                    continue;
                gc.overcharge(healerId, rangerId);
                if (gc.can_attack(rangerId, it.first)) {
                    gc.attack(rangerId, it.first);
                }
                invalidate_unit(healerId);
                invalidate_unit(rangerId);
                for (auto& it2 : targetedBy) {
                    it2.second.erase(healerId);
                }
            }
        }
    }
#ifndef NDEBUG
    cout << "Finished coordinating rangers" << endl;
#endif
}

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
        int damage = 0;
        queue<pair<int, int> > bfsQueue;
        for (auto& unit : ourUnits) {
            if (unit.get_unit_type() != Mage) {
                continue;
            }
            if (!unit.get_location().is_on_map())
                continue;
            if (!damage)
                damage = unit.get_damage();
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
                    if (!canSenseLocation[nx][ny])
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
                        if (!canSenseLocation[nx][ny])
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
                    if (score < 80.0 / (damage + 50.0))
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
#ifndef NDEBUG
            for (auto& node : path) {
                cout << node.first << " " << node.second << " - " << healerMap.weights[node.first][node.second] << " " << distanceToMage.weights[node.first][node.second] << " " << minHealerSum[node.first][node.second] << endl;
            }
#endif
            MapLocation location(planet, path[0].first, path[0].second);
            if (!canSenseLocation[path[0].first][path[0].second]) {
#ifndef NDEBUG
                cout << "Error! Couldn't sense location!" << endl;
#endif
                continue;
            }
            if (!gc.has_unit_at_location(location)) {
#ifndef NDEBUG
                cout << "Error! Doesn't have unit at location!" << endl;
#endif
                continue;
            }
            auto mage = gc.sense_unit_at_location(location);
            auto& botUnit = unitMap[mage.get_id()];
            for (size_t i = 0; i < path.size()-1; ++i) {
                mage_attack(botUnit->unit);
                if (botUnit == nullptr) {
#ifndef NDEBUG
                    cout << "Warning! The attacking mage died" << endl;
#endif
                    anyOvercharge = true;
                    break;
                }
                if (!botUnit->unit.get_location().is_on_map()) {
#ifndef NDEBUG
                    cout << "Warning! Attacking mage entered a structure" << endl;
#endif
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
#ifndef NDEBUG
                    cout << "Warning! Attacking mage couldn't make it to target spot" << endl;
#endif
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
                        if (botUnit == nullptr) {
#ifndef NDEBUG
                            cout << "Warning! The attacking mage died" << endl;
#endif
                            anyOvercharge = true;
                            break;
                        }
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
                    if (botUnit == nullptr) {
#ifndef NDEBUG
                        cout << "Warning! The attacking mage died" << endl;
#endif
                        anyOvercharge = true;
                        break;
                    }
                    location = botUnit->unit.get_location().get_map_location();
                    if (location.get_x() == path[i].first && location.get_y() == path[i].second) {
                        if (!hasDoneAnything) {
#ifndef NDEBUG
                            cout << "Warning! Attacking mage couldn't make it to target spot" << endl;
#endif
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

            if (botUnit != nullptr) mage_attack(botUnit->unit);
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
    // Side effect: findUnits sets ourTeam and opponentTeam
    rangerCanShootEnemyCountMap = PathfindingMap(w, h);
    findUnits();

    initKarboniteMap();
    initInfluence();

    computeOurStartingPositionMap();
    updatePassableMap();
    discoveryMap = PathfindingMap(w, h);
    if (planet == Earth) {
        computeDistancesToInitialLocations();
        mapConnectedness = computeConnectedness();
        existsPathToEnemy = mapConnectedness > 0;
#ifndef NDEBUG
        if (!existsPathToEnemy) {
            cout << "There doesn't exist a path to the enemy!" << endl;
        }
        else {
            cout << "There exists a path to the enemy!" << endl;
        }
#endif
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
        sigTheRound = (sig_atomic_t)round;
        printf("Round: %d\n", round);
        fflush(stdout);
        fflush(stderr);
        int timeLeft = gc.get_time_left_ms();
        // If less than 0.5 seconds left, then enter low power mode
        lowTimeRemaining = timeLeft < 4000;
        veryLowTimeRemaining = timeLeft < 1000;
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

        fflush(stdout);
        fflush(stderr);

        // NOTE: this call also updates enemy position map for some reason
        updateWorkerMaps();
        updateKarboniteMap();
        updateFuzzyKarboniteMap();
        updateEnemyInfluenceMaps();
        updateMageNearbyMap();
        updateStructureProximityMap();
        updateDamagedStructuresMap();
        updateEnemyHasRangers();
        updateEnemyHasMages();
        updateEnemyHasKnights();
        updateWithinRangeMap();

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
        
        if (planet == Earth) {
            state.remainingKarboniteOnEarth = 0;
            for (int x = 0; x < w; x++) {
                for (int y = 0; y < h; y++) {
                    state.remainingKarboniteOnEarth += 20.0 * karboniteMap.weights[x][y] / (distanceToInitialLocation[ourTeam].weights[x][y] + 10.0 + gc.get_round() * 0.4);
                }
            }
        }
        state.earthTotalUnitCount = gc.get_team_array(Earth)[0];
            
        fflush(stdout);
        fflush(stderr);

        updatePassableMap();
        updateStuckUnitMap();

        // WIP
        createUnits();
        matchWorkers();

        if (!hasUnstuckUnit && state.typeCount[Rocket] == 0 && planet == Earth) {
            bool hasDisintegrated = false;
            for (int iteration = 0; iteration < 2; ++iteration) {
                for (auto& unit : ourUnits) {
                    if (unit.get_unit_type() != Worker && (iteration == 0 || unit.get_unit_type() != Factory || !unit.structure_is_built())) {
                        continue;
                    }
                    if (rand()%10)
                        continue;
                    if (!unit.get_location().is_on_map())
                        continue;
                    const auto location1 = unit.get_location().get_map_location();
                    for (auto& unit2 : ourUnits) {
                        if (hasDisintegrated)
                            break;
                        if (!unit2.get_location().is_on_map())
                            continue;
                        const auto location2 = unit2.get_location().get_map_location();
                        int dx = location1.get_x() - location2.get_x();
                        int dy = location1.get_y() - location2.get_y();
                        if (dx == 0 && dy == 0)
                            continue;
                        if (abs(dx) > 1 || abs(dy) > 1)
                            continue;
                        timesStuck++;
                        hasDisintegrated = true;
                        gc.disintegrate_unit(unit2.get_id());
                    }
                }
            }
            findUnits();
            createUnits();
        }

        auto t1 = millis();
        preprocessingComputationTime += t1-t0;

        bool firstIteration = true;
        workersMove = false;
        while (true) {
            fflush(stdout);
            fflush(stderr);
            auto t2 = millis();
            createUnits();
#ifndef NDEBUG
            cout << "We have " << ourUnits.size() << " units" << endl;
#endif
            if (!veryLowTimeRemaining) {
                coordinateMageAttacks();
            }
            bool anyTickDone = tickUnits(firstIteration, 1 << (int)Healer);

            anyTickDone |= tickUnits(false);
            if (hasOvercharge) doOvercharge();
            addWorkerActions();
            auto t3 = millis();
#ifndef NDEBUG
            cout << "Iteration: " << (t3 - t2) << endl;
#endif
            
            executeMacroObjects();

            if (firstIteration && !veryLowTimeRemaining) {
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
            }
            firstIteration = false;
            if (!veryLowTimeRemaining) {
                coordinateRangerAttacks();
            }

            if (!anyTickDone) break;

            findUnits();
            // auto t4 = millis();
            // cout << "Execute: " << (t4 - t3) << endl;
        }

        auto t5 = millis();
#ifndef NDEBUG
        cout << "All iterations: " << std::round(t5 - t1) << endl;
#endif
        updateResearch();

        double turnTime = millis() - t0;
        totalTurnTime += turnTime;

        //if (!lowTimeRemaining)
#ifndef NDEBUG
        if (true) {
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
            cout << "Match workers time: " << std::round(matchWorkersTime) << endl;
            cout << "  Dijkstra time: " << std::round(matchWorkersDijkstraTime) << endl;
            cout << "  Dijkstra2 time: " << std::round(matchWorkersDijkstraTime2) << endl;
            cout << "  Hungarian time: " << std::round(hungarianTime) << endl;
            for (auto it : timeUsed) {
                cout << unitTypeToString[it.first] << ": " << std::round(it.second) << endl;
            }

            cout << "Turn time: " << turnTime << endl;
            
            cout << "Average: " << std::round(totalTurnTime/gc.get_round()) << endl;
            cout << "Total: " << std::round(totalTurnTime) << endl;
            cout << "Attacker success rate: " << averageAttackerSuccessRate << endl;
            cout << "Average healer success rate: " << averageHealerSuccessRate << endl;
        }
#endif

        gc.write_team_array(0, state.totalUnitCount);

        // this line helps the output logs make more sense by forcing output to be sent
        // to the manager.
        // it's not strictly necessary, but it helps.
        // pause and wait for the next turn.
        fflush(stdout);
        fflush(stderr);
#ifndef NDEBUG
        cout << "Calling gc.next_turn()" << endl;
#endif
        gc.next_turn();
    }
    // I'm convinced C++ is the better option :)
}
