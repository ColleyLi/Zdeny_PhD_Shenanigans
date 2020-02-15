#pragma once
#include "Core/functionsBaseSTL.h"
#include "Core/functionsBaseCV.h"
#include "Filtering/filtering.h"

using namespace std;
using namespace cv;

//.fits parameters
constexpr int lineBYTEcnt = 80;
constexpr int linesMultiplier = 36;
enum class fitsType : char { HMI, AIA, SECCHI };

struct FitsParams
{
	double fitsMidX = 0;
	double fitsMidY = 0;
	double R = 0;
	double theta0 = 0;
	bool succload = 0;
};

inline void swapbytes(char* input, unsigned length)
{
	for (int i = 0; i < length; i += 2)
	{
		char temp = std::move(input[i]);
		input[i] = std::move(input[i + 1]);
		input[i + 1] = std::move(temp);
	}
}

inline std::tuple<Mat, FitsParams> loadfits(std::string path)
{
	FitsParams params;
	ifstream streamIN(path, ios::binary | ios::in);
	if (!streamIN)
	{
		cout << "<loadfits> Cannot load file '" << path << "'- file does not exist dude!" << endl;
		Mat shit;
		params.succload = false;
		return std::make_tuple(shit, params);
	}
	else
	{
		bool ENDfound = false;
		char cline[lineBYTEcnt];
		int fitsSize, fitsMid, fitsSize2, angle, linecnt = 0;
		double pixelarcsec;

		while (!streamIN.eof())
		{
			streamIN.read(&cline[0], lineBYTEcnt);
			linecnt++;
			string sline(cline);

			if (sline.find("NAXIS1") != std::string::npos)
			{
				std::size_t pos = sline.find("= ");
				std::string snum = sline.substr(pos + 2);
				fitsSize = stoi(snum);
				fitsMid = fitsSize / 2;
				fitsSize2 = fitsSize * fitsSize;
			}
			else if (sline.find("CRPIX1") != std::string::npos)
			{
				std::size_t pos = sline.find("= ");
				std::string snum = sline.substr(pos + 2);
				params.fitsMidX = stod(snum) - 1.;//Nasa index od 1
			}
			else if (sline.find("CRPIX2") != std::string::npos)
			{
				std::size_t pos = sline.find("= ");
				std::string snum = sline.substr(pos + 2);
				params.fitsMidY = stod(snum) - 1.;//Nasa index od 1
			}
			else if (sline.find("CDELT1") != std::string::npos)
			{
				std::size_t pos = sline.find("= ");
				std::string snum = sline.substr(pos + 2);
				pixelarcsec = stod(snum);
			}
			else if (sline.find("RSUN_OBS") != std::string::npos)
			{
				std::size_t pos = sline.find("= ");
				std::string snum = sline.substr(pos + 2);
				params.R = stod(snum);
				params.R /= pixelarcsec;
			}
			else if (sline.find("RSUN") != std::string::npos)
			{
				std::size_t pos = sline.find("= ");
				std::string snum = sline.substr(pos + 2);
				params.R = stod(snum);
			}
			else if (sline.find("CRLT_OBS") != std::string::npos)
			{
				std::size_t pos = sline.find("= ");
				std::string snum = sline.substr(pos + 2);
				params.theta0 = stod(snum) / (360. / 2. / PI);
			}
			else if (sline.find("END                        ") != std::string::npos)
			{
				ENDfound = true;
			}

			if (ENDfound && (linecnt % linesMultiplier == 0)) break;
		}

		//opencv integration 
		Mat mat(fitsSize, fitsSize, CV_16UC1);

		streamIN.read((char*)mat.data, fitsSize2 * 2);
		swapbytes((char*)mat.data, fitsSize2 * 2);
		short* s16 = (short*)mat.data;
		ushort* us16 = (ushort*)mat.data;

		//new korekce
		for (int i = 0; i < fitsSize2; i++)
		{
			int px = (int)(s16[i]);
			px += 32768;
			us16[i] = px;
		}

		normalize(mat, mat, 0, 65535, CV_MINMAX);
		Point2f pt(fitsMid, fitsMid);
		Mat r = getRotationMatrix2D(pt, 90, 1.0);
		warpAffine(mat, mat, r, cv::Size(fitsSize, fitsSize));
		transpose(mat, mat);
		params.succload = true;
		return std::make_tuple(mat, params);
	}
}

class FitsImage
{
public:

	FitsImage(std::string path)
	{
		data = loadfits(path);
	}

	Mat image() const
	{
		return std::get<0>(data);
	}

	FitsParams params() const
	{
		return std::get<1>(data);
	}

private:
	std::tuple<Mat, FitsParams> data;
};

struct FITStime
{
private:
	int startyear;
	int startmonth;
	int startday;
	int starthour;
	int startminute;
	int startsecond;

	int year;
	int month;
	int day;
	int hour;
	int minute;
	int second;

	std::string dirpath;

	std::string yearS;
	std::string monthS;
	std::string dayS;
	std::string hourS;
	std::string minuteS;
	std::string secondS;

	void timeToStringS()
	{
		yearS = to_string(year);
		if (month < 10)
		{
			monthS = "0" + to_string(month);
		}
		else
		{
			monthS = to_string(month);
		}

		if (day < 10)
		{
			dayS = "0" + to_string(day);
		}
		else
		{
			dayS = to_string(day);
		}

		if (hour < 10)
		{
			hourS = "0" + to_string(hour);
		}
		else
		{
			hourS = to_string(hour);
		}

		if (minute < 10)
		{
			minuteS = "0" + to_string(minute);
		}
		else
		{
			minuteS = to_string(minute);
		}

		if (second < 10)
		{
			secondS = "0" + to_string(second);
		}
		else
		{
			secondS = to_string(second);
		}
	}

public:
	void resetTime()
	{
		year = startyear;
		month = startmonth;
		day = startday;
		hour = starthour;
		minute = startminute;
		second = startsecond;
	}

	FITStime(std::string dirpathh, int yearr, int monthh, int dayy, int hourr, int minutee, int secondd)
	{
		dirpath = dirpathh;
		startyear = yearr;
		startmonth = monthh;
		startday = dayy;
		starthour = hourr;
		startminute = minutee;
		startsecond = secondd;
		resetTime();
		advanceTime(0);
		timeToStringS();
	}

	std::string path()
	{
		timeToStringS();
		return dirpath + yearS + "_" + monthS + "_" + dayS + "__" + hourS + "_" + minuteS + "_" + secondS + "__CONT.fits";
	}

	void advanceTime(int deltasec)
	{
		second += deltasec;
		int monthdays;
		if (month <= 7)//first seven months
		{
			if (month % 2 == 0)
				monthdays = 30;
			else
				monthdays = 31;
		}
		else//last 5 months
		{
			if (month % 2 == 0)
				monthdays = 31;
			else
				monthdays = 30;
		}
		if (month == 2)
			monthdays = 28;//february
		if ((month == 2) && (year % 4 == 0))
			monthdays = 29;//leap year fkn february

		//plus
		if (second >= 60)
		{
			minute += std::floor((double)second / 60.0);
			second %= 60;
		}
		if (minute >= 60)
		{
			hour += std::floor((double)minute / 60.0);
			minute %= 60;
		}
		if (hour >= 24)
		{
			day += std::floor((double)hour / 24.0);
			hour %= 24;
		}
		if (day >= monthdays)
		{
			month += std::floor((double)day / monthdays);
			day %= monthdays;
		}
		//minus
		if (second < 0)
		{
			minute += std::floor((double)second / 60.0);
			second = 60 + second % 60;
		}
		if (minute < 0)
		{
			hour += std::floor((double)minute / 60.0);
			minute = 60 + minute % 60;
		}
		if (hour < 0)
		{
			day += std::floor((double)hour / 24.0);
			hour = 24 + hour % 24;
		}
		if (day < 0)
		{
			month += std::floor((double)day / monthdays);
			day = monthdays + day % monthdays;
		}
	}
};

Mat loadfits(std::string path, FitsParams& params);

void generateFitsDownloadUrlsAndCreateFile(int delta, int step, int pics, string urlmain);

void checkFitsDownloadsAndCreateFile(int delta, int step, int pics, string urlmain, string pathMasterIn);

void loadImageDebug(Mat& activeimg, double gamaa, bool colorr, double quanBot, double quanTop);

inline Mat loadImage(std::string path)
{
	Mat result;
	if (path.find(".fits") != std::string::npos || path.find(".fts") != std::string::npos)
	{
		FitsParams params;
		result = loadfits(path, params);
	}
	else
	{
		result = imread(path, IMREAD_ANYDEPTH);
		result.convertTo(result, CV_16U);
		normalize(result, result, 0, 65535, CV_MINMAX);
	}
	return result;
}