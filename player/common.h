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
extern std::map<unsigned int, BotUnit*> unitMap;

extern bc::Team ourTeam;
extern bc::Team enemyTeam;

extern bc::Planet planet;
extern const bc::PlanetMap* planetMap;
extern int w;
extern int h;

extern std::map<unsigned, std::vector<unsigned> > unitShouldGoToRocket;

void invalidate_units();