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

struct BotUnit {
    Unit unit;
    const unsigned id;
    BotUnit(const Unit unit) : unit(unit), id(unit.get_id()) {}
    virtual void tick() {}
};

struct BotWorker : BotUnit {
    BotWorker(const Unit& unit) : BotUnit(unit) {}
    void tick() {
        
        if (!unit.get_location().is_on_map()) {
            return;
        }

        const unsigned id = unit.get_id();
        Direction d = (Direction) (rand() % 9);
        // Placing 'em blueprints
        if(gc.can_blueprint(id, Factory, d) and gc.get_karbonite() > unit_type_get_blueprint_cost(Factory)){
            cout << "We are building a factory!!" << endl;
            gc.blueprint(id, Factory, d);
        }
        for (int i = 0; i < 9; ++i) {
            auto d = (Direction) i;
            if (gc.can_harvest(id, d)) {
                gc.harvest(id, d);
                break;
            }
        }
        auto unitMapLocation = unit.get_location().get_map_location();
        cout << unitMapLocation.get_x() << " " << unitMapLocation.get_y() << endl;
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

        Pathfinder pathfinder;
        auto nextLocation = pathfinder.getNextLocation(unitMapLocation, karboniteMap, passableMap);

        if (nextLocation != unitMapLocation) {
            cout << "Wanting to move from " << unitMapLocation.get_x() << " " << unitMapLocation.get_y() << endl;
            cout << "Wanting to move to " << nextLocation.get_x() << " " << nextLocation.get_y() << endl;
            cout << "Ready: " << gc.is_move_ready(id) << endl;
            auto d = unitMapLocation.direction_to(nextLocation);
            if (!planetMap.is_passable_terrain_at(nextLocation)) {
                cout << "Not passable" << endl;
            }
            if (!gc.is_occupiable(nextLocation)) {
                cout << "Not occupiable" << endl;
            }
            cout << "d = " << d << endl;
            cout << direction_dx(d) << " " << direction_dy(d) << endl;
            if (gc.is_move_ready(id) && gc.can_move(id,d)){
            //if (gc.is_move_ready(id) && gc.is_occupiable(nextLocation)) {
                cout << "Moving" << endl;
                gc.move_robot(id,d);
            }
            else {
                cout << "Not moving" << endl;
            }
        }
    }
};

struct BotKnight : BotUnit {
    BotKnight(const Unit& unit) : BotUnit(unit) {}
    void tick() {
        // Calls on the controller take unit IDs for ownership reasons.
        const auto locus = unit.get_location().get_map_location();
        const auto nearby = gc.sense_nearby_units(locus, 2);
        for ( auto place : nearby ){
            //Building 'em blueprints
            if(gc.can_build(id, place.get_id()) && unit.get_unit_type() == Worker){
                gc.build(id, place.get_id());
                continue;
            }
            //Attacking 'em enemies
            if( place.get_team() != unit.get_team() and
                    gc.is_attack_ready(id) and
                    gc.can_attack(id, place.get_id()) ){
                gc.attack(id, place.get_id());
                continue;
            }
        }
    }
};

struct BotRanger : BotUnit {
    BotRanger(const Unit& unit) : BotUnit(unit) {}
};

struct BotMage : BotUnit {
    BotMage(const Unit& unit) : BotUnit(unit) {}
};

struct BotHealer : BotUnit {
    BotHealer(const Unit& unit) : BotUnit(unit) {}
};

struct BotFactory : BotUnit {
    BotFactory(const Unit& unit) : BotUnit(unit) {}

    void tick() {
        auto garrison = unit.get_structure_garrison();

        if (garrison.size() > 0){
            Direction dir = (Direction) (rand() % 8);
            if (gc.can_unload(id, dir)){
                gc.unload(id, dir);
                return;
            }
        } else if (gc.can_produce_robot(id, Knight)){
            cout << "We are producing a Knight!!" << endl;
            gc.produce_robot(id, Knight);
            return;
        }
    }
};

struct BotRocket : BotUnit {
    BotRocket(const Unit& unit) : BotUnit(unit) {}
};


void workerLogic(const GameController& gc, const Unit& unit) {

}

int main() {
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

        auto units = gc.get_my_units();
        auto planet = gc.get_planet();
        auto allies = gc.get_team();
        auto opponents = (Team)(1 - (int)gc.get_team());

        vector<Unit> allUnits;
        vector<Unit> enemyUnits;
        for (int team = 0; team < 2; team++) {
            if (team != allies) {
                auto u = gc.sense_nearby_units_by_team(MapLocation(planet, 0, 0), 1000000, (Team)team);
                enemyUnits.insert(enemyUnits.end(), u.begin(), u.end());
            }
        }
        allUnits.insert(allUnits.end(), units.begin(), units.end());
        allUnits.insert(allUnits.end(), enemyUnits.begin(), enemyUnits.end());

        for (const auto unit : units) {
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

        // this line helps the output logs make more sense by forcing output to be sent
        // to the manager.
        // it's not strictly necessary, but it helps.
        // pause and wait for the next turn.
        fflush(stdout);
        gc.next_turn();
    }
    // I'm convinced C++ is the better option :)
}
