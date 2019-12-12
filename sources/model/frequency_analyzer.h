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

#ifndef AUDIOINPUTTHREAD_H
#define AUDIOINPUTTHREAD_H

#include <QThread>
#include <QMutex>
#include <QWaitCondition>

#include <vector>
#include <map>
#include <complex>
#include <functional>

class Frequency_Analyzer : public QThread
{
    Q_OBJECT

public:
    Frequency_Analyzer(QObject *parent = 0);
    ~Frequency_Analyzer();
    
    // called by the RT audio thread to feed new data
    void new_data(float *chunk, int size);
    
    // Arguments are: frequency bins [f,f+1), and power in each bin
    // hence the first vector size is 1 more than the second
    // TODO if useful: make a proper listener API with id, etc
    typedef std::function<void(const std::vector<float>&,const std::vector<float>&)> PowerHandler;
    PowerHandler power_handler;

    // sampling rate in Hz
    // freqs in Hz
    // PowerHandler for the callback
    // max_buffer_duration in milliseconds, specifies the largest buffer for computing the frequency content
    //             At lower frequencies, long buffers are needed for accurate frequency separation.
    //             When that max buffer duration is reached, then it is capped and the frequency resolution decreases
    //             Too low buffers also limit the min_freq, duration must be >= period
    void setup(float sampling_rate, const std::vector<float>& frequencies, PowerHandler handler, float periods = 20, float max_buffer_duration = 500);
    
    // call to remove all existing chunk references
    // this may cause signal loss, but this is usually called precisely when the signal is lost...
    void invalidate_samples();
    
protected:
    void run() override;
    
    // Multi-threading related variables
    static const unsigned long CYCLE_PERIOD = 20; // in milliseconds
    QMutex mutex, data_mutex;
    QWaitCondition condition;
    enum Status {NO_DATA = 0, HAS_NEW_DATA = 1, QUIT_NOW = 2};
    Status status;
    unsigned long waiting_time = CYCLE_PERIOD;
    
    // new data chunks arrived since the last periodic processing
    std::vector<std::pair<float*,int>> chunks;

    // The window is the usual Kaiser with alpha=3
    static void initialize_window(std::vector<float>& window);
    static void initialize_window_deriv(std::vector<float>& window);
    
    // The filter bank. One filter per frequency
    // The 4 entries in the v4sf are the real, imaginary parts of the windowed
    // sine wavelet, and the real, imaginary parts of the derived windowed sine
    // used for reassigning the power spectrum.
    // Hopefully, with SIMD, computing all 4 of them is the same price as just one
    // TODO: v8sf and compute 2 freqs at the same time
    typedef float v4sf __attribute__ ((vector_size (16)));
    std::vector<std::vector<v4sf>> windowed_sines;
    std::vector<float> frequencies;
    std::vector<float> power_normalization_factors;
    float samplerate_div_2pi;
    std::vector<float> big_buffer;
    std::vector<float> reassigned_frequencies;
    std::vector<float> power_spectrum;

    // caching computations for faster init
    // on disk for persistence between executions,
    // in memory for avoiding reloading from disk when changing the spiral size
    bool read_from_cache(std::vector<float>& window, std::vector<float>& window_deriv);
    void write_to_cache(std::vector<float>& window, std::vector<float>& window_deriv);
    std::map<int, std::vector<float>> mem_win_cache, mem_winderiv_cache;
};

#endif // AUDIOINPUTTHREAD_H
