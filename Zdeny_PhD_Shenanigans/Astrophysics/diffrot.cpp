#include "stdafx.h"
#include "diffrot.h"

DiffrotResults calculateDiffrotProfile( const IPCsettings &ipcset, FitsTime &time, DiffrotSettings drset, IPlot1D *plt1, IPlot1D *plt2 )
{
	int dy = drset.vFov / ( drset.ys - 1 );

	plt1->setAxisNames( "solar latitude [deg]", "horizontal plasma flow speed [rad/s]", std::vector<std::string> {"omegasX", "omegasXfit", "omegasXavgfit", "predicX"} );
	plt2->setAxisNames( "solar latitude [deg]", "horizontal px shift [px]", std::vector<std::string> {"shiftsX"} );

	std::vector<std::vector<double>> thetas2D;
	std::vector<std::vector<double>> omegasX2D;
	std::vector<double> shiftsX( drset.ys );
	std::vector<double> thetas( drset.ys );
	std::vector<double> omegasX( drset.ys );
	//std::vector<double> omegasXavg(drset.ys);

	std::vector<double> omegasXfit( drset.ys );
	std::vector<double> omegasXavgfit( drset.ys );
	std::vector<double> predicX( drset.ys );
	std::vector<double> iotam( drset.ys );
	std::iota( iotam.begin(), iotam.end(), 0 );

	FitsImage pic1, pic2;

	for ( int pic = 0; pic < drset.pics; pic++ )
	{
		time.advanceTime( ( bool )pic * drset.dSec * ( drset.sPic - drset.dPic ) );
		loadFitsFuzzy( pic1, time );
		time.advanceTime( drset.dPic * drset.dSec );
		loadFitsFuzzy( pic2, time );

		if ( pic1.params().succload && pic2.params().succload )
		{
			double R = ( pic1.params().R + pic2.params().R ) / 2;
			double theta0 = ( pic1.params().theta0 + pic2.params().theta0 ) / 2;

			calculateOmegas( pic1, pic2, shiftsX, thetas, omegasX, predicX, ipcset, drset, R, theta0, dy );

			omegasX2D.emplace_back( omegasX );
			thetas2D.emplace_back( thetas );

			omegasXfit = theta1Dfit( omegasX, thetas );
			omegasXavgfit = theta2Dfit( omegasX2D, thetas2D );

			drplot1( plt1, thetas, omegasX, omegasXfit, omegasXavgfit, predicX );
			drplot2( plt2, iotam, shiftsX );
		}
	}

	return fillDiffrotResults();
}

void loadFitsFuzzy( FitsImage &pic, FitsTime &time )
{
	pic.reload( time.path() );

	if ( !pic.params().succload )
	{
		for ( int pm = 1; pm < plusminusbufer; pm++ )
		{
			if ( !( pm % 2 ) )
				time.advanceTime( pm );
			else
				time.advanceTime( -pm );

			pic.reload( time.path() );

			if ( pic.params().succload )
				break;
		}
	}

	if ( !pic.params().succload )
	{
		time.advanceTime( plusminusbufer / 2 );
	}
}

void calculateOmegas( const FitsImage &pic1, const FitsImage &pic2, std::vector<double> &shiftsX, std::vector<double> &thetas, std::vector<double> &omegasX,
                      std::vector<double> &predicX, const IPCsettings &ipcset, const DiffrotSettings &drset, double R, double theta0, double dy )
{
	int yshow = 400;

	#pragma omp parallel for
	for ( int y = 0; y < drset.ys; y++ )
	{
		Mat crop1 = roicrop( pic1.image(), pic1.params().fitsMidX, pic1.params().fitsMidY + dy * ( y - drset.ys / 2 ) + sy, ipcset.getcols(), ipcset.getrows() );
		Mat crop2 = roicrop( pic2.image(), pic2.params().fitsMidX, pic2.params().fitsMidY + dy * ( y - drset.ys / 2 ) + sy, ipcset.getcols(), ipcset.getrows() );
		shiftsX[y] = phasecorrel( std::move( crop1 ), std::move( crop2 ), ipcset, nullptr, y == yshow ).x;
	}

	//filter outliers here
	//filterShiftsMA(shiftsX);

	for ( int y = 0; y < drset.ys; y++ )
	{
		thetas[y] = asin( ( ( double )dy * ( drset.ys / 2 - y ) - sy ) / R ) + theta0;
		omegasX[y] = asin( shiftsX[y] / ( R * cos( thetas[y] ) ) ) / ( double )( drset.dPic * drset.dSec );
		predicX[y] = ( 14.713 - 2.396 * pow( sin( thetas[y] ), 2 ) - 1.787 * pow( sin( thetas[y] ), 4 ) ) / ( 24. * 60. * 60. ) / ( 360. / 2. / Constants::Pi );
		predicX[y] = ( 14.296 - 1.847 * pow( sin( thetas[y] ), 2 ) - 2.615 * pow( sin( thetas[y] ), 4 ) ) / ( 24. * 60. * 60. ) / ( 360. / 2. / Constants::Pi );

	}
}

DiffrotResults fillDiffrotResults()
{
	return DiffrotResults();
}

std::vector<double> theta1Dfit( const std::vector<double> &omegas, const std::vector<double> &thetas )
{
	return polyfit( thetas, omegas, 2 );
}

std::vector<double> theta2Dfit( const std::vector<std::vector<double>> &omegasX2D, const std::vector<std::vector<double>> &thetas2D )
{
	int I = omegasX2D.size();
	int J = omegasX2D[0].size();

	std::vector<double> omegasAll( I * J );
	std::vector<double> thetasAll( I * J );

	for ( int i = 0; i < I; i++ )
	{
		for ( int j = 0; j < J; j++ )
		{
			omegasAll[i * J + j] = omegasX2D[i][j];
			thetasAll[i * J + j] = thetas2D[i][j];
		}
	}

	return polyfit( thetasAll, omegasAll, 2 );
}

void drplot1( IPlot1D *plt1, const std::vector<double> &thetas, const std::vector<double> &omegasX, const std::vector<double> &omegasXfit, const std::vector<double> &omegasXavgfit,
              const std::vector<double> &predicX )
{
	plt1->plot( thetas, std::vector<std::vector<double>> {omegasX, omegasXfit, omegasXavgfit, predicX} );
}

void drplot2( IPlot1D *plt2, const std::vector<double> &iotam, const std::vector<double> &shiftsX )
{
	plt2->plot( iotam, std::vector<std::vector<double>> {shiftsX} );
}

void filterShiftsMA( std::vector<double> &shiftsX )
{
	auto shiftsXma = shiftsX;
	for ( int i = 0; i < shiftsX.size(); i++ )
	{
		double mav = 0;
		int mavc = 0;
		for ( int m = 0; m < ma; m++ )
		{
			int idx = i - ma / 2 + m;

			if ( idx < 0 || idx == shiftsX.size() )
				break;

			mav += shiftsX[idx];
			mavc++;
		}
		mav /= ( double )mavc;
		shiftsXma[i] = mav;
	}
	shiftsX = shiftsXma;
}

std::vector<double> predictions( double theta )
{
	std::vector<double> pred;
	pred.reserve( 10 );

	pred.push_back( ( 14.713 - 2.396 * pow( sin( theta ), 2 ) - 1.787 * pow( sin( theta ), 4 ) ) / ( 24. * 60. * 60. ) / ( 360. / 2. / Constants::Pi ) ); // wiki
	pred.push_back( ( 14.296 - 1.847 * pow( sin( theta ), 2 ) - 2.615 * pow( sin( theta ), 4 ) ) / ( 24. * 60. * 60. ) / ( 360. / 2. / Constants::Pi ) ); // smth
}