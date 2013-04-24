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
#ifndef NEWSMODEL_H
#define NEWSMODEL_H

#include <QtSql>
#include <QtGui>

class NewsModel : public QSqlTableModel
{
  Q_OBJECT
private:
  QTreeView *view_;

public:
  NewsModel(QObject *parent, QTreeView *view);
  virtual QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const;
  virtual QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const;
  virtual bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole);
  virtual void sort(int column, Qt::SortOrder order);
  virtual QModelIndexList match(
      const QModelIndex &start, int role, const QVariant &value, int hits = 1,
      Qt::MatchFlags flags =
      Qt::MatchFlags(Qt::MatchExactly|Qt::MatchWrap)
      ) const;
  QString formatDate_;
  QString formatTime_;
  bool simplifiedDateTime_;

signals:
  void signalSort(int column, int order);

};

#endif // NEWSMODEL_H
