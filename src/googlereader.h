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
#ifndef GOOGLEREADER_H
#define GOOGLEREADER_H

#include <QtGui>
#include <QNetworkReply>

#define ADDFEED    "subscribe"
#define REMOVEFEED "unsubscribe"
#define RENAMEFEED "edit"

class GoogleReader : public QObject
{
  Q_OBJECT
public:
  explicit GoogleReader(QString email, QString passwd, QObject *parent = 0);

  void editFeed(QString urlFeed, QString action, QString name = "");
  void requestFeedsList();
  void requestUnreadCount();
  void requestFeed(QString urlFeed, int ot = 0);
  void editItem(QString urlFeed, QString itemId, QString action);

  QString email_;
  QString passwd_;

private:
  void requestSidAuth();
  void sendHttpPost(QUrl url, QUrl params);
  void sendHttpGet(QUrl url, QNetworkAccessManager *manager);

  QTimer *sessionTimer_;

  QNetworkAccessManager managerAuth_;
  QNetworkAccessManager managerToken_;
  QNetworkAccessManager managerHttpPost_;
  QNetworkAccessManager managerFeedsList_;
  QNetworkAccessManager managerUnreadCount_;
  QNetworkAccessManager managerFeed_;

  QString sid_;
  QString auth_;
  QString token_;
  QUrl requestUrl_;
  QUrl postArgs_;

signals:
  void signalReplySidAuth(bool ok);
  void signalReplyToken(bool ok);
  void signalReplyHttpPost(bool ok);

private slots:
  void replySidAuth(QNetworkReply *reply);
  void requestToken();
  void replyToken(QNetworkReply *reply);
  void replyHttpPost(QNetworkReply *reply);
  void replyFeedsList(QNetworkReply *reply);
  void replyUnreadCount(QNetworkReply *reply);
  void replyFeed(QNetworkReply *reply);

};

#endif // GOOGLEREADER_H
