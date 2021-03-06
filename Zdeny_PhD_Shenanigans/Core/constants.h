#pragma once

namespace Constants
{
static constexpr double Pi = 3.1415926535897932384626433;
static constexpr double TwoPi = Pi * 2;
static constexpr double HalfPi = Pi / 2;
static constexpr double QuartPi = Pi / 4;
static constexpr double E = 2.7182818284590452353602874;
static constexpr double Rad = 360. / TwoPi;
static constexpr double SecondsInDay = 24. * 60. * 60.;
static constexpr double RadPerSecToDegPerDay = Rad * SecondsInDay;
static constexpr double Inf = std::numeric_limits<double>::max();
static constexpr double IntInf = std::numeric_limits<int>::max();

static const std::vector<double> emptyvect = std::vector<double>{};
static const std::vector<std::vector<double>> emptyvect2 = std::vector<std::vector<double>>{};
static const std::string emptystring = "";
static const std::vector<std::string> emptyvectstring = std::vector<std::string>{};
}
