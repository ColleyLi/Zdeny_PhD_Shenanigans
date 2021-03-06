#include "stdafx.h"
#include "IterativePhaseCorrelation.h"
#include "Core/functionsBaseSTL.h"
#include "Core/functionsBaseCV.h"
#include "Fourier/fourier.h"
#include "Astrophysics/FITS.h"
#include "Filtering/filtering.h"
#include "Log/logger.h"
#include "Optimization/Evolution.h"

IterativePhaseCorrelation::IterativePhaseCorrelation(int rows, int cols, double bandpassL, double bandpassH)
{
  SetSize(rows, cols);
  SetBandpassParameters(bandpassL, bandpassH);
}

IterativePhaseCorrelation::IterativePhaseCorrelation(const Mat& img, double bandpassL, double bandpassH)
{
  SetSize(img.size());
  SetBandpassParameters(bandpassL, bandpassH);
}

void IterativePhaseCorrelation::SetBandpassParameters(double bandpassL, double bandpassH)
{
  mBandpassL = clamp(bandpassL, -Constants::Inf, 1); // L from [-inf, 1]
  mBandpassH = clamp(bandpassH, 0, Constants::Inf);  // H from [0, inf]
  UpdateBandpass();
}

void IterativePhaseCorrelation::SetSize(int rows, int cols)
{
  mRows = rows;
  mCols = cols > 0 ? cols : rows;

  if (mWindow.rows != mRows || mWindow.cols != mCols)
    UpdateWindow();

  if (mBandpass.rows != mRows || mBandpass.cols != mCols)
    UpdateBandpass();
}

void IterativePhaseCorrelation::SetSize(Size size)
{
  SetSize(size.height, size.width);
}

Point2f IterativePhaseCorrelation::Calculate(const Mat& image1, const Mat& image2) const
{
  return Calculate(image1.clone(), image2.clone());
}

inline Point2f IterativePhaseCorrelation::Calculate(Mat&& image1, Mat&& image2) const
try
{
  if (!IsValid(image1, image2))
  {
    LOG_ERROR("Input images are not valid");
    return {0, 0};
  }

  ConvertToUnitFloat(image1);
  ConvertToUnitFloat(image2);

  ApplyWindow(image1);
  ApplyWindow(image2);

  if (mDebugMode)
  {
    InitializePlots();
    Plot2D::Plot("IPCgrayscale", image1);
    Plot2D::Plot("IPCgrayscale", image2);
  }

  auto dft1 = CalculateFourierTransform(std::move(image1));
  auto dft2 = CalculateFourierTransform(std::move(image2));
  auto crosspower = CalculateCrossPowerSpectrum(dft1, dft2);
  ApplyBandpass(crosspower);

  Mat L3 = CalculateL3(std::move(crosspower));
  Point2f L3peak = GetPeak(L3);
  Point2f L3mid(L3.cols / 2, L3.rows / 2);
  Point2f result = L3peak - L3mid;

  if (mDebugMode)
  {
    Plot2D::SetSavePath("IPCcolormap", mDebugDirectory + "/L3.png");
    Plot2D::Plot("IPCcolormap", L3);
  }

  if (mAccuracyType == AccuracyType::Pixel)
    return L3peak - L3mid;

  if (mAccuracyType == AccuracyType::Subpixel)
  {
    int L2size = 5;
    while (IsOutOfBounds(L3peak, L3, L2size))
      if (!ReduceL2size(L2size))
        return result;

    Mat L2 = CalculateL2(L3, L3peak, 5);
    Point2f L2peak = GetPeakSubpixel(L2);
    Point2f L2mid(L2.cols / 2, L2.rows / 2);
    return L3peak - L3mid + L2peak - L2mid;
  }

  int L2size = mL2size;
  double L1ratio = mL1ratio;

  // reduce the L2size as long as the L2 is out of bounds, return pixel level estimation accuracy if it cannot be reduced anymore
  while (IsOutOfBounds(L3peak, L3, L2size))
    if (!ReduceL2size(L2size))
      return result;

  // L2
  Mat L2 = CalculateL2(L3, L3peak, L2size);
  Point2f L2mid(L2.cols / 2, L2.rows / 2);
  if (mDebugMode)
  {
    Plot2D::SetSavePath("IPCcolormap", mDebugDirectory + "/L2.png");
    Plot2D::Plot("IPCcolormap", L2);
  }

  // L2U
  Mat L2U = CalculateL2U(L2);
  Point2f L2Umid(L2U.cols / 2, L2U.rows / 2);
  Point2f L2Upeak;
  if (mDebugMode)
  {
    Plot2D::SetSavePath("IPCcolormap", mDebugDirectory + "/L2U.png");
    Plot2D::Plot("IPCcolormap", L2U, true);

    if (0)
    {
      Mat nearest, linear, cubic;
      resize(L2, nearest, L2.size() * mUpsampleCoeff, 0, 0, INTER_NEAREST);
      resize(L2, linear, L2.size() * mUpsampleCoeff, 0, 0, INTER_LINEAR);
      resize(L2, cubic, L2.size() * mUpsampleCoeff, 0, 0, INTER_CUBIC);
      Plot2D::SetSavePath(mDebugDirectory + "/L2UN.png");
      Plot2D::Plot("IPCcolormap", nearest, true);
      Plot2D::SetSavePath(mDebugDirectory + "/L2UL.png");
      Plot2D::Plot("IPCcolormap", linear, true);
      Plot2D::SetSavePath(mDebugDirectory + "/L2UC.png");
      Plot2D::Plot("IPCcolormap", cubic, true);
    }
  }

  while (true)
  {
    // reset L2U peak position
    L2Upeak = L2Umid;

    // L1
    Mat L1;
    int L1size = GetL1size(L2U, L1ratio);
    Point2f L1mid(L1size / 2, L1size / 2);
    Point2f L1peak;
    if (mDebugMode)
    {
      Plot2D::SetSavePath("IPCcolormap", mDebugDirectory + "/L1B.png");
      Plot2D::Plot("IPCcolormap", CalculateL1(L2U, L2Upeak, L1size), true);
    }

    for (int iter = 0; iter < mMaxIterations; ++iter)
    {
      L1 = CalculateL1(L2U, L2Upeak, L1size);
      L1peak = GetPeakSubpixel(L1);
      L2Upeak += Point2f(round(L1peak.x - L1mid.x), round(L1peak.y - L1mid.y));

      if (IsOutOfBounds(L2Upeak, L2U, L1size))
        break;

      if (AccuracyReached(L1peak, L1mid))
      {
        if (mDebugMode)
        {
          Plot2D::SetSavePath("IPCcolormap", mDebugDirectory + "/L1A.png");
          Plot2D::Plot("IPCcolormap", L1, true);
        }

        return L3peak - L3mid + (L2Upeak - L2Umid + L1peak - L1mid) / mUpsampleCoeff;
      }
    }

    // maximum iterations reached - reduce L1 size by reducing L1ratio
    ReduceL1ratio(L1ratio);
  }
}
catch (const std::exception& e)
{
  LOG_ERROR("Unexpected error occurred: {}", e.what());
  return {0, 0};
}

inline void IterativePhaseCorrelation::UpdateWindow()
{
  switch (mWindowType)
  {
  case WindowType::Rectangular:
    mWindow = Mat::ones(mRows, mCols, CV_32F);
    break;

  case WindowType::Hann:
    createHanningWindow(mWindow, cv::Size(mCols, mRows), CV_32F);
    break;
  }
}

inline void IterativePhaseCorrelation::UpdateBandpass()
{
  mBandpass = Mat::ones(mRows, mCols, CV_32F);

  switch (mBandpassType)
  {
  case BandpassType::Gaussian:
    if (mBandpassL <= 0 && mBandpassH < 1)
    {
      for (int r = 0; r < mRows; ++r)
        for (int c = 0; c < mCols; ++c)
          mBandpass.at<float>(r, c) = LowpassEquation(r, c);
    }
    else if (mBandpassL > 0 && mBandpassH >= 1)
    {
      for (int r = 0; r < mRows; ++r)
        for (int c = 0; c < mCols; ++c)
          mBandpass.at<float>(r, c) = HighpassEquation(r, c);
    }
    else if (mBandpassL > 0 && mBandpassH < 1)
    {
      for (int r = 0; r < mRows; ++r)
        for (int c = 0; c < mCols; ++c)
          mBandpass.at<float>(r, c) = BandpassGEquation(r, c);

      normalize(mBandpass, mBandpass, 0.0, 1.0, NORM_MINMAX);
    }
    break;

  case BandpassType::Rectangular:
    if (mBandpassL < mBandpassH)
    {
      for (int r = 0; r < mRows; ++r)
        for (int c = 0; c < mCols; ++c)
          mBandpass.at<float>(r, c) = BandpassREquation(r, c);
    }
    break;
  }

  CalculateFrequencyBandpass();
}

inline float IterativePhaseCorrelation::LowpassEquation(int row, int col) const
{
  return exp(-1.0 / (2. * std::pow(mBandpassH, 2)) * (std::pow(col - mCols / 2, 2) / std::pow(mCols / 2, 2) + std::pow(row - mRows / 2, 2) / std::pow(mRows / 2, 2)));
}

inline float IterativePhaseCorrelation::HighpassEquation(int row, int col) const
{
  return 1.0 - exp(-1.0 / (2. * std::pow(mBandpassL, 2)) * (std::pow(col - mCols / 2, 2) / std::pow(mCols / 2, 2) + std::pow(row - mRows / 2, 2) / std::pow(mRows / 2, 2)));
}

inline float IterativePhaseCorrelation::BandpassGEquation(int row, int col) const
{
  return LowpassEquation(row, col) * HighpassEquation(row, col);
}

inline float IterativePhaseCorrelation::BandpassREquation(int row, int col) const
{
  double r = sqrt(0.5 * (std::pow(col - mCols / 2, 2) / std::pow(mCols / 2, 2) + std::pow(row - mRows / 2, 2) / std::pow(mRows / 2, 2)));
  return (mBandpassL <= r && r <= mBandpassH) ? 1 : 0;
}

inline bool IterativePhaseCorrelation::IsValid(const Mat& image1, const Mat& image2) const
{
  // size must be equal for matching bandpass / window
  if (!CheckSize(image1, image2))
    return false;

  // only grayscale images are supported
  if (!CheckChannels(image1, image2))
    return false;

  return true;
}

inline bool IterativePhaseCorrelation::CheckSize(const Mat& image1, const Mat& image2) const
{
  Size ipcsize(mCols, mRows);
  return image1.size() == image2.size() && image1.size() == ipcsize && image2.size() == ipcsize;
}

inline bool IterativePhaseCorrelation::CheckChannels(const Mat& image1, const Mat& image2) const
{
  return image1.channels() == 1 && image2.channels() == 1;
}

inline void IterativePhaseCorrelation::ConvertToUnitFloat(Mat& image) const
{
  image.convertTo(image, CV_32F);
  normalize(image, image, 0, 1, CV_MINMAX);
}

inline void IterativePhaseCorrelation::ApplyWindow(Mat& image) const
{
  multiply(image, mWindow, image);
}

inline Mat IterativePhaseCorrelation::CalculateFourierTransform(Mat&& image) const
{
  return Fourier::fft(std::move(image));
}

inline Mat IterativePhaseCorrelation::CalculateCrossPowerSpectrum(const Mat& dft1, const Mat& dft2) const
{
  Mat planes1[2];
  Mat planes2[2];
  Mat CrossPowerPlanes[2];

  split(dft1, planes1);
  split(dft2, planes2);

  planes1[1] *= -1;                                                              // complex conjugate of second pic
  CrossPowerPlanes[0] = planes1[0].mul(planes2[0]) - planes1[1].mul(planes2[1]); // pointwise multiplications real
  CrossPowerPlanes[1] = planes1[0].mul(planes2[1]) + planes1[1].mul(planes2[0]); // imag

  Mat magnre, magnim;
  pow(CrossPowerPlanes[0], 2, magnre);
  pow(CrossPowerPlanes[1], 2, magnim);
  Mat normalizationdenominator = magnre + magnim;
  sqrt(normalizationdenominator, normalizationdenominator);
  normalizationdenominator += std::numeric_limits<float>::epsilon();
  CrossPowerPlanes[0] /= normalizationdenominator;
  CrossPowerPlanes[1] /= normalizationdenominator;

  Mat CrossPower;
  merge(CrossPowerPlanes, 2, CrossPower);
  return CrossPower;
}

inline void IterativePhaseCorrelation::ApplyBandpass(Mat& crosspower) const
{
  multiply(crosspower, mFrequencyBandpass, crosspower);
}

inline void IterativePhaseCorrelation::CalculateFrequencyBandpass()
{
  mFrequencyBandpass = Fourier::dupchansc(mBandpass);
  Fourier::ifftshift(mFrequencyBandpass);
}

inline Mat IterativePhaseCorrelation::CalculateL3(Mat&& crosspower) const
{
  Mat L3 = Fourier::ifft(std::move(crosspower));
  Fourier::fftshift(L3);
  return L3;
}

inline Point2f IterativePhaseCorrelation::GetPeak(const Mat& mat) const
{
  Point2i peak;
  minMaxLoc(mat, nullptr, nullptr, nullptr, &peak);

  if (peak.x < 0 || peak.y < 0 || peak.x >= mat.cols || peak.y >= mat.rows)
    return Point2f(0, 0);

  return peak;
}

inline Point2f IterativePhaseCorrelation::GetPeakSubpixel(const Mat& mat) const
{
  double M = 0;
  double My = 0;
  double Mx = 0;

  for (int r = 0; r < mat.rows; ++r)
  {
    for (int c = 0; c < mat.cols; ++c)
    {
      M += mat.at<float>(r, c);
      My += mat.at<float>(r, c) * r;
      Mx += mat.at<float>(r, c) * c;
    }
  }

  Point2f result(Mx / M, My / M);

  if (result.x < 0 || result.y < 0 || result.x > mat.cols - 1 || result.y > mat.rows - 1)
    return Point2f(mat.cols / 2, mat.rows / 2);
  else
    return result;
}

inline Mat IterativePhaseCorrelation::CalculateL2(const Mat& L3, const Point2f& L3peak, int L2size) const
{
  return roicrop(L3, L3peak.x, L3peak.y, L2size, L2size);
}

inline Mat IterativePhaseCorrelation::CalculateL2U(const Mat& L2) const
{
  Mat L2U;
  switch (mInterpolationType)
  {
  case InterpolationType::NearestNeighbor:
    resize(L2, L2U, L2.size() * mUpsampleCoeff, 0, 0, INTER_NEAREST);
    break;

  case InterpolationType::Linear:
    resize(L2, L2U, L2.size() * mUpsampleCoeff, 0, 0, INTER_LINEAR);
    break;

  case InterpolationType::Cubic:
    resize(L2, L2U, L2.size() * mUpsampleCoeff, 0, 0, INTER_CUBIC);
    break;
  }

  return L2U;
}

inline int IterativePhaseCorrelation::GetL1size(const Mat& L2U, double L1ratio) const
{
  int L1size = std::floor(L1ratio * L2U.cols);
  L1size = L1size % 2 ? L1size : L1size + 1;
  return L1size;
}

inline Mat IterativePhaseCorrelation::CalculateL1(const Mat& L2U, const Point2f& L2Upeak, int L1size) const
{
  return kirklcrop(L2U, L2Upeak.x, L2Upeak.y, L1size);
}

inline bool IterativePhaseCorrelation::IsOutOfBounds(const Point2f& peak, const Mat& mat, int size) const
{
  return peak.x - size / 2 < 0 || peak.y - size / 2 < 0 || peak.x + size / 2 >= mat.cols || peak.y + size / 2 >= mat.rows;
}

inline bool IterativePhaseCorrelation::AccuracyReached(const Point2f& L1peak, const Point2f& L1mid) const
{
  return abs(L1peak.x - L1mid.x) < 0.5 && abs(L1peak.y - L1mid.y) < 0.5;
}

inline bool IterativePhaseCorrelation::ReduceL2size(int& L2size) const
{
  L2size -= 2;
  if (mDebugMode)
    LOG_ERROR("Reducing L2size to {}", L2size);
  return L2size >= 3;
}

void IterativePhaseCorrelation::ReduceL1ratio(double& L1ratio) const
{
  L1ratio -= mL1ratioStep;
  if (mDebugMode)
    LOG_ERROR("Reducing L1ratio to {:.2f}", L1ratio);
}

void IterativePhaseCorrelation::InitializePlots() const
{
  Plot2D::Reset("IPCgrayscale");
  Plot2D::SetColorMapType(QCPColorGradient::gpGrayscale);

  Plot2D::Reset("IPCcolormap");
  Plot2D::SetColorMapType(QCPColorGradient::gpJet);

  Plot1D::Reset("IPCshift");
  Plot1D::SetXlabel("reference shift");
  Plot1D::SetYlabel("calculated shift");
  Plot1D::SetYnames({"reference", "pixel", "subpixel", "ipc", "ipc opt"});
  Plot1D::SetPens(
      {QPen(Plot::blue, Plot::pt / 2, Qt::DashLine), QPen(Plot::black, Plot::pt / 2, Qt::DashLine), QPen(Plot::orange, Plot::pt), QPen(Plot::magenta, Plot::pt), QPen(Plot::green, Plot::pt)});
  Plot1D::SetLegendPosition(Plot1D::LegendPosition::BotRight);

  Plot1D::Reset("IPCshifterror");
  Plot1D::SetXlabel("reference shift");
  Plot1D::SetYlabel("pixel error");
  Plot1D::SetYnames({"reference", "pixel", "subpixel", "ipc", "ipc opt"});
  Plot1D::SetPens(
      {QPen(Plot::blue, Plot::pt / 2, Qt::DashLine), QPen(Plot::black, Plot::pt / 2, Qt::DashLine), QPen(Plot::orange, Plot::pt), QPen(Plot::magenta, Plot::pt), QPen(Plot::green, Plot::pt)});
  Plot1D::SetLegendPosition(Plot1D::LegendPosition::BotRight);
}

void IterativePhaseCorrelation::ShowDebugStuff() const
{
  InitializePlots();

  // window
  if (1)
  {
    Mat img = roicrop(loadImage("Resources/test.png"), 2048, 2048, mCols, mRows);
    Mat w, imgw;
    createHanningWindow(w, img.size(), CV_32F);
    multiply(img, w, imgw);
    Mat w0 = w.clone();
    Mat r0 = Mat::ones(w.size(), CV_32F);
    copyMakeBorder(w0, w0, mUpsampleCoeff, mUpsampleCoeff, mUpsampleCoeff, mUpsampleCoeff, BORDER_CONSTANT, Scalar::all(0));
    copyMakeBorder(r0, r0, mUpsampleCoeff, mUpsampleCoeff, mUpsampleCoeff, mUpsampleCoeff, BORDER_CONSTANT, Scalar::all(0));

    // Plot1D::Plot(GetIota(w0.cols, 1), {GetMidRow(r0), GetMidRow(w0)}, "1DWindows", "x", "window", {"Rect", "Hann"}, Plot::pens, mDebugDirectory + "/1DWindows.png");
    // Plot1D::Plot(GetIota(w0.cols, 1), {GetMidRow(Fourier::fftlogmagn(r0)), GetMidRow(Fourier::fftlogmagn(w0))}, "1DWindowsDFT", "fx", "log DFT", {"Rect", "Hann"}, Plot::pens, mDebugDirectory +
    // "/1DWindowsDFT.png");

    Plot2D::SetSavePath("IPCgrayscale", mDebugDirectory + "/2DImage.png");
    Plot2D::Plot("IPCgrayscale", img, true);

    Plot2D::SetSavePath("IPCgrayscale", mDebugDirectory + "/2DImageWindow.png");
    Plot2D::Plot("IPCgrayscale", imgw, true);

    // Plot2D::Plot(Fourier::fftlogmagn(r0), "2DWindowDFTR", "fx", "fy", "log DFT", 0, 1, 0, 1, 0, mDebugDirectory + "/2DWindowDFTR.png");
    // Plot2D::Plot(Fourier::fftlogmagn(w0), "2DWindowDFTH", "fx", "fy", "log DFT", 0, 1, 0, 1, 0, mDebugDirectory + "/2DWindowDFTH.png");
    // Plot2D::Plot(w, "2DWindow", "x", "y", "window", 0, 1, 0, 1, 0, mDebugDirectory + "/2DWindow.png");
    // Plot2D::Plot(Fourier::fftlogmagn(img), "2DImageDFT", "fx", "fy", "log DFT", 0, 1, 0, 1, 0, mDebugDirectory + "/2DImageDFT.png");
    // Plot2D::Plot(Fourier::fftlogmagn(imgw), "2DImageWindowDFT", "fx", "fy", "log DFT", 0, 1, 0, 1, 0, mDebugDirectory + "/2DImageWindowDFT.png");
  }

  // bandpass
  if (0)
  {
    Mat bpR = Mat::zeros(mRows, mCols, CV_32F);
    Mat bpG = Mat::zeros(mRows, mCols, CV_32F);
    for (int r = 0; r < mRows; ++r)
    {
      for (int c = 0; c < mCols; ++c)
      {
        bpR.at<float>(r, c) = BandpassREquation(r, c);

        if (mBandpassL <= 0 && mBandpassH < 1)
          bpG.at<float>(r, c) = LowpassEquation(r, c);
        else if (mBandpassL > 0 && mBandpassH >= 1)
          bpG.at<float>(r, c) = HighpassEquation(r, c);
        else if (mBandpassL > 0 && mBandpassH < 1)
          bpG.at<float>(r, c) = BandpassGEquation(r, c);
      }
    }

    if (mBandpassL > 0 && mBandpassH < 1)
      normalize(bpG, bpG, 0.0, 1.0, NORM_MINMAX);

    Mat bpR0, bpG0;
    copyMakeBorder(bpR, bpR0, mUpsampleCoeff, mUpsampleCoeff, mUpsampleCoeff, mUpsampleCoeff, BORDER_CONSTANT, Scalar::all(0));
    copyMakeBorder(bpG, bpG0, mUpsampleCoeff, mUpsampleCoeff, mUpsampleCoeff, mUpsampleCoeff, BORDER_CONSTANT, Scalar::all(0));

    // Plot1D::Plot(GetIota(bpR0.cols, 1), {GetMidRow(bpR0), GetMidRow(bpG0)}, "b0", "x", "filter", {"Rect", "Gauss"}, Plot::pens, mDebugDirectory + "/1DBandpass.png");
    // Plot1D::Plot(GetIota(bpR0.cols, 1), {GetMidRow(Fourier::ifftlogmagn(bpR0)), GetMidRow(Fourier::ifftlogmagn(bpG0))}, "b1", "fx", "log IDFT", {"Rect", "Gauss"}, Plot::pens, mDebugDirectory +
    // "/1DBandpassIDFT.png"); Plot2D::Plot(bpR, "b2", "x", "y", "filter", 0, 1, 0, 1, 0, mDebugDirectory + "/2DBandpassR.png"); Plot2D::Plot(bpG, "b3", "x", "y", "filter", 0, 1, 0, 1, 0,
    // mDebugDirectory + "/2DBandpassG.png"); Plot2D::Plot(Fourier::ifftlogmagn(bpR0, 10), "b4", "fx", "fy", "log IDFT", 0, 1, 0, 1, 0, mDebugDirectory + "/2DBandpassRIDFT.png");
    // Plot2D::Plot(Fourier::ifftlogmagn(bpG0, 10), "b5", "fx", "fy", "log IDFT", 0, 1, 0, 1, 0, mDebugDirectory + "/2DBandpassGIDFT.png");
  }

  // bandpass image ringing
  if (0)
  {
    Mat img = roicrop(loadImage("Resources/test.png"), 4098 / 2, 4098 / 2, mCols, mRows);
    Mat fftR = Fourier::fft(img);
    Mat fftG = Fourier::fft(img);
    Mat filterR = Mat::zeros(img.size(), CV_32F);
    Mat filterG = Mat::zeros(img.size(), CV_32F);

    for (int r = 0; r < mRows; ++r)
    {
      for (int c = 0; c < mCols; ++c)
      {
        filterR.at<float>(r, c) = BandpassREquation(r, c);

        if (mBandpassL <= 0 && mBandpassH < 1)
          filterG.at<float>(r, c) = LowpassEquation(r, c);
        else if (mBandpassL > 0 && mBandpassH >= 1)
          filterG.at<float>(r, c) = HighpassEquation(r, c);
        else if (mBandpassL > 0 && mBandpassH < 1)
          filterG.at<float>(r, c) = BandpassGEquation(r, c);
      }
    }

    Fourier::ifftshift(filterR);
    Fourier::ifftshift(filterG);

    Mat filterRc = Fourier::dupchansc(filterR);
    Mat filterGc = Fourier::dupchansc(filterG);

    multiply(fftR, filterRc, fftR);
    multiply(fftG, filterGc, fftG);

    Mat imgfR = Fourier::ifft(fftR);
    Mat imgfG = Fourier::ifft(fftG);

    normalize(imgfR, imgfR, 0.0, 1.0, NORM_MINMAX);
    normalize(imgfG, imgfG, 0.0, 1.0, NORM_MINMAX);

    Plot2D::SetSavePath("IPCgrayscale", mDebugDirectory + "/2DBandpassImageR.png");
    Plot2D::Plot("IPCgrayscale", imgfR);

    Plot2D::SetSavePath("IPCgrayscale", mDebugDirectory + "/2DBandpassImageG.png");
    Plot2D::Plot("IPCgrayscale", imgfG);
  }

  // 2 pic
  if (0)
  {
    Point2f rawshift(rand11() * 0.25 * mCols, rand11() * 0.25 * mRows);
    Mat image1 = roicrop(loadImage("Resources/test.png"), 4096 / 2, 4096 / 2, mCols, mRows);
    Mat image2 = roicrop(loadImage("Resources/test.png"), 4096 / 2 - rawshift.x, 4096 / 2 - rawshift.y, mCols, mRows);

    if (1)
    {
      double noiseStdev = 0.03;
      Mat noise1 = Mat::zeros(image1.rows, image1.cols, CV_32F);
      Mat noise2 = Mat::zeros(image2.rows, image2.cols, CV_32F);
      randn(noise1, 0, noiseStdev);
      randn(noise2, 0, noiseStdev);
      image1 += noise1;
      image2 += noise2;
    }

    SetDebugMode(true);
    auto ipcshift = Calculate(image1, image2);
    SetDebugMode(false);

    LOG_INFO("Input raw shift = {}", rawshift);
    LOG_INFO("Resulting shift = {}", ipcshift);
    LOG_INFO("Resulting accuracy = {}", ipcshift - rawshift);
  }

  LOG_INFO("IPC debug stuff shown");
}

void IterativePhaseCorrelation::Optimize(const std::string& trainingImagesDirectory, const std::string& validationImagesDirectory, float maxShift, float noiseStdev, int itersPerImage,
                                         double validationRatio, int populationSize)
try
{
  LOG_FUNCTION("IPC optimization");

  if (itersPerImage < 1)
    throw std::runtime_error(fmt::format("Invalid iters per image ({})", itersPerImage));
  if (maxShift <= 0)
    throw std::runtime_error(fmt::format("Invalid max shift ({})", maxShift));
  if (noiseStdev < 0)
    throw std::runtime_error(fmt::format("Invalid noise stdev ({})", noiseStdev));

  InitializePlots();
  const auto trainingImages = LoadImages(trainingImagesDirectory);
  const auto validationImages = LoadImages(validationImagesDirectory);

  if (trainingImages.empty())
    throw std::runtime_error("Empty training images vector");

  const auto trainingImagePairs = CreateImagePairs(trainingImages, maxShift, itersPerImage, noiseStdev);
  const auto validationImagePairs = CreateImagePairs(validationImages, maxShift, validationRatio * itersPerImage, noiseStdev);

  LOG_INFO("Running Iterative Phase Correlation parameter optimization on a set of {}/{} training/validation images with {}/{} image pairs - each generation, {} {}x{} IPCshifts will be calculated",
           trainingImages.size(), validationImages.size(), trainingImagePairs.size(), validationImagePairs.size(), populationSize * trainingImagePairs.size() + validationImagePairs.size(), mCols,
           mRows);

  // before
  const auto referenceShifts = GetReferenceShifts(trainingImagePairs);
  const auto shiftsPixel = GetPixelShifts(trainingImagePairs);
  const auto shiftsNonit = GetNonIterativeShifts(trainingImagePairs);
  const auto shiftsBefore = GetShifts(trainingImagePairs);
  const auto objBefore = GetAverageAccuracy(referenceShifts, shiftsBefore);
  ShowOptimizationPlots(referenceShifts, shiftsPixel, shiftsNonit, shiftsBefore, {});

  // opt
  const auto obj = CreateObjectiveFunction(trainingImagePairs);
  const auto valid = CreateObjectiveFunction(validationImagePairs);
  // PlotObjectiveFunctionLandscape(obj);
  const auto optimalParameters = CalculateOptimalParameters(obj, valid, populationSize);
  if (optimalParameters.empty())
    throw std::runtime_error("Optimization failed");
  ApplyOptimalParameters(optimalParameters);

  // after
  const auto shiftsAfter = GetShifts(trainingImagePairs);
  const auto objAfter = GetAverageAccuracy(referenceShifts, shiftsAfter);
  ShowOptimizationPlots(referenceShifts, shiftsPixel, shiftsNonit, shiftsBefore, shiftsAfter);
  LOG_INFO("Average pixel accuracy improvement: {:.3f} -> {:.3f} ({}%)", objBefore, objAfter, static_cast<int>((objBefore - objAfter) / objBefore * 100));

  LOG_SUCCESS("Iterative Phase Correlation parameter optimization successful");
}
catch (const std::exception& e)
{
  LOG_ERROR("An error occured during Iterative Phase Correlation parameter optimization: {}", e.what());
}
catch (...)
{
  LOG_ERROR("An error occured during Iterative Phase Correlation parameter optimization");
}

std::vector<Mat> IterativePhaseCorrelation::LoadImages(const std::string& imagesDirectory) const
{
  LOG_INFO("Loading images from '{}'...", imagesDirectory);

  if (!std::filesystem::is_directory(imagesDirectory))
    throw std::runtime_error(fmt::format("Directory '{}' is not a valid directory", imagesDirectory));

  std::vector<Mat> images;
  for (const auto& entry : std::filesystem::directory_iterator(imagesDirectory))
  {
    const std::string path = entry.path().string();

    if (!IsImage(path))
    {
      LOG_DEBUG("Directory contains a non-image file {}", path);
      continue;
    }

    // crop the input image - good for solar images, omits the black borders
    static constexpr float cropFocusRatio = 0.5;
    auto image = loadImage(path);
    image = roicropmid(image, cropFocusRatio * image.cols, cropFocusRatio * image.rows);
    images.push_back(image);
    LOG_DEBUG("Loaded image {}", path);
  }
  return images;
}

std::vector<std::tuple<Mat, Mat, Point2f>> IterativePhaseCorrelation::CreateImagePairs(const std::vector<Mat>& images, double maxShift, int itersPerImage, double noiseStdev) const
{
  for (const auto& image : images)
  {
    if (image.rows < mRows + maxShift || image.cols < mCols + maxShift)
      throw std::runtime_error(fmt::format("Could not optimize IPC parameters - input image is too small for specified IPC window size & max shift ratio ([{},{}] < [{},{}])", image.rows, image.cols,
                                           mRows + maxShift, mCols + maxShift));
  }

  std::vector<std::tuple<Mat, Mat, Point2f>> imagePairs;
  imagePairs.reserve(images.size() * itersPerImage);

  for (const auto& image : images)
  {
    for (int i = 0; i < itersPerImage; ++i)
    {
      // random shift from a random point
      Point2f shift(rand11() * maxShift, rand11() * maxShift);
      LOG_DEBUG("Creating {}x{} image pair shifted by [{:.2f}, {:.2f}] px", mCols, mRows, shift.x, shift.y);
      Mat T = (Mat_<float>(2, 3) << 1., 0., shift.x, 0., 1., shift.y);
      Mat imageS;
      warpAffine(image, imageS, T, image.size());
      Point2i point(clamp(rand01() * image.cols, mCols, image.cols - mCols), clamp(rand01() * image.rows, mRows, image.rows - mRows));
      Mat image1 = roicrop(image, point.x, point.y, mCols, mRows);
      Mat image2 = roicrop(imageS, point.x, point.y, mCols, mRows);

      ConvertToUnitFloat(image1);
      ConvertToUnitFloat(image2);

      AddNoise(image1, noiseStdev);
      AddNoise(image2, noiseStdev);

      imagePairs.push_back({image1, image2, shift});

      if (mDebugMode)
      {
        Mat hcct;
        hconcat(image1, image2, hcct);
        showimg(hcct, fmt::format("IPC optimization pair {}", i));
      }
    }
  }
  return imagePairs;
}

void IterativePhaseCorrelation::AddNoise(Mat& image, double noiseStdev) const
{
  if (noiseStdev <= 0)
    return;

  Mat noise = Mat::zeros(image.rows, image.cols, CV_32F);
  randn(noise, 0, noiseStdev);
  image += noise;
}

const std::function<double(const std::vector<double>&)> IterativePhaseCorrelation::CreateObjectiveFunction(const std::vector<std::tuple<Mat, Mat, Point2f>>& imagePairs) const
{
  return [&](const std::vector<double>& params) {
    IterativePhaseCorrelation ipc(mRows, mCols);
    ipc.SetBandpassType(static_cast<BandpassType>((int)params[BandpassTypeParameter]));
    ipc.SetBandpassParameters(params[BandpassLParameter], params[BandpassHParameter]);
    ipc.SetInterpolationType(static_cast<InterpolationType>((int)params[InterpolationTypeParameter]));
    ipc.SetWindowType(static_cast<WindowType>((int)params[WindowTypeParameter]));
    ipc.SetUpsampleCoeff(params[UpsampleCoeffParameter]);
    ipc.SetL1ratio(params[L1ratioParameter]);

    if (std::floor(ipc.GetL2size() * ipc.GetUpsampleCoeff() * ipc.GetL1ratio()) < 3)
      return std::numeric_limits<double>::max();

    double avgerror = 0;
    for (const auto& [image1, image2, shift] : imagePairs)
    {
      const auto error = ipc.Calculate(image1, image2) - shift;
      avgerror += sqrt(error.x * error.x + error.y * error.y);
    }
    return avgerror / imagePairs.size();
  };
}

std::vector<double> IterativePhaseCorrelation::CalculateOptimalParameters(const std::function<double(const std::vector<double>&)>& obj, const std::function<double(const std::vector<double>&)>& valid,
                                                                          int populationSize) const
{
  Evolution evo(ParameterCount);
  evo.mNP = populationSize;
  evo.mMutStrat = Evolution::RAND1;
  evo.SetParameterNames({"BPT", "BPL", "BPH", "ITPT", "WINT", "UC", "L1R"});
  evo.mLB = {0, -.5, 0.0, 0, 0, 11, 0.1};
  evo.mUB = {2, 1.0, 1.5, 3, 2, 51, 0.8};
  return evo.Optimize(obj, valid);
}

void IterativePhaseCorrelation::ApplyOptimalParameters(const std::vector<double>& optimalParameters)
{
  if (optimalParameters.size() != ParameterCount)
    throw std::runtime_error("Cannot apply optimal parameters - wrong parameter count");

  SetBandpassType(static_cast<BandpassType>((int)optimalParameters[BandpassTypeParameter]));
  SetBandpassParameters(optimalParameters[BandpassLParameter], optimalParameters[BandpassHParameter]);
  SetInterpolationType(static_cast<InterpolationType>((int)optimalParameters[InterpolationTypeParameter]));
  SetWindowType(static_cast<WindowType>((int)optimalParameters[WindowTypeParameter]));
  SetUpsampleCoeff(optimalParameters[UpsampleCoeffParameter]);
  SetL1ratio(optimalParameters[L1ratioParameter]);

  LOG_INFO("Final IPC BandpassType: {}",
           BandpassType2String(static_cast<BandpassType>((int)optimalParameters[BandpassTypeParameter]), optimalParameters[BandpassLParameter], optimalParameters[BandpassHParameter]));
  LOG_INFO("Final IPC BandpassL: {:.2f}", optimalParameters[BandpassLParameter]);
  LOG_INFO("Final IPC BandpassH: {:.2f}", optimalParameters[BandpassHParameter]);
  LOG_INFO("Final IPC InterpolationType: {}", InterpolationType2String(static_cast<InterpolationType>((int)optimalParameters[InterpolationTypeParameter])));
  LOG_INFO("Final IPC WindowType: {}", WindowType2String(static_cast<WindowType>((int)optimalParameters[WindowTypeParameter])));
  LOG_INFO("Final IPC UpsampleCoeff: {}", static_cast<int>(optimalParameters[UpsampleCoeffParameter]));
  LOG_INFO("Final IPC L1ratio: {:.2f}", optimalParameters[L1ratioParameter]);
}

std::string IterativePhaseCorrelation::BandpassType2String(BandpassType type, double bandpassL, double bandpassH) const
{
  switch (type)
  {
  case BandpassType::Rectangular:
    if (mBandpassL <= 0 && mBandpassH < 1)
      return "Rectangular low pass";
    else if (mBandpassL > 0 && mBandpassH >= 1)
      return "Rectangular high pass";
    else if (mBandpassL > 0 && mBandpassH < 1)
      return "Rectangular band pass";
    else if (mBandpassL <= 0 && mBandpassH >= 1)
      return "Rectangular all pass";

  case BandpassType::Gaussian:
    if (mBandpassL <= 0 && mBandpassH < 1)
      return "Gaussian low pass";
    else if (mBandpassL > 0 && mBandpassH >= 1)
      return "Gaussian high pass";
    else if (mBandpassL > 0 && mBandpassH < 1)
      return "Gausian band pass";
    else if (mBandpassL <= 0 && mBandpassH >= 1)
      return "Gaussian all pass";
  }
  return "Unknown";
}

std::string IterativePhaseCorrelation::WindowType2String(WindowType type) const
{
  switch (type)
  {
  case WindowType::Rectangular:
    return "Rectangular";
  case WindowType::Hann:
    return "Hann";
  }
  return "Unknown";
}

std::string IterativePhaseCorrelation::InterpolationType2String(InterpolationType type) const
{
  switch (type)
  {
  case InterpolationType::NearestNeighbor:
    return "NearestNeighbor";
  case InterpolationType::Linear:
    return "Linear";
  case InterpolationType::Cubic:
    return "Cubic";
  }
  return "Unknown";
}

void IterativePhaseCorrelation::ShowOptimizationPlots(const std::vector<Point2f>& shiftsReference, const std::vector<Point2f>& shiftsPixel, const std::vector<Point2f>& shiftsNonit,
                                                      const std::vector<Point2f>& shiftsBefore, const std::vector<Point2f>& shiftsAfter) const
{
  std::vector<double> shiftsXReference, shiftsXReferenceError;
  std::vector<double> shiftsXPixel, shiftsXPixelError;
  std::vector<double> shiftsXNonit, shiftsXNonitError;
  std::vector<double> shiftsXBefore, shiftsXBeforeError;
  std::vector<double> shiftsXAfter, shiftsXAfterError;

  for (int i = 0; i < shiftsReference.size(); ++i)
  {
    const auto& referenceShift = shiftsReference[i];
    const auto& shiftPixel = shiftsPixel[i];
    const auto& shiftNonit = shiftsNonit[i];
    const auto& shiftBefore = shiftsBefore[i];

    shiftsXReference.push_back(referenceShift.x);
    shiftsXReferenceError.push_back(0.0);

    shiftsXPixel.push_back(shiftPixel.x);
    shiftsXNonit.push_back(shiftNonit.x);
    shiftsXBefore.push_back(shiftBefore.x);

    shiftsXPixelError.push_back(shiftPixel.x - referenceShift.x);
    shiftsXNonitError.push_back(shiftNonit.x - referenceShift.x);
    shiftsXBeforeError.push_back(shiftBefore.x - referenceShift.x);

    if (i < shiftsAfter.size())
    {
      const auto& shiftAfter = shiftsAfter[i];

      shiftsXAfter.push_back(shiftAfter.x);
      shiftsXAfterError.push_back(shiftAfter.x - referenceShift.x);
    }
  }

  Plot1D::Plot("IPCshift", shiftsXReference, {shiftsXReference, shiftsXPixel, shiftsXNonit, shiftsXBefore, shiftsXAfter}, false);
  Plot1D::Plot("IPCshifterror", shiftsXReference, {shiftsXReferenceError, shiftsXPixelError, shiftsXNonitError, shiftsXBeforeError, shiftsXAfterError}, false);
}

std::vector<Point2f> IterativePhaseCorrelation::GetShifts(const std::vector<std::tuple<Mat, Mat, Point2f>>& imagePairs) const
{
  std::vector<Point2f> out;
  out.reserve(imagePairs.size());

  for (const auto& [image1, image2, referenceShift] : imagePairs)
    out.push_back(Calculate(image1, image2));

  return out;
}

std::vector<Point2f> IterativePhaseCorrelation::GetNonIterativeShifts(const std::vector<std::tuple<Mat, Mat, Point2f>>& imagePairs) const
{
  std::vector<Point2f> out;
  out.reserve(imagePairs.size());

  mAccuracyType = AccuracyType::Subpixel;
  for (const auto& [image1, image2, referenceShift] : imagePairs)
    out.push_back(Calculate(image1, image2));
  mAccuracyType = AccuracyType::SubpixelIterative;

  return out;
}

std::vector<Point2f> IterativePhaseCorrelation::GetPixelShifts(const std::vector<std::tuple<Mat, Mat, Point2f>>& imagePairs) const
{
  std::vector<Point2f> out;
  out.reserve(imagePairs.size());

  mAccuracyType = AccuracyType::Pixel;
  for (const auto& [image1, image2, referenceShift] : imagePairs)
    out.push_back(Calculate(image1, image2));
  mAccuracyType = AccuracyType::SubpixelIterative;

  return out;
}

std::vector<Point2f> IterativePhaseCorrelation::GetReferenceShifts(const std::vector<std::tuple<Mat, Mat, Point2f>>& imagePairs) const
{
  std::vector<Point2f> out;
  out.reserve(imagePairs.size());

  for (const auto& [image1, image2, referenceShift] : imagePairs)
    out.push_back(referenceShift);

  return out;
}

double IterativePhaseCorrelation::GetAverageAccuracy(const std::vector<Point2f>& shiftsReference, const std::vector<Point2f>& shifts) const
{
  if (shiftsReference.size() != shifts.size())
    throw std::runtime_error("Reference shift vector has different size than calculated shift vector");

  double avgerror = 0;
  for (int i = 0; i < shifts.size(); ++i)
  {
    const auto error = shifts[i] - shiftsReference[i];
    avgerror += sqrt(error.x * error.x + error.y * error.y);
  }
  return avgerror / shifts.size();
}

double IterativePhaseCorrelation::GetFractionalPart(double x)
{
  return abs(x - std::floor(x));
}

void IterativePhaseCorrelation::PlotObjectiveFunctionLandscape(const std::function<double(const std::vector<double>&)>& obj)
{
  LOG_FUNCTION("PlotObjectiveFunctionLandscape");
  const int rows = 17;
  const int cols = 17;
  Mat landscape(rows, cols, CV_32F);
  const double parameterXmin = 0.1;
  const double parameterXmax = 0.9;
  const double parameterYmin = 0.0;
  const double parameterYmax = 1.0;

#pragma omp parallel for
  for (int r = 0; r < rows; ++r)
  {
    LOG_INFO("Calculating objective function landscape ( {} / {} )", r + 1, rows);
    for (int c = 0; c < cols; ++c)
    {
      std::vector<double> parameters(ParameterCount);
      // default
      parameters[BandpassTypeParameter] = static_cast<int>(mBandpassType);
      parameters[BandpassLParameter] = mBandpassL;
      parameters[BandpassHParameter] = mBandpassH;
      parameters[InterpolationTypeParameter] = static_cast<int>(mInterpolationType);
      parameters[WindowTypeParameter] = static_cast<int>(mWindowType);
      parameters[UpsampleCoeffParameter] = mUpsampleCoeff;
      parameters[L1ratioParameter] = mL1ratio;

      // modified
      parameters[L1ratioParameter] = parameterXmin + (double)c / (cols - 1) * (parameterXmax - parameterXmin);
      parameters[BandpassHParameter] = parameterYmin + (double)r / (rows - 1) * (parameterYmax - parameterYmin);

      landscape.at<float>(r, c) = log(obj(parameters));
    }
  }

  Plot2D::Plot("IPCcolormap", landscape);
}
