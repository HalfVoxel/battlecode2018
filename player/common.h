#pragma once

#include <cstdio>
#include <vector>
#include <algorithm>
#include <iostream>
#include <stack>
#include <map>

#include "bc.hpp"

#define MAX_MAP_SIZE 50
typedef std::pair<int,int> pii;

struct BotUnit;

extern bc::GameController gc;

extern std::vector<bc::Unit> ourUnits;
extern std::vector<bc::Unit> enemyUnits;
extern std::vector<bc::Unit> allUnits;
extern double pathfindingTime;
extern double mapComputationTime;
extern double unitInvalidationTime;
extern std::map<unsigned int, BotUnit*> unitMap;

extern bc::Team ourTeam;
extern bc::Team enemyTeam;

extern bc::Planet planet;
extern const bc::PlanetMap* planetMap;
extern int w;
extern int h;

extern std::map<unsigned, std::vector<unsigned> > unitShouldGoToRocket;

void invalidate_units();
void invalidate_unit(unsigned int id);

void setup_signal_handlers();

inline double millis() {
    return 1000.0 * (double)clock() / (double)CLOCKS_PER_SEC;
}

inline bool isOnMap(bc::MapLocation location) {
   return location.get_x() >= 0 && location.get_y() >= 0 && location.get_x() < w && location.get_y() < h;
}