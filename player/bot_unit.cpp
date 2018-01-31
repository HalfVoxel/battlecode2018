#include "bot_unit.h"
#include "maps.h"
#include "influence.h"

using namespace bc;
using namespace std;

double averageAttackerSuccessRate;

// Relative values of different unit types when at "low" (not full) health
const float unit_defensive_strategic_value[] = {
    1, // Worker
    4, // Knight
    3, // Ranger
    7, // Mage
    5, // Healer
    3, // Factory
    4, // Rocket
};

// Relative values of different unit types when at full or almost full health
const float unit_strategic_value[] = {
    1, // Worker
    4, // Knight
    3, // Ranger
    7, // Mage
    5, // Healer
    3, // Factory
    4, // Rocket
};

// Relative values of different unit types when at Mars
const float unit_martian_strategic_value[] = {
    0.5, // Worker
    3, // Knight
    4, // Ranger
    6, // Mage
    5, // Healer
    0.1, // Factory
    0.1, // Rocket
};

void BotUnit::tick() {}
PathfindingMap BotUnit::getTargetMap() { return PathfindingMap(); }
PathfindingMap BotUnit::getCostMap() { return PathfindingMap(); }

void addRocketTarget(const Unit& unit, PathfindingMap& targetMap) {

    if (gc.get_round() > 650) {
        if (planet == Earth) {
            targetMap += rocketAttractionMap;
        }
    }
    else {
        for (auto rocketId : unitShouldGoToRocket[unit.get_id()]) {
            if (!gc.can_sense_unit(rocketId)) {
                continue;
            }
            auto unit = gc.get_unit(rocketId);
            if(!unit.get_location().is_on_map()) {
                continue;
            }
            auto rocketLocation = unit.get_location().get_map_location();
            targetMap.weights[rocketLocation.get_x()][rocketLocation.get_y()] += 10000;
        }
    }
}

MapLocation BotUnit::getNextLocation(MapLocation from, bool allowStructures) {
    bool canMove = false;
    int x = from.get_x();
    int y = from.get_y();
    for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
            int nx = x + dx;
            int ny = y + dy;
            if (nx >= 0 && ny >= 0 && nx < (int)passableMap.weights.size() && ny < (int)passableMap.weights[nx].size()) {
                if (passableMap.weights[nx][ny] < 1000) {
                    canMove = true;
                }
            }
        }
    }
    if (!canMove) {
        return from;
    }
    if (isRocketFodder) {
        if (y > 0 && passableMap.weights[x][y-1] < 1000) {
            return from.add(South);
        }
        if (x > 0) {
            return from.add(West);
        }
        return from;
    }
    double start = millis();
    auto targetMap = getTargetMap();
    targetMapComputationTime += millis() - start;
    double start2 = millis();
    auto costMap = getCostMap();
    costMapComputationTime += millis() - start2;
    if (allowStructures) {
        costMap.weights[x][y] = 1;
    }
    else {
        costMap.weights[x][y] = numeric_limits<double>::infinity();
        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                int nx = x + dx;
                int ny = y + dy;
                if (nx >= 0 && ny >= 0 && nx < (int)costMap.weights.size() && ny < (int)costMap.weights[nx].size()) {
                    MapLocation location(gc.get_planet(), nx, ny);
                    if (gc.can_sense_location(location) && gc.has_unit_at_location(location)) {
                        costMap.weights[nx][ny] = numeric_limits<double>::infinity();
                    }
                }
            }
        }
    }
    mapComputationTime += millis() - start;
    start = millis();
    Pathfinder pathfinder;
    auto nextLocation = pathfinder.getNextLocation(from, targetMap, costMap);
    pathfindingScore = pathfinder.bestScore;
    pathfindingTime += millis() - start;

    return nextLocation;
}

MapLocation BotUnit::getNextLocation() {
    return getNextLocation(unit.get_location().get_map_location(), true);
}

void BotUnit::moveToLocation(MapLocation nextLocation) {
    auto unitMapLocation = unit.get_location().get_map_location();
    if (nextLocation != unitMapLocation) {
        auto d = unitMapLocation.direction_to(nextLocation);
        if (gc.is_move_ready(id)) {
            if (gc.can_move(id, d)) {
                passableMap.weights[unitMapLocation.get_x()][unitMapLocation.get_y()] = 1;
                gc.move_robot(id,d);
                invalidate_unit(id);
                unitMapLocation = unit.get_location().get_map_location();
                passableMap.weights[unitMapLocation.get_x()][unitMapLocation.get_y()] = 1000;
            }
            else if(gc.has_unit_at_location(nextLocation)) {
                auto u = gc.sense_unit_at_location(nextLocation);
                if (u.get_team() == unit.get_team() && (u.get_unit_type() == Factory || u.get_unit_type() == Rocket)) {
                    if (gc.can_load(u.get_id(), unit.get_id())) {
                        if (u.get_unit_type() == Rocket && unit.get_unit_type() == Worker) {
                            ++launchedWorkerCount;
                        }
                        passableMap.weights[unitMapLocation.get_x()][unitMapLocation.get_y()] = 1;
                        gc.load(u.get_id(), unit.get_id());
                        invalidate_unit(u.get_id());
                        invalidate_unit(unit.get_id());
                    }
                }
            }
        }
    }
}

bool BotUnit::unloadFrontUnit() {
    BotUnit* u = unitMap[unit.get_structure_garrison()[0]];
    auto nextLocation = u->getNextLocation(unit.get_location().get_map_location(), false);
    Direction dir = unit.get_location().get_map_location().direction_to(nextLocation);
    if (gc.can_unload(id, dir)){
        gc.unload(id, dir);
        invalidate_unit(u->unit.get_id());
        invalidate_unit(id);
        return true;
    }
    return false;
}

bool exists_enemy_in_range(int x, int y, int attackRange) {
    int dis;
    for (dis = 1; (dis+1)*(dis+1) <= attackRange; ++dis);
    for (int dx = -dis; dx <= dis; ++dx) {
        for (int dy = -dis; dy <= dis; ++dy) {
            int dis2 = dx*dx + dy*dy;
            if (dis2 > attackRange)
                continue;
            int nx = x + dx;
            int ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                continue;
            if (enemyExactPositionMap.weights[nx][ny] > 0) {
                return true;
            }
        }
    }
    return false;
}

void mage_attack(const Unit& unit) {
    if (!gc.is_attack_ready(unit.get_id())) return;

    if (!unit.get_location().is_on_map()) return;

    int attackRange = unit.get_attack_range();
    const auto locus = unit.get_location().get_map_location();
    int x = locus.get_x();
    int y = locus.get_y();
    if (!exists_enemy_in_range(x, y, attackRange)) {
        return;
    }

    // Calls on the controller take unit IDs for ownership reasons.
    const auto nearby = gc.sense_nearby_units(locus, unit.get_attack_range() + 20);

    const Unit* best_unit = nullptr;
    double best_unit_score = 0;

    auto low_health = unit.get_health() / (float)unit.get_max_health() < 0.8f;
    auto& values = planet == Mars ? unit_martian_strategic_value : (low_health ? unit_defensive_strategic_value : unit_strategic_value);

    auto hitScore = vector<vector<double>>(w, vector<double>(h));

    for (auto& place : nearby) {
        if (place.get_health() <= 0) continue;

        float fractional_health = place.get_health() / (float)place.get_max_health();
        float value = values[place.get_unit_type()] / (fractional_health + 2.0);
        if (place.get_team() != unit.get_team() && place.get_unit_type() == Knight)
            value += 1;
        if (place.get_team() == unit.get_team()) value = -1.5 * value;

        auto location = place.get_location().get_map_location();
        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                int x = location.get_x() + dx;
                int y = location.get_y() + dy;
                if (x >= 0 && y >= 0 && x < w && y < h) {
                    hitScore[x][y] += value;
                }
            }
        }
    }

    for (auto& place : nearby) {
        if (place.get_health() <= 0) continue;
        if (!gc.can_attack(unit.get_id(), place.get_id())) continue;

        auto location = place.get_location().get_map_location();
        double score = hitScore[location.get_x()][location.get_y()];
        if (score > best_unit_score) {
            best_unit_score = score;
            best_unit = &place;
        }
    }

    if (best_unit != nullptr) {
        //Attacking 'em enemies
        gc.attack(unit.get_id(), best_unit->get_id());
        invalidate_units();
#ifndef NDEBUG
        cout << "Mage attack with score " << best_unit_score << endl;
#endif
        turnsSinceLastFight = 0;
    }
}

void attack_all_in_range(const Unit& unit) {
    int id = unit.get_id();
    if (!gc.is_attack_ready(id)) return;

    if (!unit.get_location().is_on_map()) return;

    if (gc.get_round()%2 && unit.get_unit_type() == Ranger) return;

    int attackRange = unit.get_attack_range();
    const auto locus = unit.get_location().get_map_location();
    int x = locus.get_x();
    int y = locus.get_y();
    double interpolationFactor = 0.999;
    if (!exists_enemy_in_range(x, y, attackRange)) {
        averageAttackerSuccessRate = averageAttackerSuccessRate * interpolationFactor;
        return;
    }
    double start = millis();

    // Calls on the controller take unit IDs for ownership reasons.
    const auto nearby = gc.sense_nearby_units(locus, attackRange);

    const Unit* best_unit = nullptr;
    //float totalWeight = 0;

    auto low_health = unit.get_health() / (float)unit.get_max_health() < 0.8f;
    auto& values = low_health ? unit_defensive_strategic_value : unit_strategic_value;

    int nearbyFriendly = 0;
    for (auto& place : nearby) {
        if (place.get_team() == unit.get_team())
            nearbyFriendly++;
    }
    
    float bestValue = 0;

    for (auto& place : nearby) {
        if (place.get_health() <= 0) continue;
        if (!gc.can_attack(id, place.get_id())) continue;
        if (place.get_team() == unit.get_team()) continue;

        float fractional_health = place.get_health() / (float)place.get_max_health();
        if (place.is_robot())
            fractional_health *= fractional_health;
        float value = values[place.get_unit_type()];
        if (nearbyFriendly <= 2 && place.get_unit_type() == Mage)
            value -= 2;
        value /= (fractional_health + 0.3);
        //const auto location = place.get_location().get_map_location();
        //value *= 1.0 + 0.05 * withinRangeMap.weights[location.get_x()][location.get_y()];

        if (value > bestValue) {
            bestValue = value;
            best_unit = &place;
        }

        // Reservoir sampling
        /*totalWeight += value;
        if (((rand() % 100000)/100000.0f) * totalWeight <= value) {
            best_unit = &place;
        }*/
    }

    int attackSuccessful = 0;
    if (best_unit != nullptr) {
        attackSuccessful = 1;
        //Attacking 'em enemies
        gc.attack(id, best_unit->get_id());
        invalidate_unit(id);
        turnsSinceLastFight = 0;
    }

    averageAttackerSuccessRate = averageAttackerSuccessRate * interpolationFactor + attackSuccessful * (1-interpolationFactor);

    attackComputationTime += millis()-start;
}

PathfindingMap BotUnit::defaultMilitaryTargetMap() {
    bool isHurt = (unit.get_health() < 0.5 * unit.get_max_health());
    MapReuseObject reuseObject(MapType::Target, unit.get_unit_type(), isHurt);

    PathfindingMap targetMap;
    if (reusableMaps.count(reuseObject)) {
        targetMap = reusableMaps[reuseObject];
    }
    else {
        targetMap = enemyNearbyMap * 0.0003 + discoveryMap * 0.0001;

        for (auto& enemy : enemyUnits) {
            if (enemy.get_location().is_on_map()) {
                auto pos = enemy.get_location().get_map_location();
                if (unit.get_unit_type() == Mage) {
                    targetMap.maxInfluence(mageTargetInfluence, pos.get_x(), pos.get_y());
                    if (hasOvercharge) {
                        targetMap.maxInfluence(mageToOverchargeInfluence, pos.get_x(), pos.get_y());
                    }
                }
                else if (unit.get_unit_type() == Ranger) {
                    double factor = 1;
                    switch (enemy.get_unit_type()) {
                        case Worker:
                            factor = 0.4;
                            break;
                        case Ranger:
                            factor = 1.0;
                            break;
                        case Mage:
                            factor = 1.5;
                            break;
                        case Knight:
                            factor = 1.2;
                            break;
                        case Healer:
                            factor = 1.2;
                            break;
                        case Factory:
                            factor = 1.6;
                            break;
                        case Rocket:
                            factor = 0.9;
                            break;
                    }
                    targetMap.maxInfluenceMultiple(rangerTargetInfluence, pos.get_x(), pos.get_y(), factor);
                }
                else {
                    double factor = 1;
                    switch (enemy.get_unit_type()) {
                        case Worker:
                            factor = 0.05;
                            break;
                        case Ranger:
                            factor = 2.0;
                            break;
                        case Mage:
                            factor = 2.0;
                            break;
                        case Knight:
                            factor = 1.2;
                            break;
                        case Healer:
                            factor = 1.3;
                            break;
                        case Factory:
                            factor = 0.6;
                            break;
                        case Rocket:
                            factor = 0.4;
                            break;
                    }
                    factor *= 1.4 - 0.5 * enemy.get_health() / (0.0 + enemy.get_max_health());
                    targetMap.maxInfluenceMultiple(knightTargetInfluence, pos.get_x(), pos.get_y(), factor);
                }
            }
        }

        if (unit.get_unit_type() == Knight) {
            for (auto& enemy : enemyUnits) {
                if (enemy.get_location().is_on_map()) {
                    auto pos = enemy.get_location().get_map_location();
                    if (enemy.get_unit_type() == Ranger) {
                        targetMap.addInfluence(knightHideFromRangerInfluence, pos.get_x(), pos.get_y());
                    }
                }
            }
        }

        if (planet == Earth) {
            auto&& initial_units = gc.get_starting_planet((Planet)0).get_initial_units();
            for (auto& enemy : initial_units) {
                if (enemy.get_team() == enemyTeam && enemy.get_location().is_on_map()) {
                    auto pos = enemy.get_location().get_map_location();
                    targetMap.weights[pos.get_x()][pos.get_y()] = max(targetMap.weights[pos.get_x()][pos.get_y()], 0.01);
                }
            }
        }

        if (isHurt) {
            for (auto& u : ourUnits) {
                if (u.get_unit_type() == Healer) {
                    if (!u.get_location().is_on_map()) {
                        continue;
                    }
                    auto pos = u.get_location().get_map_location();
                    double factor = 10;
                    if (unit.get_unit_type() == Mage) {
                        factor = 0.4;
                    }
                    else if (unit.get_unit_type() == Knight) {
                        factor = 0.01;
                    }
                    targetMap.addInfluenceMultiple(healerInfluence, pos.get_x(), pos.get_y(), factor);
                }
            }
            targetMap /= enemyInfluenceMap + 1.0;
        }

        for (auto& u : ourUnits) {
            if (u.get_location().is_on_map() && is_structure(u.get_unit_type())) {
                auto pos = u.get_location().get_map_location();
                targetMap.weights[pos.get_x()][pos.get_y()] = 0;
            }
        }

        targetMap /= stuckUnitMap + 1.0;

        if (unit.get_unit_type() == Mage) {
            targetMap /= enemyKnightNearbyMap + mageNearbyFuzzyMap + 0.1;
        }

        reusableMaps[reuseObject] = targetMap;
    }

    addRocketTarget(unit, targetMap);
    return targetMap;
}

PathfindingMap BotUnit::defaultMilitaryCostMap () {
    MapReuseObject reuseObject(MapType::Cost, unit.get_unit_type(), false);

    if (reusableMaps.count(reuseObject)) {
        return reusableMaps[reuseObject];
    }
    else {
        if (unit.get_unit_type() == Knight) {
            auto costMap = passableMap + structureProximityMap * 0.1 + rocketHazardMap * 10.0;
            for (auto& enemy : enemyUnits) {
                if (enemy.get_location().is_on_map()) {
                    auto pos = enemy.get_location().get_map_location();
                    if(enemy.get_unit_type() == Knight) {
                        costMap.addInfluence(knightHideFromKnightInfluence, pos.get_x(), pos.get_y());
                    }
                }
            }
            reusableMaps[reuseObject] = costMap;
            return costMap;
        }
        else if (unit.get_unit_type() == Ranger){
            auto costMap = (passableMap + enemyInfluenceMap * 2.0) / (nearbyFriendMap + 1.0) + structureProximityMap * 0.1 + rocketHazardMap * 10.0 + enemyKnightNearbyMap;
            reusableMaps[reuseObject] = costMap;
            return costMap;
        }
        else{
            auto costMap = (passableMap + enemyInfluenceMap * 0.5) / (nearbyFriendMap * 0.3 + 1.0) + structureProximityMap * 0.1 + rocketHazardMap * 10.0 + enemyKnightNearbyMap;
            reusableMaps[reuseObject] = costMap;
            return costMap;
        }
    }
}

double attackingTime;
double movingTime;

void BotUnit::default_military_behaviour() {

    if (unit.get_unit_type() == Ranger) {
        if (!unit.ranger_is_sniping() && unit.get_location().is_on_map() && gc.can_begin_snipe(unit.get_id(), unit.get_location().get_map_location()) && unit.get_ability_heat() < 10) {
            auto location = unit.get_location().get_map_location();
            if (enemyNearbyMap.weights[location.get_x()][location.get_y()] == 0) { // Only shoot if we feel safe
                double totalWeight = enemyPositionMap.sum();
                double r = (rand()%1000)/1000.0 * totalWeight;
                for (int x = 0; x < w; ++x) {
                    for (int y = 0; y < h; ++y) {
                        r -= enemyPositionMap.weights[x][y];
                        if (r < 0) {
                            if (gc.can_begin_snipe(unit.get_id(), MapLocation(gc.get_planet(), x, y))) {
                                gc.begin_snipe(unit.get_id(), MapLocation(gc.get_planet(), x, y));
                            }
                            break;
                        }
                    }
                }
            }
        }
    }

    if (unit.get_unit_type() == Mage) {
        mage_attack(unit);
    }
    else {
        attack_all_in_range(unit);
    }

    if (veryLowTimeRemaining)
        return;

    if (gc.is_move_ready(unit.get_id())) {
        auto nextLocation = getNextLocation();
        moveToLocation(nextLocation);
    }

    if (unit.get_unit_type() == Mage) {
        mage_attack(unit);
    }
    else {
        attack_all_in_range(unit);
    }
}
