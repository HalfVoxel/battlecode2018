#include "influence.h"

#include <stdlib.h>
#include <cmath>
#include <algorithm>

using namespace std;

vector<vector<double> > wideEnemyInfluence;
vector<vector<double> > rangerTargetInfluence;
vector<vector<double> > enemyRangerTargetInfluence;
vector<vector<double> > enemyMageTargetInfluence;
vector<vector<double> > enemyKnightTargetInfluence;
vector<vector<double> > mageTargetInfluence;
vector<vector<double> > mageProximityInfluence;
vector<vector<double> > mageNearbyFuzzyInfluence;
vector<vector<double> > healerTargetInfluence;
vector<vector<double> > healerOverchargeInfluence;
vector<vector<double> > knightTargetInfluence;
vector<vector<double> > healerProximityInfluence;
vector<vector<double> > healerInfluence;
vector<vector<double> > workerProximityInfluence;
vector<vector<double> > workerAdditiveInfluence;
vector<vector<double> > factoryProximityInfluence;
vector<vector<double> > rocketProximityInfluence;
vector<vector<double> > rangerProximityInfluence;
vector<vector<double> > knightHideFromRangerInfluence;
vector<vector<double> > knightHideFromKnightInfluence;
vector<vector<double> > mageHideFromKnightInfluence;
vector<vector<double> > enemyFactoryNearbyInfluence;
vector<vector<double> > mageToOverchargeInfluence;
vector<vector<double> > healerSafetyInfluence;

vector<vector<double>> calculate_uniform_disc_influence(int squared_radius) {
    int r = (int)ceil(sqrt(squared_radius));
    auto res = vector<vector<double>>(2*r+1, vector<double>(2*r+1));
    for (int dx = -r; dx <= r; ++dx) {
        for (int dy = -r; dy <= r; ++dy) {
            int dis2 = dx*dx + dy*dy;
            if (dis2 > squared_radius) {
                continue;
            }
            res[dx+r][dy+r] = 1;
        }
    }
    return res;
}

vector<vector<double>> calculate_rough_disc_influence(int squared_radius) {
    int r = (int)ceil(sqrt(squared_radius)) + 1;
    auto res = vector<vector<double>>(2*r+1, vector<double>(2*r+1));
    for (int dx = -r; dx <= r; ++dx) {
        for (int dy = -r; dy <= r; ++dy) {
            int dis2 = dx*dx + dy*dy;
            if (dis2 <= squared_radius) {
                res[dx+r][dy+r] = 1;
            } else {
                int dx2 = abs(dx)+1, dy2 = abs(dy)+1;
                dis2 = dx2*dx2 + dy2*dy2;
                if (dis2 <= squared_radius)
                    res[dx+r][dy+r] = 0.5;
            }
        }
    }
    return res;
}

void initInfluence() {
    int r = 7;
    rangerTargetInfluence = vector<vector<double>>(2*r+1, vector<double>(2*r+1));
    for (int dx = -r; dx <= r; ++dx) {
        for (int dy = -r; dy <= r; ++dy) {
            int dis2 = dx*dx + dy*dy;
            if (dis2 <= 10) {
                continue;
            }
            if (dis2 > 50) {
                continue;
            }
            rangerTargetInfluence[dx+r][dy+r] = 1;
        }
    }
    
    r = 8;
    enemyRangerTargetInfluence = vector<vector<double>>(2*r+1, vector<double>(2*r+1));
    for (int dx = -r; dx <= r; ++dx) {
        for (int dy = -r; dy <= r; ++dy) {
            int x = abs(dx)+1;
            int y = abs(dy)+1;
            int dis2 = x*x + y*y;
            if (dis2 <= 10) {
                continue;
            }
            x = max(0, abs(dx)-1);
            y = max(0, abs(dy)-1);
            dis2 = x*x + y*y;
            if (dis2 > 50) {
                continue;
            }
            enemyRangerTargetInfluence[dx+r][dy+r] = 1;
        }
    }
    
    r = 12;
    wideEnemyInfluence = vector<vector<double>>(2*r+1, vector<double>(2*r+1));
    for (int dx = -r; dx <= r; ++dx) {
        for (int dy = -r; dy <= r; ++dy) {
            int dis2 = dx*dx + dy*dy;
            wideEnemyInfluence[dx+r][dy+r] = 50.0 / (50 + dis2);
        }
    }
    
    healerTargetInfluence = calculate_uniform_disc_influence(30);
    mageTargetInfluence = calculate_uniform_disc_influence(30);
    enemyMageTargetInfluence = calculate_rough_disc_influence(30);
    mageProximityInfluence = calculate_uniform_disc_influence(30);
    knightTargetInfluence = calculate_uniform_disc_influence(2);
    enemyKnightTargetInfluence = calculate_rough_disc_influence(2);
    
    r = 3;
    knightHideFromRangerInfluence = vector<vector<double>>(2*r+1, vector<double>(2*r+1));
    for (int dx = -r; dx <= r; ++dx) {
        for (int dy = -r; dy <= r; ++dy) {
            int dis2 = dx*dx + dy*dy;
            if (dis2 == 1)
                knightHideFromRangerInfluence[dx+r][dy+r] = 0.1;
            else if (dis2 == 2)
                knightHideFromRangerInfluence[dx+r][dy+r] = 0.09;
            else if (dis2 == 4)
                knightHideFromRangerInfluence[dx+r][dy+r] = 0.08;
            else if (dis2 <= 10)
                knightHideFromRangerInfluence[dx+r][dy+r] = 0.05;
        }
    }
    
    r = 2;
    knightHideFromKnightInfluence = vector<vector<double>>(2*r+1, vector<double>(2*r+1));
    for (int dx = -r; dx <= r; ++dx) {
        for (int dy = -r; dy <= r; ++dy) {
            int maxDis = max(abs(dx), abs(dy));
            if (maxDis == 1)
                knightHideFromKnightInfluence[dx+r][dy+r] = 1.5;
            if (maxDis == 2)
                knightHideFromKnightInfluence[dx+r][dy+r] = 1.0;
        }
    }
    
    r = 7;
    mageHideFromKnightInfluence = vector<vector<double>>(2*r+1, vector<double>(2*r+1));
    for (int dx = -r; dx <= r; ++dx) {
        for (int dy = -r; dy <= r; ++dy) {
            int dis2 = dx*dx + dy*dy;
            mageHideFromKnightInfluence[dx+r][dy+r] = 50.0 / (dis2 * dis2);
        }
    }
    
    r = 7;
    mageNearbyFuzzyInfluence = vector<vector<double>>(2*r+1, vector<double>(2*r+1));
    for (int dx = -r; dx <= r; ++dx) {
        for (int dy = -r; dy <= r; ++dy) {
            int dis2 = dx*dx + dy*dy;
            mageNearbyFuzzyInfluence[dx+r][dy+r] = 1 / (1.0 + dis2);
        }
    }
    
    r = 5;
    healerProximityInfluence = vector<vector<double>>(2*r+1, vector<double>(2*r+1));
    for (int dx = -r; dx <= r; ++dx) {
        for (int dy = -r; dy <= r; ++dy) {
            int dis2 = dx*dx + dy*dy;
            healerProximityInfluence[dx+r][dy+r] = 1 / (1.0 + dis2);
        }
    }

    r = 10;
    healerOverchargeInfluence = vector<vector<double>>(2*r+1, vector<double>(2*r+1));
    for (int dx = -r; dx <= r; ++dx) {
        for (int dy = -r; dy <= r; ++dy) {
            int dis2 = dx*dx + dy*dy;
            if (dis2 > 80 && dis2 < 110) {
                healerOverchargeInfluence[dx+r][dy+r] = 1;
            }
        }
    }
    
    healerInfluence = calculate_uniform_disc_influence(31);
    
    r = 5;
    workerProximityInfluence = vector<vector<double>>(2*r+1, vector<double>(2*r+1));
    for (int dx = -r; dx <= r; ++dx) {
        for (int dy = -r; dy <= r; ++dy) {
            int dis2 = dx*dx + dy*dy;
            workerProximityInfluence[dx+r][dy+r] = 0.05 / (1.0 + dis2);
        }
    }

    r = 6;
    workerAdditiveInfluence = vector<vector<double>>(2*r+1, vector<double>(2*r+1));
    for (int dx = -r; dx <= r; ++dx) {
        for (int dy = -r; dy <= r; ++dy) {
            int dis2 = dx*dx + dy*dy;
            workerAdditiveInfluence[dx+r][dy+r] = 4.0 / (4.0 + dis2);
        }
    }
    
    r = 5;
    rangerProximityInfluence = vector<vector<double>>(2*r+1, vector<double>(2*r+1));
    for (int dx = -r; dx <= r; ++dx) {
        for (int dy = -r; dy <= r; ++dy) {
            int dis2 = dx*dx + dy*dy;
            rangerProximityInfluence[dx+r][dy+r] = 1.0 / (1.0 + dis2);
            if (dis2 == 0) {
                rangerProximityInfluence[dx+r][dy+r] = 0.5;
            }
        }
    }
    
    r = 5;
    factoryProximityInfluence = vector<vector<double>>(2*r+1, vector<double>(2*r+1));
    for (int dx = -r; dx <= r; ++dx) {
        for (int dy = -r; dy <= r; ++dy) {
            int dis2 = dx*dx + dy*dy;
            factoryProximityInfluence[dx+r][dy+r] = 0.01 / (1.0 + dis2);
            if (dis2 <= 2) {
                factoryProximityInfluence[dx+r][dy+r] = 0.4;
            }
            if (dis2 == 0) {
                factoryProximityInfluence[dx+r][dy+r] = 5;
            }
        }
    }
    
    r = 8;
    enemyFactoryNearbyInfluence = vector<vector<double>>(2*r+1, vector<double>(2*r+1));
    for (int dx = -r; dx <= r; ++dx) {
        for (int dy = -r; dy <= r; ++dy) {
            int dis2 = dx*dx + dy*dy;
            enemyFactoryNearbyInfluence[dx+r][dy+r] = 10.0 / (10.0 + dis2);
        }
    }
    
    r = 5;
    rocketProximityInfluence = vector<vector<double>>(2*r+1, vector<double>(2*r+1));
    for (int dx = -r; dx <= r; ++dx) {
        for (int dy = -r; dy <= r; ++dy) {
            int dis2 = dx*dx + dy*dy;
            rocketProximityInfluence[dx+r][dy+r] = 0.1 / (1.0 + dis2);
            if (dis2 <= 2) {
                rocketProximityInfluence[dx+r][dy+r] = 0.2;
            }
        }
    }

    r = 9;
    mageToOverchargeInfluence = vector<vector<double>>(2*r+1, vector<double>(2*r+1));
    for (int dx = -r; dx <= r; ++dx) {
        for (int dy = -r; dy <= r; ++dy) {
            int dis2 = dx*dx + dy*dy;
            if (dx*dx+dy*dy > 68)
                mageToOverchargeInfluence[dx+r][dy+r] = 40.0 / dis2;
            else if (dx*dx+dy*dy > 50)
                mageToOverchargeInfluence[dx+r][dy+r] = 0.4;
        }
    }

    r = 10;
    healerSafetyInfluence = vector<vector<double>>(2*r+1, vector<double>(2*r+1));
    for (int dx = -r; dx <= r; ++dx) {
        for (int dy = -r; dy <= r; ++dy) {
            int dis2 = dx*dx + dy*dy;
            if (dx*dx+dy*dy > 80)
                healerSafetyInfluence[dx+r][dy+r] = 40.0 / dis2;
            else if (dx*dx+dy*dy > 50)
                healerSafetyInfluence[dx+r][dy+r] = 0.1;
        }
    }
}
