#pragma once
#include "stdafx.h"

struct IPlot1D
{
	// x - y
	inline virtual void plot( const std::vector<double> &y ) = 0;
	inline virtual void plot( const std::vector<double> &x, const std::vector<double> &y ) = 0;
	inline virtual void plot( const double x, const double y ) = 0;
	inline virtual void setAxisNames( std::string xlabel, std::string ylabel ) = 0;

	// x - y1/y2
	inline virtual void plot( const std::vector<double> &x, const std::vector<double> &y1, const std::vector<double> &y2 ) = 0;
	inline virtual void plot( const double x, const double y1, const double y2 ) = 0;
	inline virtual void setAxisNames( std::string xlabel, std::string ylabel1, std::string ylabel2 ) = 0;

	// x - y1/y2...yn
	inline virtual void plot( const std::vector<double> &x, const std::vector<std::vector<double>> &ys ) = 0;
	inline virtual void setAxisNames( std::string xlabel, std::string ylabel, std::vector<std::string> ylabels ) = 0;

	// universal stuff
	inline virtual void clear( bool second = false ) = 0;
	inline virtual void save( std::string path ) = 0;
};