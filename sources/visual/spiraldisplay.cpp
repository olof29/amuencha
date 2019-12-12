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

#include <QPainter>
#include <QPainterPath>
#include <QPointF>
//#include <QPalette>
#include "spiraldisplay.h"

#include <iostream>
#include <fstream>
#include <cmath>
#include <complex>
#include <boost/math/constants/constants.hpp>

using namespace boost::math::float_constants;
using namespace std;

SpiralDisplay::SpiralDisplay(QWidget *parent) : QWidget(parent)
{
    QPalette pal(palette());
    pal.setColor(QPalette::Background, Qt::white);
    setAutoFillBackground(true);
    setPalette(pal);
    
    // 12ET
    // Other music systems like are best kept for later... but doable in practice
    // with different base note (A440 here) and different note names, splits, etc
    note_positions.resize(12);
    for (int i=0; i<12; ++i) note_positions[i] = polar(0.9, half_pi-i*two_pi/12.);
    note_names.resize(12);
    note_names = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    
    display_spectrum.resize(num_ID);
    all_spirals.resize(num_ID);
}

void SpiralDisplay::set_min_max_notes(int min_midi_note, int max_midi_note) 
{
    if (max_midi_note<min_midi_note) swap(min_midi_note, max_midi_note);
    
    this->min_midi_note = min_midi_note;
    this->max_midi_note = max_midi_note;
    display_bins.clear();
    QPainterPath empty;
    base_spiral.swap(empty);
    for(int id=0; id<num_ID; ++id) all_spirals[id].clear();
    update();
}

void SpiralDisplay::set_gain(float gain)
{
    this->gain = gain;
}

QString SpiralDisplay::get_note_name_and_frequency(int midi_note)
{
    const float fref = 440;
    const float log2_fref = log2(fref);
    const int aref = 69; // use the midi numbering scheme, because why not
    float f = exp2((midi_note - aref)/12. + log2_fref);
    int octave_number = midi_note/12 - 2;
    return note_names[midi_note%12]+QString::number(octave_number)+" ("+QString::number(f,'f',3)+"Hz)";
}

void SpiralDisplay::compute_frequencies()
{
    const float w = width();
    const float h = height();
    const float hw = w<h ? w : h;
    // Now the spiral
    // Start with A440, but this could be parametrizable as well
    const float fref = 440;
    const float log2_fref = log2(fref);
    const int aref = 69; // use the midi numbering scheme, because why not
    float log2_fmin = (min_midi_note - aref)/12. + log2_fref;
    float log2_fmax = (max_midi_note - aref)/12. + log2_fref;
    int approx_pix_bin_width = 3;
    // number of frequency bins is the number of pixels
    // along the spiral path / approx_pix_bin_width 
    // According to mathworld, the correct formula for the path length
    // from the origin involves sqrt and log computations.
    // Here, we just want some approximate pixel count
    // => use all circles for the approx
    int num_octaves = (max_midi_note - min_midi_note +11)/12;
    float approx_num_pix = 0.5 * hw * pi * num_octaves;
    int num_bins = (int)(approx_num_pix / approx_pix_bin_width);
    // one more bound than number of bins
    display_bins.resize(num_bins+1);
    bin_sizes.resize(num_bins);
    spiral_positions.resize(num_bins+1);
    spiral_r_a.resize(num_bins+1);
    const float rmin = 0.1;
    const float rmax = 0.9;
    // The spiral and bounds are the same independently of how 
    // the log space is divided into notes (e.g. 12ET)
    // Make it so c is on the y axis. Turn clockwise because people are 
    // used to it (e.g. wikipedia note circle)
    const float theta_min = half_pi - two_pi*(min_midi_note%12)/12;
    // wrap in anti-trigonometric direction
    const float theta_max = theta_min - two_pi*(max_midi_note - min_midi_note)/12;
    
    frequencies.resize(num_bins);
    for (int b=0; b<num_bins; ++b) {
        float bratio = (float)b/(num_bins-1.);
        frequencies[b] = exp2(log2_fmin + (log2_fmax - log2_fmin) * bratio);
    }
    for (int b=0; b<=num_bins; ++b) {
        float bratio = (float)(b-0.5)/(float)(num_bins-1.);
        display_bins[b] = exp2(log2_fmin + (log2_fmax - log2_fmin) * bratio);
        spiral_r_a[b].r = rmin + (rmax - rmin) * bratio;
        spiral_r_a[b].a = theta_min + (theta_max - theta_min) * bratio;
        spiral_positions[b] = polar(spiral_r_a[b].r, spiral_r_a[b].a);
    }
    for (int b=0; b<num_bins; ++b) {
        bin_sizes[b] = display_bins[b+1]-display_bins[b];
    }
    for (int id=0; id<num_ID; ++id) {
        display_spectrum[id].resize(num_bins);
        fill(display_spectrum[id].begin(), display_spectrum[id].end(), 0.);
    }
}

void SpiralDisplay::power_handler(int ID, const vector<float>& reassigned_frequencies, const vector<float>& power_spectrum)
{
    
    fill(display_spectrum[ID].begin(), display_spectrum[ID].end(), 0.);
    
    // simple histogram-like sum, assuming power entries are normalized
    int nidx = reassigned_frequencies.size();
    for (int idx=0; idx<nidx; ++idx) {
        float rf = reassigned_frequencies[idx];
        int ri = idx;
        // reassigned frequencies are never too far off the original
        //if (rf>display_bins[idx] && rf<display_bins[idx+1]) ri = idx;
        //else...
        while (rf<display_bins[ri]) {
            --ri;
            if (ri==-1) break;
        }
        if (ri==-1) continue; // ignore this frequency, it is below display min
        while (rf>display_bins[ri+1]) {
            ++ri;
            if (ri==nidx) break;
        }
        if (ri==nidx) continue; // ignore this frequency, it is above display max
        
        // Normalization:
        // - for a given frequency, the sine/window size dependency was already
        //   handled in the frequency analyzer
        // - but the result should not depend on how many frequencies are provided:
        //   increasing the resolution should not increase the power
        // => we need a kind of density, not just the histogram-like sum of powers
        //    falling into each bin
        // - consider the energy is coming from all the original bin size & sum
        // - This way, using finer bins do not increase the total sum
        display_spectrum[ID][ri] += power_spectrum[idx] * bin_sizes[idx];
    }
    
    // - Then, spread on the destination bin for getting uniform density
    //   measure independently of the target bin size
    for (int idx=0; idx<nidx; ++idx) display_spectrum[ID][idx] /= bin_sizes[idx];

    this->update();
}

void SpiralDisplay::set_visual_fading(int value)
{
    visual_fading = max(1,value);
}
    
void SpiralDisplay::paintEvent(QPaintEvent *event) {
    QPainter painter(this);
    const float w = width();
    const float h = height();
    const float hw = w<h ? w : h;
    const float w_half = w * 0.5;
    const float h_half = h * 0.5;
    const float hw_half = hw*0.5;
    auto xy = [&](float x, float y) {
        //return QPointF((x+1.)*.5*width(), (1.-y)*.5*height());
        return QPointF(w_half + x * hw_half, h_half - y * hw_half);
    };
    
    if (display_bins.empty()) {
        compute_frequencies();
    }
    
    painter.setPen(Qt::black);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QFont font = painter.font();
    font.setPointSize(font.pointSizeF()*1.1);
    QPainterPath path;
    // twelve notes, TODO = temperament. Here 12ET
    for (int i=0; i<12; ++i) {
        path.moveTo(xy(0.,0.));
        path.lineTo(xy(note_positions[i].real(), note_positions[i].imag()));
        painter.drawText(xy(note_positions[i].real()*1.05-0.02, note_positions[i].imag()*1.05-0.01), note_names[i]);
    }
    painter.drawPath(path);
    
    int num_octaves = (max_midi_note - min_midi_note +11)/12;
    
    for (int id=0; id<num_ID; ++id) {
        QPainterPath power_spiral;
        power_spiral.moveTo(xy(spiral_positions[0].real(),spiral_positions[0].imag()));
        for (int b=0; b<display_spectrum[id].size(); ++b) {
            float amplitude = 0.8/num_octaves * min(1.f, display_spectrum[id][b] * gain);
            //if (display_spectrum[id][b]>0) cout << display_spectrum[id][b] << endl;
            // power normalised between 0 and 1 => 0.1 = spiral branch
            float r = spiral_r_a[b].r + amplitude;
            auto p = polar(r, spiral_r_a[b].a);
            power_spiral.lineTo(xy(p.real(),p.imag()));
            r = spiral_r_a[b+1].r + amplitude;
            p = polar(r, spiral_r_a[b+1].a);
            power_spiral.lineTo(xy(p.real(),p.imag()));
        }
        for (int b=spiral_positions.size()-1; b>=0; --b) {
            power_spiral.lineTo(xy(spiral_positions[b].real(),spiral_positions[b].imag()));
        }
        
        int nspirals = all_spirals[id].size();
        if (nspirals>=visual_fading) {
            for (int vf = visual_fading; vf<=nspirals; ++vf) all_spirals[id].pop_front();
            nspirals=visual_fading;
        }
        else ++nspirals;
        all_spirals[id].emplace_back(power_spiral.simplified());
        
        int spidx = 1;
        for (const auto& spiral: all_spirals[id]) {
            // whitefactor is 0 for the newest one
            int whitefactor = 255 - 255*spidx/nspirals;
            QColor c;
            if (id==1) c = QColor::fromRgb(255,whitefactor,whitefactor);
            else c = QColor::fromRgb(whitefactor,whitefactor,255);
            painter.setBrush(c);
            painter.setPen(c);
            painter.drawPath(spiral);
            ++spidx;
        }
    }
    
    // Overlay the base spiral in black
    if (base_spiral.isEmpty()) {
        base_spiral.moveTo(xy(spiral_positions.back().real(),spiral_positions.back().imag()));
        for (int b=spiral_positions.size()-1; b>=0; --b) {
            base_spiral.lineTo(xy(spiral_positions[b].real(),spiral_positions[b].imag()));
        }
        //base_spiral = base_spiral.simplified();
    }

    painter.setPen(Qt::black);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(base_spiral);
    
}

void SpiralDisplay::resizeEvent(QResizeEvent *event)
{
    QPainterPath emptyPath;
    base_spiral.swap(emptyPath);
    for (int id=0; id<num_ID; ++id) all_spirals[id].clear();
}
