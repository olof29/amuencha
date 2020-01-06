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
#include <cmath>
#include <mutex>
#include <list>
#include <functional>
#include <algorithm>

#include <boost/math/constants/constants.hpp>

#include <QDebug>
#include <QtMultimedia/QAudioDeviceInfo>
#include <QUrl>
//#include <QAudioDeviceInfo>
#include <QAudioFormat>
#include <QVariant>
#include <QStringList>
//#include <QAudioDecoder>
#include <QFileDialog>
#include <QMessageBox>
#include <QProgressDialog>

#include <string.h>
extern "C" {
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
}

#include "mainwindow.h"
#include "ui_mainwindow.h"

using namespace std;



/*

================================================
=================   TODO   =====================
================================================

- Fonction d'auto-correlation signal ou énergie sur l'ensemble de la chanson
  => donne le tempo quand auto-correlation est max
  Note: possible que ça se décale un peu au cours du temps pour certains morceaux joués à l'arrache
        ou au contraire trafiqués en studio, ou encore ralentissements/accel volontaires
  => Calculer sur des fenêtres glissantes, par exemple sur 10 premières secondes
  PUIS mise à jour en temps réel (cf ci-dessous)

- Avec ce tempo de départ, trouver le décalage/shift qui donne les instants où
  l'énergie est max sur les quelques premières secondes
  Puis autoriser une variation d'un temps fort à l'autre de +- quelques échantillons : cherche max énergie autour de la prochaine valeur max attendue en fonction du tempo
  => Affiche le "tempo instantanné" = dernière période comme statistique. Affiche le tempo moyen = moyenne de ces instantannés depuis le début de la chanson.
  
- Détecter avec les max d'énergie si une structure se dégage tous les 3, 4 ou 6 temps (ou autre), un max un peu plus fort que les autres => trouve la mesure en nombre de temps fondamentaux.

Note : possible de faire tous ces calculs de temps au chargement de la chanson, puis de les noter dans une structure, en travaillant uniquement sur l'énergie calculée avec le signal au carré dans le domaine temporel et non pas fréquentiel

=> Autorise une option par la suite de ne calculer (ou de ne visualiser) les fréquences qu'aux temps forts, ce qui peut potentiellement rendre plus lisible l'affichage. Ou encore, de ne les calculer que sur les quart voire 8ème de temps => gagne en temps de calcul. Même si les fréquences restent calculées de façon très rapprochée, le calcul des accords ci-dessous et leur affichage textuel peut quand à lui rester réglable avec un seuil minimum (e.g. tous les 1/4 temps max)
  
- À ces moments-là (et uniquement), calculer les notes/freqs dominantes : max dans spectre cumulés sur plusieurs octaves
- Les regrouper en accords correspondants, si plusieurs notes/correspondance il y a 
  Option : détecter les renversements, les duplications de fondamentales, etc.

- Afficher sous forme accord - durée avec des majuscules et tirets (e.g. Cm - 2 temps, G# - 3, etc)
  et respecter la notation classique pour les accords plus complexes (e.g. Csus4/G - 4 temps)
- Afficher les notes isolées sous forme note / durée (mélodie) avec des minuscules (c1 d2 e1 e1) etc.
– Possible de mettre des | pour indiquer les mesures

- Option : notation c/2, c/4 pour les demi et quart temps, et c1 c2 c4 pour ce qui correspond à des noires, blanches, rondes sur une mesure à 4 temps.

- Plus tard : export au format lilypond OU appel direct à lilypond en tant que bibliothèque externe
  => option pour mettre sous forme de partition si c'est utile.










*/

// ==================
// Architecture notes
// ==================

// two analyzers:
// - analyze new input samples frequencies
// - analyze the song frequencies

// rtaudio callback 
// - lock
// - asks to shift the buffer for some len = available samples
// - stores the new samples
// - duplex mode : send the samples to output - don't mix multiple sources, the driver does it (e.g. jack)
// - unlock
// - warn the frequency analyzer that samples have arrived

// ffmpeg thread
// - pre-load mode: keep all samples, buffer them as they are decoded
//   (may require an extra thread?)
// - feed them to the line out / mixing in the application with line in, or use separate RTaudio?

// frequency analyzer
// - gets samples from RT thread async (or the ffmpeg thread)
// - computes the power spectrum with a much lower period (about 10 times/second)
//    - If no new sample, sleep until new samples are available
//    - discard samples if too old, only keep the last needed (rare, period << buffer size)
// - warns the display spiral that a new power spectrum has arrived

// display spiral
// - handles multiple sources: Line in may be off, song may be paused, independently of each other
//   - TODO = id for the source, also specified in callback
// - for each source (in source order, e.g. display line freqs on top of song)
//   - display the freqs with the source color

// OK archi above:
// - one RT thread from the audio system (outside app control, handled by rtaudio)
// - one thread for the spiral update, disconnected from the first, at its own freq
// - one main app thread

// Note: the above system, with sample provider notifying the spiral thread, is OK for both
// the input line and the decoded song. For the input line, this is real time samples
// for the decoded song, these are given by the ffmpeg codec. However, song samples are played
// at the real speed, but that is set by the duplex line. => maybe need some extra codec thread?


// buffer of midi output messages
Ring_Buffer *theMidiOutBuffer = NULL;

static MainWindow* main_window = 0;

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    av_register_all();
    
    ui->setupUi(this);
    
    std::map<RtAudio::Api, QString> apiMap;
    apiMap[RtAudio::MACOSX_CORE] = "OS-X Core Audio";
    apiMap[RtAudio::WINDOWS_ASIO] = "Windows ASIO";
    apiMap[RtAudio::WINDOWS_DS] = "Windows Direct Sound";
    apiMap[RtAudio::WINDOWS_WASAPI] = "Windows WASAPI";
    apiMap[RtAudio::UNIX_JACK] = "Jack Client";
    apiMap[RtAudio::LINUX_ALSA] = "Linux ALSA";
    apiMap[RtAudio::LINUX_PULSE] = "Linux PulseAudio";
    apiMap[RtAudio::LINUX_OSS] = "Linux OSS";
    apiMap[RtAudio::RTAUDIO_DUMMY] = "RtAudio Dummy";
    
    std::vector<RtAudio::Api> apis;
    RtAudio::getCompiledApi(apis);
    
    for(int i=0; i<apis.size(); ++i) ui->liste_drivers->addItem(apiMap[apis[i]], apis[i]);
    // Load the devices from the default driver
    rt_audio = new MyRtAudio(RtAudio::UNSPECIFIED);
    ui->liste_drivers->setItemText(ui->liste_drivers->currentIndex(), apiMap[rt_audio->getCurrentApi()]);
    update_devices(rt_audio->getCurrentApi());
    
    // Defaults are set in the GUI editor
    ui->spiral_display->set_min_max_notes(ui->min_freq_slider->value(), ui->max_freq_slider->value());
    ui->min_freq_label->setText(ui->spiral_display->get_note_name_and_frequency(ui->min_freq_slider->value()));
    ui->max_freq_label->setText(ui->spiral_display->get_note_name_and_frequency(ui->max_freq_slider->value()));
    ui->spiral_display->set_visual_fading(ui->visual_fading_sb->value());
    on_mic_dup_cb_toggled(ui->mic_dup_cb->isChecked());
    on_display_gain_valueChanged(ui->display_gain->value());
    
    
    main_window = this;
}

MainWindow::~MainWindow()
{
    if (rt_audio->isStreamOpen()) rt_audio->abortStream();
    
    delete record_analyzer;
    delete song_analyzer;
    
    delete rt_audio;
    delete ui;
}

void MainWindow::update_devices(RtAudio::Api api)
{
    if (!rt_audio) rt_audio = new MyRtAudio(api);
    else if (rt_audio->getCurrentApi()!=api) rt_audio->openRtApi(api);
    
    ui->liste_micros->clear();
    ui->liste_sorties->clear();
    
    int ndevices = rt_audio->getDeviceCount();
    int micro_device = 0, sortie_device = 0;
    for (int i=0; i<ndevices; ++i) {
        RtAudio::DeviceInfo info = rt_audio->getDeviceInfo(i);
        if (info.probed == false) continue;
        if (info.inputChannels>0 || info.duplexChannels>0) {
            if (info.isDefaultInput) {
                ui->liste_micros->insertItem(0, QString::fromStdString(info.name), i);
                micro_device = i;
            }
            else ui->liste_micros->addItem(QString::fromStdString(info.name), i);
        }
        if (info.outputChannels>0 || info.duplexChannels>0) {
            if (info.isDefaultOutput) {
                ui->liste_sorties->insertItem(0, QString::fromStdString(info.name), i);
                sortie_device = i;
            }
            else ui->liste_sorties->addItem(QString::fromStdString(info.name), i);
        }
    }
    int default_micro = ui->liste_micros->findData(micro_device);
    if (default_micro>=0) ui->liste_micros->setCurrentIndex(default_micro);
    int default_sortie = ui->liste_sorties->findData(sortie_device);
    if (default_sortie>=0) ui->liste_sorties->setCurrentIndex(default_sortie);
}

void MainWindow::on_liste_drivers_currentIndexChanged(int index)
{
    update_devices((RtAudio::Api)ui->liste_drivers->itemData(ui->liste_drivers->currentIndex()).toInt());
}

//static long long fake_time = 0;

int audio_available_callback(void *outputBuffer, void *inputBuffer, unsigned int nBufferFrames,
           double streamTime, RtAudioStreamStatus status, void *user_data)
{
    MainWindow* mw = (MainWindow*)user_data;

/*    
    // Replace by fake sinusoids to test the algo
    for (int i=0; i<nBufferFrames; ++i) {
        using namespace boost::math::float_constants;
        ++fake_time;
        float t = (float)fake_time / mw->sampling_rate;
        // One at C3
        ((float*)inputBuffer)[i] = sin(two_pi * t * 261.63);
        // One at E3
        ((float*)inputBuffer)[i] += sin(two_pi * t * 261.63*5/4);//329.63);//293.66);
        // One at G3
        ((float*)inputBuffer)[i] += sin(two_pi * t * 261.63*3/2);//392);
        ((float*)inputBuffer)[i] /= 3.;
    }
*/  
    
    // Only one channel was specified in the RtAudio stream definition
    // => no input muxing here TODO: handle stereo input,
    // like in the former acquire_samples<double> (see git history)
    
    mw->audio_mutex.lock();
    if (mw->is_recording && !mw->replay_mode) {
        // general mic gain before all other operations
        for (int i=0; i<nBufferFrames; ++i) ((float*)inputBuffer)[i] *= mw->mic_gain;
        
        // store data for replay
        mw->recording.emplace_back(make_pair(mw->song_position, vector<float>(nBufferFrames)));
        float* samples = &mw->recording.back().second[0];
        copy((float*)inputBuffer, (float*)inputBuffer+nBufferFrames, samples);
        
        // feed data to the analyzer associated with this line
        if (mw->record_analyzer) mw->record_analyzer->new_data(samples, nBufferFrames);
    }
    
    // process the input
    if (!mw->mic_dup) std::fill((float*)inputBuffer,(float*)inputBuffer+nBufferFrames,0.f);
    
    if (mw->replay_mode && mw->replay_position<mw->replay_view.size()) {
        float* samples = &mw->replay_view[mw->replay_position].get().second[0];
        int sample_size = mw->replay_view[mw->replay_position].get().second.size();
        if (mw->record_analyzer) mw->record_analyzer->new_data(samples, sample_size);
        copy(samples, samples + min((int)nBufferFrames,sample_size), (float*)inputBuffer);
        // in practice, sample_size and nBufferFrames are always equal
        // for Jack, but for other systems...
        for (int i=sample_size; i<nBufferFrames; ++i) ((float*)inputBuffer)[i] = 0;
    }
    
    if ((mw->is_playing && !mw->replay_mode) 
    || (mw->replay_mode && mw->replay_position<mw->replay_view.size())) {
        int song_position = mw->replay_mode ? mw->replay_view[mw->replay_position].get().first : mw->song_position;
        int nframes = mw->replay_mode ? min((int)mw->replay_view[mw->replay_position].get().second.size(),(int)nBufferFrames) : nBufferFrames;
        float* song = &mw->song[song_position];
        float* samples = (float*)inputBuffer;
        if (song_position+nframes > mw->song.size()) {
            int nplayed = mw->song.size() - song_position;
            for (int i=0; i<nplayed; ++i) {
                samples[i] = samples[i] * mw->rec_mix_factor + song[i] * mw->song_mix_factor;
            }
            for (int i=nplayed; i<=nframes; ++i) {
                samples[i] = samples[i] * mw->rec_mix_factor;
            }
            if (mw->song_analyzer) mw->song_analyzer->new_data(&mw->song[song_position], nplayed);
            if (!mw->replay_mode) mw->set_song_position(mw->song_position + nplayed, false);
        }
        else {
            for (int i=0; i<nframes; ++i) {
                samples[i] = samples[i] * mw->rec_mix_factor + song[i] * mw->song_mix_factor;
            }
            if (mw->song_analyzer) mw->song_analyzer->new_data(&mw->song[song_position], nframes);
            if (!mw->replay_mode) mw->set_song_position(mw->song_position + nframes, false);
        }
        if (mw->replay_mode) mw->set_replay_position(mw->replay_position+1, false);
    }
    mw->audio_mutex.unlock();
    
    // stereo output with non-interleaved channels is just dup
    //std::copy((float*)inputBuffer, (float*)inputBuffer+nBufferFrames, (float*)outputBuffer);
    //std::copy((float*)inputBuffer, (float*)inputBuffer+nBufferFrames, (float*)outputBuffer+nBufferFrames);
    for (int i=0; i<nBufferFrames; ++i) {
        *((float*)outputBuffer + i*2 ) = *((float*)inputBuffer + i);
        *((float*)outputBuffer + i*2 + 1) = *((float*)inputBuffer + i);
    }

    return 0;
}

void audio_error_callback(RtAudioError::Type type, const std::string &errorText) {
    // TODO: inter-thread communication, using main_window widget here from another thread is NOK
    //QMessageBox::warning(main_window,QString("Audio stream error"),QString::fromStdString(errorText));
    
    std::cout << errorText << std::endl;
}

float MainWindow::get_sample_rate()
{
    if (sampling_rate) return sampling_rate;
    
    // The sample rate parameter is problematic as it must match the device sample rate
    // And when in/out do not match, with no duplex mode, then problem.
    // For now, Use the line in rate as it might be the most limiting factor
    // NOTE: with the limitation to use devices from the same driver, the risk is limited
    // FFMPEG filter graph will produce samples at this rate
    RtAudio::DeviceInfo input_info = rt_audio->getDeviceInfo(ui->liste_micros->itemData(ui->liste_micros->currentIndex()).toInt());
    RtAudio::DeviceInfo output_info = rt_audio->getDeviceInfo(ui->liste_sorties->itemData(ui->liste_sorties->currentIndex()).toInt());
    int rate = 0;
    if (input_info.preferredSampleRate == output_info.preferredSampleRate) {
        rate = input_info.preferredSampleRate;
    } else for (int irate: input_info.sampleRates) for (int orate: output_info.sampleRates) {
        if (irate==orate && irate>rate) rate = irate;
    }
    return rate;
}

void MainWindow::setup_lines_in_out()
{
    // internal logic: lines already open <=> sampling rate set
    assert(sampling_rate == 0);
    
    // Attempt to open the lines in/out
    int ndevices = rt_audio->getDeviceCount();
    
    RtAudio::StreamParameters inputParameters;
    RtAudio::StreamParameters *ip = &inputParameters;
    ip->deviceId = ui->liste_micros->itemData(ui->liste_micros->currentIndex()).toInt();
    if (ip->deviceId<0 || ip->deviceId>=ndevices) {
        ip = 0; // no input stream
    } else {
        // TODO: handle stereo line inputs.
        // For now, take only one channel, there is a bug when 2 are specified
        ip->nChannels = 1;
    }
    
    RtAudio::StreamParameters outputParameters;
    RtAudio::StreamParameters *op = &outputParameters;
    op->deviceId = ui->liste_sorties->itemData(ui->liste_sorties->currentIndex()).toInt();
    if (op->deviceId<0 || op->deviceId>=ndevices) {
        op = 0; // no output stream
    } else {
        // Stereo out. TODO: check that this is supported by the device
        // if (info. ...)
        op->nChannels = 2;
    }
    
    RtAudio::StreamOptions options;
    //options.flags |= RTAUDIO_NONINTERLEAVED | RTAUDIO_MINIMIZE_LATENCY | RTAUDIO_SCHEDULE_REALTIME;
    // Non-interleaving does not work with all drivers :-(
    options.flags |= RTAUDIO_MINIMIZE_LATENCY | RTAUDIO_SCHEDULE_REALTIME;
    options.streamName = "Amuencha"; // For Jack
    
    try {
        sampling_rate = get_sample_rate();
        
        //unsigned int nframes = 0; // query the smallest amount of frames that can be returned, for minimal latency
        unsigned int nframes = (int)(sampling_rate * 0.001); // each 1ms

        rt_audio->openStream(op, ip, 
            // Ask for conversion to float for the frequency analyzer using SSE.
            // RtAudio internally converts no matter what the device supports
            RTAUDIO_FLOAT32,
            sampling_rate,
            &nframes,
            &audio_available_callback,
            this,
            &options,
            audio_error_callback
        );
        
        rt_audio->startStream();
    }
    catch ( RtAudioError& e ) {
        ui->chords_sequence->insertPlainText(QString::fromStdString(e.getMessage()));
        return;
    }
    
    ui->liste_drivers->setEnabled(false);
    ui->liste_micros->setEnabled(false);
    ui->liste_sorties->setEnabled(false);
    ui->periods_sb->setEnabled(false);
    ui->min_freq_slider->setEnabled(false);
    ui->max_freq_slider->setEnabled(false);
}

void MainWindow::stop_lines_in_out()
{
    rt_audio->stopStream();
    ui->liste_drivers->setEnabled(true);
    ui->liste_micros->setEnabled(true);
    ui->liste_sorties->setEnabled(true);
    ui->periods_sb->setEnabled(true);
    ui->min_freq_slider->setEnabled(true);
    ui->max_freq_slider->setEnabled(true);
    sampling_rate = 0;
    audio_mutex.lock();
    delete record_analyzer; record_analyzer = 0;
    delete song_analyzer; song_analyzer = 0;
    audio_mutex.unlock();
}

void MainWindow::on_bouton_ouvrir_clicked()
{
    AVFormatContext *fmt_ctx = 0;
    AVCodec *dec = 0;
    AVCodecContext *dec_ctx = 0;
    
    string fileName = QFileDialog::getOpenFileName(this, tr("Open File")).toStdString();
    if (fileName.empty()) return;
    if (avformat_open_input(&fmt_ctx, fileName.c_str(), 0, 0)<0) {
        QMessageBox::critical(this,tr("Can't open file"),tr("File is unreadable."));
        return;
    }
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        QMessageBox::critical(this,tr("Can't open file"),tr("File info cannot be parsed."));
        return;
    }
    int audio_stream_index = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, &dec, 0);
    if (audio_stream_index < 0) {
        QMessageBox::critical(this,tr("Can't open file"),tr("Cannot find an audio stream."));
        return;
    }
    dec_ctx = avcodec_alloc_context3(dec);
    if (!dec_ctx) {
        QMessageBox::critical(this,tr("Can't open file"),tr("Not enough memory to create the codec."));
        return;
    }
    avcodec_parameters_to_context(dec_ctx, fmt_ctx->streams[audio_stream_index]->codecpar);
    av_opt_set_int(dec_ctx, "refcounted_frames", 1, 0);
    if (avcodec_open2(dec_ctx, dec, NULL) < 0) {
        QMessageBox::critical(this,tr("Can't open file"),tr("Format is not recognized."));
        return;
    }
    
    AVPacket packet;
    AVFrame *frame = av_frame_alloc();
    if (!frame) {
        QMessageBox::critical(this,tr("Can't decode file"),tr("Not enough memory to create the frames."));
        return;
    }
    
    SwrContext *swr = swr_alloc();
    av_opt_set_int(swr, "in_channel_layout",  dec_ctx->channel_layout, 0);
    av_opt_set_int(swr, "out_channel_layout", AV_CH_LAYOUT_MONO ,  0);
    av_opt_set_int(swr, "in_sample_rate",     dec_ctx->sample_rate, 0);
    av_opt_set_int(swr, "out_sample_rate",    (int)get_sample_rate(), 0);
    av_opt_set_sample_fmt(swr, "in_sample_fmt",  dec_ctx->sample_fmt, 0);
    av_opt_set_sample_fmt(swr, "out_sample_fmt", AV_SAMPLE_FMT_FLTP,  0);
    swr_init(swr);
    
    int estimated_num_samples = (int)((int64_t)get_sample_rate() * fmt_ctx->duration / AV_TIME_BASE);
    song.clear();
    // reserve with little extra half-second for the approximation
    song.reserve(estimated_num_samples+(int64_t)(get_sample_rate()*0.5));
    
    QProgressDialog progress(tr("Loading song..."), tr("Abort"), 0, estimated_num_samples, this);
    progress.setWindowModality(Qt::WindowModal);

    while (true) {
        if (av_read_frame(fmt_ctx, &packet)<0) break;
        if (packet.stream_index!=audio_stream_index) {
            av_packet_unref(&packet);
            continue;
        }
        
        if (avcodec_send_packet(dec_ctx, &packet)<0) {
            QMessageBox::critical(this,tr("Can't decode file"),tr("Error while sending a packet to the decoder."));
            break;
        }
        int ret = 1;
        while (ret >= 0) {
            ret = avcodec_receive_frame(dec_ctx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) {
                QMessageBox::critical(this,tr("Can't decode file"),tr("Error while receiving a frame from the decoder."));
                goto end;
            }
            
            int cur_size = song.size();
            song.resize(cur_size+frame->nb_samples);
            uint8_t* outbuf = reinterpret_cast<uint8_t*>(&song[cur_size]);
            swr_convert(swr, &outbuf, frame->nb_samples, const_cast<const uint8_t**>(frame->extended_data), frame->nb_samples);
            
            progress.setValue(song.size());
            
            av_frame_unref(frame);
        }
        av_packet_unref(&packet);
    }
end:
    // flush the last few samples, second by second of output
    int cur_size = song.size();
    int max_to_read = song.capacity() - song.size();
    song.resize(song.capacity());
    uint8_t* outbuf = reinterpret_cast<uint8_t*>(&song[cur_size]);
    int num_read = swr_convert(swr, &outbuf, max_to_read, 0, 0);
    song.resize(cur_size+num_read);
    
    progress.setValue(song.size());
    avcodec_free_context(&dec_ctx);
    avformat_close_input(&fmt_ctx);
    av_frame_free(&frame);
    swr_free(&swr);
    
    progress.setValue(estimated_num_samples);
    
    //ui->chords_sequence->appendPlainText("Read "+QString::number(song.size()));
    if (song.size()>0) {
        ui->play_pause->setEnabled(true);
        ui->positionChanson->setEnabled(true);
        ui->positionChanson->setValue(0);
    }
    song_position = 0;
    ui->play_pause->click();
}

void MainWindow::common_clicked(bool& is_doing, QPushButton* button, 
                    const char* theme_start, const char* theme_stop, 
                    Frequency_Analyzer* &analyzer, int id) 
{
    audio_mutex.lock();
    if (is_doing) {
        is_doing = false;
        audio_mutex.unlock();
        
        if (!is_recording && !is_playing && !replay_mode) stop_lines_in_out();
        
        button->setIcon(QIcon::fromTheme(theme_start));
        
        return;
    }
    audio_mutex.unlock();
    
    if (!sampling_rate) setup_lines_in_out();
    if (!sampling_rate) return; // error while setting the lines

    audio_mutex.lock();
    
    if (!analyzer) {
        analyzer = new Frequency_Analyzer(this);
        using namespace std::placeholders;
        analyzer->setup(sampling_rate, 
                        ui->spiral_display->frequencies, 
                        std::bind(&SpiralDisplay::power_handler, ui->spiral_display, id, _1, _2), 
                        ui->periods_sb->value());
        analyzer->start(QThread::NormalPriority);
    }
    
    is_doing = true;
    audio_mutex.unlock();
    
    button->setIcon(QIcon::fromTheme(theme_stop));
}

void MainWindow::on_play_pause_clicked()
{
    common_clicked(is_playing, ui->play_pause, 
                   "media-playback-start", "media-playback-pause",
                   song_analyzer, 0);
}

void MainWindow::on_bouton_enregistrer_clicked()
{
    common_clicked(is_recording, ui->bouton_enregistrer, 
                   "media-record", "media-playback-stop",
                   record_analyzer, 1);
    // just started new recording => erase old samples
    if (is_recording) {
        audio_mutex.lock();
        recording.clear();
        replay_view.clear();
        audio_mutex.unlock();
    }

    // whether starting new, or stopping old, replay is allowed now
    ui->bouton_rejouer->setEnabled(true);
    ui->positionRecord->setEnabled(true);
    ui->clear_record->setEnabled(true);
    //set_replay_position(0);
}

void MainWindow::on_bouton_rejouer_clicked()
{
    audio_mutex.lock();
    if (replay_mode) {
        replay_mode = false;
        audio_mutex.unlock();
        if (!is_recording && !is_playing && !replay_mode) stop_lines_in_out();
        ui->bouton_rejouer->setIcon(QIcon::fromTheme("media-playback-start"));
        ui->positionRecord->setEnabled(false);
        ui->bouton_enregistrer->setEnabled(true);
        ui->play_pause->setEnabled(!song.empty());
        ui->positionChanson->setEnabled(!song.empty());
        return;
    }
    
    if (recording.empty()) {
        audio_mutex.unlock();
        return;
    }
    
    replay_mode = true;
    replay_view.clear();
    replay_view.insert(replay_view.end(), recording.begin(), recording.end());
    set_replay_position(0,false);

    audio_mutex.unlock();
    
    if (!sampling_rate) setup_lines_in_out();
    if (!sampling_rate) return; // error while setting the lines

    audio_mutex.lock();
    if (!record_analyzer) {
        record_analyzer = new Frequency_Analyzer(this);
        using namespace std::placeholders;
        record_analyzer->setup(sampling_rate, 
                        ui->spiral_display->frequencies, 
                        std::bind(&SpiralDisplay::power_handler, ui->spiral_display, 1, _1, _2), 
                        ui->periods_sb->value());
        record_analyzer->start(QThread::NormalPriority);
    }
    audio_mutex.unlock();
    
    ui->bouton_rejouer->setIcon(QIcon::fromTheme("media-playback-stop"));
    ui->bouton_enregistrer->setEnabled(false);
    ui->play_pause->setEnabled(false);
    ui->positionChanson->setEnabled(false);
    ui->positionRecord->setEnabled(true);
}

void MainWindow::on_clear_record_clicked()
{
    // stop replay mode
    audio_mutex.lock();
    if (replay_mode) {
        replay_mode = false;
        audio_mutex.unlock();
        if (!is_recording && !is_playing && !replay_mode) stop_lines_in_out();
        ui->bouton_rejouer->setIcon(QIcon::fromTheme("media-playback-start"));
        ui->positionRecord->setEnabled(false);
        ui->bouton_enregistrer->setEnabled(true);
        ui->play_pause->setEnabled(!song.empty());
        ui->positionChanson->setEnabled(!song.empty());
        audio_mutex.lock();
    }
    
    if (record_analyzer) record_analyzer->invalidate_samples();
    
    recording.clear();
    replay_view.clear();
    audio_mutex.unlock();
    
    ui->bouton_rejouer->setEnabled(is_recording);
    ui->positionRecord->setEnabled(false);
    ui->positionRecord->setValue(0);
}

void MainWindow::on_gain_valueChanged(int value)
{
    mic_gain = pow(value*0.01, 4);
}

void MainWindow::on_display_gain_valueChanged(int value)
{
    // max value*0.01 is 2, hence max gain here is 16
    // but the non-linear progression makes it easier to cover larger gain range depending on mic
    ui->spiral_display->set_gain(pow(value*0.01, 4));
}

void MainWindow::on_mic_dup_cb_toggled(bool checked)
{
    mic_dup = checked;
}

void MainWindow::on_min_freq_slider_valueChanged(int value)
{
    ui->min_freq_label->setText(ui->spiral_display->get_note_name_and_frequency(value));
    ui->spiral_display->set_min_max_notes(ui->min_freq_slider->value(), ui->max_freq_slider->value());
}

void MainWindow::on_max_freq_slider_valueChanged(int value)
{
    ui->max_freq_label->setText(ui->spiral_display->get_note_name_and_frequency(value));
    ui->spiral_display->set_min_max_notes(ui->min_freq_slider->value(), ui->max_freq_slider->value());
}

void MainWindow::on_visual_fading_sb_valueChanged(int value)
{
    ui->spiral_display->set_visual_fading(value);
}

void MainWindow::on_song_rec_mix_valueChanged(int value)
{
    song_mix_factor = (value<=50) ? 1.0f : ((100-value) / 50.f);
    rec_mix_factor = (value>=50) ? 1.0f : (value / 50.f);
}

void MainWindow::set_song_position(int64_t position, bool lock) {
    if (lock) audio_mutex.lock();
    song_position = position;
    ui->positionChanson->blockSignals(true);
    ui->positionChanson->setValue((int)(position*100/(int64_t)song.size()));
    ui->positionChanson->blockSignals(false);
    if (lock) audio_mutex.unlock();
}

void MainWindow::set_replay_position(int64_t position, bool lock) {
    if (lock) audio_mutex.lock();
    replay_position = position;
    ui->positionRecord->blockSignals(true);
    ui->positionRecord->setValue((int)(position*100/(int64_t)replay_view.size()));
    ui->positionRecord->blockSignals(false);
    if (lock) audio_mutex.unlock();
}

void MainWindow::on_positionChanson_valueChanged(int value)
{
    audio_mutex.lock();
    song_position = (int64_t)value * (int64_t)song.size() / 100;
    audio_mutex.unlock();
}

void MainWindow::on_positionRecord_valueChanged(int value)
{
    audio_mutex.lock();
    replay_position = (int64_t)value * (int64_t)replay_view.size() / 100;
    audio_mutex.unlock();
}
