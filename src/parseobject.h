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
#ifndef PARSEOBJECT_H
#define PARSEOBJECT_H

#include <QtSql>
#include <QDateTime>
#include <QDomDocument>
#include <QQueue>
#include <QObject>
#include <QUrl>

struct FeedItemStruct {
  QString title;
  QString updated;
  QString link;
  QString linkBase;
  QString language;
  QString author;
  QString authorUri;
  QString authorEmail;
  QString description;
};

struct NewsItemStruct {
  QString id;
  QString title;
  QString updated;
  QString link;
  QString linkAlternate;
  QString language;
  QString author;
  QString authorUri;
  QString authorEmail;
  QString description;
  QString content;
  QString category;
  QString eUrl;
  QString eType;
  QString eLength;
};

struct FeedCountStruct{
  int feedId;
  int parentId;
  int unreadCount;
  int newCount;
  int undeleteCount;
  QString updated;
  QString lastBuildDate;
  QString htmlUrl;
  QString xmlUrl;
  QString title;
};

Q_DECLARE_METATYPE(FeedCountStruct)

class RSSListing;

class ParseObject : public QObject
{
  Q_OBJECT
public:
  explicit ParseObject(QObject *parent);

public slots:
  void parseXml(const QByteArray &data, const QString &feedUrl,
                const QDateTime &dtReply);

signals:
  void signalReadyParse(const QByteArray &xml, const QString &feedUrl,
                        const QDateTime &dtReply);
  void feedUpdated(const QString &feedUrl, const bool &changed, int newCount);
  void feedCountsUpdate(FeedCountStruct counts);

private slots:
  void getQueuedXml();
  void slotParse(const QByteArray &xmlData, const QString &feedUrl,
                 const QDateTime &dtReply);
  void addAtomNewsIntoBase(NewsItemStruct &newsItem);
  void addRssNewsIntoBase(NewsItemStruct &newsItem);

private:
  void parseAtom(const QString &feedUrl, const QDomDocument &doc);
  void parseRss(const QString &feedUrl, const QDomDocument &doc);
  QString toPlainText(const QString &text);
  QString parseDate(const QString &dateString, const QString &urlString);
  int recountFeedCounts(int feedId, const QString &feedUrl,
                        const QString &updated, const QString &lastBuildDate);

  RSSListing* rssl_;
  QTimer *parseTimer_;
  QString currentFeedUrl_;
  QByteArray currentXml_;
  QDateTime currentDtReady_;
  QQueue<QString> feedsQueue_;
  QQueue<QByteArray> xmlsQueue_;
  QQueue<QDateTime> dtReadyQueue_;

  int parseFeedId_;
  bool duplicateNewsMode_;
  bool feedChanged_;

};

#endif // PARSEOBJECT_H
