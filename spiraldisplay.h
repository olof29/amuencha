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

#ifndef SPIRAL_DISPLAY_H
#define SPIRAL_DISPLAY_H

#include <QWidget>
#include <QPaintEvent>

#include <vector>
#include <list>
#include <complex>

class SpiralDisplay : public QWidget
{
    Q_OBJECT
public:
    explicit SpiralDisplay(QWidget *parent = 0);
    
    void set_min_max_notes(int min_midi_note, int max_midi_note);
    void set_gain(float gain);

    inline float get_min_frequency() {return display_bins.front();}
    inline float get_max_frequency() {return display_bins.back();}
    
    QString get_note_name_and_frequency(int midi_note);
    
    // central frequencies (log space)
    std::vector<float> frequencies;
    
    // Callback when the power spectrum is available at the prescribed frequencies
    // The ID is that of the caller, setting the color of the display
    void power_handler(int ID, const std::vector<float>& reassigned_frequencies, const std::vector<float>& power_spectrum);
    
    void set_visual_fading(int value);
    
    static const int num_ID = 2;
    
protected:
    int min_midi_note, max_midi_note;
    float gain = 1;
    
    void compute_frequencies();
    
    // local copy for maintaining the display, adapted to the drawing bins
    std::vector<std::vector<float>> display_spectrum;
    
    // bin low bounds, each bin consists of [f_b, f_b+1)
    std::vector<float> display_bins;
    // duplicate info for faster processing = delta_f in each bin
    std::vector<float> bin_sizes;
    
    // xy position of that frequency bin bound on the spiral
    std::vector<std::complex<float>> spiral_positions;
    // same info, but r.exp(angle)
    // avoid all the sqrt, cos and sin at each redraw
    struct Radius_Angle {float r, a;};
    std::vector<Radius_Angle> spiral_r_a;
    // 12ET by default
    std::vector<std::complex<float>> note_positions;
    std::vector<QString> note_names;

    std::vector<std::list<QPainterPath>> all_spirals;
    QPainterPath base_spiral;
    int visual_fading;
    
    // QWidget interface
    void paintEvent(QPaintEvent *event);
    void resizeEvent(QResizeEvent *event);
};

#endif // AVDISPLAY_H
