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
#include "newsheader.h"
#include "rsslisting.h"

NewsHeader::NewsHeader(NewsModel *model, QWidget *parent)
  : QHeaderView(Qt::Horizontal, parent)
  , model_(model)
  , show_(false)
  , move_(false)
{
  setObjectName("newsHeader");
  setContextMenuPolicy(Qt::CustomContextMenu);
  setMovable(true);
  setDefaultAlignment(Qt::AlignLeft|Qt::AlignVCenter);
  setMinimumSectionSize(22);
  setStretchLastSection(false);

  viewMenu_ = new QMenu(this);
  columnVisibleActGroup_ = new QActionGroup(this);
  columnVisibleActGroup_->setExclusive(false);
  connect(columnVisibleActGroup_, SIGNAL(triggered(QAction*)),
          this, SLOT(columnVisible(QAction*)));

  buttonColumnView_ = new QPushButton(this);
  buttonColumnView_->setIcon(QIcon(":/images/images/column.png"));
  buttonColumnView_->setObjectName("buttonColumnView");
  buttonColumnView_->setFlat(true);
  buttonColumnView_->setCursor(Qt::ArrowCursor);
  buttonColumnView_->setFocusPolicy(Qt::NoFocus);
  buttonColumnView_->setMaximumWidth(30);
  connect(buttonColumnView_, SIGNAL(clicked()), this, SLOT(slotButtonColumnView()));

  QHBoxLayout *buttonLayout = new QHBoxLayout();
  buttonLayout->setMargin(0);
  buttonLayout->addWidget(buttonColumnView_, 0, Qt::AlignRight|Qt::AlignVCenter);
  setLayout(buttonLayout);

  connect(this, SIGNAL(sectionMoved(int,int,int)), SLOT(slotSectionMoved(int, int, int)));

  this->installEventFilter(this);
}

void NewsHeader::init()
{
  if (count() == 0) return;

  for (int i = 0; i < count(); ++i)
    hideSection(i);

  showSection(model_->fieldIndex("feedId"));
  showSection(model_->fieldIndex("title"));
  showSection(model_->fieldIndex("published"));
  showSection(model_->fieldIndex("author_name"));
  showSection(model_->fieldIndex("read"));
  showSection(model_->fieldIndex("starred"));
  showSection(model_->fieldIndex("category"));

  for (int i = 0; i < count(); i++) {
    model_->setHeaderData(i, Qt::Horizontal,
                          model_->headerData(i, Qt::Horizontal, Qt::DisplayRole),
                          Qt::EditRole);
  }
  model_->setHeaderData(model_->fieldIndex("feedId"), Qt::Horizontal, "", Qt::DisplayRole);
  model_->setHeaderData(model_->fieldIndex("read"), Qt::Horizontal, "", Qt::DisplayRole);
  model_->setHeaderData(model_->fieldIndex("read"), Qt::Horizontal,
                        QPixmap(":/images/readSection"), Qt::DecorationRole);
  model_->setHeaderData(model_->fieldIndex("starred"), Qt::Horizontal, "", Qt::DisplayRole);
  model_->setHeaderData(model_->fieldIndex("starred"), Qt::Horizontal,
                        QPixmap(":/images/starSection"), Qt::DecorationRole);

  moveSection(visualIndex(model_->fieldIndex("starred")), 0);
  moveSection(visualIndex(model_->fieldIndex("read")), 1);
  moveSection(visualIndex(model_->fieldIndex("feedId")), 2);
  resizeSection(model_->fieldIndex("feedId"), 22);
  moveSection(visualIndex(model_->fieldIndex("title")), 3);
  moveSection(visualIndex(model_->fieldIndex("author_name")), 4);
  resizeSection(model_->fieldIndex("author_name"), 100);
  moveSection(visualIndex(model_->fieldIndex("category")), 5);
  resizeSection(model_->fieldIndex("title"), 200);

  resizeSection(model_->fieldIndex("starred"), 22);
  setResizeMode(model_->fieldIndex("starred"), QHeaderView::Fixed);
  resizeSection(model_->fieldIndex("feedId"), 22);
  setResizeMode(model_->fieldIndex("feedId"), QHeaderView::Fixed);
  resizeSection(model_->fieldIndex("read"), 22);
  setResizeMode(model_->fieldIndex("read"), QHeaderView::Fixed);

  move_ = true;
}

void NewsHeader::createMenu()
{
  QListIterator<QAction *> iter(columnVisibleActGroup_->actions());
  while (iter.hasNext()) {
    QAction *action = iter.next();
    delete action;
  }

  for (int i = 0; i < count(); i++) {
    int lIdx = logicalIndex(i);
    if ((lIdx == model_->fieldIndex("feedId")) ||
        (lIdx == model_->fieldIndex("title")) ||
        (lIdx == model_->fieldIndex("published")) ||
        (lIdx == model_->fieldIndex("received")) ||
        (lIdx == model_->fieldIndex("author_name")) ||
        (lIdx == model_->fieldIndex("category")) ||
        (lIdx == model_->fieldIndex("read")) ||
        (lIdx == model_->fieldIndex("starred")) ||
        (lIdx == model_->fieldIndex("label")) ||
        (lIdx == model_->fieldIndex("rights")) ||
        (lIdx == model_->fieldIndex("link_href"))) {
      QAction *action = columnVisibleActGroup_->addAction(
            model_->headerData(lIdx, Qt::Horizontal, Qt::EditRole).toString());
      action->setData(lIdx);
      action->setCheckable(true);
      action->setChecked(!isSectionHidden(lIdx));
    }
  }

  viewMenu_->addActions(columnVisibleActGroup_->actions());
}

bool NewsHeader::eventFilter(QObject *obj, QEvent *event)
{
  Q_UNUSED(obj)

  if (event->type() == QEvent::Show) {
    show_ = true;
    return false;
  } else if (event->type() == QEvent::Resize) {
    if ((count() == 0) || !show_) return false;

    if (buttonColumnView_->height() != height())
      buttonColumnView_->setFixedHeight(height());

    QResizeEvent *resizeEvent = static_cast<QResizeEvent*>(event);
    bool minSize = false;
    int newWidth = resizeEvent->size().width();
    int size = 0;
    QVector<int> widthCol(count(), 0);
    static int idxColSize = count()-1;
    int tWidth = 0;
    for (int i = 0; i < count(); i++) tWidth += sectionSize(i);
    if (tWidth > newWidth) {
      minSize = true;
      size = tWidth - newWidth;
      int titleSectionSize = sectionSize(model_->fieldIndex("title"));
      if ((titleSectionSize - size) >= 40) {
        widthCol[visualIndex(model_->fieldIndex("title"))] = size;
        size = 0;
      } else {
        widthCol[visualIndex(model_->fieldIndex("title"))] = titleSectionSize - 40;
        size = size + 40 - titleSectionSize;
      }
    } else {
      size = newWidth - tWidth;
      widthCol[visualIndex(model_->fieldIndex("title"))] = size;
      size = 0;
    }
    int countCol = 0;
    bool sizeOne = false;
    while (size) {
      int lIdx = logicalIndex(idxColSize);
      if (!isSectionHidden(lIdx)) {
        if (!((model_->fieldIndex("read") == lIdx) || (model_->fieldIndex("starred") == lIdx) ||
              (model_->fieldIndex("feedId") == lIdx) || (model_->fieldIndex("title") == lIdx))) {
          if (((sectionSize(lIdx) >= 40) && !minSize) ||
              ((sectionSize(lIdx) - widthCol[idxColSize] > 40) && minSize)) {
            widthCol[idxColSize]++;
            size--;
            sizeOne = true;
          }
        }
      }
      if (idxColSize == 0) idxColSize = count()-1;
      else idxColSize--;

      if (++countCol == count()) {
        if (!sizeOne) break;
        sizeOne = false;
        countCol = 0;
      }
    }

    for (int i = count()-1; i >= 0; i--) {
      int lIdx = logicalIndex(i);
      if ((!isSectionHidden(lIdx) && (sectionSize(lIdx) >= 40)) ||
          (model_->fieldIndex("title") == lIdx)) {
        if (!minSize) {
          resizeSection(lIdx, sectionSize(lIdx) + widthCol[i]);
        } else {
          resizeSection(lIdx, sectionSize(lIdx) - widthCol[i]);
        }
      }
    }
    event->ignore();
    return true;
  } else if ((event->type() == QEvent::HoverMove) ||
             (event->type() == QEvent::HoverEnter) ||
             (event->type() == QEvent::HoverLeave)) {
    QHoverEvent *hoverEvent = static_cast<QHoverEvent*>(event);
    if (hoverEvent->pos().x() >= width() - buttonColumnView_->width()) {
      if ((event->type() == QEvent::HoverMove) && !(QApplication::mouseButtons() & Qt::LeftButton)) {
        QHoverEvent* pe =
            new QHoverEvent(QEvent::HoverLeave, hoverEvent->oldPos(), hoverEvent->pos());
        QApplication::sendEvent(this, pe);
      }
      return true;
    } else {
      return false;
    }
  } else {
    return false;
  }
}

/*virtual*/ void NewsHeader::mousePressEvent(QMouseEvent *event)
{
  QPoint nPos = event->pos();
  nPos.setX(nPos.x() + 5);
  idxCol_ = visualIndex(logicalIndexAt(nPos));
  posX_ = event->pos().x();
  nPos = event->pos();
  nPos.setX(nPos.x() - 5);
  QHeaderView::mousePressEvent(event);
}

/*virtual*/ void NewsHeader::mouseMoveEvent(QMouseEvent *event)
{
  bool sizeMin = false;
  if ((event->buttons() & Qt::LeftButton) && cursor().shape() == Qt::SplitHCursor) {
    int oldWidth = width();
    int newWidth = 0;

    for (int i = 0; i < count(); i++) newWidth += sectionSize(i);
    if (posX_ > event->pos().x()) sizeMin =  true;
    if (!sizeMin) {
      if (event->pos().x() < oldWidth) {
        for (int i = count()-1; i >= 0; i--) {
          int lIdx = logicalIndex(i);
          if (!isSectionHidden(lIdx)) {
            if (!((model_->fieldIndex("read") == lIdx) ||
                  (model_->fieldIndex("starred") == lIdx) ||
                  (model_->fieldIndex("feedId") == lIdx))) {
              int sectionWidth = sectionSize(lIdx) + oldWidth - newWidth;
              if (sectionWidth > 40) {
                if (i >= idxCol_) {
                  resizeSection(lIdx, sectionWidth);
                  sizeMin = true;
                  break;
                }
              }
            }
          }
        }
      }
      int tWidth = 0;
      for (int i = idxCol_; i < count(); i++) {
        if (!isSectionHidden(logicalIndex(i))) {
          tWidth += 40;
        }
      }
      if (event->pos().x()+tWidth > oldWidth) {
        sizeMin = false;
      }
    } else {
      int stopColFix = 0;
      for (int i = count()-1; i >= 0; i--) {
        int lIdx = logicalIndex(i);
        if (!isSectionHidden(lIdx)) {
          if (!((model_->fieldIndex("read") == lIdx) ||
                (model_->fieldIndex("starred") == lIdx) ||
                (model_->fieldIndex("feedId") == lIdx))) {
            stopColFix = i;
            break;
          }
        }
      }

      int sectionWidth = sectionSize(logicalIndex(stopColFix)) + oldWidth - newWidth;
      if ((sectionWidth > 40)) {
        if (!((model_->fieldIndex("read") == logicalIndex(idxCol_)) ||
              (model_->fieldIndex("starred") == logicalIndex(idxCol_)) ||
              (model_->fieldIndex("feedId") == logicalIndex(idxCol_))) || idxCol_ < stopColFix) {
          resizeSection(logicalIndex(stopColFix), sectionWidth);
        } else sizeMin = false;
      }
    }
    if (!sizeMin) {
      if (posX_ > event->pos().x()) posX_ = event->pos().x();
      event->ignore();
      return;
    }
  }
  if (posX_ > event->pos().x()) posX_ = event->pos().x();

  QHeaderView::mouseMoveEvent(event);
}

/*virtual*/ void NewsHeader::mouseDoubleClickEvent(QMouseEvent*)
{
}

void NewsHeader::slotButtonColumnView()
{
  viewMenu_->setFocus();
  viewMenu_->show();
  QPoint pPoint;
  pPoint.setX(mapToGlobal(QPoint(0,0)).x() + width() - viewMenu_->width() - 1);
  pPoint.setY(mapToGlobal(QPoint(0,0)).y() + height() + 1);
  viewMenu_->popup(pPoint);
}

void NewsHeader::columnVisible(QAction *action)
{
  int columnShowCount = 0;
  for (int i = 0; i < count(); i++) {
    if (!isSectionHidden(i)) columnShowCount++;
  }
  if ((columnShowCount  == 1) && !action->isChecked()) {
    action->setChecked(true);
    return;
  }

  int idx = action->data().toInt();
  setSectionHidden(idx, !isSectionHidden(idx));
  if (!isSectionHidden(idx)) {
    if ((model_->fieldIndex("starred") == idx) ||
        (model_->fieldIndex("read") == idx) ||
        (model_->fieldIndex("feedId") == idx))
      resizeSection(idx, 22);
    else
      resizeSection(idx, 60);
  }
  resize(size().width()+1, size().height());
}

void NewsHeader::slotSectionMoved(int lIdx, int oldVIdx, int newVIdx)
{
  Q_UNUSED(oldVIdx)

  if (!move_) return;

  if ((model_->fieldIndex("read") == lIdx) ||
      (model_->fieldIndex("starred") == lIdx) ||
      (model_->fieldIndex("feedId") == lIdx)) {
    for (int i = count()-1; i >= 0; i--) {
      if (!isSectionHidden(logicalIndex(i))) {
        if (i == newVIdx) {
          resizeSection(lIdx, 45);
          break;
        } else {
          resizeSection(lIdx, 22);
          break;
        }
      }
    }
    resize(size().width()+1, size().height());
  }
  createMenu();
}

void NewsHeader::retranslateStrings()
{
  if (count() == 0) return;

  model_->setHeaderData(model_->fieldIndex("feedId"), Qt::Horizontal, tr("Icon Feed"));
  model_->setHeaderData(model_->fieldIndex("title"), Qt::Horizontal, tr("Title"));
  model_->setHeaderData(model_->fieldIndex("published"), Qt::Horizontal, tr("Published"));
  model_->setHeaderData(model_->fieldIndex("received"), Qt::Horizontal, tr("Received"));
  model_->setHeaderData(model_->fieldIndex("author_name"), Qt::Horizontal, tr("Author"));
  model_->setHeaderData(model_->fieldIndex("category"), Qt::Horizontal, tr("Category"));
  model_->setHeaderData(model_->fieldIndex("read"), Qt::Horizontal, tr("Read"));
  model_->setHeaderData(model_->fieldIndex("starred"), Qt::Horizontal, tr("Star"));
  model_->setHeaderData(model_->fieldIndex("label"), Qt::Horizontal, tr("Label"));
  model_->setHeaderData(model_->fieldIndex("rights"), Qt::Horizontal, tr("Title Feed"));
  model_->setHeaderData(model_->fieldIndex("link_href"), Qt::Horizontal, tr("Link"));

  createMenu();

  model_->setHeaderData(model_->fieldIndex("feedId"), Qt::Horizontal, "", Qt::DisplayRole);
  model_->setHeaderData(model_->fieldIndex("read"), Qt::Horizontal, "", Qt::DisplayRole);
  model_->setHeaderData(model_->fieldIndex("read"), Qt::Horizontal,
                        QPixmap(":/images/readSection"), Qt::DecorationRole);
  model_->setHeaderData(model_->fieldIndex("starred"), Qt::Horizontal, "", Qt::DisplayRole);
  model_->setHeaderData(model_->fieldIndex("starred"), Qt::Horizontal,
                        QPixmap(":/images/starSection"), Qt::DecorationRole);
}

void NewsHeader::setColumns(RSSListing *rssl, const QModelIndex &indexFeed)
{
  if (count() == 0) return;

  move_ = false;
  rssl->settings_->beginGroup("NewsHeader");

  QByteArray state = rssl->settings_->value("state").toByteArray();
  QString indexColumnsStr = rssl->settings_->value("columns").toString();
  if (indexColumnsStr.isEmpty()) {
    rssl->settings_->setValue("state", saveState());
    rssl->settings_->setValue("columns", columnsList());
    rssl->settings_->setValue("sortBy", model_->fieldIndex("published"));
    rssl->settings_->setValue("sortOrder", Qt::DescendingOrder);
  } else if (state != saveState()) {
    restoreState(state);
  }

  indexColumnsStr = rssl->feedsTreeModel_->dataField(indexFeed, "columns").toString();
  if (!indexColumnsStr.isEmpty()) {
    QStringList indexColumnsList = indexColumnsStr.split(",", QString::SkipEmptyParts);
    for (int i = 0; i < count(); ++i) {
      bool show = indexColumnsList.contains(QString::number(logicalIndex(i)));
      setSectionHidden(logicalIndex(i),!show);
    }
    for (int i = 0; i < indexColumnsList.count(); ++i) {
      QString indexStr = indexColumnsList.at(i);
      moveSection(visualIndex(indexStr.toInt()), i);
    }
    int sortBy = rssl->feedsTreeModel_->dataField(indexFeed, "sort").toInt();
    int sortType = rssl->feedsTreeModel_->dataField(indexFeed, "sortType").toInt();
    setSortIndicator(sortBy, Qt::SortOrder(sortType));
  } else {
    indexColumnsStr = rssl->settings_->value("columns").toString();
    QStringList indexColumnsList = indexColumnsStr.split(",", QString::SkipEmptyParts);
    for (int i = 0; i < count(); ++i) {
      bool show = indexColumnsList.contains(QString::number(logicalIndex(i)));
      setSectionHidden(logicalIndex(i),!show);
    }
    for (int i = 0; i < indexColumnsList.count(); ++i) {
      QString indexStr = indexColumnsList.at(i);
      moveSection(visualIndex(indexStr.toInt()), i);
    }
    int sortBy = rssl->settings_->value("sortBy", model_->fieldIndex("published")).toInt();
    int sortType = rssl->settings_->value("sortOrder", Qt::DescendingOrder).toInt();
    setSortIndicator(sortBy, Qt::SortOrder(sortType));
  }
  rssl->settings_->endGroup();

  createMenu();

  if (!isVisible())
    show_ = true;
  resize(size().width()+1, size().height());
  moveSection(visualIndex(model_->fieldIndex("id")), 0);
  move_ = true;
}

QString NewsHeader::columnsList()
{
  QString indexColumnsStr;
  for (int i = 0; i < count(); ++i) {
    if (!isSectionHidden(logicalIndex(i))) {
      indexColumnsStr.append(",");
      indexColumnsStr.append(QString::number(logicalIndex(i)));
    }
  }
  indexColumnsStr.append(",");
  return indexColumnsStr;
}

void NewsHeader::saveStateColumns(RSSListing *rssl, NewsTabWidget *newsTabWidget)
{
  int feedId = newsTabWidget->feedId_;
  int feedParId = newsTabWidget->feedParId_;
  QModelIndex indexOld = rssl->feedsTreeModel_->getIndexById(feedId, feedParId);

  rssl->settings_->beginGroup("NewsHeader");
  rssl->settings_->setValue("state", saveState());
  if (rssl->feedsTreeModel_->dataField(indexOld, "columns").isNull()) {
    rssl->settings_->setValue("columns", columnsList());
    rssl->settings_->setValue("sortBy", sortIndicatorSection());
    rssl->settings_->setValue("sortOrder", sortIndicatorOrder());
  }
  rssl->settings_->endGroup();
}
