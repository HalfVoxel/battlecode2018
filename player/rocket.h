#pragma once

#include "common.h"
#include "bot_unit.h"

extern int launchedWorkerCount;

struct BotRocket : BotUnit {
    BotRocket(const Unit& unit) : BotUnit(unit) {}

    void tick();
};

tuple<bool,MapLocation,int> find_best_landing_spot();