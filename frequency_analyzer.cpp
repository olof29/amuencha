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

#include <algorithm>
#include <cmath>

#include <boost/math/special_functions/bessel.hpp>
#include <boost/math/constants/constants.hpp>

#include <sys/stat.h>

#include "sse_mathfun.h"

#include "frequency_analyzer.h"

using namespace std;
using namespace boost::math::float_constants;

Frequency_Analyzer::Frequency_Analyzer(QObject *parent) : QThread(parent)
{
    status = NO_DATA;
}

Frequency_Analyzer::~Frequency_Analyzer()
{
    mutex.lock();
    status = QUIT_NOW;
    // Do not even wait the end of a cycle, quit
    condition.wakeOne();
    mutex.unlock();
    wait(CYCLE_PERIOD*2);
    terminate();
    wait(); // until run terminates
}

void Frequency_Analyzer::new_data(float *chunk, int size)
{
    
    // producer, called from another thread. 
    // Must not wake the main thread uselessly to notify that new data is available
    // The idea is that the new_data may be called very fast from a RT audio thread
    // but that frequencies are computed at most 10 times/second (or whatever the cyclic period is set)
    // => decouple thread frequencies
    // And when no data is here (recording off, no song...), the main thread is fully passive (no CPU hog)
    
    mutex.lock();
    
    // Store a pointer here and not a full data copy, which is much faster
    // The real data is stored in the AudioRecording object
    // Kind of duplicates the AudioRecording list, however:
    // - This way, there is no need for mutex/lock in the AudioRecording structure
    // - The position of the last unprocessed chunk within that structure need not be stored there
    chunks.emplace_back(make_pair(chunk, size));
    
    // set the flag that data has arrived
    status = HAS_NEW_DATA;
    
    // IF AND ONLY IF the thread was blocked forever, then wake it up
    if (waiting_time != CYCLE_PERIOD) {
        // and now resume the cyclic scheduling
        waiting_time = CYCLE_PERIOD;
        condition.wakeOne();
    }
    
    // Otherwise, do NOT wake the other thread, keep the low-freq cycle to decrease load
    mutex.unlock();
    
}

void Frequency_Analyzer::run()
{
    // Solution with a wait condition + time
    // other possible solution = timer, but that would need to be stopped
    
    mutex.lock();
    
    // waiting_time is a mutex-protected info
    waiting_time = CYCLE_PERIOD;
    
    // loop starts with mutex locked
    while (true) {
        condition.wait(&mutex, waiting_time);
        
        if (status==QUIT_NOW) break;
        
        if (status==HAS_NEW_DATA) {
            // Swap the chunks to a local variable, so:
            // - the class chunks becomes empty
            // - the audio thread can feed it more data while computing frequencies
            vector<pair<float*,int>> chunks;
            chunks.swap(this->chunks);
            status = NO_DATA; // will be updated if new data indeed arrives
            mutex.unlock();
            
            // Now, we can take the time to do the frequency computations
            data_mutex.lock();

            int new_data_size = 0;
            for (auto c: chunks) new_data_size += c.second;
            // shift the old data to make room for the new
            int new_data_pos = big_buffer.size()-new_data_size;
//cout << "data " << new_data_size << " " << big_buffer.size() << endl;
            // std::copy can cope with overlapping regions in this copy direction
            if (new_data_pos>0) copy(big_buffer.begin()+new_data_size, big_buffer.end(), big_buffer.begin());
            // now copy each chunk at its position in the big buffer
            for (auto c: chunks) {
                if (new_data_pos<0) {
                    // discard too old chunks
                    if (c.second <= -new_data_pos) {
                        new_data_pos += c.second;
                        continue;
                    }
                    // partial copy of chunks that fall on the edge
                    copy(c.first+new_data_pos, c.first+c.second, &big_buffer[0]);
                    new_data_pos = c.second+new_data_pos;
                    continue;
                }
                copy(c.first, c.first+c.second, &big_buffer[new_data_pos]);
                new_data_pos += c.second;
            }

            // Apply the filter bank
            float *bbend = &big_buffer[0] + big_buffer.size();
            for (int idx=0; idx<frequencies.size(); ++idx) {
                const auto& ws = windowed_sines[idx];
                int wsize = ws.size();
                float* sig = bbend - wsize;
                v4sf acc = {0.f, 0.f, 0.f, 0.f};
                for (int i=0; i<wsize; ++i) acc += ws[i] * sig[i];
                float norm = acc[0]*acc[0] + acc[1]*acc[1];
                float reassign = frequencies[idx];
                if (norm>0) {
                    reassign -= (acc[0] * acc[3] - acc[1] * acc[2]) * samplerate_div_2pi / norm;
                }
                reassigned_frequencies[idx] = reassign;
                power_spectrum[idx] = norm * power_normalization_factors[idx];
            }
            
            // Notify our listener that new power/frequency content has arrived
            power_handler(reassigned_frequencies, power_spectrum);
            
            // setup can now lock and change data structures if needed
            data_mutex.unlock();
            
            // relock for the condition wait
            mutex.lock();
            continue;
        }
        
        // No more data ? Force waiting until data arrives
        waiting_time = ULONG_MAX;
        // keep the lock for next loop
    }
    mutex.unlock();
    
}

void Frequency_Analyzer::setup(float sampling_rate, const std::vector<float> &frequencies, PowerHandler handler, float periods, float max_buffer_duration)
{
    // Block data processing while changing the data structures
    data_mutex.lock();
    
    this->samplerate_div_2pi = sampling_rate/two_pi;
    this->frequencies = frequencies;
    
    this->reassigned_frequencies = frequencies;
    this->power_spectrum.resize(frequencies.size());
    
    power_handler = handler;

    // Prepare the windows
    //std::vector<std::vector<v4sf>> windowed_sines;
    windowed_sines.resize(frequencies.size());
    power_normalization_factors.resize(frequencies.size());

    int big_buffer_size = 0;
    
    for (int idx=0; idx<frequencies.size(); ++idx) {
        // for each freq, span at least 20 periods for more precise measurements
        // This still gives reasonable latencies, e.g. 50ms at 400Hz, 100ms at 200Hz, 400ms at 50Hz...
        // Could also span more for even better measurements, with larger
        // computation cost and latency
        float f = frequencies[idx];
        int window_size = (int)(min(periods / f, max_buffer_duration * 0.001f) * sampling_rate);
        vector<float> window(window_size);
        vector<float> window_deriv(window_size);
        if (!read_from_cache(window, window_deriv)) {
            initialize_window(window);
            initialize_window_deriv(window_deriv);
            write_to_cache(window, window_deriv);
        }
        windowed_sines[idx].resize(window_size);
        float wsum = 0;
        for (int i=0; i<window_size;) {
            if (i<window_size-4) {
                v4sf tfs = {
                    (float)(i-window_size-1) / sampling_rate,
                    (float)(i+1-window_size-1) / sampling_rate,
                    (float)(i+2-window_size-1) / sampling_rate,
                    (float)(i+3-window_size-1) / sampling_rate
                };
                tfs *= (float)(-two_pi*f);
                v4sf sin_tf, cos_tf;
                sincos_ps(tfs, &sin_tf, &cos_tf);
                for (int j=0; j<3; ++j) {
                    v4sf ws = {
                        cos_tf[j] * window[i+j],
                        sin_tf[j] * window[i+j],
                        cos_tf[j] * window_deriv[i+j],
                        sin_tf[j] * window_deriv[i+j]
                    };
                    windowed_sines[idx][i+j] = ws;
                    wsum += window[i+j];
                }
                i+=4;
                continue;
            }
            float t = (float)(i-window_size-1) / sampling_rate;
            float re = cosf(-two_pi*t*f);
            float im = sinf(-two_pi*t*f);
            v4sf ws = {
                re * window[i],
                im * window[i],
                re * window_deriv[i],
                im * window_deriv[i]
            };
            windowed_sines[idx][i] = ws;
            wsum += window[i];
            ++i;
        }
        power_normalization_factors[idx] = 1. / (wsum*wsum);
        big_buffer_size = max(big_buffer_size, window_size);
    }

    big_buffer.clear();
    // fill with 0 signal content to start with
    big_buffer.resize(big_buffer_size, 0.f);

    // Processing can resume with the new data structures in place.
    data_mutex.unlock();
}

void Frequency_Analyzer::invalidate_samples()
{
    mutex.lock();
    data_mutex.lock();
    chunks.clear();
    data_mutex.unlock();
    mutex.unlock();
}

void Frequency_Analyzer::initialize_window(std::vector<float>& window) {
    // Kaiser window with a parameter of alpha=3 that nullifies the window on edges
    int size = window.size();
    const float two_over_N = 2./size;
    const float alpha = 3.;
    const float alpha_pi = alpha * pi;
    const float inv_denom = 1./boost::math::cyl_bessel_i(0., alpha_pi);
    for (int i=0; i<size; ++i) {
            float p = i * two_over_N - 1.;
        window[i] = boost::math::cyl_bessel_i(0., alpha_pi * sqrt(1. - p*p)) * inv_denom;
    }
}

void Frequency_Analyzer::initialize_window_deriv(std::vector<float>& window) {
    // Derivative of the Kaiser window with a parameter of alpha=3 that nullifies the window on edges
    int size = window.size();
    const float two_over_N = 2./size;
    const float alpha = 3.;
    const float alpha_pi = alpha * pi;
    const float inv_denom = 1./boost::math::cyl_bessel_i(0., alpha_pi);
    for (int i=1; i<size; ++i) {
        float p = i * two_over_N - 1.;
        window[i] = boost::math::cyl_bessel_i(1., alpha_pi * sqrt(1. - p*p)) * inv_denom * alpha_pi / sqrt(1. - p*p) * (-p)*two_over_N;
    }
    // lim I1(x)/x as x->0  =  1/2
    window[0] = 0.5 * inv_denom * alpha_pi * alpha_pi * two_over_N;
}

bool Frequency_Analyzer::read_from_cache(std::vector<float> &window, std::vector<float> &window_deriv)
{
    auto it = mem_win_cache.find(window.size());
    if (it!=mem_win_cache.end()) {
        window = it->second;
        auto itd = mem_winderiv_cache.find(window.size());
        if (itd!=mem_winderiv_cache.end()) {
            window_deriv = itd->second;
            return true;
        }
        // else, load from disk
    }

    // TODO: make the cache location parametrizable (and an option to not use it)
    ifstream file(".amuencha_cache/w"+to_string(window.size())+".bin", ios::binary);
    if (file.is_open()) {
        file.read(reinterpret_cast<char*>(&window[0]), window.size()*sizeof(float));
        file.read(reinterpret_cast<char*>(&window_deriv[0]), window_deriv.size()*sizeof(float));
        if (file.tellg()!=(window.size()+window_deriv.size())*sizeof(float)) {
            cerr << "Error: invalid cache .amuencha_cache/w"+to_string(window.size())+".bin" << endl;
            return false;
        }
        return true;
    }
    return false;
}

void Frequency_Analyzer::write_to_cache(std::vector<float> &window, std::vector<float> &window_deriv)
{
#if defined(_WIN32) || defined(_WIN64)
    mkdir(".amuencha_cache");
#else
    mkdir(".amuencha_cache", 0755);
#endif
    ofstream file(".amuencha_cache/w"+to_string(window.size())+".bin", ios::binary|ios::trunc);
    file.write(reinterpret_cast<char*>(&window[0]), window.size()*sizeof(float));
    file.write(reinterpret_cast<char*>(&window_deriv[0]), window_deriv.size()*sizeof(float));
    mem_win_cache[window.size()] = window;
    mem_winderiv_cache[window.size()] = window_deriv;
}
