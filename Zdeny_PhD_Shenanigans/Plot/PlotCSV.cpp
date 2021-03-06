#include "stdafx.h"
#include "PlotCSV.h"
#include "Plot1D.h"

int PlotCSV::counter = 0;

void PlotCSV::plot(const std::string& path, const std::string& savepath)
{
  auto [x, ys, xlabel, ylabels] = ParseCSV(path);

  if (x.empty() || ys.empty())
    return;

  std::string plotname = std::string("csv plot ") + to_string(counter);
  std::string ylabel = "";

  // Plot1D::Plot(path, x, ys, plotname, xlabel, ylabel, ylabels, Plot::pens, savepath);
  counter++;
}

std::tuple<std::vector<double>, std::vector<std::vector<double>>, std::string, std::vector<std::string>> PlotCSV::ParseCSV(const std::string& path)
{
  std::ifstream file(path);
  if (!file.is_open() || !file.good())
  {
    LOG_ERROR("Could not open file {}", path);
    return {};
  }

  // fill label vector from first line
  std::vector<std::string> labels;
  std::string line, colname;
  std::getline(file, line);
  std::stringstream ss(line);
  while (std::getline(ss, colname, ','))
    labels.push_back(colname);

  // fill x & ys vectors from the rest of the file
  std::vector<std::string> ylabels{labels.begin() + 1, labels.end()};
  std::vector<std::vector<double>> ys(ylabels.size());
  std::string xlabel = labels[0];
  std::vector<double> x;
  double value;

  while (std::getline(file, line))
  {
    std::stringstream ss(line);
    int col = 0;

    while (ss >> value)
    {
      if (!col)
        x.push_back(value);
      else
        ys[col - 1].push_back(value);

      if (ss.peek() == ',')
        ss.ignore();

      col++;
    }

    if (col != ys.size() + 1)
    {
      LOG_ERROR("Inconsistent csv columns for x={}: {}!={}", x.back(), col, ys.size());
      return {};
    }
  }

  return {x, ys, xlabel, ylabels};
}
