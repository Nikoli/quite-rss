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
#include "feedstreemodel.h"

#include <QApplication>
#include <QDateTime>
#include <QDebug>

FeedsTreeModel::FeedsTreeModel(const QString& tableName,
                               const QStringList& captions,
                               const QStringList& fieldNames,
                               int rootParentId,
                               const QString& decoratedField,
                               QObject* parent)
  : QyurSqlTreeModel(tableName, captions, fieldNames,
                     rootParentId, decoratedField, parent)
{
}

QVariant FeedsTreeModel::data(const QModelIndex &index, int role) const
{
  if (role == Qt::FontRole) {
    QFont font = font_;
    if (QyurSqlTreeModel::proxyColumnByOriginal("text") == index.column()) {
      if ((0 < index.sibling(index.row(), proxyColumnByOriginal("unread")).data(Qt::EditRole).toInt()) &&
          (QyurSqlTreeModel::proxyColumnByOriginal("unread") != index.column()))
        font.setBold(true);
    }
    return font;
  } else if (role == Qt::DisplayRole){
    if (QyurSqlTreeModel::proxyColumnByOriginal("unread") == index.column()) {
      int unread = index.sibling(index.row(), proxyColumnByOriginal("unread")).data(Qt::EditRole).toInt();
      if (0 == unread) {
        return QVariant();
      } else {
        QString qStr = QString("(%1)").arg(unread);
        return qStr;
      }
    } else if (QyurSqlTreeModel::proxyColumnByOriginal("undeleteCount") == index.column()) {
      QString qStr = QString("(%1)").
          arg(index.sibling(index.row(), proxyColumnByOriginal("undeleteCount")).data(Qt::EditRole).toInt());
      return qStr;
    } else if (QyurSqlTreeModel::proxyColumnByOriginal("updated") == index.column()) {
      QDateTime dtLocal;
      QString strDate = index.sibling(index.row(), proxyColumnByOriginal("updated")).
          data(Qt::EditRole).toString();

      if (!strDate.isNull()) {
        QDateTime dtLocalTime = QDateTime::currentDateTime();
        QDateTime dtUTC = QDateTime(dtLocalTime.date(), dtLocalTime.time(), Qt::UTC);
        int nTimeShift = dtLocalTime.secsTo(dtUTC);

        QDateTime dt = QDateTime::fromString(strDate, Qt::ISODate);
        dtLocal = dt.addSecs(nTimeShift);

        QString strResult;
        if (QDateTime::currentDateTime().date() <= dtLocal.date())
          strResult = dtLocal.toString(formatTime_);
        else
          strResult = dtLocal.toString(formatDate_);
        return strResult;
      } else {
        return QVariant();
      }
    }
  } else if (role == Qt::TextColorRole) {
    QBrush brush;
    brush = qApp->palette().brush(QPalette::WindowText);
    if (QyurSqlTreeModel::proxyColumnByOriginal("unread") == index.column()) {
      brush = qApp->palette().brush(QPalette::Link);
    } else if (QyurSqlTreeModel::proxyColumnByOriginal("text") == index.column()) {
      if (index.sibling(index.row(), proxyColumnByOriginal("newCount")).data(Qt::EditRole).toInt() > 0) {
        brush = qApp->palette().brush(QPalette::Link);
      }
    }
    return brush;
  } else if (role == Qt::DecorationRole) {
    if (QyurSqlTreeModel::proxyColumnByOriginal("text") == index.column()) {
      if (isFolder(index)) {
        return QPixmap(":/images/folder");
      } else {
        QString strDate = index.sibling(index.row(), proxyColumnByOriginal("updated")).
            data(Qt::EditRole).toString();
        if (strDate.isEmpty())
          return QPixmap(":/images/feedError");

        if (defaultIconFeeds_)
          return QPixmap(":/images/feed");

        QByteArray byteArray = index.sibling(index.row(), proxyColumnByOriginal("image")).
            data(Qt::EditRole).toByteArray();
        if (!byteArray.isNull()) {
          QPixmap icon;
          if (icon.loadFromData(QByteArray::fromBase64(byteArray))) {
            return icon;
          }
        }
        return QPixmap(":/images/feed");
      }
    }
  } else if (role == Qt::TextAlignmentRole) {
    if (QyurSqlTreeModel::proxyColumnByOriginal("id") == index.column()) {
      int flag = Qt::AlignRight|Qt::AlignVCenter;
      return flag;
    }
  }

  return QyurSqlTreeModel::data(index, role);
}

QVariant FeedsTreeModel::dataField(const QModelIndex &index, const QString &fieldName) const
{
  return index.sibling(index.row(),proxyColumnByOriginal(fieldName)).data(Qt::EditRole);
}

/*virtual*/ bool	FeedsTreeModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
  return QyurSqlTreeModel::setData(index, value, role);
}

Qt::ItemFlags FeedsTreeModel::flags(const QModelIndex &index) const
{
  Qt::ItemFlags defaultFlags = QyurSqlTreeModel::flags(index);

  if (index.isValid())
    return Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled | defaultFlags;
  else
    return Qt::ItemIsDropEnabled | defaultFlags;
}

Qt::DropActions FeedsTreeModel::supportedDropActions() const
{
  return Qt::MoveAction;
}

/**
 * @brief Проверка, что узел является категорией, а не лентой
 *
 *  Если поле xmlUrl пустое, то узел считается категорией
 * @param index Проверяемый узел
 * @return Признал ленты
 * @retval true Узел - категория
 * @retval false Узел - лента
 *----------------------------------------------------------------------------*/
bool FeedsTreeModel::isFolder(const QModelIndex &index) const
{
  return index.sibling(index.row(), proxyColumnByOriginal("xmlUrl"))
      .data(Qt::EditRole).toString().isEmpty();
}
