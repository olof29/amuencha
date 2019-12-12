/* 
  Analyseur de MUsique et ENtraînement au CHAnt

  This file is released under either of the two licenses below, your choice:
  - LGPL v2.1 or later, https://www.gnu.org
    The GNU Lesser General Public Licence, version 2.1 or,
    at your option, any later version.
  - CeCILL-C, http://www.cecill.info
    The CeCILL-C license is more adapted to the French laws,
    but can be converted to the GNU LGPL.
  
  You can use, modify and/or redistribute the software under the terms of any
  of these licences, which should have been provided to you together with this
  sofware. If that is not the case, you can find a copy of the licences on
  the indicated web sites.
  
  By Nicolas . Brodu @ Inria . fr
  
  See http://nicolas.brodu.net/programmation/amuencha/ for more information
*/

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <complex>
#include <cmath>
#include <algorithm>

#include <boost/math/special_functions/bessel.hpp>
 
#include <fftw3.h>

using namespace std;

vector<double> window;

// NUTTALL (4b) has wider central lobe (bleeds on nearby frequencies) but very low aliasing
// WELSH has a sharp peak, reasonably low aliasing
// KAISER_NULL is a compromise for maximizing the energy in the main lobe with low aliasing. The lobe width is slightly large, due to forcing the window bounds to be nearly null with a classic parameter value (alpha=3)
// KAISER_SHARP uses a different parametrization for a sharper main lobe, but lower attenuation
// HANN is classic and a good compromise for lobe width and low aliasing. With discretized signals, when the frequency to analyse is right, only the central peak and the two nearest frequencies at 1/10th of the power of the central appears.
enum WindowType {HANN=0, WELSH = 1, NUTTALL = 2, KAISER_NULL = 3, KAISER_SHARP = 4};

void initialize_window(int size, WindowType type = HANN) {
    // Use the "DFT-even" method, which periodizes the window
    // the trick is to enlarge the window by 1 sample and delete the rightmost null
    // coefficient, which then coincides with the next frame leftmost coefficient
    // All there is to do is to apply the formula with N instead of N-1 for samples
    // ranging normally with 0<=i<N (the usual C convention)
    window.resize(size);
    switch (type) {
        case KAISER_SHARP: {
            const double two_over_N = 2./size;
            const double alpha = 3.; // usual value, causes near-null window bounds
            const double alpha_pi = alpha * 3.14159265358979323846;
            const double inv_denom = 1./boost::math::cyl_bessel_i(0., alpha_pi);
            for (int i=0; i<size; ++i) {
                double p = i * two_over_N - 1.;
                window[i] = boost::math::cyl_bessel_i(0., alpha_pi * sqrt(1. - p*p)) * inv_denom;
            }
        } break;
        case KAISER_NULL: {
            const double two_over_N = 2./size;
            const double alpha = sqrt(5.)/2.; // main lobe has a width of 3 samples
            const double alpha_pi = alpha * 3.14159265358979323846;
            const double inv_denom = 1./boost::math::cyl_bessel_i(0., alpha_pi);
            for (int i=0; i<size; ++i) {
                double p = i * two_over_N - 1.;
                window[i] = boost::math::cyl_bessel_i(0., alpha_pi * sqrt(1. - p*p)) * inv_denom;
            }
        } break;
        case NUTTALL: {
            const double alpha0 = 0.355768;
            const double alpha1 = 0.487396;
            const double alpha2 = 0.144232;
            const double alpha3 = 0.012604;
            const double two_pi_div_N = 2. * 3.14159265358979323846 / size;
            const double four_pi_div_N = 4. * 3.14159265358979323846 / size; 
            const double six_pi_div_N = 6. * 3.14159265358979323846 / size;
            for (int i=0; i<size; ++i) {
                window[i] = alpha0 - alpha1 * cos(two_pi_div_N * i) + alpha2 * cos(four_pi_div_N * i) - alpha3 * cos(six_pi_div_N * i);
            }
        } break;
        case WELSH: {
            const double two_over_N = 2./size;
            for (int i=0; i<size; ++i) {
                //double p = (i - Ndiv2) / Ndiv2;
                double p = i * two_over_N - 1.;
                window[i] = 1. - p*p;
            }
        } break;
        // default or HANN
        default: {
            const double pi_div_N = 3.14159265358979323846 / size;
            for (int i=0; i<size; ++i) {
                double sin_i = sin(pi_div_N * i);
                window[i] = sin_i * sin_i;
            }
        };
    }
}
    

void test_octave_setup() {
    double sampling_rate = 48000; // Hz
    double min_freq = 65.41; // C1
    double max_freq = 7902.13; // B7
    double min_df = 0.01;
    double max_buffer_duration = 10;
    
    // higher end of the highest octave must end at some divisor of the sampling rate
    // for the initial subsampling. 2* for Nyquist
    int initial_subsampling = (int)floor(sampling_rate / (2*max_freq));
    // which means, this sets the highest octave frequency
    //double high_octave_end = sampling_rate / (double)initial_subsampling * 0.5;
    // And since we need to encompass also the min_freq, this sets the number of octaves
    // on which to perform fftw
    /*er = ir/is;
    er*0.25 / 2^(N-1) = min_freq;
    er*0.25 / min_freq = 2^(N-1);
    but use also the last half-buffer at N, hence keep N-1
    */
    int number_of_octaves = (int)ceil(log2(sampling_rate / (double)initial_subsampling * 0.25 / min_freq));
    
    cout << "min_freq = " << min_freq << endl;
    cout << "max_freq = " << max_freq << endl;
    cout << "initial subsampling = " << initial_subsampling << endl;
    
    // Compute the buffer length in samples, for all octaves
    double effective_rate = sampling_rate / (double)initial_subsampling;
    for (int level=0; level<number_of_octaves; ++level) {
        // max branch = 0.9 * HW, min branch = 0.1 * HW
        // => ratio of 9 between #pixels at high/low ends 
        double effective_df = min_df * (1. + 8. * level / (number_of_octaves-1.));
        // min_df given in semitones, e.g. 0.01 for a cent. *12 to cover one octave.
        // S from rough linear approx, since that would need to be in log scale
        // freq size = S/2+1, with cte at 0
        // but we use only the second part of the buffer, at rate*0.25 to rate*0.5
        int S = (int)ceil(12./effective_df * 4);
        double duration = S / effective_rate;
        // if that is too much, we have to reduce the frequency resolution further
        if (duration > max_buffer_duration) {
            S = (int)floor(effective_rate * max_buffer_duration);
        }
        // freq size = S/2+1
        // max freq = rate / 2 at index S/2
        double hz_df = effective_rate / S; // inverse of duration, smaller period
        cout << endl << "level = " << level << endl;
        cout << "rate = " << effective_rate << endl;
        cout << "freq range used = " << (level==(number_of_octaves-1)?hz_df:effective_rate*0.25) << " to "  << effective_rate*0.5 << endl;
        cout << "effective_df = " << effective_df << endl;
        cout << "S = " << S << endl;
        cout << "duration = " << S/effective_rate << endl;
        cout << "final df (Hz) = " << hz_df << endl;
        cout << "final df (rough semitones) = " << 12./(effective_rate*0.25/hz_df) << endl;
        
        effective_rate *= 0.5;
    }
}    
    
    
int main(int argc, char** argv) {
    vector<double> values;
    ifstream file(argv[1]);
    double x;
    while (file >> x) {
        values.push_back(x);
    }
    
    const int sampling_rate = 48000; // Hz
    const int frame_duration = 100; // ms
    
    test_octave_setup();
    return 0;
    
    int frame_size = sampling_rate * frame_duration / 1000;
    int num_frames = values.size() / frame_size;
    
    // initialize the window function.
    initialize_window(frame_size);
    /*for (int i=0; i<frame_size; ++i) {
        cout << window[i] << endl;
    }
    return 0;*/
    
    vector<double> fvalues(frame_size);
    vector<complex<double>> freqs(frame_size/2 +1);
    
    // TODO: Use fftw_alloc_real(N) and fftw_alloc_complex(N)
    // for ensuring aligment of data, hence use of SIMD instructions
    // with the vectors, those are probably not used
    
    fftw_plan plan = fftw_plan_dft_r2c_1d(frame_size, &fvalues[0], reinterpret_cast<fftw_complex*>(&freqs[0]), FFTW_ESTIMATE);
    
    vector<double> power_spectra(frame_size/2 +1, 0.);
    for (int fi = 0; fi < num_frames; ++fi) {
        copy_n(&values[fi * frame_size], frame_size, &fvalues[0]);
        // convolve with symmetric window = multiply sample by sample
        const double* v = &values[fi * frame_size];
        for (int i=0; i<frame_size; ++i) {
            fvalues[i] = v[i]*window[i];
        }
        fftw_execute(plan);
        for (int pi=0; pi<power_spectra.size(); ++pi) {
            const auto& f = freqs[pi];
            power_spectra[pi] += f.real()*f.real() + f.imag()*f.imag();
        }
    }
    // should normalize freqs by sqrt(N), hence power by N
    // average over all frames
    for (int pi=0; pi<power_spectra.size(); ++pi) {
        power_spectra[pi] /= num_frames * frame_size;
        cout << power_spectra[pi] << endl;
    }
    
    fftw_destroy_plan(plan);
   
    return 0;
}
/*
>> [x, ix] = max(ps)
x =  345.88
ix =  45
>> (ix-1)/(size(ps,1)-1)*F/2
ans =  440
*/
