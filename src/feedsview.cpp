#include "feedsview.h"

FeedsView::FeedsView(QWidget * parent) :
    QTreeView(parent)
{
  setObjectName("feedsTreeView_");
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setEditTriggers(QAbstractItemView::NoEditTriggers);

  setSelectionBehavior(QAbstractItemView::SelectRows);
  setSelectionMode(QAbstractItemView::ExtendedSelection);

  setUniformRowHeights(true);

  header()->setStretchLastSection(false);
  header()->setVisible(false);

  setContextMenuPolicy(Qt::CustomContextMenu);
}

/*virtual*/ void FeedsView::mousePressEvent(QMouseEvent *event)
{
  QTreeView::mousePressEvent(event);
}

/*virtual*/ void FeedsView::mouseMoveEvent(QMouseEvent *event)
{

}
