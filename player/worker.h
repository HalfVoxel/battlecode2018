#pragma once

#include "common.h"
#include "bot_unit.h"

struct BotWorker : BotUnit {
	int rocketDelay = 0;
    bool didBuild = false;
    PathfindingMap calculatedTargetMap;

    BotWorker(const bc::Unit& unit) : BotUnit(unit) {}
    PathfindingMap getCostMap();
    PathfindingMap getTargetMap();
    PathfindingMap getOriginalTargetMap();
    void tick();
};

void matchWorkers();
void addWorkerActions();