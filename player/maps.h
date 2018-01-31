#pragma once

#include "common.h"
#include "pathfinding.hpp"

extern PathfindingMap karboniteMap;
extern PathfindingMap fuzzyKarboniteMap;
extern PathfindingMap enemyInfluenceMap;
extern PathfindingMap workerProximityMap;
extern PathfindingMap workerAdditiveMap;
extern PathfindingMap workersNextToMap;
extern PathfindingMap structureProximityMap;
extern PathfindingMap damagedStructureMap;
extern PathfindingMap passableMap;
extern PathfindingMap enemyNearbyMap;
extern PathfindingMap enemyFactoryNearbyMap;
extern PathfindingMap enemyPositionMap;
extern PathfindingMap enemyExactPositionMap;
extern PathfindingMap nearbyFriendMap;
extern PathfindingMap rocketHazardMap;
extern PathfindingMap rocketAttractionMap;
extern PathfindingMap rocketProximityMap;
extern PathfindingMap healerOverchargeMap;
extern PathfindingMap stuckUnitMap;
extern PathfindingMap mageNearbyMap;
extern PathfindingMap mageNearbyFuzzyMap;
extern PathfindingMap ourStartingPositionMap;
extern PathfindingMap discoveryMap;
extern PathfindingMap distanceToInitialLocation[2];
extern PathfindingMap withinRangeMap;
extern PathfindingMap rangerCanShootEnemyCountMap;
extern PathfindingMap enemyKnightNearbyMap;

enum class MapType { Target, Cost };

struct MapReuseObject {
    MapType mapType;
    UnitType unitType;
    bool isHurt;

    MapReuseObject (MapType _mapType, UnitType _unitType, bool _isHurt) : mapType(_mapType), unitType(_unitType), isHurt(_isHurt) {
    }

    bool operator< (const MapReuseObject& other) const {
        if (mapType != other.mapType) {
            return mapType < other.mapType;
        }
        if (unitType != other.unitType) {
            return unitType < other.unitType;
        }
        if (isHurt != other.isHurt) {
            return isHurt < other.isHurt;
        }
        return 0;
    }
};

extern map<MapReuseObject, PathfindingMap> reusableMaps;
