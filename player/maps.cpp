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
PathfindingMap nearbyFriendMap;

map<MapReuseObject, PathfindingMap> reusableMaps;
