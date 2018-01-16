#include "common.h"
#include "bot_unit.h"

using namespace bc;

GameController gc;

std::vector<bc::Unit> ourUnits;
std::vector<bc::Unit> enemyUnits;
std::vector<bc::Unit> allUnits;
double pathfindingTime;
double mapComputationTime;
double unitInvalidationTime;
map<unsigned int, BotUnit*> unitMap;

Team ourTeam;
Team enemyTeam;

Planet planet;
const PlanetMap* planetMap;
int w;
int h;

map<unsigned, vector<unsigned> > unitShouldGoToRocket;

/** Call if our units may have been changed in some way, e.g damaged by Mage splash damage and killed */
void invalidate_units() {
	auto t0 = millis();
    for (auto& unit : ourUnits) {
        if (gc.has_unit(unit.get_id())) {
            unitMap[unit.get_id()]->unit = gc.get_unit(unit.get_id());
        } else {
            unitMap[unit.get_id()] = nullptr;
            // Unit has suddenly disappeared, oh noes!
            // Maybe it went into space or something
        }
    }
    unitInvalidationTime += millis() - t0;
}
