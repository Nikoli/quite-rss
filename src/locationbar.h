/* =============================================================================
* QuiteRSS is a open-source cross-platform RSS/Atom news feeds reader
* Copyright (C) 2011-2014 QuiteRSS Team <quiterssteam@gmail.com>
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
* =========================================================================== */
#ifndef LOCATIONBAR_H
#define LOCATIONBAR_H

#include <QLineEdit>

class LocationBar : public QLineEdit
{
  Q_OBJECT
public:
  LocationBar(QWidget *parent = 0);

private:
  void mouseReleaseEvent(QMouseEvent*);
  void focusInEvent(QFocusEvent *event);

  bool focus_;

};

#endif // LOCATIONBAR_H
