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

#ifndef CLICKABLESLIDER_H
#define CLICKABLESLIDER_H

#include <QSlider>

// Slider that moves to the click position. Code taken from
// https://stackoverflow.com/questions/11132597/qslider-mouse-direct-jump
class ClickableSlider : public QSlider
{
public:
    ClickableSlider(QWidget *parent = 0);
    
protected:
    void mousePressEvent(QMouseEvent *event);
};

#endif // CLICKABLESLIDER_H
