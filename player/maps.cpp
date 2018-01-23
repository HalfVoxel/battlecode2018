#include "maps.h"

using namespace bc;

PathfindingMap karboniteMap;
PathfindingMap fuzzyKarboniteMap;
PathfindingMap enemyInfluenceMap;
PathfindingMap workerProximityMap;
PathfindingMap workersNextToMap;
PathfindingMap structureProximityMap;
PathfindingMap damagedStructureMap;
PathfindingMap passableMap;
PathfindingMap enemyNearbyMap;
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

map<MapReuseObject, PathfindingMap> reusableMaps;
