#include "bot_unit.h"
#include "maps.h"
#include "influence.h"

using namespace bc;
using namespace std;

// Relative values of different unit types when at "low" (not full) health
float unit_defensive_strategic_value[] = {
    1, // Worker
    4, // Knight
    5, // Ranger
    4, // Mage
    2, // Healer
    2, // Factory
    1, // Rocket
};

// Relative values of different unit types when at full or almost full health
float unit_strategic_value[] = {
    2, // Worker
    1, // Knight
    3, // Ranger
    3, // Mage
    2, // Healer
    2, // Factory
    2, // Rocket
};

void BotUnit::tick() {}
PathfindingMap BotUnit::getTargetMap() { return PathfindingMap(); }
PathfindingMap BotUnit::getCostMap() { return PathfindingMap(); }

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
    time_t start = clock();
    auto targetMap = getTargetMap();
    auto costMap = getCostMap();
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
    mapComputationTime += (clock() - start + 0.0) / CLOCKS_PER_SEC;
    start = clock();
    Pathfinder pathfinder;
    auto nextLocation = pathfinder.getNextLocation(from, targetMap, costMap);
    pathfindingScore = pathfinder.bestScore;
    pathfindingTime += (clock() - start + 0.0) / CLOCKS_PER_SEC;

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
                invalidate_units();
                unitMapLocation = unit.get_location().get_map_location();
                passableMap.weights[unitMapLocation.get_x()][unitMapLocation.get_y()] = 1000;
            }
            else if(gc.has_unit_at_location(nextLocation)) {
                auto u = gc.sense_unit_at_location(nextLocation);
                if (u.get_team() == unit.get_team() && (u.get_unit_type() == Factory || u.get_unit_type() == Rocket)) {
                    if (gc.can_load(u.get_id(), unit.get_id())) {
                        passableMap.weights[unitMapLocation.get_x()][unitMapLocation.get_y()] = 1;
                        gc.load(u.get_id(), unit.get_id());
                        invalidate_units();
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
        invalidate_units();
        return true;
    }
    return false;
}

void mage_attack(const Unit& unit) {
    if (!gc.is_attack_ready(unit.get_id())) return;

    if (!unit.get_location().is_on_map()) return;

    // Calls on the controller take unit IDs for ownership reasons.
    const auto locus = unit.get_location().get_map_location();
    const auto nearby = gc.sense_nearby_units(locus, unit.get_attack_range() + 20);

    const Unit* best_unit = nullptr;
    double best_unit_score = 0;

    auto low_health = unit.get_health() / (float)unit.get_max_health() < 0.8f;
    auto& values = low_health ? unit_defensive_strategic_value : unit_strategic_value;

    auto hitScore = vector<vector<double>>(w, vector<double>(h));

    for (auto& place : nearby) {
        if (place.get_health() <= 0) continue;
        if (!gc.can_attack(unit.get_id(), place.get_id())) continue;
        
        float fractional_health = place.get_health() / (float)place.get_max_health();
        float value = values[place.get_unit_type()] / (fractional_health + 2.0);
        if (place.get_team() == unit.get_team()) value = -value;

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
    }
}

void attack_all_in_range(const Unit& unit) {
    if (!gc.is_attack_ready(unit.get_id())) return;

    if (!unit.get_location().is_on_map()) return;

    // Calls on the controller take unit IDs for ownership reasons.
    const auto locus = unit.get_location().get_map_location();
    const auto nearby = gc.sense_nearby_units(locus, unit.get_attack_range());

    const Unit* best_unit = nullptr;
    float totalWeight = 0;

    auto low_health = unit.get_health() / (float)unit.get_max_health() < 0.8f;
    auto& values = low_health ? unit_defensive_strategic_value : unit_strategic_value;

    for (auto& place : nearby) {
        if (place.get_health() <= 0) continue;
        if (!gc.can_attack(unit.get_id(), place.get_id())) continue;
        if (place.get_team() == unit.get_team()) continue;

        float fractional_health = place.get_health() / (float)place.get_max_health();
        float value = values[place.get_unit_type()] / fractional_health;
        value *= value;

        // Reservoir sampling
        totalWeight += value;
        if (((rand() % 100000)/100000.0f) * totalWeight <= value) {
            best_unit = &place;
        }
    }

    if (best_unit != nullptr) {
        //Attacking 'em enemies
        gc.attack(unit.get_id(), best_unit->get_id());
        invalidate_units();
    }
}

PathfindingMap BotUnit::defaultMilitaryTargetMap() {
    bool isHurt = (unit.get_health() < 0.8 * unit.get_max_health());
    MapReuseObject reuseObject(MapType::Target, unit.get_unit_type(), isHurt);

    PathfindingMap targetMap;
    if (reusableMaps.count(reuseObject)) {
        targetMap = reusableMaps[reuseObject];
    }
    else {
        targetMap = enemyNearbyMap * 0.0001;

        for (auto& enemy : enemyUnits) {
            if (enemy.get_location().is_on_map()) {
                auto pos = enemy.get_location().get_map_location();
                if (unit.get_unit_type() == Mage) {
                    targetMap.maxInfluence(mageTargetInfluence, pos.get_x(), pos.get_y());
                }
                else if (unit.get_unit_type() == Ranger) {
                    targetMap.maxInfluence(rangerTargetInfluence, pos.get_x(), pos.get_y());
                }
                else {
                    targetMap.maxInfluence(rangerTargetInfluence, pos.get_x(), pos.get_y());
                }
            }
        }
        
        auto&& initial_units = gc.get_starting_planet((Planet)0).get_initial_units();
        for (auto& enemy : initial_units) {
            if (enemy.get_team() == enemyTeam && enemy.get_location().is_on_map()) {
                auto pos = enemy.get_location().get_map_location();
                targetMap.weights[pos.get_x()][pos.get_y()] = max(targetMap.weights[pos.get_x()][pos.get_y()], 0.01);
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
                        factor = 0.1;
                    }
                    targetMap.addInfluenceMultiple(healerInfluence, pos.get_x(), pos.get_y(), factor);
                }

                if (u.get_unit_type() == Factory) {
                    if (!u.get_location().is_on_map()) {
                        continue;
                    }
                    auto pos = u.get_location().get_map_location();
                    targetMap.weights[pos.get_x()][pos.get_y()] += 0.1;
                }
            }
        }

        for (auto& u : ourUnits) {
            if (u.get_location().is_on_map() && is_structure(u.get_unit_type())) {
                auto pos = u.get_location().get_map_location();
                targetMap.weights[pos.get_x()][pos.get_y()] = 0;
            }
        }
        reusableMaps[reuseObject] = targetMap;
    }
        
    for (auto rocketId : unitShouldGoToRocket[unit.get_id()]) {
        auto unit = gc.get_unit(rocketId);
        if (!unit.get_location().is_on_map()) {
            continue;
        }
        auto rocketLocation = unit.get_location().get_map_location();
        targetMap.weights[rocketLocation.get_x()][rocketLocation.get_y()] += 100;
    }
    return targetMap;
}

PathfindingMap BotUnit::defaultMilitaryCostMap () {
    return (passableMap + enemyInfluenceMap * 2.0) / (nearbyFriendMap + 1.0);
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
