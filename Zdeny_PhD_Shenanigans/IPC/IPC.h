#pragma once
#include "stdafx.h"
#include "Core/functionsBaseSTL.h"
#include "Core/functionsBaseCV.h"
#include "Fourier/fourier.h"
#include "Astrophysics/FITS.h"
#include "Filtering/filtering.h"
#include "Log/logger.h"

using namespace std;
using namespace cv;

static constexpr int maxPCit = 20;
static constexpr double loglimit = 5;

class IPCsettings
{
private:
  double stdevLmultiplier = 1;
  double stdevHmultiplier = 200;
  int rows = 0;
  int cols = 0;

public:
  enum Speak
  {
    None,
    Errors,
    All
  };

  int L2size = 15;
  double L1ratio = 0.35;
  int UC = 51;
  double epsilon = 0;
  bool interpolate = 1;
  bool applyWindow = 1;
  bool applyBandpass = 1;
  bool subpixel = 1;
  bool crossCorrel = 0;
  Speak speak = Errors;
  double minimalShift = 0;
  Mat bandpass;
  Mat window;
  bool save = false;
  string savedir = "";
  cv::Size savesize = cv::Size(500, 500);
  mutable int savecntr = -1;

  IPCsettings(int Rows, int Cols, double StdevLmultiplier, double StdevHmultiplier) : rows(Rows), cols(Cols), stdevLmultiplier(StdevLmultiplier), stdevHmultiplier(StdevHmultiplier)
  {
    bandpass = bandpassian(rows, cols, stdevLmultiplier, stdevHmultiplier);
    window = edgemask(rows, cols);
  }

  void setBandpassParameters(double StdevLmultiplier, double StdevHmultiplier)
  {
    stdevLmultiplier = StdevLmultiplier;
    stdevHmultiplier = StdevHmultiplier;
    bandpass = bandpassian(rows, cols, stdevLmultiplier, stdevHmultiplier);
  }

  void setSize(int Rows, int Cols)
  {
    rows = Rows;
    cols = Cols;
    bandpass = bandpassian(rows, cols, stdevLmultiplier, stdevHmultiplier);
    window = edgemask(rows, cols);
  }

  int getrows() const { return rows; }
  int getcols() const { return cols; }
  double getL() const { return stdevLmultiplier; }
  double getH() const { return stdevHmultiplier; }
  Mat getWindow() const { return window; }
  Mat getBandpass() const { return bandpass; }

  inline cv::Point2f Calculate(const Mat& img1, const Mat& img2)
  {
    LOG_DEBUG("XDD");
    return cv::Point2f();
  }
};

inline void ipcsaveimg(const char* filename, const Mat& img, const IPCsettings& set)
{
  saveimg(set.savedir + to_string(set.savecntr) + "_" + filename, img, false, set.savesize, true, 0, 1);
}

inline Point2f ipccore(Mat&& sourceimg1, Mat&& sourceimg2, const IPCsettings& set, bool forceshow = false)
{
  sourceimg1.convertTo(sourceimg1, CV_32F, 1. / 65535);
  sourceimg2.convertTo(sourceimg2, CV_32F, 1. / 65535);

  std::vector<Mat> showMatsGRS; // grayscale
  std::vector<Mat> showMatsCLR; // color

  if (set.applyWindow)
  {
    multiply(sourceimg1, set.window, sourceimg1);
    multiply(sourceimg2, set.window, sourceimg2);
  }

  if (set.speak > IPCsettings::Errors || forceshow)
  {
    showMatsGRS.push_back(sourceimg1);
    showMatsGRS.push_back(sourceimg2);
    if (set.applyBandpass)
      showMatsCLR.push_back(set.bandpass);
    if (set.applyWindow)
      showMatsCLR.push_back(set.window);
  }

  if (set.save)
    set.savecntr++;

  if (set.save)
  {
    auto sourceimg1vs = applyQuantileColorMap(sourceimg1);
    auto sourceimg2vs = applyQuantileColorMap(sourceimg2);
    // ipcsaveimg("ipc_image1.png", sourceimg1vs, set);
    // ipcsaveimg("ipc_image2.png", sourceimg2vs, set);
  }

  Point2f output;
  Mat L3;

  Mat DFT1 = fourier(std::move(sourceimg1));
  Mat DFT2 = fourier(std::move(sourceimg2));

  Mat planes1[2];
  Mat planes2[2];
  Mat CrossPowerPlanes[2];

  split(DFT1, planes1);
  split(DFT2, planes2);

  planes1[1] *= -1;                                                              // complex conjugate of second pic
  CrossPowerPlanes[0] = planes1[0].mul(planes2[0]) - planes1[1].mul(planes2[1]); // pointwise multiplications real
  CrossPowerPlanes[1] = planes1[0].mul(planes2[1]) + planes1[1].mul(planes2[0]); // imag

  if (!set.crossCorrel)
  {
    Mat magnre, magnim;
    pow(CrossPowerPlanes[0], 2, magnre);
    pow(CrossPowerPlanes[1], 2, magnim);
    Mat normalizationdenominator = magnre + magnim;
    sqrt(normalizationdenominator, normalizationdenominator);
    CrossPowerPlanes[0] /= (normalizationdenominator + set.epsilon);
    CrossPowerPlanes[1] /= (normalizationdenominator + set.epsilon);
  }

  Mat CrossPower;
  merge(CrossPowerPlanes, 2, CrossPower);

  if (set.applyBandpass)
    CrossPower = bandpass(CrossPower, set.bandpass);

  if constexpr (0) // CORRECT - complex magnitude - input can be real or complex whatever
  {
    Mat L3complex;
    dft(CrossPower, L3complex, DFT_INVERSE + DFT_SCALE);
    Mat L3planes[2];
    split(L3complex, L3planes);

    if (set.speak > IPCsettings::Errors)
    {
      auto minmaxR = minMaxMat(L3planes[0]);
      auto minmaxI = minMaxMat(L3planes[1]);
      LOG_DEBUG("L3 real min/max: {}/{}", std::get<0>(minmaxR), std::get<1>(minmaxR));
      LOG_DEBUG("L3 imag min/max: {}/{}", std::get<0>(minmaxI), std::get<1>(minmaxI));
    }

    magnitude(L3planes[0], L3planes[1], L3);
  }
  else // real only (assume pure real input)
  {
    dft(CrossPower, L3, DFT_INVERSE + DFT_SCALE + DFT_REAL_OUTPUT);
    if (set.speak > IPCsettings::Errors)
    {
      auto minmax = minMaxMat(L3);
      LOG_DEBUG("L3 real min/max: {}/{}", std::get<0>(minmax), std::get<1>(minmax));
    }
  }

  L3 = quadrantswap(L3);
  // normalize(L3, L3, 0, 1, NORM_MINMAX); //not needed, can see the quality by value at peak

  if (set.minimalShift)
    L3 = L3.mul(1 - kirkl(L3.rows, L3.cols, set.minimalShift));

  Point2i L3peak;
  double maxR;
  minMaxLoc(L3, nullptr, &maxR, nullptr, &L3peak);
  Point2f L3mid(L3.cols / 2, L3.rows / 2);
  Point2f imageshift_PIXEL(L3peak.x - L3mid.x, L3peak.y - L3mid.y);

  if (set.speak > IPCsettings::Errors || forceshow)
  {
    Mat L3v;
    resize(L3, L3v, cv::Size(2000, 2000), 0, 0, INTER_NEAREST);
    showMatsCLR.push_back(crosshair(L3v, Point2f(round((float)(L3peak.x) * 2000. / (float)L3.cols), round((float)(L3peak.y) * 2000. / (float)L3.rows))));

    Mat L3vz = roicrop(L3, L3mid.x, L3mid.y, L3.cols / 7, L3.rows / 7);
    resize(L3vz, L3vz, cv::Size(2000, 2000), 0, 0, set.interpolate ? INTER_LINEAR : INTER_NEAREST);
    showMatsCLR.push_back(crosshair(L3vz, Point2f(L3vz.cols / 2, L3vz.rows / 2)));

    if (set.save)
    {
      auto L3vs = applyQuantileColorMap(L3v);
      auto L3vzs = applyQuantileColorMap(L3vz);
      // ipcsaveimg("ipc_L3.png", L3vs, set);
      ipcsaveimg("ipc_L3_interp_zoom.png", L3vzs, set);
      // Plot2D::Plot(L3vz, std::to_string(set.savecntr) + "_ipc_L3_interp_zoom.png", "x [px]", "y [px]", "phase correlation", -L3.cols / 7 / 2, +L3.cols / 7 / 2, -L3.rows / 7 / 2, +L3.rows / 7 / 2,
      // 0, set.savedir + std::to_string(set.savecntr) + "_ipc_L3_interp_zoom.png");
    }
  }

  output = imageshift_PIXEL;

  if (set.subpixel)
  {
    bool converged = false;
    int L2size = set.L2size;
    if (!(L2size % 2))
      L2size++; // odd!+
    while (!converged)
    {
      if (((L3peak.x - L2size / 2) < 0) || ((L3peak.y - L2size / 2) < 0) || ((L3peak.x + L2size / 2) >= L3.cols) || ((L3peak.y + L2size / 2) >= L3.rows))
      {
        L2size -= 2;
        if (L2size < 3)
        {
          break;
        }
      }
      else
      {
        Point2f imageshift_SUBPIXEL;
        Mat L2 = roicrop(L3, L3peak.x, L3peak.y, L2size, L2size);
        Mat L2U;

        if (set.interpolate)
          resize(L2, L2U, L2.size() * set.UC, 0, 0, INTER_LINEAR);
        else
          resize(L2, L2U, L2.size() * set.UC, 0, 0, INTER_NEAREST);

        Point2f L2mid(L2.cols / 2, L2.rows / 2);
        Point2f L2Umid(L2U.cols / 2, L2U.rows / 2);
        Point2f L2Upeak = L2Umid;
        if (set.speak > IPCsettings::Errors || forceshow)
        {
          Mat L2Uv;
          resize(L2U, L2Uv, cv::Size(2000, 2000), 0, 0, INTER_LINEAR);
          showMatsCLR.push_back(crosshair(L2Uv, L2Umid * 2000 / L2U.cols));
          if (set.save)
          {
            auto L2Uvs = applyQuantileColorMap(crosshair(L2Uv, L2Umid * 2000 / L2U.cols));
            // ipcsaveimg("ipc_L2.png", L2Uvs, set);
          }
        }

        int L1size = std::floor((float)L2U.cols * set.L1ratio);
        if (!(L1size % 2))
          L1size++; // odd!+
        Mat L1 = kirklcrop(L2U, L2Upeak.x, L2Upeak.y, L1size);
        Point2f L1mid(L1.cols / 2, L1.rows / 2);
        imageshift_SUBPIXEL = (Point2f)L3peak - L3mid + findCentroid(L1) - L1mid;

        for (int i = 0; i < maxPCit; i++)
        {
          L1 = kirklcrop(L2U, L2Upeak.x, L2Upeak.y, L1size);
          Point2f L1peak = findCentroid(L1);
          L2Upeak.x += round(L1peak.x - L1mid.x);
          L2Upeak.y += round(L1peak.y - L1mid.y);
          if ((L2Upeak.x > (L2U.cols - L1mid.x - 1)) || (L2Upeak.y > (L2U.rows - L1mid.y - 1)) || (L2Upeak.x < (L1mid.x + 1)) || (L2Upeak.y < (L1mid.y + 1)))
          {
            L2size += -2;
            break;
          }
          if ((abs(L1peak.x - L1mid.x) < 0.5) && (abs(L1peak.y - L1mid.y) < 0.5))
          {
            L1 = kirklcrop(L2U, L2Upeak.x, L2Upeak.y, L1size);
            if (set.speak > IPCsettings::Errors || forceshow)
            {
              Mat L1v;
              resize(L1, L1v, cv::Size(2000, 2000), 0, 0, INTER_LINEAR);
              showMatsCLR.push_back(crosshair(L1v, L1mid * 2000 / L1.cols));
              if (set.save)
              {
                auto L1vs = applyQuantileColorMap(crosshair(L1v, L1mid * 2000 / L1.cols));
                // ipcsaveimg("ipc_L1.png", L1vs, set);
              }
            }
            converged = true;
            break;
          }
          else if (i == maxPCit - 1)
          {
            L2size += -2;
          }
        }

        if (converged)
        {
          imageshift_SUBPIXEL.x = (float)L3peak.x - (float)L3mid.x + 1.0 / (float)set.UC * ((float)L2Upeak.x - (float)L2Umid.x + findCentroid(L1).x - (float)L1mid.x); // image shift in L3 - final
          imageshift_SUBPIXEL.y = (float)L3peak.y - (float)L3mid.y + 1.0 / (float)set.UC * ((float)L2Upeak.y - (float)L2Umid.y + findCentroid(L1).y - (float)L1mid.y); // image shift in L3 - final
          output = imageshift_SUBPIXEL;
        }
        else if (L2size < 3)
        {
          break;
        }
      }
    }
  }
  if (set.speak > IPCsettings::Errors || forceshow)
  {
    showimg(showMatsGRS, "IPC input", false);
    showimg(showMatsCLR, "IPC pipeline", true);
  }
  return output;
}

inline Point2f phasecorrel(const Mat& sourceimg1In, const Mat& sourceimg2In, const IPCsettings& set, bool forceshow = false)
{
  Mat sourceimg1 = sourceimg1In.clone();
  Mat sourceimg2 = sourceimg2In.clone();

  return ipccore(std::move(sourceimg1), std::move(sourceimg2), set, forceshow);
}

inline Point2f phasecorrel(Mat&& sourceimg1, Mat&& sourceimg2, const IPCsettings& set, bool forceshow = false)
{
  return ipccore(std::move(sourceimg1), std::move(sourceimg2), set, forceshow);
}
