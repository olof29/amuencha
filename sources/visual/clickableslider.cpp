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

#include <QWidget>
#include <QStyleOptionSlider>
#include <QRect>
#include <QMouseEvent>

#include "clickableslider.h"

ClickableSlider::ClickableSlider(QWidget *parent) : QSlider(parent) {
}

// Code taken from
// https://stackoverflow.com/questions/11132597/qslider-mouse-direct-jump
void ClickableSlider::mousePressEvent(QMouseEvent *event) {
    QStyleOptionSlider opt;
    initStyleOption(&opt);
    QRect sr = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);

    if (event->button() == Qt::LeftButton &&
        !sr.contains(event->pos())) {
      int newVal;
      if (orientation() == Qt::Vertical) {
         double halfHandleHeight = (0.5 * sr.height()) + 0.5;
         int adaptedPosY = height() - event->y();
         if ( adaptedPosY < halfHandleHeight )
               adaptedPosY = halfHandleHeight;
         if ( adaptedPosY > height() - halfHandleHeight )
               adaptedPosY = height() - halfHandleHeight;
         double newHeight = (height() - halfHandleHeight) - halfHandleHeight;
         double normalizedPosition = (adaptedPosY - halfHandleHeight)  / newHeight ;

         newVal = minimum() + (maximum()-minimum()) * normalizedPosition;
      } else {
          double halfHandleWidth = (0.5 * sr.width()) + 0.5;
          int adaptedPosX = event->x();
          if ( adaptedPosX < halfHandleWidth )
                adaptedPosX = halfHandleWidth;
          if ( adaptedPosX > width() - halfHandleWidth )
                adaptedPosX = width() - halfHandleWidth;
          double newWidth = (width() - halfHandleWidth) - halfHandleWidth;
          double normalizedPosition = (adaptedPosX - halfHandleWidth)  / newWidth ;

          newVal = minimum() + ((maximum()-minimum()) * normalizedPosition);
      }

      if (invertedAppearance())
          setValue( maximum() - newVal );
      else
          setValue(newVal);

      event->accept();
    } else {
      QSlider::mousePressEvent(event);
    }
}
