#pragma once

#include "common.h"
#include "pathfinding.hpp"

extern PathfindingMap karboniteMap;
extern PathfindingMap fuzzyKarboniteMap;
extern PathfindingMap enemyInfluenceMap;
extern PathfindingMap workerProximityMap;
extern PathfindingMap workersNextToMap;
extern PathfindingMap structureProximityMap;
extern PathfindingMap damagedStructureMap;
extern PathfindingMap passableMap;
extern PathfindingMap enemyNearbyMap;
extern PathfindingMap enemyPositionMap;
extern PathfindingMap nearbyFriendMap;
extern PathfindingMap rocketHazardMap;
extern PathfindingMap rocketAttractionMap;

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
