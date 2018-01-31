#include "maps.h"

using namespace bc;

PathfindingMap karboniteMap;
PathfindingMap fuzzyKarboniteMap;
PathfindingMap enemyInfluenceMap;
PathfindingMap workerProximityMap;
PathfindingMap workerAdditiveMap;
PathfindingMap workersNextToMap;
PathfindingMap structureProximityMap;
PathfindingMap damagedStructureMap;
PathfindingMap passableMap;
PathfindingMap enemyNearbyMap;
PathfindingMap enemyFactoryNearbyMap;
PathfindingMap enemyPositionMap;
PathfindingMap enemyExactPositionMap;
PathfindingMap nearbyFriendMap;
PathfindingMap rocketHazardMap;
PathfindingMap rocketAttractionMap;
PathfindingMap rocketProximityMap;
PathfindingMap healerOverchargeMap;
PathfindingMap stuckUnitMap;
PathfindingMap mageNearbyMap;
PathfindingMap mageNearbyFuzzyMap;
PathfindingMap ourStartingPositionMap;
PathfindingMap discoveryMap;
PathfindingMap distanceToInitialLocation[2];
PathfindingMap withinRangeMap;
PathfindingMap rangerCanShootEnemyCountMap;
PathfindingMap enemyKnightNearbyMap;

map<MapReuseObject, PathfindingMap> reusableMaps;
