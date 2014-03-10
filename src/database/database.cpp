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
#include "database.h"

#include "common.h"
#include "mainapplication.h"
#include "mainwindow.h"
#include "settings.h"
#include "VersionNo.h"

const int versionDB = 15;

const QString kCreateFeedsTableQuery(
    "CREATE TABLE feeds("
    "id integer primary key, "
    "text varchar, "             // Feed text (replaces title at the moment)
    "title varchar, "            // Feed title
    "description varchar, "      // Feed description
    "xmlUrl varchar, "           // URL-link of the feed
    "htmlUrl varchar, "          // URL-link site, that contains the feed
    "language varchar, "         // Feed language
    "copyrights varchar, "       // Feed copyrights
    "author_name varchar, "      // Feed author: name
    "author_email varchar, "     //              e-mail
    "author_uri varchar, "       //              personal web page
    "webMaster varchar, "        // e-mail of feed's technical support
    "pubdate varchar, "          // Feed publication timestamp
    "lastBuildDate varchar, "    // Timestamp of last modification of the feed
    "category varchar, "         // Categories of content of the feed
    "contributor varchar, "      // Feed contributors (tab separated)
    "generator varchar, "        // Application has used to generate the feed
    "docs varchar, "             // URL-link to document describing RSS-standart
    "cloud_domain varchar, "     // Web-service providing rssCloud interface
    "cloud_port varchar, "       //   .
    "cloud_path varchar, "       //   .
    "cloud_procedure varchar, "  //   .
    "cloud_protocal varchar, "   //   .
    "ttl integer, "              // Time in minutes the feed can be cached
    "skipHours varchar, "        // Tip for aggregators, not to update the feed (specify hours of the day that can be skipped)
    "skipDays varchar, "         // Tip for aggregators, not to update the feed (specify day of the week that can be skipped)
    "image blob, "               // gif, jpeg, png picture, that can be associated with the feed
    "unread integer, "           // number of unread news
    "newCount integer, "         // number of new news
    "currentNews integer, "      // current displayed news
    "label varchar, "            // user purpose label(s)
    "undeleteCount integer, "    // number of all news (not marked deleted)
    "tags varchar, "             // user purpose tags
    // --- Categories ---
    "hasChildren integer default 0, "  // Children presence. Default - none
    "parentId integer default 0, "     // parent id of the feed. Default - tree root
    "rowToParent integer, "            // sequence number relative to parent
    // --- General ---
    "updateIntervalEnable int, "    // auto update enable flag
    "updateInterval int, "          // auto update interval
    "updateIntervalType varchar, "  // auto update interval type(minutes, hours,...)
    "updateOnStartup int, "         // update the feed on aplication startup
    "displayOnStartup int, "        // show the feed in separate tab in application startup
    // --- Reading ---
    "markReadAfterSecondsEnable int, "    // Enable "Read" timer
    "markReadAfterSeconds int, "          // Number of seconds that must elapse to mark news "Read"
    "markReadInNewspaper int, "           // mark Read when Newspaper layout
    "markDisplayedOnSwitchingFeed int, "  // mark Read on switching to another feed
    "markDisplayedOnClosingTab int, "     // mark Read on tab closing
    "markDisplayedOnMinimize int, "       // mark Read on minimizing to tray
    // --- Display ---
    "layout text, "      // news display layout
    "filter text, "      // news display filter
    "groupBy int, "      // column number to sort by
    "displayNews int, "  // 0 - display content from news; 1 - download content from link
    "displayEmbeddedImages integer default 1, "  // display images embedded in news
    "loadTypes text, "                           // type of content to load ("images" or "images sounds" - images only or images and sound)
    "openLinkOnEmptyContent int, "               // load link, if content is empty
    // --- Columns ---
    "columns text, "  // columns list and order of the news displayed in list
    "sort text, "     // column name to sort by
    "sortType int, "  // sort type (ascend, descend)
    // --- Clean Up ---
    "maximumToKeep int, "           // maximum number of news to keep
    "maximumToKeepEnable int, "     // enable limitation
    "maximumAgeOfNews int, "        // maximum store time of the news
    "maximumAgoOfNewEnable int, "   // enable limitation
    "deleteReadNews int, "          // delete read news
    "neverDeleteUnreadNews int, "   // don't delete unread news
    "neverDeleteStarredNews int, "  // don't delete starred news
    "neverDeleteLabeledNews int, "  // don't delete labeled news
    // --- Status ---
    "status text, "                 // last update result
    "created text, "                // feed creation timestamp
    "updated text, "                // last update timestamp
    "lastDisplayed text, "           // last display timestamp
    "f_Expanded integer default 1, "  // expand folder flag
    "flags text, "                    // more flags (example "focused", "hidden")
    "authentication integer default 0, "    // enable authentification, sets on feed creation
    "duplicateNewsMode integer default 0, " // news duplicates process mode
    "typeFeed integer default 0, "          // reserved for future purposes
    "showNotification integer default 0, "  //
    "disableUpdate integer default 0, "     // disable update feed
    "javaScriptEnable integer default 1 "   //
    ")");

const QString kCreateNewsTableQuery(
    "CREATE TABLE news("
    "id integer primary key, "
    "feedId integer, "                     // feed id from feed table
    "guid varchar, "                       // news unique number
    "guidislink varchar default 'true', "  // flag shows that news unique number is URL-link to news
    "description varchar, "                // brief description
    "content varchar, "                    // full content (atom)
    "title varchar, "                      // title
    "published varchar, "                  // publish timestamp
    "modified varchar, "                   // modification timestamp
    "received varchar, "                   // recieve news timestamp (set on recieve)
    "author_name varchar, "                // author name
    "author_uri varchar, "                 // author web page (atom)
    "author_email varchar, "               // author e-mail (atom)
    "category varchar, "                   // category. May be several item tabs separated
    "label varchar, "                      // label (user purpose label(s))
    "new integer default 1, "              // Flag "new". Set on receive, reset on application close
    "read integer default 0, "             // Flag "read". Set after news has been focused
    "starred integer default 0, "          // Flag "sticky". Set by user
    "deleted integer default 0, "          // Flag "deleted". News is marked deleted by remains in DB,
                                           //   for purpose not to display after next update.
                                           //   News are deleted by cleanup process only
    "attachment varchar, "                 // Links to attachments (tabs separated)
    "comments varchar, "                   // News comments page URL-link
    "enclosure_length, "                   // Media-object, associated to news:
    "enclosure_type, "                     //   length, type,
    "enclosure_url, "                      //   URL-address
    "source varchar, "                     // source, incese of republication (atom: <link via>)
    "link_href varchar, "                  // URL-link to news (atom: <link self>)
    "link_enclosure varchar, "             // URL-link to huge amoun of data,
                                           //   that can't be received in the news
    "link_related varchar, "               // URL-link for related data of the news (atom)
    "link_alternate varchar, "             // URL-link to alternative news representation
    "contributor varchar, "                // contributors (tabs separated)
    "rights varchar, "                     // copyrights
    "deleteDate varchar, "                 // news delete timestamp
    "feedParentId integer default 0 "      // parent feed id from feed table
    ")");

const QString kCreateFiltersTable(
    "CREATE TABLE filters("
    "id integer primary key, "
    "name varchar, "              // filter name
    "type integer, "              // filter type (and, or, for all)
    "feeds varchar, "             // feed list, that are using the filter
    "enable integer default 1, "  // 1 - filter used; 0 - filter not used
    "num integer "                // Sequence number. Used to sort filters
    ")");

const QString kCreateFilterConditionsTable(
    "CREATE TABLE filterConditions("
    "id integer primary key, "
    "idFilter int, "            // filter Id
    "field varchar, "           // field to filter by
    "condition varchar, "       // condition has applied to filed
    "content varchar "          // field content that is used by filter
    ")");

const QString kCreateFilterActionsTable(
    "CREATE TABLE filterActions("
    "id integer primary key, "
    "idFilter int, "            // filter Id
    "action varchar, "          // action that has appled for filter
    "params varchar "           // action parameters
    ")");

const QString kCreateLabelsTable(
    "CREATE TABLE labels("
    "id integer primary key, "
    "name varchar, "            // label name
    "image blob, "              // label image
    "color_text varchar, "      // news text color displayed in news list
    "color_bg varchar, "        // news background color displayed in news list
    "num integer, "             // sequence number to sort with
    "currentNews integer "      // current displayed news
    ")");

const QString kCreatePasswordsTable(
    "CREATE TABLE passwords("
    "id integer primary key, "
    "server varchar, "          // server
    "username varchar, "        // username
    "password varchar "         // password
    ")");

int Database::version()
{
  return versionDB;
}

void Database::initialization()
{
  QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE");
  if (mainApp->storeDBMemory())
    db.setDatabaseName(":memory:");
  else
    db.setDatabaseName(mainApp->dbFileName());
  if (!db.open()) {
    QString message = QString("Cannot open SQLite database! \n"
                              "Error: %1").arg(db.lastError().text());
    qCritical() << message;
    QMessageBox::critical(0, QObject::tr("Error"), message);
  } else {
    if (!mainApp->dbFileExists()) {
      {
        QSqlDatabase dbFile = QSqlDatabase::addDatabase("QSQLITE", "initialization");
        dbFile.setDatabaseName(mainApp->dbFileName());
        dbFile.open();
        createTables(dbFile);
        createLabels(dbFile);
        dbFile.close();
      }
      QSqlDatabase::removeDatabase("initialization");
    }

    if (mainApp->dbFileExists()) {
      prepareDatabase();
    }

    if (mainApp->storeDBMemory()) {
      createTables(db);
    }

    if (mainApp->storeDBMemory()) {
      QSqlQuery q(db);
      q.prepare("ATTACH DATABASE :file AS 'storage';");
      q.bindValue(":file", mainApp->dbFileName());
      q.exec();
      foreach (const QString &table, tablesList()) {
        q.exec(QString("INSERT OR REPLACE INTO main.%1 SELECT * FROM storage.%1;").arg(table));
      }
      q.exec("DETACH 'storage'");
      q.finish();
    }
  }
}

void Database::setPragma(QSqlDatabase &db)
{
  QSqlQuery q(db);
  q.setForwardOnly(true);
  q.exec("PRAGMA synchronous = NORMAL");
  q.exec("PRAGMA journal_mode = MEMORY");
  q.exec("PRAGMA page_size = 4096");
  q.exec("PRAGMA cache_size = 16384");
  q.exec("PRAGMA temp_store = MEMORY");
}

void Database::createTables(QSqlDatabase &db)
{
  setPragma(db);

  db.transaction();

  db.exec(kCreateFeedsTableQuery);
  db.exec(kCreateNewsTableQuery);
  // Create index for feedId field
  db.exec("CREATE INDEX feedId ON news(feedId)");

  // Create extra feeds table just in case
  db.exec("CREATE TABLE feeds_ex(id integer primary key, "
          "feedId integer, "  // feed Id
          "name varchar, "    // parameter name
          "value varchar "    // parameter value
          ")");
  // Create extra news table just in case
  db.exec("CREATE TABLE news_ex(id integer primary key, "
          "feedId integer, "  // feed Id
          "newsId integer, "  // news Id
          "name varchar, "    // parameter name
          "value varchar "    // parameter value
          ")");
  // Create filters table
  db.exec(kCreateFiltersTable);
  db.exec(kCreateFilterConditionsTable);
  db.exec(kCreateFilterActionsTable);
  // Create extra filters just in case
  db.exec("CREATE TABLE filters_ex(id integer primary key, "
          "idFilter integer, "  // filter Id
          "name text, "         // parameter name
          "value text"          // parameter value
          ")");
  // Create labels table
  db.exec(kCreateLabelsTable);
  // Create password table
  db.exec(kCreatePasswordsTable);
  //
  db.exec("CREATE TABLE info(id integer primary key, name varchar, value varchar)");
  QSqlQuery q(db);
  q.prepare("INSERT INTO info(name, value) VALUES ('version', :version)");
  q.bindValue(":version", version());
  q.exec();
  q.prepare("INSERT INTO info(name, value) VALUES('appVersion', :appVersion)");
  q.bindValue(":appVersion", STRPRODUCTVER);
  q.exec();
  q.finish();

  db.commit();
}

void Database::prepareDatabase()
{
  // Version DB > 0.12.1
  {
    Settings settings;

    QSqlDatabase dbFile = QSqlDatabase::addDatabase("QSQLITE", "initialization");
    dbFile.setDatabaseName(mainApp->dbFileName());
    dbFile.open();
    setPragma(dbFile);
    QSqlQuery q(dbFile);

    int dbVersion = 0;
    q.exec("SELECT value FROM info WHERE name='version'");
    if (q.first()) {
      dbVersion = q.value(0).toInt();
    }

    QString appVersion = QString();
    q.exec("SELECT value FROM info WHERE name='appVersion'");
    if (q.first()) {
      appVersion = q.value(0).toString();
    }

    // Create backups for DB and Settings
    if (appVersion != STRPRODUCTVER) {
      Common::createFileBackup(mainApp->dbFileName(), STRPRODUCTVER);
      Common::createFileBackup(settings.fileName(), STRPRODUCTVER);
    }

    if (dbVersion < 14) {
      q.exec("ALTER TABLE feeds ADD COLUMN showNotification integer default 0");
      q.exec("ALTER TABLE feeds ADD COLUMN disableUpdate integer default 0");
      q.exec("ALTER TABLE feeds ADD COLUMN javaScriptEnable integer default 1");
    }

    // Update appVersion anyway
    if (appVersion.isEmpty()) {
      q.prepare("INSERT INTO info(name, value) VALUES('appVersion', :appVersion)");
      q.bindValue(":appVersion", STRPRODUCTVER);
      q.exec();
    } else {
      q.prepare("UPDATE info SET value=:appVersion WHERE name='appVersion'");
      q.bindValue(":appVersion", STRPRODUCTVER);
      q.exec();
    }

    if (dbVersion < version()) {
      q.prepare("UPDATE info SET value=:version WHERE name='version'");
      q.bindValue(":version", version());
      q.exec();
    }

    q.finish();
    dbFile.close();

    settings.setValue("VersionDB", version());
  }
  QSqlDatabase::removeDatabase("initialization");
}

void Database::createLabels(QSqlDatabase &db)
{
  QSqlQuery q(db);
  for (int i = 0; i < 6; i++) {
    q.prepare("INSERT INTO labels(name, image) "
              "VALUES (:name, :image)");
    q.bindValue(":name", MainWindow::nameLabels().at(i));

    QFile file(QString(":/images/label_%1").arg(i+1));
    file.open(QFile::ReadOnly);
    q.bindValue(":image", file.readAll());
    file.close();

    q.exec();

    int labelId = q.lastInsertId().toInt();
    q.exec(QString("UPDATE labels SET num='%1' WHERE id=='%1'").arg(labelId));
  }
}

QSqlDatabase Database::connection(const QString &connectionName)
{
  QSqlDatabase db;
  if (mainApp->storeDBMemory()) {
    db = QSqlDatabase::database();
  }
  else {
    db = QSqlDatabase::database(connectionName, true);
    if (!db.isValid()) {
      db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
      db.setDatabaseName(mainApp->dbFileName());
      db.open();
      setPragma(db);
    }
  }
  return db;
}

void Database::saveMemoryDatabase()
{
  QString fileName(mainApp->dbFileName() % ".tmp");
  {
    QSqlDatabase dbFile = QSqlDatabase::addDatabase("QSQLITE", "saveMemory");
    dbFile.setDatabaseName(fileName);
    dbFile.open();
    createTables(dbFile);
    dbFile.close();
  }
  QSqlDatabase::removeDatabase("saveMemory");

  QSqlDatabase db = QSqlDatabase::database();
  QSqlQuery q(db);
  q.prepare("ATTACH DATABASE :file AS 'storage';");
  q.bindValue(":file", fileName);
  q.exec();
  foreach (const QString &table, tablesList()) {
    q.exec(QString("INSERT OR REPLACE INTO storage.%1 SELECT * FROM main.%1;").arg(table));
  }
  q.exec("DETACH 'storage'");
  q.finish();

  QFile::remove(mainApp->dbFileName());
  QFile::rename(fileName, mainApp->dbFileName());
}
