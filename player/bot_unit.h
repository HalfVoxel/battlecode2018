#pragma once

#include "common.h"
#include "pathfinding.hpp"

double averageAttackerSuccessRate;

struct BotUnit {
    bc::Unit unit;
    const unsigned id;
    double pathfindingScore;
    bool hasDoneTick;
    bool isRocketFodder;
    BotUnit(const bc::Unit& unit) : unit(unit.clone()), id(unit.get_id()), hasDoneTick(false), isRocketFodder(false) {}
    virtual ~BotUnit() {}
    virtual void tick();
    virtual PathfindingMap getTargetMap();
    virtual PathfindingMap getCostMap();

    bc::MapLocation getNextLocation(bc::MapLocation from, bool allowStructures);

    bc::MapLocation getNextLocation();

    void moveToLocation(bc::MapLocation nextLocation);

    bool unloadFrontUnit();

    PathfindingMap defaultMilitaryTargetMap();

    PathfindingMap defaultMilitaryCostMap ();

    void default_military_behaviour();
};

void addRocketTarget(const Unit& unit, PathfindingMap& targetMap);

// Relative values of different unit types when at "low" (not full) health
extern float unit_defensive_strategic_value[];

// Relative values of different unit types when at full or almost full health
extern float unit_strategic_value[];
