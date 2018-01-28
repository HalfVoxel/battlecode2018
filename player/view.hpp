#pragma once
#include <vector>
#include <functional>
#include <string>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <queue>
#include <random>
#include <assert.h>

using namespace std;

struct Region {
    int xmin, ymin, xmax, ymax;

    int width() {
        return xmax - xmin + 1;
    }

    int height() {
        return ymax - ymin + 1;
    }
};

struct Color {
    int r, g, b;
};

Color interpolate(Color a, Color b, double t) {
    t = max(min(t, 1.0), 0.0);
    return {
        (int)(a.r*(1-t) + b.r*t),
        (int)(a.g*(1-t) + b.g*t),
        (int)(a.b*(1-t) + b.b*t)
    };
}

// function<string(int, int)> colors(v2D<Site>& cols) {
//     //vector<string> playerColors = { "\033[40m", "\033[44m", "\033[45m", "\033[41m", "\033[42m" };
//     Color black = { 0, 0, 0 };
//     vector<Color> playerColors = { { 54, 54, 54}, { 55, 128, 230 }, { 201, 27, 0 }, { 202, 48, 199 }, { 0, 194, 0 }, { 0, 197, 199 }, { 199, 196, 0 } };

//     auto lambda = [&cols, playerColors, black](int x, int y) {
//         auto data = cols[{x,y}];
//         auto col = playerColors[min((int)data.owner, (int)playerColors.size()-1)];
//         col = interpolate(black, col, sqrt(data.strength/255.0)*0.5 + 0.5);
//         std::stringstream ss;
//         ss << "\x1b[" << 48 << ";2;" << col.r << ";" << col.g << ";" << col.b << "m";
//         return ss.str();
//     };
//     return colors(lambda);
// }

// function<string(int, int)> colors(double mn, double mx, v2D<double>& values) {
//     return colors(mn, mx, [&](int x, int y){
//         return values[{x,y}];
//     });
// }

int bit(int i, int index) {
    return (i >> index) & 1;
}

Color intToColor (int i) {
    int r = bit(i, 1) + bit(i, 3) * 2 + 1;
    int g = bit(i, 2) + bit(i, 4) * 2 + 1;
    int b = bit(i, 0) + bit(i, 5) * 2 + 1;
    return Color { (int)(255*r*0.25),(int)(255*g*0.25),(int)(255*b*0.25) };
}

// Dummy methods
function<string(int, int)> colors(function<string(int, int)> cols) {
    return cols;
}

function<string(int, int)> labels(function<string(int, int)> vals) {
    return vals;
}

// function<string(int, int)> colorsByID(v2D<int>& values) {
//     return colorsByID([&](int x, int y) { return values[{x,y}]; });
// }

function<string(int, int)> colors(double mn, double mx, function<double(int, int)> values) {
    Color blue = { 55, 128, 230 };
    Color black = { 0, 0, 0 };
    Color red = { 229, 91, 47 };
    Color Rwhite = { 255, 180, 180 };
    Color Bwhite = { 180, 180, 255 };

    auto lambda = [=](int x, int y) {
        auto data = values(x,y);
        auto relative = (data - mn)/(mx - mn);
        Color col;
        Color middle = relative < -0.001 ? red : blue;
        Color top = relative < -0.001 ? Rwhite : Bwhite;
        if (abs(relative) < 0.5f) {
            col = interpolate(black, middle, abs(relative)*2);
        } else {
            col = interpolate(middle, top, (abs(relative)-0.5)*2);
        }

        // Make it brighter
        relative = (relative > 0 ? 1 : -1) * (1 - 0.9 * (1 - abs(relative)));
        Color textCol;
        if (abs(relative) < 0.5f) {
            textCol = interpolate(black, middle, abs(relative)*2);
        } else {
            textCol = interpolate(middle, top, (abs(relative)-0.5)*2);
        }

        std::stringstream ss;
        ss << "\x1b[" << 48 << ";2;" << col.r << ";" << col.g << ";" << col.b << "m";
        ss << "\x1b[" << 38 << ";2;" << textCol.r << ";" << textCol.g << ";" << textCol.b << "m";
        return ss.str();
    };
    return colors(lambda);
}

function<string(int, int)> colorsByID(function<int(int, int)> values) {
    return colors([=](int x, int y) {
        auto col = intToColor(values(x,y));
        Color textCol;

        if (max(col.r, max(col.g, col.b)) > 128) {
            textCol = Color { max((int)(col.r*0.7) - 50, 0), max((int)(col.g*0.7) - 50, 0), max((int)(col.b*0.7) - 50, 0) };
        } else {
            textCol = Color { min((int)(col.r*1.3) + 50, 255), min((int)(col.g*1.3) + 50, 255), min((int)(col.b*1.3) + 50, 255) };
        }

        std::stringstream ss;
        ss << "\x1b[" << 48 << ";2;" << col.r << ";" << col.g << ";" << col.b << "m";
        ss << "\x1b[" << 38 << ";2;" << textCol.r << ";" << textCol.g << ";" << textCol.b << "m";
        return ss.str();
    });
}

// function<string(int, int)> labels(vector<vector<Direction>>& directions) {
//     auto lambda = [&](int x, int y) {
//         auto move = directions[{x,y}];
//         switch(move) {
//             case STILL:
//             return string("  o ");
//             case NORTH:
//             return string("  ^ ");
//             case EAST:
//             return string("  > ");
//             case SOUTH:
//             return string("  v ");
//             case WEST:
//             return string("  < ");
//             default:
//             exit(2);
//         }
//     };

//     return labels(lambda);
// }

function<string(int, int)> labels(function<int(int, int)> values) {
    auto lambda = [=](int x, int y) {
        auto value = values(x, y);
        auto str = min(value, 99);
        return string(" ") + string(str < 10 ? " " : "") + to_string(str) + string(" ");
    };
    return labels(lambda);
}

// function<string(int, int)> labels(v2D<Site>& values) {
//     auto lambda = [&](int x, int y) { return (int)values[{x,y}].strength; };
//     return labels(lambda);
// }

function<string(int, int)> labels(vector<vector<int>>& values) {
    auto lambda = [&](int x, int y) { return (int)values[x][y]; };
    return labels(lambda);
}

void printAt(Region region, pii screenPos, const function<string(int, int)> colors, const function<string(int, int)> values) {
#ifndef NDEBUG
    int col = screenPos.first;
    int row = screenPos.second;

    bool hasOffset = row >= 1 && col >= 1;

    //HIGHLIGHT = ['', '\033[5m\033[94m', '\033[5m\033[91m']
    auto reset = "\033[0m";
    if (hasOffset) {
        cout << "\033[" << row << ";" << col << "H";
    } else {
        cout << endl;
    }

    cout << "    ";
    cout << "\033[90m";
    for (int x = region.xmin; x <= region.xmax; x++) {
        cout << " " << (x < 10 ? " " : "") << x << " ";
    }

    cout << '\n';
    // Note: The tinyview viewer displays maps with (0,0) at the bottom left corner
    // Thus we need to reverse the order of the rows here to show the same thing
    for (int y = region.ymax; y >= region.ymin; y--) {
        if (hasOffset) cout << "\033[" << (row+1+y-region.ymin) << ";" << col << "H";
        cout << "\033[90m";
        cout << (y < 10 ? " " : "") << y << ": ";
        cout << reset;

        for (int x = region.xmin; x <= region.xmax; x++) {
            cout << colors(x,y) << values(x,y) << reset;
        }
        if (!hasOffset) cout << '\n';
    }
    cout << endl;
#endif
}

void print(Region region, const function<string(int, int)> colors, const function<string(int, int)> values) {
    printAt(region, {0, 0}, colors, values);
}

void printAt(Region region, pii screenPos, const function<string(int, int)> colors) {
    printAt(region, screenPos, colors, [](int x, int y) { return "    "; });
}

void print(Region region, const function<string(int, int)> colors) {
    print(region, colors, [](int x, int y) { return "    "; });
}

void print(Region region, double mn, double mx, const function<double(int, int)> values) {
    print(region, colors(mn, mx, values));
}