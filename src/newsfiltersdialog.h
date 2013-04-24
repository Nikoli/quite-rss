/* ============================================================
* QuiteRSS is a open-source cross-platform RSS/Atom news feeds reader
* Copyright (C) 2011-2013 QuiteRSS Team <quiterssteam@gmail.com>
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
* ============================================================ */
#ifndef NEWSFILTERSDIALOG_H
#define NEWSFILTERSDIALOG_H

#include "dialog.h"

class NewsFiltersDialog : public Dialog
{
  Q_OBJECT
private:
  QSettings *settings_;

  QPushButton *editButton;
  QPushButton *deleteButton;
  QPushButton *moveUpButton;
  QPushButton *moveDownButton;
  QPushButton *applyFilterButton;

public:
  explicit NewsFiltersDialog(QWidget *parent = 0, QSettings *settings = 0);
  QTreeWidget *filtersTree;

private slots:
  void closeDialog();
  void newFilter();
  void editFilter();
  void deleteFilter();

  void moveUpFilter();
  void moveDownFilter();

  void slotCurrentItemChanged(QTreeWidgetItem *current, QTreeWidgetItem *);
  void slotItemChanged(QTreeWidgetItem *item,int column);

  void applyFilter();

};

#endif // NEWSFILTERSDIALOG_H
