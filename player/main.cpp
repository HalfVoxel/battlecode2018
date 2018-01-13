#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>
#include <random>
#include <functional>
#include <iostream>
#include <map>

#include "bc.hpp"
#include "pathfinding.hpp"

using namespace bc;
using namespace std;

GameController gc;

vector<Unit> ourUnits;
vector<Unit> enemyUnits;
vector<Unit> allUnits;
Team ourTeam;
Team enemyTeam;
PathfindingMap karboniteMap;
PathfindingMap enemyInfluenceMap;
PathfindingMap workerProximityMap;
map<Planet, PathfindingMap> planetPassableMap;
void invalidate_units();
struct BotUnit;
map<unsigned int, BotUnit*> unitMap;
vector<vector<double> > rangerTargetInfluence;
vector<vector<double> > mageTargetInfluence;
vector<vector<double> > knightTargetInfluence;
vector<vector<double> > healerProximityInfluence;
vector<vector<double> > healerInfluence;
vector<vector<double> > workerProximityInfluence;
vector<vector<double> > factoryProximityInfluence;
vector<vector<double> > rocketProximityInfluence;
double averageHealerSuccessRate;
map<unsigned, vector<unsigned> > unitShouldGoToRocket;

void initInfluence() {
    int r = 7;
    rangerTargetInfluence = vector<vector<double>>(2*r+1, vector<double>(2*r+1));
    for (int dx = -r; dx <= r; ++dx) {
        for (int dy = -r; dy <= r; ++dy) {
            int dis2 = dx*dx + dy*dy;
            if (dis2 <= 10) {
                continue;
            }
            if (dis2 > 50) {
                continue;
            }
            rangerTargetInfluence[dx+r][dy+r] = 1;
        }
    }
    
    r = 6;
    mageTargetInfluence = vector<vector<double>>(2*r+1, vector<double>(2*r+1));
    for (int dx = -r; dx <= r; ++dx) {
        for (int dy = -r; dy <= r; ++dy) {
            int dis2 = dx*dx + dy*dy;
            if (dis2 > 30) {
                continue;
            }
            mageTargetInfluence[dx+r][dy+r] = 1;
        }
    }
    
    r = 2;
    knightTargetInfluence = vector<vector<double>>(2*r+1, vector<double>(2*r+1));
    for (int dx = -r; dx <= r; ++dx) {
        for (int dy = -r; dy <= r; ++dy) {
            int dis2 = dx*dx + dy*dy;
            if (dis2 > 1) {
                continue;
            }
            knightTargetInfluence[dx+r][dy+r] = 1;
        }
    }
    
    r = 5;
    healerProximityInfluence = vector<vector<double>>(2*r+1, vector<double>(2*r+1));
    for (int dx = -r; dx <= r; ++dx) {
        for (int dy = -r; dy <= r; ++dy) {
            int dis2 = dx*dx + dy*dy;
            healerProximityInfluence[dx+r][dy+r] = 1 / (1.0 + dis2);
        }
    }
    
    r = 6;
    healerInfluence = vector<vector<double>>(2*r+1, vector<double>(2*r+1));
    for (int dx = -r; dx <= r; ++dx) {
        for (int dy = -r; dy <= r; ++dy) {
            int dis2 = dx*dx + dy*dy;
            if (dis2 <= 30) {
                healerInfluence[dx+r][dy+r] = 1;
            }
        }
    }
    
    r = 5;
    workerProximityInfluence = vector<vector<double>>(2*r+1, vector<double>(2*r+1));
    for (int dx = -r; dx <= r; ++dx) {
        for (int dy = -r; dy <= r; ++dy) {
            int dis2 = dx*dx + dy*dy;
            workerProximityInfluence[dx+r][dy+r] = 0.7 / (1.0 + dis2);
        }
    }
    
    r = 5;
    factoryProximityInfluence = vector<vector<double>>(2*r+1, vector<double>(2*r+1));
    for (int dx = -r; dx <= r; ++dx) {
        for (int dy = -r; dy <= r; ++dy) {
            int dis2 = dx*dx + dy*dy;
            factoryProximityInfluence[dx+r][dy+r] = 0.5 / (1.0 + dis2);
        }
    }
    
    r = 5;
    rocketProximityInfluence = vector<vector<double>>(2*r+1, vector<double>(2*r+1));
    for (int dx = -r; dx <= r; ++dx) {
        for (int dy = -r; dy <= r; ++dy) {
            int dis2 = dx*dx + dy*dy;
            rocketProximityInfluence[dx+r][dy+r] = 1.0 / (1.0 + dis2);
            if (dis2 == 1) {
                rocketProximityInfluence[dx+r][dy+r] = 5.0;
            }
        }
    }
}

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


static_assert((int)Worker == 0, "");
static_assert((int)Rocket == 6, "");

void attack_all_in_range(const Unit& unit) {
    if (!gc.is_attack_ready(unit.get_id())) return;

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

void move_with_pathfinding_to(const Unit& unit, MapLocation target) {
    auto id = unit.get_id();
    if (!gc.is_move_ready(id)) return;

    auto unitMapLocation = unit.get_location().get_map_location();
    auto planet = unitMapLocation.get_planet();
    auto& planetMap = gc.get_starting_planet(planet);
    int w = planetMap.get_width();
    int h = planetMap.get_height();

    PathfindingMap rewardMap(w, h);
    rewardMap.weights[target.get_x()][target.get_y()] = 1;

    Pathfinder pathfinder;
    auto nextLocation = pathfinder.getNextLocation(unitMapLocation, rewardMap, planetPassableMap[planet]);

    if (nextLocation != unitMapLocation) {
        auto d = unitMapLocation.direction_to(nextLocation);
        if (gc.can_move(id,d)){
            gc.move_robot(id,d);
        }
    }
}

struct BotUnit {
    Unit unit;
    const unsigned id;
    BotUnit(const Unit& unit) : unit(unit), id(unit.get_id()) {}
    virtual void tick() {}

    MapLocation moveUnit(PathfindingMap targetMap, PathfindingMap costMap) {
        auto unitMapLocation = unit.get_location().get_map_location();
        auto planet = unitMapLocation.get_planet();
        auto tmpLocationPenalty = planetPassableMap[planet].weights[unitMapLocation.get_x()][unitMapLocation.get_y()];
        planetPassableMap[planet].weights[unitMapLocation.get_x()][unitMapLocation.get_y()] = 0;
        Pathfinder pathfinder;
        auto nextLocation = pathfinder.getNextLocation(unitMapLocation, targetMap, planetPassableMap[planet] + enemyInfluenceMap + workerProximityMap);
        planetPassableMap[planet].weights[unitMapLocation.get_x()][unitMapLocation.get_y()] = tmpLocationPenalty;

        if (nextLocation != unitMapLocation) {
            auto d = unitMapLocation.direction_to(nextLocation);
            if (gc.is_move_ready(id)) {
                if (gc.can_move(id, d)) {
                    gc.move_robot(id,d);
                }
                else if(gc.has_unit_at_location(nextLocation)) {
                    auto u = gc.sense_unit_at_location(nextLocation);
                    if (u.get_team() == unit.get_team() && (u.get_unit_type() == Factory || u.get_unit_type() == Rocket)) {
                        if (gc.can_load(u.get_id(), unit.get_id())) {
                            gc.load(u.get_id(), unit.get_id());
                        }
                    }
                }
            }
        }
        return nextLocation;
    }

    void default_military_behaviour() {

        attack_all_in_range(unit);

        auto unitMapLocation = unit.get_location().get_map_location();
        auto planet = unitMapLocation.get_planet();
        auto& planetMap = gc.get_starting_planet(planet);
        int w = planetMap.get_width();
        int h = planetMap.get_height();
        PathfindingMap targetMap(w, h);

        for (auto& enemy : enemyUnits) {
            if (enemy.get_location().is_on_map()) {
                auto pos = enemy.get_location().get_map_location();
                targetMap.maxInfluence(rangerTargetInfluence, pos.get_x(), pos.get_y());
            }
        }

        auto initial_units = gc.get_starting_planet((Planet)0).get_initial_units();
        for (auto& enemy : initial_units) {
            if (enemy.get_team() == enemyTeam && enemy.get_location().is_on_map()) {
                auto pos = enemy.get_location().get_map_location();
                targetMap.weights[pos.get_x()][pos.get_y()] = max(targetMap.weights[pos.get_x()][pos.get_y()], 0.01);
            }
        }

        if (unit.get_health() < 0.8 * unit.get_max_health()) {
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
        
        for (auto rocketId : unitShouldGoToRocket[unit.get_id()]) {
            if (gc.has_unit(rocketId)) {
                auto unit = gc.get_unit(rocketId);
                if (!unit.get_location().is_on_map()) {
                    continue;
                }
                auto rocketLocation = unit.get_location().get_map_location();
                targetMap.weights[rocketLocation.get_x()][rocketLocation.get_y()] += 100;
            }
            else {
                cout << "Warning! A rocket disappeared" << endl;
            }
        }

        moveUnit(targetMap, planetPassableMap[planet] + enemyInfluenceMap);

        attack_all_in_range(unit);
    }
};

/** Call if our units may have been changed in some way, e.g damaged by Mage splash damage and killed */
void invalidate_units() {
    for (auto& unit : ourUnits) {
        unitMap[unit.get_id()]->unit = gc.get_unit(unit.get_id());
    }
}

struct State {
    map<UnitType, int> typeCount;
    double remainingKarboniteOnEarth;
    int totalRobotDamage;
    int totalUnitCount;
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
   auto planet = location.get_planet();
   auto& planetMap = gc.get_starting_planet(planet);
   int w = planetMap.get_width();
   int h = planetMap.get_height();
   return location.get_x() >= 0 && location.get_y() >= 0 && location.get_x() < w && location.get_y() < h;
}

struct BotWorker : BotUnit {
    BotWorker(const Unit& unit) : BotUnit(unit) {}
    void tick() {

        if (!unit.get_location().is_on_map()) {
            return;
        }

        const auto locus = unit.get_location().get_map_location();
        const auto nearby = gc.sense_nearby_units(locus, 2);

        auto unitMapLocation = unit.get_location().get_map_location();

        const unsigned id = unit.get_id();

        for (auto place : nearby) {
            //Building 'em blueprints
            if(gc.can_build(id, place.get_id())) {
                const int& placeId = place.get_id();
                double score = (place.get_health() / (0.0 + place.get_max_health()));
                macroObjects.emplace_back(score, 0, 1, [=]{
                    if(gc.can_build(id, place.get_id())) {
                        gc.build(id, placeId);
                    }
                });
            }
            if(gc.can_repair(id, place.get_id()) && place.get_health() < place.get_max_health()) {
                const int& placeId = place.get_id();
                double score = 2 - (place.get_health() / (0.0 + place.get_max_health()));
                macroObjects.emplace_back(score, 0, 1, [=]{
                    if(gc.can_repair(id, place.get_id())) {
                        gc.repair(id, placeId);
                    }
                });
            }
        }

        double bestHarvestScore = -1;
        Direction bestHarvestDirection;
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
        double replicateScore = karbonitePerWorker * 0.005 + 2.5 / state.typeCount[Worker];

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
                    double score = factor * (state.totalUnitCount - state.typeCount[Factory] - 12 * state.typeCount[Rocket]);
                    macroObjects.emplace_back(score, unit_type_get_blueprint_cost(Rocket), 2, [=]{
                        if(gc.can_blueprint(id, Rocket, d)){
                            gc.blueprint(id, Rocket, d);
                        }
                    });
                }
            }

            if(gc.can_replicate(id, d)) {
                macroObjects.emplace_back(replicateScore, unit_type_get_replicate_cost(), 2, [=]{
                    if(gc.can_replicate(id, d)) {
                        gc.replicate(id, d);
                    }
                });
            }
        }

        auto planet = unitMapLocation.get_planet();
        auto& planetMap = gc.get_starting_planet(planet);
        int w = planetMap.get_width();
        int h = planetMap.get_height();

        PathfindingMap damagedStructureMap(w, h);
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
                        damagedStructureMap.weights[x][y] = max(damagedStructureMap.weights[x][y], 15 * (2.0 - remainingLife));
                    }
                }
            }
        }
        
        PathfindingMap targetMap = karboniteMap + damagedStructureMap;
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

        for (auto rocketId : unitShouldGoToRocket[unit.get_id()]) {
            if (gc.has_unit(rocketId)) {
                auto unit = gc.get_unit(rocketId);
                if(!unit.get_location().is_on_map()) {
                    continue;
                }
                auto rocketLocation = unit.get_location().get_map_location();
                targetMap.weights[rocketLocation.get_x()][rocketLocation.get_y()] += 100;
            }
            else {
                cout << "Warning! A rocket disappeared" << endl;
            }
        }

        auto nextLocation = moveUnit(targetMap, planetPassableMap[planet] + enemyInfluenceMap + workerProximityMap);
        
        if(unit.get_ability_heat() < 10) {
            unitMapLocation = nextLocation;
            Pathfinder pathfinder;
            nextLocation = pathfinder.getNextLocation(unitMapLocation, karboniteMap + damagedStructureMap, planetPassableMap[planet] + enemyInfluenceMap + workerProximityMap);

            if (nextLocation != unitMapLocation) {
                auto d = unitMapLocation.direction_to(nextLocation);
                double score = replicateScore + 0.1 * log(1.0 + pathfinder.bestScore);
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
    void tick() {
        if (!unit.get_location().is_on_map()) return;

        default_military_behaviour();
    }
};

struct BotRanger : BotUnit {
    BotRanger(const Unit& unit) : BotUnit(unit) {}

    void tick() {
        if (!unit.get_location().is_on_map()) return;

        default_military_behaviour();
    }
};

struct BotMage : BotUnit {
    BotMage(const Unit& unit) : BotUnit(unit) {}

    void tick() {
        if (!unit.get_location().is_on_map()) return;

        default_military_behaviour();
    }
};

struct BotHealer : BotUnit {
    BotHealer(const Unit& unit) : BotUnit(unit) {}

    void tick() {
        if (!unit.get_location().is_on_map()) return;

        auto unitMapLocation = unit.get_location().get_map_location();
        auto planet = unitMapLocation.get_planet();
        auto& planetMap = gc.get_starting_planet(planet);
        int w = planetMap.get_width();
        int h = planetMap.get_height();
        PathfindingMap damagedRobotMap(w, h);
        PathfindingMap targetMap(w, h);
        PathfindingMap healerProximityMap(w, h);
        bool succeededHealing = false;
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

                for (int i = 0; i < 8; i++) {
                    Direction d = (Direction) i;
                    auto location = u.get_location().get_map_location().add(d);
                    int x = location.get_x();
                    int y = location.get_y();
                    if (x >= 0 && x < w && y >= 0 && y < h) {
                        damagedRobotMap.weights[x][y] = max(damagedRobotMap.weights[x][y], 15 * (2.0 - remainingLife));
                    }
                }

                if (u.get_unit_type() == Healer) {
                    healerProximityMap.addInfluence(healerProximityInfluence, uMapLocation.get_x(), uMapLocation.get_y());
                }

                if (gc.can_heal(id, u.get_id()) && gc.is_heal_ready(id)) {
                    gc.heal(id, u.get_id());
                    succeededHealing = true;
                }
            }
        }
        
        for (auto rocketId : unitShouldGoToRocket[unit.get_id()]) {
            if (gc.has_unit(rocketId)) {
                auto unit = gc.get_unit(rocketId);
                if(!unit.get_location().is_on_map()) {
                    continue;
                }
                auto rocketLocation = unit.get_location().get_map_location();
                targetMap.weights[rocketLocation.get_x()][rocketLocation.get_y()] += 100;
            }
            else {
                cout << "Warning! A rocket disappeared" << endl;
            }
        }


        moveUnit(damagedRobotMap + targetMap, planetPassableMap[planet] + healerProximityMap + enemyInfluenceMap);

        for (auto& u : ourUnits) {
            if (is_robot(u.get_unit_type())) {
                double remainingLife = u.get_health() / (u.get_max_health() + 0.0);
                if (remainingLife == 1.0) {
                    continue;
                }
                if (gc.can_heal(id, u.get_id()) && gc.is_heal_ready(id)) {
                    gc.heal(id, u.get_id());
                    succeededHealing = true;
                }
            }
        }
        double interpolationFactor = 0.99;
        averageHealerSuccessRate = averageHealerSuccessRate * interpolationFactor + succeededHealing * (1-interpolationFactor);
    }
};

struct BotFactory : BotUnit {
    BotFactory(const Unit& unit) : BotUnit(unit) {}

    void tick() {
        auto garrison = unit.get_structure_garrison();
        if (garrison.size() > 0){
            Direction dir = (Direction) (rand() % 8);
            if (gc.can_unload(id, dir)){
                gc.unload(id, dir);
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
        if (gc.can_produce_robot(id, Healer)){
            double score = 0.0;
            if (state.typeCount[Ranger] > 6) {
                score += 2.5;
            }
            if (state.totalRobotDamage > 200) {
                score += 1.0;
            }
            score /= state.typeCount[Healer];
            score += averageHealerSuccessRate * 1.5;
            macroObjects.emplace_back(score, unit_type_get_factory_cost(Healer), 2, [=] {
                if (gc.can_produce_robot(id, Healer)) {
                    gc.produce_robot(id, Healer);
                }
            });
        }
    }
};

struct BotRocket : BotUnit {
    BotRocket(const Unit& unit) : BotUnit(unit) {}
    
    void tick() {
        if (!unit.get_location().is_on_map()) {
            return;
        }
        cout << "In garrison: " << unit.get_structure_garrison().size() << endl;
        if(unit.get_location().get_map_location().get_planet() == Mars) {
            auto garrison = unit.get_structure_garrison();
            if (garrison.size() > 0){
                Direction dir = (Direction) (rand() % 8);
                if (gc.can_unload(id, dir)){
                    gc.unload(id, dir);
                }
            }
        }
        else {
            if (unit.get_structure_garrison().size() == unit.get_structure_max_capacity() || gc.get_round() == 749) {
                auto& marsMap = gc.get_starting_planet(Mars);
                int w = marsMap.get_width();
                int h = marsMap.get_height();
                while(true) {
                    int x = rand()%w;
                    int y = rand()%h;
                    auto location = MapLocation(Mars, x, y);
                    if (gc.can_launch_rocket(unit.get_id(), location)) {
                        gc.launch_rocket(unit.get_id(), location);
                        break;
                    }
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
        auto unit = gc.get_unit(id);
        if (unit.get_unit_type() == Worker) {
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
        }
        switch(researchInfo.get_level(Healer)) {
            case 0: 
                scores[Healer] = 9 + 2 * state.typeCount[Healer];
                break;
            case 1: 
                scores[Healer] = 8 + 1.5 * state.typeCount[Healer];
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

        UnitType bestType;
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

void find_units() {
    ourUnits = gc.get_my_units();
    auto planet = gc.get_planet();
    ourTeam = gc.get_team();
    enemyTeam = (Team)(1 - (int)gc.get_team());

    allUnits = vector<Unit>();
    enemyUnits = vector<Unit>();
    for (int team = 0; team < 2; team++) {
        if (team != ourTeam) {
            auto u = gc.sense_nearby_units_by_team(MapLocation(planet, 0, 0), 1000000, (Team)team);
            enemyUnits.insert(enemyUnits.end(), u.begin(), u.end());
        }
    }
    allUnits.insert(allUnits.end(), ourUnits.begin(), ourUnits.end());
    allUnits.insert(allUnits.end(), enemyUnits.begin(), enemyUnits.end());
}

int main() {
    srand(time(0));

    printf("Player C++ bot starting\n");
    printf("Connecting to manager...\n");

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

    auto& earthMap = gc.get_starting_planet(Earth);
    int w = earthMap.get_width();
    int h = earthMap.get_height();
    karboniteMap = PathfindingMap(w, h);
    for (int i = 0; i < w; i++) {
        for (int j = 0; j < h; j++) {
            auto location = MapLocation(Earth, i, j);
            int karbonite = earthMap.get_initial_karbonite_at(location);
            karboniteMap.weights[i][j] = karbonite;
        }
    }

    initInfluence();
            
    Researcher researcher;

    // loop through the whole game.
    while (true) {

        for (int i = 0; i < w; i++) {
            for (int j = 0; j < h; j++) {
                auto location = MapLocation(gc.get_planet(), i, j);
                if (gc.can_sense_location(location)) {
                    int karbonite = gc.get_karbonite_at(location);
                    karboniteMap.weights[i][j] = karbonite;
                }
            }
        }
        
        enemyInfluenceMap = PathfindingMap(w, h);
        for (auto& u : enemyUnits) {
            if (u.get_location().is_on_map()) {
                if (u.get_unit_type() == Ranger) {
                    auto pos = u.get_location().get_map_location();
                    enemyInfluenceMap.addInfluence(rangerTargetInfluence, pos.get_x(), pos.get_y());
                }
                if (u.get_unit_type() == Mage) {
                    auto pos = u.get_location().get_map_location();
                    enemyInfluenceMap.addInfluence(mageTargetInfluence, pos.get_x(), pos.get_y());
                }
                if (u.get_unit_type() == Knight) {
                    auto pos = u.get_location().get_map_location();
                    enemyInfluenceMap.addInfluence(knightTargetInfluence, pos.get_x(), pos.get_y());
                }
            }
        }
        
        workerProximityMap = PathfindingMap(w, h);
        for (auto& u : ourUnits) {
            if (u.get_location().is_on_map()) {
                if (u.get_unit_type() == Worker) {
                    auto pos = u.get_location().get_map_location();
                    workerProximityMap.addInfluence(workerProximityInfluence, pos.get_x(), pos.get_y());
                }
                if (u.get_unit_type() == Factory) {
                    auto pos = u.get_location().get_map_location();
                    workerProximityMap.addInfluence(factoryProximityInfluence, pos.get_x(), pos.get_y());
                }
                if (u.get_unit_type() == Rocket) {
                    auto pos = u.get_location().get_map_location();
                    workerProximityMap.addInfluence(rocketProximityInfluence, pos.get_x(), pos.get_y());
                }
            }
        }

        unitShouldGoToRocket.clear();

        for (auto& unit : ourUnits) {
            if (unit.get_unit_type() == Rocket) {
                selectTravellersForRocket(unit);
            }
        }

        unsigned round = gc.get_round();
        printf("Round: %d\n", round);

        find_units();

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

        Planet planets[] = { Earth, Mars };
        for (int p = 0; p < 2; ++p) {
            auto& planetMap = gc.get_starting_planet(planets[p]);
            int w = planetMap.get_width();
            int h = planetMap.get_height();
            PathfindingMap passableMap(w, h);
            for (int i = 0; i < w; i++) {
                for (int j = 0; j < h; j++) {
                    auto location = MapLocation(planets[p], i, j);
					if (planetMap.is_passable_terrain_at(location)) {
                        passableMap.weights[i][j] = 1.0;
                    }
                    else {
                        passableMap.weights[i][j] = numeric_limits<double>::infinity();
                    }
                }
            }
            planetPassableMap[planets[p]] = passableMap;
        }

        for (const auto unit : enemyUnits) {
            auto unitMapLocation = unit.get_location().get_map_location();
            auto planet = unitMapLocation.get_planet();
            planetPassableMap[planet].weights[unitMapLocation.get_x()][unitMapLocation.get_y()] = 1000;
        }

        for (const auto unit : ourUnits) {
            if (unit.get_unit_type() != Factory && unit.get_unit_type() != Rocket && unit.get_location().is_on_map()) {
                auto unitMapLocation = unit.get_location().get_map_location();
                auto planet = unitMapLocation.get_planet();
                planetPassableMap[planet].weights[unitMapLocation.get_x()][unitMapLocation.get_y()] = 1000;
            }
        }

        for (const auto unit : ourUnits) {
            assert(gc.has_unit(unit.get_id()));
            const unsigned id = unit.get_id();
            BotUnit* botUnitPtr;

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
                unitMap[id] = botUnitPtr;
            } else {
                botUnitPtr = unitMap[id];
            }
            botUnitPtr->unit = unit;
        }

        for (const auto unit : ourUnits) {
            unitMap[unit.get_id()]->tick();
        }
        sort(macroObjects.rbegin(), macroObjects.rend());
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
            }
        }
        
        auto researchInfo = gc.get_research_info();
        if (researchInfo.get_queue().size() == 0) {
            auto type = researcher.getBestResearch();
            gc.queue_research(type);
        }

        // this line helps the output logs make more sense by forcing output to be sent
        // to the manager.
        // it's not strictly necessary, but it helps.
        // pause and wait for the next turn.
        fflush(stdout);
        gc.next_turn();
    }
    // I'm convinced C++ is the better option :)
}
