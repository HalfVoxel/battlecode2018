#pragma once
#include <vector>

extern std::vector<std::vector<double> > wideEnemyInfluence;
extern std::vector<std::vector<double> > rangerTargetInfluence;
extern std::vector<std::vector<double> > enemyRangerTargetInfluence;
extern std::vector<std::vector<double> > enemyMageTargetInfluence;
extern std::vector<std::vector<double> > enemyKnightTargetInfluence;
extern std::vector<std::vector<double> > mageTargetInfluence;
extern std::vector<std::vector<double> > mageProximityInfluence;
extern std::vector<std::vector<double> > mageNearbyFuzzyInfluence;
extern std::vector<std::vector<double> > healerTargetInfluence;
extern std::vector<std::vector<double> > knightTargetInfluence;
extern std::vector<std::vector<double> > healerProximityInfluence;
extern std::vector<std::vector<double> > healerInfluence;
extern std::vector<std::vector<double> > workerProximityInfluence;
extern std::vector<std::vector<double> > workerAdditiveInfluence;
extern std::vector<std::vector<double> > factoryProximityInfluence;
extern std::vector<std::vector<double> > rocketProximityInfluence;
extern std::vector<std::vector<double> > rangerProximityInfluence;
extern std::vector<std::vector<double> > knightHideFromRangerInfluence;
extern std::vector<std::vector<double> > knightHideFromKnightInfluence;
extern std::vector<std::vector<double> > mageHideFromKnightInfluence;
extern std::vector<std::vector<double> > enemyFactoryNearbyInfluence;
extern std::vector<std::vector<double> > mageToOverchargeInfluence;

void initInfluence();
