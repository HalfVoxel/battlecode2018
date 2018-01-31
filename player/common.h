#pragma once

#include <cstdio>
#include <vector>
#include <algorithm>
#include <iostream>
#include <stack>
#include <map>
#include <functional>
#include <signal.h>

#define NO_IMPLICIT_COPIES

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
extern double targetMapComputationTime;
extern double costMapComputationTime;
extern double attackComputationTime;
extern double unitInvalidationTime;
extern double matchWorkersTime;
extern double matchWorkersDijkstraTime;
extern double matchWorkersDijkstraTime2;
extern double hungarianTime;
extern std::map<unsigned int, BotUnit*> unitMap;
extern std::vector<std::vector<bool> > canSenseLocation;
extern std::vector<std::vector<bc::Unit*> > unitAtLocation;

extern bc::Team ourTeam;
extern bc::Team enemyTeam;

extern bc::Planet planet;
extern const bc::PlanetMap* planetMap;
extern int w;
extern int h;
extern int turnsSinceLastFight;
extern bool lowTimeRemaining;
extern bool veryLowTimeRemaining;
extern bool hasOvercharge;
extern int launchedWorkerCount;
extern int timesStuck;

// Amount of karbonite that is contested. I.e close to both our team and the opponent team.
// We want to replicate more if there is a lot of contested karbonite to make sure we get it before the enemy.
extern int contestedKarbonite;

extern std::map<unsigned, std::vector<unsigned> > unitShouldGoToRocket;

extern bool existsPathToEnemy;
// True if mars has any landing spots (i.e does mars have any traversable ground)
extern bool anyReasonableLandingSpotOnInitialMars;

// Sort of how many distinct ways there are to get to the enemy
// If the enemy is behind a wall, then this is 0
// If there is a choke point of size 1 then this is 1.
// If the player starts in 2 places, each of them can reach the enemy in 1 way, then this is 2.
// All paths to the enemy must thouch distinct nodes.
// Maximum value is 6 to avoid wasting too much time calculating it.
extern int mapConnectedness;

extern int lastFactoryBlueprintTurn;
extern int lastRocketBlueprintTurn;
extern int initialDistanceToEnemyLocation;
extern bool workersMove;
extern volatile sig_atomic_t sigTheRound;

struct State {
    std::map<bc::UnitType, int> typeCount;
    double remainingKarboniteOnEarth;
    int totalRobotDamage;
    int totalUnitCount;
    int earthTotalUnitCount;
};

extern State state;

struct MacroObject {
    double score;
    unsigned cost;
    int priority;
    int rnd;
    std::function<void()> lambda;

    MacroObject(double _score, unsigned _cost, int _priority, std::function<void()> _lambda) {
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

extern std::vector<MacroObject> macroObjects;
extern double bestMacroObjectScore;

void invalidate_units();
void invalidate_unit(unsigned int id);

void setup_signal_handlers();

inline double millis() {
    return 1000.0 * (double)clock() / (double)CLOCKS_PER_SEC;
}

inline bool isOnMap(bc::MapLocation location) {
   return location.get_x() >= 0 && location.get_y() >= 0 && location.get_x() < w && location.get_y() < h;
}
