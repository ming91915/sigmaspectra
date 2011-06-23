////////////////////////////////////////////////////////////////////////////////////
// This file is part of SigmaSpectra.
// 
// SigmaSpectra is free software: you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version.
// 
// SigmaSpectra is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
// PARTICULAR PURPOSE.  See the GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License along with
// SigmaSpectra.  If not, see <http://www.gnu.org/licenses/>.
// 
// Copyright 2008 Albert Kottke
////////////////////////////////////////////////////////////////////////////////////


#include "Motion.h"

#include <fftw3.h>
#include <cmath>
#include <complex>

#include <QRegExp>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTextStream>
#include <QtDebug>

Motion::Motion( const QString & fileName )
    : AbstractMotion(), m_fileName(fileName)
{
    if (!fileName.isEmpty()) {
        processFile();
    }
}

Motion::~Motion()
{
}

QString Motion::name() const
{
    return m_event + QDir::separator() + m_station + m_comp;
}

const QString & Motion::fileName() const
{
    return m_fileName;
}
        
const QString & Motion::component() const
{
    return m_comp;
}

const QString & Motion::details() const
{
    return m_details;
}
        
int Motion::componentCount() const
{
    return 1;
}

double Motion::dur5_75() const
{
    return m_dur5_75;
}
double Motion::dur5_95() const
{
    return m_dur5_95;
}

const QVector<double> & Motion::time() const
{
    return m_time;
}

const QVector<double> & Motion::acc() const
{
    return m_acc;
}

const QVector<double> & Motion::vel() const
{
    return m_vel;
}

const QVector<double> & Motion::disp() const
{
    return m_disp;
}

double Motion::pga() const
{
    return m_pga;
}

double Motion::pgv() const
{
    return m_pgv;
}

double Motion::pgd() const
{
    return m_pgd;
}

void Motion::scaleBy(const double factor)
{
    // Scale factor relative to the previous
    const double relScale = factor / m_prevScale;
    
    for (int i = 0; i < m_time.size(); ++i ) {
        m_acc[i] *= relScale;
        m_vel[i] *= relScale;
        m_disp[i] *= relScale;
    }

    m_pga *= relScale;
    m_pgv *= relScale;
    m_pgd *= relScale;
    m_ariasInt *= (relScale * relScale);

    AbstractMotion::scaleBy(factor);
}

void Motion::processFile()
{
    QFileInfo fileInfo( m_fileName );

    if(!fileInfo.isFile()) {
        qCritical("File is not found!");
    }

    // Open the file
    QFile file( fileInfo.absoluteFilePath() );
    file.open( QIODevice::ReadOnly | QIODevice::Text );

    QTextStream fin( &file );

    // 
    // Find the station and event identifiers
    // 
    QRegExp rx(".*/([^/]+)/([^/]+)((?:\\d{3})|(?:-{0,2}[NSEWTLR]+)|(?:NOR)|(?:SOU)|(?:EAS)|(?:WES))(\\.AT2$)");
    int pos = rx.indexIn( fileInfo.absoluteFilePath() );
    if ( pos > -1 ) {
        m_event = rx.cap(1);
        m_station = rx.cap(2);
        m_comp = rx.cap(3);
    }

    // Number of data points 
    int n; 
    // Read the header based on file type
    if ( fileInfo.completeSuffix() == "AT2" ) {
        /*
         * AT2 files contain 4 header lines:
         * (1) Not important
         * (2) Event information
         * (3) Not important
         * (4) number of data points, time step
         */
        // Skip the first line
        QString line = fin.readLine();
        // Read the event information and trim the white space
        m_details = fin.readLine().trimmed();
        // Skip the third line
        line = fin.readLine();
        // Read the number of data points and timestep
        fin >> n >> m_dt;
        // Finish reading the line
        line = fin.readLine();
    } else {
        // FIXME
        throw( std::string("File type not supported!" ) );
    }

    // 
    // Read the acceleration time history and compute the PGA
    // 
    m_acc.resize(n);
    m_time.resize(n);

    for ( int i = 0; i < n; i++ ){
        fin >> m_acc[i];
        m_time[i] = m_dt * i;
    }

    m_pga = findMaxAbs( m_acc );

    // Compute the velocity, peak ground velocity, displacement and peak ground
    // displacement.  The acceleration time series is scaled by gravity in
    // units of cm/sec/sec.
    
    m_vel = cumtrapz( m_acc, m_dt, 980.665);
    m_pgv = findMaxAbs(m_vel);
    
    m_disp = cumtrapz( m_vel, m_dt);
    m_pgd = findMaxAbs(m_vel);

    // 
    // Compute the Arias intensity
    // 
    QVector<double> arias(n);
    arias[0] = 0.0;
    for ( int i = 1; i < n; i++ ) {
        arias[i] = arias.at(i-1) + M_PI * 0.25 * m_dt * ( pow( m_acc.at(i), 2 ) + pow( m_acc.at(i-1), 2 ) );
    }

    m_ariasInt = arias.last();


    // 
    // Compute the durations based on the arias intensity
    // 
    int i5  = 0;
    int i75 = 0;
    int i95 = 0;

    for ( int i = 0; i < n; i++ ) {
        // Normalize the intensity relative to the maximum
        double norm = arias.at(i) / m_ariasInt;
        // Calculate the index values for 5, 75, and 95 percent intensities
        if ( norm < 0.05 ) ++i5;
        if ( norm < 0.75 ) ++i75;
        if ( norm < 0.95 ) ++i95;
    }
    // Compute the durations
    m_dur5_95 = m_dt * ( i95 - i5 );
    m_dur5_75 = m_dt * ( i75 - i5 );

    // 
    // Compute the frequency QVector and Fourier amplitude spectrum
    // 
    // Compute the next largest power of two
    n = 1;
    while (n < m_acc.size()) {
        n <<= 1;
    }
    // Initialize the input array and pad it with zeros
    QVector<double> data(n);
    for ( int i = 0; i < data.size(); i++ ) {
        data[i] = (i < m_acc.size()) ? m_acc.at(i) : 0.0;
    }

    // Compute the Fourier amplitude spectrum

    QVector<std::complex<double> > fas;
    fft( data, fas );

    // Compute the frequency QVector
    QVector<double> freq(fas.size());
    double dFreq = 1 / ( 2 * m_dt * ( freq.size() - 1 ) );
    for ( int i = 0; i < freq.size(); i++ ) {
        freq[i] = i * dFreq;
    }

    //
    // The acceleration response spectrum is computed from the motion using a
    // single-degree of freedom transfer function applied to the Fourier
    // Amplitude spectrum.
    //
    m_sa = calcRespSpec( m_damping, m_period, freq, fas );

    m_lnSa.resize(m_sa.size());
    // Compute the average response
    double sum = 0.0;
    for ( int i = 0; i < m_lnSa.size(); i++ ) {
        m_lnSa[i] = log( m_sa.at(i) );
        sum += m_lnSa.at(i);
    }

    m_avgLnSa = sum / m_lnSa.size();
}

QVector<double> Motion::calcRespSpec(const double damping, const QVector<double> & period, const QVector<double>& freq, const QVector<std::complex<double> >& fas )
{
    // Allocate the space for the response

    QVector<std::complex<double> > tf(fas.size());
    QVector<double> ts;
    QVector<double> sa(period.size());

    const double deltaFreq = 1 / (m_dt * m_time.size());

    for ( int i = 0; i < period.size(); i++ ) {
        const double f = 1 / period.at(i);

        /*
        The FAS needs to extend to have a sampling rate that is 10 times larger
        than the maximum frequency which corresponds to 5 times larger Nyquist
        frequency. This is required for sufficient resolution of the peak value
        in the time domain. For example, at a frequency 100 Hz (period of 0.01
        sec) the FAS has to extend to 500 Hz. The FAS is initialized to be zero
        which allows for resolution of the time series.
        */
        const int size = qMax(fas.size(), int((f * 5.0) / deltaFreq));
        QVector<std::complex<double> > Y(size,
                                         std::complex<double>(0., 0.));

        // The amplitude of the FAS needs to be scaled to reflect the increased
        // number of points.
        const double scale = double(Y.size()) / double(fas.size());

        // Only apply the SDOF transfer function over frequencies defined by the original motion
        calcSdofTf( damping, f, freq, tf );
        for ( int j = 0; j < fas.size(); j++ )
            Y[j] = scale * tf.at(j) * fas.at(j);

        // Compute the inverse
        ifft( Y, ts );
        // Store the maximum
        sa[i] = findMaxAbs( ts );
    }

    return sa;
}

void Motion::calcSdofTf( const double damping, const double fn, const QVector<double>& freq, QVector<std::complex<double> >& tf )
{
    for ( int i = 0; i < freq.size(); i++ )
    {
        /*
         * The single degree of freedom transfer function
         *                          - fn^2
         *  H = -------------------------------------------------
         *       ( f^2 - fn^2 ) - 2 * sqrt(-1) * damping * fn * f
         */
        tf[i] = ( -fn * fn ) / std::complex<double>( freq.at(i) * freq.at(i) - fn * fn, -2.0 * damping * fn * freq.at(i) );
    }
}

double Motion::findMaxAbs( const QVector<double>& v )
{
    //  Assume the first value is the largest
    double max = fabs( v.at(0) );
    // Check the remaining values
    for ( int i = 1; i < v.size(); i++ ) {
        if ( fabs( v.at(i) ) > max ) {
            max = fabs( v.at(i) );
        }
    }
    // Return the maximum
    return max;
}
        
QVector<double> Motion::cumtrapz( const QVector<double>& ft, const double dt, const double scale)
{
    QVector<double> gt(ft.size());

    gt[0] = 0.0;

    for ( int i = 1; i < ft.size(); i++ ) {
        gt[i] = gt.at(i-1) + scale * dt * (ft.at(i) + ft.at(i-1)) / 2.;
    }

    return gt;
}

void Motion::fft( const QVector<double>& ts, QVector<std::complex<double> >& fas )
{
	// The number of elements ts the double array is 2 * n, but only the first
	// n are filled with data.  For the complex array, n/2 + 1 elements are
	// required.

	// Copy the tsput QVector tsto a double array
	double* tsArray = (double*) fftw_malloc( sizeof(double) * 2 * ts.size() );

	for( int i = 0; i < ts.size(); i++ )
		tsArray[i] = ts.at(i);

	// Allocate the space for the fasput
	int n = ts.size()/2 + 1;
	fftw_complex* fasArray = (fftw_complex*)fftw_malloc( sizeof(fftw_complex) * n );

	// Create the plan and execute the FFT
	fftw_plan p = fftw_plan_dft_r2c_1d( ts.size(), tsArray, fasArray, FFTW_ESTIMATE );
	fftw_execute(p);

	// Copy the data to the fasput QVector
	fas.resize( n );
	for( int i = 0; i < n; i++ )
		fas[i] = std::complex<double>( fasArray[i][0], fasArray[i][1] );

	// Free the memory
	fftw_destroy_plan(p);
	fftw_free(tsArray);
	fftw_free(fasArray);
}
	
void Motion::ifft( const QVector<std::complex<double> >& fas, QVector<double>& ts )
{
	// Copy the fasput QVector fasto a double array
	fftw_complex* fasArray = (fftw_complex*) fftw_malloc( sizeof(fftw_complex) * fas.size() );

	for( int i = 0; i < fas.size(); i++ ) {
		fasArray[i][0] = fas.at(i).real();
		fasArray[i][1] = fas.at(i).imag();
	}

	// Allocate the space for the tsput
	int n = 2 * ( fas.size() - 1 );
	double* tsArray = (double*)fftw_malloc( sizeof(double) * n );

	// Create the plan and execute the FFT
	fftw_plan p = fftw_plan_dft_c2r_1d( n, fasArray, tsArray, FFTW_ESTIMATE );
	fftw_execute(p);

	// Copy the data to the tsput QVector and normalize by QVector length
	ts.resize( 2 * ( fas.size() - 1 ) );
	for( int i = 0; i < ts.size(); i++ ) 
		ts[i] = tsArray[i] / ts.size();

	// Free the memory
	fftw_destroy_plan(p);
	fftw_free(fasArray);
	fftw_free(tsArray);
}
