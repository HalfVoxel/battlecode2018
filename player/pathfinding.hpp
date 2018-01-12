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

#include "bc.hpp"

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

    PathfindingMap operator+ (const PathfindingMap& other) const {
        auto ret = (*this);
        return (ret += other);
    }

    double sum() {
        double ret = 0.0;
        for (int i = 0; i < h; i++) {
            for (int j = 0; j < w; j++) {
                ret += weights[i][j];
            }
        }
        return ret;
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
    MapLocation getNextLocation (const MapLocation& from, const PathfindingMap& values, PathfindingMap& costs) const {
        int w = values.w;
        int h = values.h;
        vector<vector<double> > cost(w, vector<double>(h, numeric_limits<double>::infinity()));
        vector<vector<Position> > parent(w, vector<Position>(h));
    
        cost[from.get_x()][from.get_y()] = 0;
        priority_queue<PathfindingEntry> pq;
        int dx[8]={1,1,1,0,0,-1,-1,-1};
        int dy[8]={1,0,-1,1,-1,1,0,-1};
        auto averageScore = [&values, &cost](Position pos) {
            return values.weights[pos.x][pos.y] / (cost[pos.x][pos.y] + 1.0);
        };
        Position bestPosition(from.get_x(), from.get_y());
        auto bestScore = averageScore(bestPosition);
        pq.push(PathfindingEntry(0.0, bestPosition));
    
        while (!pq.empty()) {
            auto currentEntry = pq.top();
            auto currentPos = currentEntry.pos;
            pq.pop();
            if (currentEntry.cost > cost[currentPos.x][currentPos.y]) {
                continue;
            }
            auto currentScore = averageScore(currentPos);
            if (currentScore > bestScore) {
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
                if (newCost < cost[x][y]) {
                    cost[x][y] = newCost;
                    parent[x][y] = currentPos;
                    pq.push(PathfindingEntry(newCost, Position(x, y)));
                }
            }
        }
        Position currentPos = bestPosition;
        if (currentPos.x == from.get_x() && currentPos.y == from.get_y()) {
            return MapLocation(from.get_planet(), currentPos.x, currentPos.y);
        }
        vector<Position> path = {currentPos};
        while (true) {
            auto p = parent[currentPos.x][currentPos.y];
            path.push_back(p);
            if (p.x == from.get_x() && p.y == from.get_y()) {
                break;
            }
            currentPos = p;
        }
        reverse(path.begin(), path.end());
        return MapLocation(from.get_planet(), currentPos.x, currentPos.y);
    }
};
