#pragma once

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>
#include <random>
#include <functional>
#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <queue>

#include "common.h"

using namespace bc;
using namespace std;

struct Position {
    int x, y;

    Position () {
        x = -1;
        y = -1;
    }

    Position (int _x, int _y) : x(_x), y(_y) {
    }

    Position (const Position &other) : x(other.x), y(other.y) {
    }
};

struct PathfindingEntry {
    double cost;
    Position pos;

    PathfindingEntry (double _cost, Position _pos) : cost(_cost), pos(_pos) {
    }

    bool operator< (const PathfindingEntry& other) const {
        return cost > other.cost;
    }
};

struct PathfindingMap {
    vector<vector<double> > weights;
    int w, h;

    PathfindingMap() {
    }

    PathfindingMap(int width, int height) {
        w = width;
        h = height;
        weights = vector<vector<double> >(width, vector<double>(height, 0.0));
    }

    PathfindingMap& operator+= (const PathfindingMap& other) {
        for (int i = 0; i < w; i++) {
            for (int j = 0; j < h; j++) {
                weights[i][j] += other.weights[i][j];
            }
        }
        return (*this);
    }

    PathfindingMap& operator+= (double other) {
        for (int i = 0; i < w; i++) {
            for (int j = 0; j < h; j++) {
                weights[i][j] += other;
            }
        }
        return (*this);
    }

    PathfindingMap operator+ (const PathfindingMap& other) const {
        auto ret = (*this);
        return (ret += other);
    }

    PathfindingMap& operator-= (const PathfindingMap& other) {
        for (int i = 0; i < w; i++) {
            for (int j = 0; j < h; j++) {
                weights[i][j] -= other.weights[i][j];
            }
        }
        return (*this);
    }

    PathfindingMap operator- (const PathfindingMap& other) const {
        auto ret = (*this);
        return (ret -= other);
    }

    PathfindingMap& operator*= (const PathfindingMap& other) {
        for (int i = 0; i < w; i++) {
            for (int j = 0; j < h; j++) {
                weights[i][j] *= other.weights[i][j];
            }
        }
        return (*this);
    }

    PathfindingMap operator* (const PathfindingMap& other) const {
        auto ret = (*this);
        return (ret *= other);
    }

    PathfindingMap& operator/= (const PathfindingMap& other) {
        for (int i = 0; i < w; i++) {
            for (int j = 0; j < h; j++) {
                weights[i][j] /= other.weights[i][j];
            }
        }
        return (*this);
    }

    PathfindingMap operator/ (const PathfindingMap& other) const {
        auto ret = (*this);
        return (ret /= other);
    }

    PathfindingMap operator+ (double factor) const {
        PathfindingMap ret = PathfindingMap(w, h);
        for (int i = 0; i < w; i++) {
            for (int j = 0; j < h; j++) {
                ret.weights[i][j] = weights[i][j] + factor;
            }
        }
        return ret;
    }

    PathfindingMap operator- (double factor) const {
        PathfindingMap ret = PathfindingMap(w, h);
        for (int i = 0; i < w; i++) {
            for (int j = 0; j < h; j++) {
                ret.weights[i][j] = weights[i][j] - factor;
            }
        }
        return ret;
    }

    PathfindingMap operator* (double factor) const {
        PathfindingMap ret = PathfindingMap(w, h);
        for (int i = 0; i < w; i++) {
            for (int j = 0; j < h; j++) {
                ret.weights[i][j] = weights[i][j] * factor;
            }
        }
        return ret;
    }

    void operator*= (double factor) {
        for (int i = 0; i < w; i++) {
            for (int j = 0; j < h; j++) {
                weights[i][j] *= factor;
            }
        }
    }

    double sum() const {
        double ret = 0.0;
        for (int i = 0; i < w; i++) {
            for (int j = 0; j < h; j++) {
                ret += weights[i][j];
            }
        }
        return ret;
    }

    double getMax() const {
        double ret = 0.0;
        for (int i = 0; i < w; i++) {
            for (int j = 0; j < h; j++) {
                ret = max(ret, weights[i][j]);
            }
        }
        return ret;
    }

    void addInfluence(double influence, const MapLocation& pos) {
        weights[pos.get_x()][pos.get_y()] += influence;
    }

    void addInfluence(const vector<vector<double> >& influence, int x0, int y0) {
        int r = influence.size() / 2;
        for (int dx = -r; dx <= r; dx++) {
            for (int dy = -r; dy <= r; dy++) {
                int x = x0 + dx;
                int y = y0 + dy; 
                if (x >= 0 && y >= 0 && x < w && y < h) {
                    weights[x][y] += influence[dx+r][dy+r];
                }
            }
        }
    }
    
    void addInfluenceMultiple(const vector<vector<double> >& influence, int x0, int y0, double factor) {
        int r = influence.size() / 2;
        for (int dx = -r; dx <= r; dx++) {
            for (int dy = -r; dy <= r; dy++) {
                int x = x0 + dx;
                int y = y0 + dy; 
                if (x >= 0 && y >= 0 && x < w && y < h) {
                    weights[x][y] += influence[dx+r][dy+r] * factor;
                }
            }
        }
    }

    void maxInfluence(const vector<vector<double> >& influence, int x0, int y0) {
        int r = influence.size() / 2;
        for (int dx = -r; dx <= r; dx++) {
            for (int dy = -r; dy <= r; dy++) {
                int x = x0 + dx;
                int y = y0 + dy; 
                if (x >= 0 && y >= 0 && x < w && y < h) {
                    weights[x][y] = max(weights[x][y], influence[dx+r][dy+r]);
                }
            }
        }
    }

    void maxInfluenceMultiple(const vector<vector<double> >& influence, int x0, int y0, double factor) {
        int r = influence.size() / 2;
        for (int dx = -r; dx <= r; dx++) {
            for (int dy = -r; dy <= r; dy++) {
                int x = x0 + dx;
                int y = y0 + dy; 
                if (x >= 0 && y >= 0 && x < w && y < h) {
                    weights[x][y] = max(weights[x][y], influence[dx+r][dy+r] * factor);
                }
            }
        }
    }

    void print() const {
        for (int i = 0; i < h; i++) {
            for (int j = 0; j < w; j++) {
                cout << setfill(' ') << setw(6) << setprecision(1) << fixed << weights[j][i] << " ";
            }
            cout << endl;
        }
    }
};

struct Pathfinder {

    double bestScore;

    bool existsPathToLocation(const MapLocation& from, const MapLocation& to, const PathfindingMap& costs) {
        static vector<vector<double> > cost(MAX_MAP_SIZE, vector<double>(MAX_MAP_SIZE));
        static vector<vector<int> > version(MAX_MAP_SIZE, vector<int>(MAX_MAP_SIZE));
        static vector<vector<Position> > parent(MAX_MAP_SIZE, vector<Position>(MAX_MAP_SIZE));
        static priority_queue<PathfindingEntry> pq;
        static int pathfindingVersion = 0;

        int w = costs.w;
        int h = costs.h;
        // Make sure map is sane
        assert(w <= MAX_MAP_SIZE);
        assert(h <= MAX_MAP_SIZE);

        pathfindingVersion++;
    
        int dx[8]={1,1,1,0,0,-1,-1,-1};
        int dy[8]={1,0,-1,1,-1,1,0,-1};
        int x0 = from.get_x(), y0 = from.get_y();
        Position bestPosition(x0, y0);
        pq.push(PathfindingEntry(0.0, bestPosition));
        cost[x0][y0] = 0;
        version[x0][y0] = pathfindingVersion;

        int tox = to.get_x();
        int toy = to.get_y();
        bool hasPath = false;

        while (!pq.empty()) {
            auto currentEntry = pq.top();
            auto currentPos = currentEntry.pos;
            if (currentPos.x == tox && currentPos.y == toy) {
                hasPath = true;
                break;
            }
            pq.pop();
            if (currentEntry.cost > cost[currentPos.x][currentPos.y]) {
                continue;
            }
            for (int i = 0; i < 8; i++) {
                int x = currentPos.x + dx[i];
                int y = currentPos.y + dy[i];
                if (x < 0 || x >= w || y < 0 || y >= h) {
                    continue;
                }
                double newCost = currentEntry.cost + costs.weights[x][y];
                if (newCost < cost[x][y] || (version[x][y] != pathfindingVersion && newCost < numeric_limits<double>::infinity())) {
                    cost[x][y] = newCost;
                    parent[x][y] = currentPos;
                    version[x][y] = pathfindingVersion;
                    pq.push(PathfindingEntry(newCost, Position(x, y)));
                }
            }
        }

        // Clear queue (required as it is reused for the next pathfinding call)
        while(!pq.empty()) pq.pop();

        return hasPath;
    }

    vector<vector<double>> getDistanceToAllTiles (int x0, int y0, const PathfindingMap& costs) {
        static priority_queue<PathfindingEntry> pq;

        int w = costs.w;
        int h = costs.h;

        // Make sure map is sane
        assert(w <= MAX_MAP_SIZE);
        assert(h <= MAX_MAP_SIZE);

        vector<vector<double> > cost(w, vector<double>(h, numeric_limits<double>::infinity()));
    
        int dx[8]={1,1,1,0,0,-1,-1,-1};
        int dy[8]={1,0,-1,1,-1,1,0,-1};
        pq.push(PathfindingEntry(0.0, Position(x0, y0)));
        cost[x0][y0] = 0;

        while (!pq.empty()) {
            auto currentEntry = pq.top();
            auto currentPos = currentEntry.pos;
            pq.pop();
            if (currentEntry.cost > cost[currentPos.x][currentPos.y]) {
                continue;
            }
            for (int i = 0; i < 8; i++) {
                int x = currentPos.x + dx[i];
                int y = currentPos.y + dy[i];
                if (x < 0 || x >= w || y < 0 || y >= h) {
                    continue;
                }
                double newCost = currentEntry.cost + costs.weights[x][y];
                if (newCost < cost[x][y]) {
                    cost[x][y] = newCost;
                    pq.push(PathfindingEntry(newCost, Position(x, y)));
                }
            }
        }

        // Clear queue (required as it is reused for the next pathfinding call)
        while(!pq.empty()) pq.pop();

        return cost;
    }

    vector<Position> getPath (const MapLocation& from, const PathfindingMap& values, const PathfindingMap& costs) {
        static vector<vector<double> > cost(MAX_MAP_SIZE, vector<double>(MAX_MAP_SIZE));
        static vector<vector<int> > version(MAX_MAP_SIZE, vector<int>(MAX_MAP_SIZE));
        static vector<vector<Position> > parent(MAX_MAP_SIZE, vector<Position>(MAX_MAP_SIZE));
        static priority_queue<PathfindingEntry> pq;
        static int pathfindingVersion = 0;

        int w = values.w;
        int h = values.h;

        // Make sure map is sane
        assert(w <= MAX_MAP_SIZE);
        assert(h <= MAX_MAP_SIZE);

        pathfindingVersion++;
    
        int dx[8]={1,1,1,0,0,-1,-1,-1};
        int dy[8]={1,0,-1,1,-1,1,0,-1};
        auto averageScore = [&values](Position pos) {
            return values.weights[pos.x][pos.y] / (cost[pos.x][pos.y] + 1.0);
        };
        int x0 = from.get_x(), y0 = from.get_y();
        Position bestPosition(x0, y0);
        cost[x0][y0] = costs.weights[x0][y0];
        bestScore = averageScore(bestPosition);
        pq.push(PathfindingEntry(0.0, bestPosition));
        cost[x0][y0] = 0;
        version[x0][y0] = pathfindingVersion;
        parent[x0][y0] = bestPosition;
        double valueUpperBound = values.getMax();
    
        while (!pq.empty()) {
            auto currentEntry = pq.top();
            auto currentPos = currentEntry.pos;
            pq.pop();
            if (currentEntry.cost > cost[currentPos.x][currentPos.y]) {
                continue;
            }
            if (valueUpperBound / (currentEntry.cost + 1.0) <= bestScore) {
                break;
            }
            auto currentScore = averageScore(currentPos);
            if (currentScore > bestScore && (currentPos.x != x0 || currentPos.y != y0)) {
                bestPosition = currentPos;
                bestScore = currentScore;
            }
            for (int i = 0; i < 8; i++) {
                int x = currentPos.x + dx[i];
                int y = currentPos.y + dy[i];
                if (x < 0 || x >= w || y < 0 || y >= h) {
                    continue;
                }
                double newCost = currentEntry.cost + costs.weights[x][y];
                if (newCost < cost[x][y] || version[x][y] != pathfindingVersion) {
                    cost[x][y] = newCost;
                    parent[x][y] = currentPos;
                    version[x][y] = pathfindingVersion;
                    pq.push(PathfindingEntry(newCost, Position(x, y)));
                }
            }
        }

        // Clear queue (required as it is reused for the next pathfinding call)
        while(!pq.empty()) pq.pop();

        Position currentPos = bestPosition;
        vector<Position> path = {currentPos};
        while (currentPos.x != x0 || currentPos.y != y0) {
            auto p = parent[currentPos.x][currentPos.y];
            path.push_back(p);
            currentPos = p;
        }
        reverse(path.begin(), path.end());
        return path;
    }

    MapLocation getNextLocation (const MapLocation& from, const PathfindingMap& values, const PathfindingMap& costs) {
        auto path = getPath(from, values, costs);
        auto pos = path[path.size() > 1 ? 1 : 0];
        return MapLocation(from.get_planet(), pos.x, pos.y);
    }
};
