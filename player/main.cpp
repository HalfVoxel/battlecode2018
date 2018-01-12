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

struct BotUnit {
    Unit unit;
    const unsigned id;
    BotUnit(const Unit unit) : unit(unit), id(unit.get_id()) {}
    virtual void tick() {}
};

struct State {
    map<UnitType, int> typeCount;
} state;

struct MacroObject {
    double score;
    int cost;
    BotUnit* unit;
    Direction direction;
    UnitType unitType;

    MacroObject(double _score, int _cost, BotUnit* _unit, Direction _direction, UnitType _unitType) {
        score = _score;
        cost = _cost;
        unit = _unit;
        direction = _direction;
        unitType = _unitType;
    }

    MacroObject(double _score, int _cost, BotUnit* _unit, UnitType _unitType) {
        score = _score;
        cost = _cost;
        unit = _unit;
        unitType = _unitType;
    }

    bool operator<(const MacroObject& other) const {
        return score < other.score;
    }
};
    
vector<MacroObject> macroObjects;

struct BotWorker : BotUnit {
    BotWorker(const Unit unit) : BotUnit(unit) {}
    void tick() {

        if (!unit.get_location().is_on_map()) {
            return;
        }

        const auto locus = unit.get_location().get_map_location();
        const auto nearby = gc.sense_nearby_units(locus, 2);

        for (int i = 0; i < 9; ++i) {
            auto d = (Direction) i;
            if (gc.can_harvest(id, d)) {
                gc.harvest(id, d);
                break;
            }
        }

        for (auto place : nearby) {
            //Building 'em blueprints
            if(gc.can_build(id, place.get_id())) {
                gc.build(id, place.get_id());
                return;
            }
        }

        const unsigned id = unit.get_id();
        for (int i = 0; i < 8; i++) {
            Direction d = (Direction) i;
            // Placing 'em blueprints
            if(gc.can_blueprint(id, Factory, d) and gc.get_karbonite() >= unit_type_get_blueprint_cost(Factory)){
                double score = state.typeCount[Factory] < 3 ? (3 - state.typeCount[Factory]) : 0;
                macroObjects.emplace_back(score, unit_type_get_blueprint_cost(Factory), this, d, Factory);
            }

            if(gc.can_replicate(id, d) && gc.get_karbonite() >= unit_type_get_replicate_cost()) {
                double score = state.typeCount[Worker] < 10 ? (10 - state.typeCount[Factory]) : 0;
                macroObjects.emplace_back(score, unit_type_get_replicate_cost(), this, d, Worker);
            }
        }



        auto unitMapLocation = unit.get_location().get_map_location();

        double bestHarvestScore = -1;
        Direction bestHarvestDirection;
        auto planet = unitMapLocation.get_planet();
        auto& planetMap = gc.get_starting_planet(planet);
        int w = planetMap.get_width();
        int h = planetMap.get_height();
        PathfindingMap passableMap(w, h);
        for (int i = 0; i < w; i++) {
            for (int j = 0; j < h; j++) {
                auto location = MapLocation(planet, i, j);
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

        PathfindingMap karboniteMap(w, h);
        for (int i = 0; i < w; i++) {
            for (int j = 0; j < h; j++) {
                auto location = MapLocation(planet, i, j);
                int karbonite = planetMap.get_initial_karbonite_at(location);
                if (gc.can_sense_location(location)) {
                    karbonite = gc.get_karbonite_at(location);
                }
                karboniteMap.weights[i][j] = karbonite;
            }
        }

        PathfindingMap damagedStructureMap(w, h);
        for (auto& unit : ourUnits) {
            if (unit.get_unit_type() == Factory) {
                for (int i = 0; i < 8; i++) {
                    Direction d = (Direction) i;
                    auto location = unit.get_location().get_map_location().add(d);
                    int x = location.get_x();
                    int y = location.get_y();
                    if (x >= 0 && x < w && y >= 0 && y < h) {
                        damagedStructureMap.weights[x][y] = 20;
                    }
                }
            }
        }

        Pathfinder pathfinder;
        auto nextLocation = pathfinder.getNextLocation(unitMapLocation, karboniteMap + damagedStructureMap, passableMap);

        if (nextLocation != unitMapLocation) {
            auto d = unitMapLocation.direction_to(nextLocation);
            if (gc.is_move_ready(id) && gc.can_move(id,d)){
                gc.move_robot(id,d);
            }
            else {
            }
        }
    }
};

void attack_all_in_range(const Unit& unit) {
    // Calls on the controller take unit IDs for ownership reasons.
    const auto locus = unit.get_location().get_map_location();
    const auto nearby = gc.sense_nearby_units(locus, unit.get_attack_range());
    for (auto place : nearby) {
        //Attacking 'em enemies
        if(place.get_team() != unit.get_team() && gc.can_attack(unit.get_id(), place.get_id()) && gc.is_attack_ready(unit.get_id())){
            gc.attack(unit.get_id(), place.get_id());
            break;
        }
    }
}

struct BotKnight : BotUnit {
    BotKnight(const Unit unit) : BotUnit(unit) {}
    void tick() {
        if (!unit.get_location().is_on_map()) return;

        // Calls on the controller take unit IDs for ownership reasons.
        attack_all_in_range(unit);
    }
};

void move_with_pathfinding_to(const Unit& unit, MapLocation target) {
    auto unitMapLocation = unit.get_location().get_map_location();
    auto planet = unitMapLocation.get_planet();
    auto& planetMap = gc.get_starting_planet(planet);
    int w = planetMap.get_width();
    int h = planetMap.get_height();
    PathfindingMap passableMap(w, h);
    for (int i = 0; i < w; i++) {
        for (int j = 0; j < h; j++) {
            auto location = MapLocation(planet, i, j);
            if (planetMap.is_passable_terrain_at(location)) {
                passableMap.weights[i][j] = 1.0;
                if (gc.can_sense_location(location) && !gc.is_occupiable(location)) {
                    passableMap.weights[i][j] = 1000.0;
                }
            } else {
                passableMap.weights[i][j] = numeric_limits<double>::infinity();
            }
        }
    }

    auto id = unit.get_id();

    PathfindingMap rewardMap(w, h);
    rewardMap.weights[target.get_x()][target.get_y()] = 1;

    Pathfinder pathfinder;
    auto nextLocation = pathfinder.getNextLocation(unitMapLocation, rewardMap, passableMap);

    if (nextLocation != unitMapLocation) {
        auto d = unitMapLocation.direction_to(nextLocation);
        if (gc.is_move_ready(id) && gc.can_move(id,d)){
            gc.move_robot(id,d);
        }
    }
}

struct BotRanger : BotUnit {
    BotRanger(const Unit unit) : BotUnit(unit) {}

    void tick() {
        if (!unit.get_location().is_on_map()) return;

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

        for (auto enemy : gc.get_starting_planet((Planet)0).get_initial_units()) {
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
            cout << "Before movement " << unit.get_location().get_map_location().get_x() << " " << unit.get_location().get_map_location().get_y() << endl;
            move_with_pathfinding_to(unit, p);
            unit = gc.get_unit(unit.get_id());
            cout << "After movement " << unit.get_location().get_map_location().get_x() << " " << unit.get_location().get_map_location().get_y() << endl;
        }

        attack_all_in_range(unit);
    }
};

struct BotMage : BotUnit {
    BotMage(const Unit unit) : BotUnit(unit) {}
};

struct BotHealer : BotUnit {
    BotHealer(const Unit unit) : BotUnit(unit) {}
};

struct BotFactory : BotUnit {
    BotFactory(const Unit unit) : BotUnit(unit) {}

    void tick() {
        auto garrison = unit.get_structure_garrison();
        bool unloaded = false;
        if (garrison.size() > 0){
            Direction dir = (Direction) (rand() % 8);
            if (gc.can_unload(id, dir)){
                gc.unload(id, dir);
                unloaded = true;
                cout << "Unloading a knight!" << endl;
            }
        }
        if (gc.can_produce_robot(id, Ranger)){
            double score = 0.5;
            macroObjects.emplace_back(score, unit_type_get_factory_cost(Ranger), this, Ranger);
        }
    }
};

struct BotRocket : BotUnit {
    BotRocket(const Unit unit) : BotUnit(unit) {}
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
    gc.queue_research(Knight);
    gc.queue_research(Knight);
    gc.queue_research(Knight);

    if (bc_has_err()) {
        // If there was an error creating gc, just die.
        printf("Failed, dying.\n");
        exit(1);
    }
    printf("Connected!\n");
    map<unsigned int, BotUnit*> unitMap;

    // loop through the whole game.
    while (true) {
        unsigned round = gc.get_round();
        printf("Round: %d\n", round);

        find_units();

        macroObjects.clear();
        state = State();
        for (auto& unit : ourUnits) {
            state.typeCount[unit.get_unit_type()]++;
        }

        for (const auto unit : ourUnits) {
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
        for (auto& macroObject : macroObjects) {
            if (macroObject.score <= 0) {
                break;
            }
            if (gc.get_karbonite() >= macroObject.cost) {
                auto unit = macroObject.unit;
                if (unit->unit.get_unit_type() == Worker) {
                    auto d = macroObject.direction;
                    if (macroObject.unitType == Worker) {
                        if(gc.can_replicate(unit->id, d) && gc.get_karbonite() >= unit_type_get_replicate_cost()) {
                            cout << "We are replicating!!" << endl;
                            gc.replicate(unit->id, d);
                        }
                    }
                    else {
                        if(gc.can_blueprint(unit->id, macroObject.unitType, d) and gc.get_karbonite() >= unit_type_get_blueprint_cost(macroObject.unitType)){
                            cout << "We are building a factory!!" << endl;
                            cout << "Score = " << macroObject.score << endl;
                            gc.blueprint(unit->id, macroObject.unitType, d);
                        }
                    }
                }
                else { // Factory
                    if (gc.can_produce_robot(unit->id, macroObject.unitType)){
                        gc.produce_robot(unit->id, macroObject.unitType);
                    }
                }
            }
            else {
                break;
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
