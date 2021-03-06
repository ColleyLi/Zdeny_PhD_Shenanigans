#pragma once
#include "Optimization.h"

struct PatternSearch : public OptimizationAlgorithm
{
  double minStep = 1e-5;
  int multistartMaxCnt = 1;
  int multistartCnt = 0;
  int maxExploitCnt = 0;
  double stepReducer = 0.5;

  PatternSearch(int N) : OptimizationAlgorithm(N){};
  OptimizationResult Optimize(ObjectiveFunction obj, ValidationFunction valid) override;
};
