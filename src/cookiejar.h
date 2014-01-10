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
#ifndef COOKIEJAR_H
#define COOKIEJAR_H

#include <QFile>
#include <QStringList>
#include <QNetworkCookieJar>

class CookieJar : public QNetworkCookieJar
{
  Q_OBJECT
public:
  explicit CookieJar(QString dataDirPath, int saveCookies, QObject *parent);

  bool setCookiesFromUrl(const QList<QNetworkCookie> &cookieList, const QUrl &url);

  QList<QNetworkCookie> getAllCookies();
  void setAllCookies(const QList<QNetworkCookie> &cookieList);

  void saveCookies();
  void loadCookies();

  int saveCookies_;

public slots:
  void clearCookies();

private:
  QString dataDirPath_;

};

#endif // COOKIEJAR_H
