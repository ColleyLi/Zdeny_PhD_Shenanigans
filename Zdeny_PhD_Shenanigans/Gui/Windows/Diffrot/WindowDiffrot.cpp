#include "stdafx.h"
#include "WindowDiffrot.h"
#include "Astrophysics/diffrot.h"
#include "Astrophysics/diffrotFileIO.h"
#include "Optimization/Evolution.h"

WindowDiffrot::WindowDiffrot(QWidget* parent, Globals* globals) : QMainWindow(parent), globals(globals)
{
  ui.setupUi(this);
  connect(ui.pushButton, SIGNAL(clicked()), this, SLOT(calculateDiffrot()));
  connect(ui.pushButton_2, SIGNAL(clicked()), this, SLOT(showResults()));
  connect(ui.pushButton_3, SIGNAL(clicked()), this, SLOT(showIPC()));
  connect(ui.pushButton_5, SIGNAL(clicked()), this, SLOT(checkDiskShifts()));
  connect(ui.pushButton_6, SIGNAL(clicked()), this, SLOT(saveDiffrot()));
  connect(ui.pushButton_7, SIGNAL(clicked()), this, SLOT(loadDiffrot()));
  connect(ui.pushButton_8, SIGNAL(clicked()), this, SLOT(optimizeDiffrot()));
  connect(ui.pushButton_9, SIGNAL(clicked()), this, SLOT(video()));
  connect(ui.pushButton_10, SIGNAL(clicked()), this, SLOT(movingPeak()));
}

void WindowDiffrot::calculateDiffrot()
{
  LOG_FUNCTION("CalculateDiffrot");
  FitsTime time = GetStartFitsTime();
  drres = calculateDiffrotProfile(*globals->IPC, time, drset);
  showResults();
  saveDiffrot();
}

void WindowDiffrot::showResults()
{
  drres.ShowResults(ui.lineEdit_20->text().toDouble(), ui.lineEdit_16->text().toDouble(), ui.lineEdit_18->text().toDouble(), ui.lineEdit_19->text().toDouble());
  LOG_SUCCESS("Differential rotation profile shown.");
}

void WindowDiffrot::showIPC()
{
  LOG_INFO("Showimg diffrot profile IPC landscape...");
  FitsParams params1, params2;
  IPCsettings set = *globals->IPCset;
  set.speak = IPCsettings::All;
  // set.save = true;
  FitsTime time = GetStartFitsTime();

  LOG_INFO("Loading file '" + time.path() + "'...");
  Mat pic1 = roicrop(loadfits(time.path(), params1), params1.fitsMidX, params1.fitsMidY, set.getcols(), set.getrows());

  time.advanceTime(ui.lineEdit_5->text().toDouble() * ui.lineEdit_8->text().toDouble());
  int predShift = 0;
  if (ui.checkBox_4->isChecked())
  {
    predShift = predictDiffrotShift(ui.lineEdit_5->text().toDouble(), ui.lineEdit_8->text().toDouble(), params1.R);
    LOG_DEBUG("Predicted shift used = {}", predShift);
  }

  LOG_INFO("Loading file '" + time.path() + "'...");
  Mat pic2 = roicrop(loadfits(time.path(), params1), params1.fitsMidX + predShift, params1.fitsMidY, set.getcols(), set.getrows());

  phasecorrel(pic1, pic2, set);
}

void WindowDiffrot::checkDiskShifts()
{
  LOG_INFO("Checking diffrot disk shifts...");
  FitsTime time = GetStartFitsTime();
  IPCsettings set = *globals->IPCset;

  int edgeN = 0.025 * 4096;
  int edgeS = 0.974 * 4096;
  int edgeW = 0.027 * 4096;
  int edgeE = 0.975 * 4096;
  int center = 0.5 * 4096;

  int pics = ui.lineEdit_7->text().toDouble();
  std::vector<double> shiftsN;
  std::vector<double> shiftsS;
  std::vector<double> shiftsW;
  std::vector<double> shiftsE;
  std::vector<double> shiftsFX;
  std::vector<double> shiftsFY;
  FitsImage pic1, pic2;
  int lag1, lag2;
  Mat picshow;

  for (int pic = 0; pic < pics; pic++)
  {
    LOG_DEBUG("{} / {} ...", pic + 1, pics);
    time.advanceTime((bool)pic * (ui.lineEdit_6->text().toDouble() - ui.lineEdit_5->text().toDouble()) * ui.lineEdit_8->text().toDouble());
    loadFitsFuzzy(pic1, time, lag1);
    time.advanceTime(ui.lineEdit_5->text().toDouble() * ui.lineEdit_8->text().toDouble());
    loadFitsFuzzy(pic2, time, lag2);

    if (!pic)
      picshow = pic1.image().clone();

    if (pic1.params().succload && pic2.params().succload)
    {
      shiftsN.push_back(phasecorrel(roicrop(pic1.image(), center, edgeN, set.getcols(), set.getrows()), roicrop(pic2.image(), center, edgeN, set.getcols(), set.getrows()), set).y);
      shiftsS.push_back(phasecorrel(roicrop(pic1.image(), center, edgeS, set.getcols(), set.getrows()), roicrop(pic2.image(), center, edgeS, set.getcols(), set.getrows()), set).y);
      shiftsW.push_back(phasecorrel(roicrop(pic1.image(), edgeW, center, set.getcols(), set.getrows()), roicrop(pic2.image(), edgeW, center, set.getcols(), set.getrows()), set).x);
      shiftsE.push_back(phasecorrel(roicrop(pic1.image(), edgeE, center, set.getcols(), set.getrows()), roicrop(pic2.image(), edgeE, center, set.getcols(), set.getrows()), set).x);
      shiftsFX.push_back(pic2.params().fitsMidX - pic1.params().fitsMidX);
      shiftsFY.push_back(pic2.params().fitsMidY - pic1.params().fitsMidY);
    }
  }

  LOG_INFO("<<<<<<<<<<<<<<<<<<   ABSIPC median   /   ABSFITS median   /   ABSDIFF median   >>>>>>>>>>>>>>>>>>>>>");
  LOG_INFO("Diffrot shifts N = {} / {} / {}", median(abs(shiftsN)), median(abs(shiftsFY)), median(abs(shiftsN - shiftsFY)));
  LOG_INFO("Diffrot shifts S = {} / {} / {}", median(abs(shiftsS)), median(abs(shiftsFY)), median(abs(shiftsS - shiftsFY)));
  LOG_INFO("Diffrot shifts W = {} / {} / {}", median(abs(shiftsW)), median(abs(shiftsFX)), median(abs(shiftsW - shiftsFX)));
  LOG_INFO("Diffrot shifts E = {} / {} / {}", median(abs(shiftsE)), median(abs(shiftsFX)), median(abs(shiftsE - shiftsFX)));

  std::vector<Mat> picsshow(4);
  picsshow[0] = roicrop(picshow, center, edgeN, set.getcols(), set.getrows());
  picsshow[3] = roicrop(picshow, center, edgeS, set.getcols(), set.getrows());
  picsshow[2] = roicrop(picshow, edgeW, center, set.getcols(), set.getrows());
  picsshow[1] = roicrop(picshow, edgeE, center, set.getcols(), set.getrows());
  showimg(picsshow, "pics");

  std::vector<double> iotam(shiftsFX.size());
  std::iota(iotam.begin(), iotam.end(), 0);
  iotam = (double)(ui.lineEdit_7->text().toDouble() - 1) * ui.lineEdit_6->text().toDouble() * 45 / 60 / 60 / 24 / (iotam.size() - 1) * iotam;
  // Plot1D::Plot(iotam, std::vector<std::vector<double>>{shiftsFX, shiftsW, shiftsE}, "shiftsX", "time [days]", "45sec shiftX [px]",
  // std::vector<std::string>{"shifts fits header X", "shifts IPC west edge", "shifts IPC east edge"});
  // Plot1D::Plot(iotam, std::vector<std::vector<double>>{shiftsFY, shiftsN, shiftsS}, "shiftsY", "time [days]", "45sec shiftY [px]",
  // std::vector<std::string>{"shifts fits header Y", "shifts IPC north edge", "shifts IPC south edge"});
}

void WindowDiffrot::saveDiffrot()
{
  SaveDiffrotResultsToFile(ui.lineEdit_9->text().toStdString(), ui.lineEdit_23->text().toStdString(), &drres, globals->IPC.get());
}

void WindowDiffrot::loadDiffrot()
{
  LoadDiffrotResultsFromFile(ui.lineEdit_24->text().toStdString(), &drres);
  drres.saveDir = ui.lineEdit_9->text().toStdString();
}

void WindowDiffrot::optimizeDiffrot()
{
  LOG_FUNCTION("OptimizeDiffrot");
  FitsTime starttime = GetStartFitsTime();

  try
  {
    auto f = [&](const std::vector<double>& args) {
      int winsize = std::floor(args[5]);
      int L2size = std::floor(args[2]);
      int upsampleCoeff = std::floor(args[6]);
      winsize = winsize % 2 ? winsize + 1 : winsize;
      L2size = L2size % 2 ? L2size : L2size + 1;
      upsampleCoeff = upsampleCoeff % 2 ? upsampleCoeff : upsampleCoeff + 1;
      IterativePhaseCorrelation ipc_opt(winsize, winsize, args[0], args[1]);
      ipc_opt.SetL2size(L2size);
      ipc_opt.SetUpsampleCoeff(upsampleCoeff);
      // ipc_opt.SetApplyBandpass(args[3] > 0 ? true : false);
      // ipc_opt.SetApplyWindow(args[4] > 0 ? true : false);
      // ipc_opt.SetInterpolationType(args[7] > 0 ? INTER_CUBIC : INTER_LINEAR);
      FitsTime time_opt = starttime;
      DiffrotSettings drset_opt = drset;
      drset_opt.speak = false;
      return calculateDiffrotProfile(ipc_opt, time_opt, drset_opt).GetError();
    };

    const int runs = 2;
    for (int run = 0; run < runs; ++run)
    {
      Evolution evo(8);
      evo.mNP = 50;
      evo.mMutStrat = Evolution::RAND1;
      evo.mLB = {0.0001, 0.0001, 5, -1, -1, 64, 5, -1};
      evo.mUB = {5, 500, 17, 1, 1, 350, 51, 1};
      evo.SetFileOutputDir("E:\\Zdeny_PhD_Shenanigans\\articles\\diffrot\\temp\\");
      evo.SetParameterNames({"BPL", "BPH", "L2", "+BP", "+HANN", "WSIZE", "UC", "+BICUBIC"});
      evo.SetOptimizationName(std::string("diffrot full") + " p" + to_string(drset.pics) + " s" + to_string(drset.sPic) + " y" + to_string(drset.ys));
      auto result = evo.Optimize(f);
      LOG_SUCCESS("Evolution run {}/{} result = {}", run + 1, runs, result);
    }
  }
  catch (...)
  {
    LOG_ERROR("Evolution optimization somehow failed");
  }
}

void WindowDiffrot::UpdateDrset()
{
  drset.pics = ui.lineEdit_7->text().toDouble();
  drset.ys = ui.lineEdit_2->text().toDouble();
  drset.sPic = ui.lineEdit_6->text().toDouble();
  drset.dPic = ui.lineEdit_5->text().toDouble();
  drset.vFov = ui.lineEdit_4->text().toDouble();
  drset.dSec = ui.lineEdit_8->text().toDouble();
  drset.medianFilter = ui.checkBox->isChecked();
  drset.movavgFilter = ui.checkBox_2->isChecked();
  drset.medianFilterSize = ui.lineEdit_21->text().toDouble();
  drset.movavgFilterSize = ui.lineEdit_22->text().toDouble();
  drset.visual = ui.checkBox_3->isChecked();
  drset.savepath = ui.lineEdit_9->text().toStdString();
  drset.sy = ui.lineEdit_25->text().toDouble();
  drset.pred = ui.checkBox_4->isChecked();
}

void WindowDiffrot::video()
{
  LOG_FUNCTION("Video");
  FitsTime time = GetStartFitsTime();

  drset.video = true;
  calculateDiffrotProfile(*globals->IPC, time, drset);
  drset.video = false;
}

void WindowDiffrot::movingPeak()
{
  LOG_FUNCTION("MovingPeak");
  FitsTime starttime = GetStartFitsTime();
  IPCsettings ipcset = *globals->IPCset;
  ipcset.save = true;
  ipcset.savedir = ui.lineEdit_9->text().toStdString();
  FitsImage pic1, pic2;
  int lag1, lag2;

  const int profiles = 1;
  const int sy = 0;
  const bool saveimgs = true;

  std::vector<double> dts;
  std::vector<std::vector<double>> shiftsX(profiles);

  for (int profile = 0; profile < profiles; ++profile)
  {
    LOG_DEBUG("profile {}/{} ...", profile + 1, profiles);

    for (int dpic = 0; dpic < drset.pics; ++dpic)
    {
      FitsTime time = starttime;
      time.advanceTime(profile * drset.dSec);
      loadFitsFuzzy(pic1, time, lag1);
      time.advanceTime(dpic * drset.dSec);
      loadFitsFuzzy(pic2, time, lag2);

      if (pic1.params().succload && pic2.params().succload)
      {
        Mat crop1 = roicrop(pic1.image(), pic1.params().fitsMidX, pic1.params().fitsMidY + sy, ipcset.getcols(), ipcset.getrows());
        Mat crop2 = roicrop(pic2.image(), pic2.params().fitsMidX, pic2.params().fitsMidY + sy, ipcset.getcols(), ipcset.getrows());
        auto shift = phasecorrel(std::move(crop1), std::move(crop2), ipcset, saveimgs);

        if (!profile)
          dts.push_back((double)dpic * drset.dSec / 60);

        shiftsX[profile].push_back(shift.x);
      }
    }
  }

  destroyAllWindows();

  // Plot1D::Plot(dts, shiftsX, "shiftsX", "time step [min]", "west-east image shift [px]", {}, Plot::pens, ipcset.savedir + "plot.png");
}

FitsTime WindowDiffrot::GetStartFitsTime()
{
  UpdateDrset();
  return FitsTime(ui.lineEdit_17->text().toStdString(), ui.lineEdit_10->text().toInt(), ui.lineEdit_11->text().toInt(), ui.lineEdit_12->text().toInt(), ui.lineEdit_13->text().toInt(),
                  ui.lineEdit_14->text().toInt(), ui.lineEdit_15->text().toInt());
}
