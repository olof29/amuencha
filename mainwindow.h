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

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <list>
#include <vector>
#include <map>
#include <functional>

#include <QMainWindow>
#include <QFile>
#include <QProcess>
#include <QMutex>
#include <QPushButton>

#include <rtaudio/RtAudio.h>

#include "frequency_analyzer.h"

struct MyRtAudio : public RtAudio {
    using RtAudio::RtAudio;
    // Make this very useful function public
    using RtAudio::openRtApi;
};

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();
    
protected slots:

    void on_bouton_ouvrir_clicked();
    void on_bouton_enregistrer_clicked();
    void on_play_pause_clicked();
    void on_bouton_rejouer_clicked();
    
    void on_liste_drivers_currentIndexChanged(int index);
    void on_display_gain_valueChanged(int value);
    void on_mic_dup_cb_toggled(bool checked);
    
    void on_min_freq_slider_valueChanged(int value);
    void on_max_freq_slider_valueChanged(int value);
    void on_visual_fading_sb_valueChanged(int value);
    void on_song_rec_mix_valueChanged(int value);
    void on_positionChanson_valueChanged(int value);
    void on_positionRecord_valueChanged(int value);
    void on_gain_valueChanged(int value);
    
protected:
    void update_devices(RtAudio::Api api);
    float get_sample_rate();
    void set_song_position(int64_t position, bool lock = true);
    void set_replay_position(int64_t position, bool lock = true);
    void common_clicked(bool& is_doing, QPushButton* button, 
                        const char* theme_start, const char* theme_stop, 
                        Frequency_Analyzer* &analyzer, int id); 
    
    void setup_lines_in_out();
    void stop_lines_in_out();
    
    Ui::MainWindow *ui;
    MyRtAudio* rt_audio = 0;
    typedef std::list<std::pair<int64_t,std::vector<float>>> AudioRecording;
    AudioRecording recording;
    std::vector<std::reference_wrapper<AudioRecording::value_type>> replay_view;
    int64_t replay_position = 0;
    std::vector<float> song;
    int64_t song_position = 0;
    bool is_recording = false;
    bool is_playing = false;
    bool replay_mode = false;
    bool mic_dup;
    float song_mix_factor = 1;
    float rec_mix_factor = 1;
    float mic_gain = 1;
    
    Frequency_Analyzer* record_analyzer = 0;
    Frequency_Analyzer* song_analyzer = 0;
    
    // the sample rate as set by the lines in/out, or 0
    // use get_sample_rate() that sets up the lines first
    float sampling_rate = 0;
    
    QMutex audio_mutex;
    friend int audio_available_callback(void*, void *, unsigned int, double, RtAudioStreamStatus, void *);

private slots:
    void on_clear_record_clicked();
};

#endif // MAINWINDOW_H
