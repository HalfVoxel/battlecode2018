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
map<Planet, PathfindingMap> planetPassableMap;
void invalidate_units();
struct BotUnit;
map<unsigned int, BotUnit*> unitMap;

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
    float best_value = -1;
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
            best_value = value;
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

    void default_military_behaviour() {
        Unit* closest_unit = nullptr;
        float closest_dist = 100000;

        for (auto& enemy : enemyUnits) {
            if (enemy.get_location().is_on_map()) {
                auto pos = enemy.get_location().get_map_location();
                auto dist = pos.distance_squared_to(unit.get_location().get_map_location());
                if (dist < closest_dist) {
                    closest_unit = &enemy;
                    closest_dist = dist;
                }
            }
        }

        auto initial_units = gc.get_starting_planet((Planet)0).get_initial_units();
        for (auto& enemy : initial_units) {
            if (enemy.get_team() == enemyTeam && enemy.get_location().is_on_map()) {
                auto pos = enemy.get_location().get_map_location();
                auto dist = pos.distance_squared_to(unit.get_location().get_map_location());
                if (dist < closest_dist) {
                    closest_unit = &enemy;
                    closest_dist = dist;
                }
            }
        }

        if (closest_unit != nullptr) {
            auto p = closest_unit->get_location().get_map_location();
            move_with_pathfinding_to(unit, p);
            unit = gc.get_unit(unit.get_id());
        }

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
} state;

struct MacroObject {
    double score;
    int cost;
    int priority;
    int rnd;
    function<void()> lambda;

    MacroObject(double _score, int _cost, int _priority, function<void()> _lambda) {
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
                macroObjects.emplace_back(1, 0, 1, [=]{
                    if(gc.can_build(id, place.get_id())) {
                        gc.build(id, placeId);
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

        for (int i = 0; i < 8; i++) {
            Direction d = (Direction) i;
            // Placing 'em blueprints
            auto newLocation = unitMapLocation.add(d);
            if(isOnMap(newLocation) && gc.is_occupiable(newLocation)) {
                double score = state.typeCount[Factory] < 3 ? (3 - state.typeCount[Factory]) : 5.0 / (5.0 + state.typeCount[Factory]);
                macroObjects.emplace_back(score, unit_type_get_blueprint_cost(Factory), 2, [=]{
                    if(gc.can_blueprint(id, Factory, d)){
                        gc.blueprint(id, Factory, d);
                    }
                });
            }

            if(gc.can_replicate(id, d)) {
                double karbonitePerWorker = (state.remainingKarboniteOnEarth + 0.0) / state.typeCount[Worker];
                double score = karbonitePerWorker * 0.006;
                macroObjects.emplace_back(score, unit_type_get_replicate_cost(), 2, [=]{
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
            if (unit.get_unit_type() == Factory) {
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

        Pathfinder pathfinder;
        auto nextLocation = pathfinder.getNextLocation(unitMapLocation, karboniteMap + damagedStructureMap, planetPassableMap[planet]);

        if (nextLocation != unitMapLocation) {
            auto d = unitMapLocation.direction_to(nextLocation);
            if (gc.is_move_ready(id) && gc.can_move(id,d)){
                gc.move_robot(id,d);
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
        for (auto& u : ourUnits) {
            if (is_robot(u.get_unit_type())) {
                if (u.get_id() == id) {
                    continue;
                }
                double remainingLife = u.get_health() / (u.get_max_health() + 0.0);
                if (remainingLife == 1.0) {
                    continue;
                }
                for (int i = 0; i < 8; i++) {
                    Direction d = (Direction) i;
                    auto location = u.get_location().get_map_location().add(d);
                    int x = location.get_x();
                    int y = location.get_y();
                    if (x >= 0 && x < w && y >= 0 && y < h) {
                        damagedRobotMap.weights[x][y] = max(damagedRobotMap.weights[x][y], 15 * (2.0 - remainingLife));
                    }
                }

                auto distance = u.get_location().get_map_location().distance_squared_to(unitMapLocation);
                if (gc.can_heal(id, u.get_id()) && gc.is_heal_ready(id)) {
                    gc.heal(id, u.get_id());
                }
            }
        }
        Pathfinder pathfinder;
        auto nextLocation = pathfinder.getNextLocation(unitMapLocation, damagedRobotMap, planetPassableMap[planet]);

        if (nextLocation != unitMapLocation) {
            auto d = unitMapLocation.direction_to(nextLocation);
            if (gc.is_move_ready(id) && gc.can_move(id,d)){
                gc.move_robot(id,d);
            }
        }
        for (auto& u : ourUnits) {
            if (is_robot(u.get_unit_type())) {
                double remainingLife = u.get_health() / (u.get_max_health() + 0.0);
                if (remainingLife == 1.0) {
                    continue;
                }
                if (gc.can_heal(id, u.get_id()) && gc.is_heal_ready(id)) {
                    gc.heal(id, u.get_id());
                }
            }
        }
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
            if (state.typeCount[Ranger] > 3) {
                score += 2.5;
            }
            if (state.totalRobotDamage > 200) {
                score += 4.0;
            }
            score /= state.typeCount[Healer];
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
    gc.queue_research(Ranger);
    gc.queue_research(Ranger);
    gc.queue_research(Ranger);
    gc.queue_research(Worker);
    gc.queue_research(Worker);
    gc.queue_research(Worker);

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

    // loop through the whole game.
    while (true) {

        for (int i = 0; i < w; i++) {
            for (int j = 0; j < h; j++) {
                auto location = MapLocation(Earth, i, j);
                if (gc.can_sense_location(location)) {
                    int karbonite = gc.get_karbonite_at(location);
                    karboniteMap.weights[i][j] = min(karboniteMap.weights[i][j], (double) karbonite);
                }
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
                        if (gc.can_sense_location(location) && !gc.is_occupiable(location)) {
                            passableMap.weights[i][j] = 1000.0;
                        }
                    }
                    else {
                        passableMap.weights[i][j] = numeric_limits<double>::infinity();
                    }
                }
            }
            planetPassableMap[planets[p]] = passableMap;
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

            BotUnit& botUnit = *botUnitPtr;
            botUnit.unit = unit;
            botUnit.tick();
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

        // this line helps the output logs make more sense by forcing output to be sent
        // to the manager.
        // it's not strictly necessary, but it helps.
        // pause and wait for the next turn.
        fflush(stdout);
        gc.next_turn();
    }
    // I'm convinced C++ is the better option :)
}
