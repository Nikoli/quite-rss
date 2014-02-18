/* ============================================================
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
* ============================================================ */
#ifndef UPDATEAPPDIALOG_H
#define UPDATEAPPDIALOG_H

#include <QNetworkReply>
#include <QWebPage>
#include <QWebFrame>

#include "dialog.h"

class UpdateAppDialog : public Dialog
{
  Q_OBJECT
public:
  explicit UpdateAppDialog(const QString &lang, QWidget *parent, bool show = true);
  ~UpdateAppDialog();

signals:
  void signalNewVersion(const QString &newVersion);

private slots:
  void closeDialog();
  void finishUpdatesChecking();
  void slotFinishHistoryReply();
  void updaterRun();
  void renderStatistics();

private:
  QString lang_;
  bool showDialog_;

  QWebPage *page_;
  QNetworkReply *reply_;
  QNetworkReply *historyReply_;

  QLabel *infoLabel;
  QTextBrowser *history_;

  QPushButton *installButton_;
  QCheckBox *remindAboutVersion_;

};

#endif // UPDATEAPPDIALOG_H
