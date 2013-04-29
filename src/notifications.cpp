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
#include "notifications.h"
#include "optionsdialog.h"
#include "rsslisting.h"

NotificationWidget::NotificationWidget(QList<int> idFeedList,
                                       QList<int> cntNewNewsList,
                                       QWidget *parent)
  : QWidget(0,  Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
  , idFeedList_(idFeedList)
  , cntNewNewsList_(cntNewNewsList)
{
  setAttribute(Qt::WA_TranslucentBackground);
  setFocusPolicy(Qt::NoFocus);
  setAttribute(Qt::WA_AlwaysShowToolTips);

  int countShowNews;
  int widthTitleNews;
  QString fontFamily;
  int fontSize;

  if (idFeedList_.count()) {
    RSSListing *rssl_ = qobject_cast<RSSListing*>(parent);
    position_ = rssl_->positionNotify_;
    timeShowNews_ = rssl_->timeShowNewsNotify_;
    countShowNews = rssl_->countShowNewsNotify_;
    widthTitleNews = rssl_->widthTitleNewsNotify_;
    fontFamily = rssl_->notificationFontFamily_;
    fontSize = rssl_->notificationFontSize_;
  } else {
    OptionsDialog *options = qobject_cast<OptionsDialog*>(parent);
    position_ = options->positionNotify_->currentIndex();
    timeShowNews_ = options->timeShowNewsNotify_->value();
    countShowNews = options->countShowNewsNotify_->value();
    widthTitleNews = options->widthTitleNewsNotify_->value();
    fontFamily = options->fontsTree_->topLevelItem(4)->text(2).section(", ", 0, 0);
    fontSize = options->fontsTree_->topLevelItem(4)->text(2).section(", ", 1).toInt();

    cntNewNewsList_ << 100;
  }

  iconTitle_ = new QLabel(this);
  iconTitle_->setPixmap(QPixmap(":/images/quiterss16"));
  textTitle_ = new QLabel(this);

  closeButton_ = new QToolButton(this);
  closeButton_->setStyleSheet(
        "QToolButton { border: none; padding: 0px; "
        "image: url(:/images/close); }"
        "QToolButton:hover {"
        "image: url(:/images/closeHover); }");

  QHBoxLayout *titleLayout = new QHBoxLayout();
  titleLayout->setMargin(5);
  titleLayout->setSpacing(5);
  titleLayout->addWidget(iconTitle_);
  titleLayout->addWidget(textTitle_, 1);
  titleLayout->addWidget(closeButton_);

  QWidget *titlePanel_ = new QWidget(this);
  titlePanel_->setCursor(Qt::PointingHandCursor);
  titlePanel_->setObjectName("titleNotification");
  titlePanel_->setLayout(titleLayout);
  titlePanel_->installEventFilter(this);

  numPage_ = new QLabel(this);

  leftButton_ = new QToolButton(this);
  leftButton_->setIcon(QIcon(":/images/moveLeft"));
  leftButton_->setCursor(Qt::PointingHandCursor);
  leftButton_->setEnabled(false);
  rightButton_ = new QToolButton(this);
  rightButton_->setIcon(QIcon(":/images/moveRight"));
  rightButton_->setCursor(Qt::PointingHandCursor);

  QHBoxLayout *bottomLayout = new QHBoxLayout();
  bottomLayout->setMargin(2);
  bottomLayout->setSpacing(5);
  bottomLayout->addSpacing(3);
  bottomLayout->addWidget(numPage_);
  bottomLayout->addStretch(1);
  bottomLayout->addWidget(leftButton_);
  bottomLayout->addWidget(rightButton_);
  bottomLayout->addSpacing(3);

  QWidget *bottomPanel_ = new QWidget(this);
  bottomPanel_->setObjectName("bottomNotification");
  bottomPanel_->setLayout(bottomLayout);

  stackedWidget_ = new QStackedWidget(this);

  QVBoxLayout *pageLayout_ = new QVBoxLayout();
  pageLayout_->setMargin(5);
  pageLayout_->setSpacing(0);
  QWidget *pageWidget = new QWidget(this);
  pageWidget->setLayout(pageLayout_);
  stackedWidget_->addWidget(pageWidget);

  QVBoxLayout* mainLayout = new QVBoxLayout();
  mainLayout->setMargin(2);
  mainLayout->setSpacing(0);
  mainLayout->addWidget(titlePanel_);
  mainLayout->addWidget(stackedWidget_);
  mainLayout->addWidget(bottomPanel_);

  QWidget *mainWidget = new QWidget(this);
  mainWidget->setObjectName("notificationWidget");
  mainWidget->setLayout(mainLayout);
  mainWidget->setMouseTracking(true);

  QVBoxLayout* layout = new QVBoxLayout();
  layout->setMargin(0);
  layout->addWidget(mainWidget);

  setLayout(layout);

  int cntAllNews = 0;
  foreach (int cntNews, cntNewNewsList_) {
    cntAllNews = cntAllNews + cntNews;
  }
  textTitle_->setText(QString(tr("Incoming News: %1")).arg(cntAllNews));

  if (cntAllNews > countShowNews) rightButton_->setEnabled(true);
  else rightButton_->setEnabled(false);

  QSqlQuery q;
  int cnt = 0;
  for (int i = 0; i < idFeedList_.count(); i++) {
    int idFeed = idFeedList_[i];
    QString qStr = QString("SELECT text, image, parentId FROM feeds WHERE id=='%1'").
        arg(idFeed);
    q.exec(qStr);
    QString titleFeed;
    QPixmap iconFeed;
    int parIdFeed = -1;
    if (q.next()) {
      titleFeed = q.value(0).toString();
      QByteArray byteArray = q.value(1).toByteArray();
      parIdFeed = q.value(2).toInt();
      if (!byteArray.isNull()) {
        iconFeed.loadFromData(QByteArray::fromBase64(byteArray));
      }
    }

    int cntNews = 0;
    qStr = QString("SELECT id, title FROM news WHERE new=1 AND feedId=='%1'").
        arg(idFeed);
    q.exec(qStr);
    while (q.next()) {
      if (cntNews >= cntNewNewsList_[i]) break;
      else cntNews++;

      if (cnt >= countShowNews) {
        cnt = 1;
        pageLayout_ = new QVBoxLayout();
        pageLayout_->setMargin(5);
        pageLayout_->setSpacing(0);
        QWidget *pageWidget = new QWidget(this);
        pageWidget->setLayout(pageLayout_);
        stackedWidget_->addWidget(pageWidget);
      } else cnt++;

      NewsItem *newsItem = new NewsItem(idFeed, parIdFeed, q.value(0).toInt(),
                                        widthTitleNews, this);
      if (!iconFeed.isNull())
        newsItem->iconNews_->setPixmap(iconFeed);
      newsItem->iconNews_->setToolTip(titleFeed);
      connect(newsItem, SIGNAL(signalMarkRead(int)),
              this, SLOT(markRead(int)));
      connect(newsItem, SIGNAL(signalTitleClicked(int, int, int)),
              this, SIGNAL(signalOpenNews(int, int, int)));
      connect(newsItem, SIGNAL(signalOpenExternalBrowser(QUrl)),
              this, SIGNAL(signalOpenExternalBrowser(QUrl)));

      newsItem->titleNews_->setFont(QFont(fontFamily, fontSize));

      QFont font = newsItem->titleNews_->font();
      font.setBold(true);
      newsItem->titleNews_->setFont(font);
      QString titleStr = newsItem->titleNews_->fontMetrics().elidedText(
            q.value(1).toString(), Qt::ElideRight, newsItem->titleNews_->sizeHint().width());
      newsItem->titleNews_->setText(titleStr);
      newsItem->titleNews_->setToolTip(q.value(1).toString());
      pageLayout_->addWidget(newsItem);
    }
  }

  if (idFeedList_.isEmpty()) {
    for (int i = 0; i < cntNewNewsList_.at(0); i++) {
      if (cnt >= countShowNews) {
        cnt = 1;
        pageLayout_ = new QVBoxLayout();
        pageLayout_->setMargin(5);
        pageLayout_->setSpacing(0);
        QWidget *pageWidget = new QWidget(this);
        pageWidget->setLayout(pageLayout_);
        stackedWidget_->addWidget(pageWidget);
      } else cnt++;

      NewsItem *newsItem = new NewsItem(0, 0, 0, widthTitleNews, this);

      newsItem->iconNews_->setPixmap(QPixmap(":/images/feed"));
      newsItem->iconNews_->setToolTip("Title Feed");
      connect(newsItem, SIGNAL(signalMarkRead(int)),
              this, SLOT(markRead(int)));
      connect(newsItem, SIGNAL(signalTitleClicked(int, int, int)),
              this, SIGNAL(signalOpenNews(int, int, int)));
      connect(newsItem, SIGNAL(signalOpenExternalBrowser(QUrl)),
              this, SIGNAL(signalOpenExternalBrowser(QUrl)));

      newsItem->titleNews_->setFont(QFont(fontFamily, fontSize));

      QFont font = newsItem->titleNews_->font();
      font.setBold(true);
      newsItem->titleNews_->setFont(font);
      QString title("Test News Test News Test News Test News Test News");
      QString titleStr = newsItem->titleNews_->fontMetrics().elidedText(
            title, Qt::ElideRight, newsItem->titleNews_->sizeHint().width());
      newsItem->titleNews_->setText(titleStr);
      newsItem->titleNews_->setToolTip(title);
      pageLayout_->addWidget(newsItem);
    }
  }

  pageLayout_->addStretch(1);
  numPage_->setText(QString(tr("Page %1 of %2").arg("1").arg(stackedWidget_->count())));

  showTimer_ = new QTimer(this);
  connect(showTimer_, SIGNAL(timeout()),
          this, SIGNAL(signalDelete()));
  connect(closeButton_, SIGNAL(clicked()),
          this, SIGNAL(signalDelete()));
  connect(leftButton_, SIGNAL(clicked()),
          this, SLOT(previousPage()));
  connect(rightButton_, SIGNAL(clicked()),
          this, SLOT(nextPage()));

  showTimer_->start(timeShowNews_*1000);
}

/*virtual*/ void NotificationWidget::showEvent(QShowEvent*)
{
  QPoint point;
  switch(position_) {
  case 0:
    point = QPoint(5, 5);
    break;
  case 1:
    point = QPoint(QApplication::desktop()->availableGeometry(0).width()-width()-5,
                   5);
    break;
  case 2:
    point = QPoint(5,
                   QApplication::desktop()->availableGeometry(0).height()-height()-5);
    break;
  default:
    point = QPoint(QApplication::desktop()->availableGeometry(0).width()-width()-5,
                   QApplication::desktop()->availableGeometry(0).height()-height()-5);
    break;
  }
  move(point);
}

bool NotificationWidget::eventFilter(QObject *obj, QEvent *event)
{
  if(event->type() == QEvent::MouseButtonPress) {
    emit signalShow();
    emit signalDelete();
    return true;
  } else {
    return QObject::eventFilter(obj, event);
  }
}

/*virtual*/ void NotificationWidget::enterEvent(QEvent*)
{
  showTimer_->stop();
}

/*virtual*/ void NotificationWidget::leaveEvent(QEvent*)
{
  showTimer_->start(timeShowNews_*1000);
}

void NotificationWidget::nextPage()
{
  stackedWidget_->setCurrentIndex(stackedWidget_->currentIndex()+1);
  if (stackedWidget_->currentIndex()+1 == stackedWidget_->count())
    rightButton_->setEnabled(false);
  if (stackedWidget_->currentIndex() != 0)
    leftButton_->setEnabled(true);
  numPage_->setText(QString(tr("Page %1 of %2").
                            arg(stackedWidget_->currentIndex()+1).
                            arg(stackedWidget_->count())));
}

void NotificationWidget::previousPage()
{
  stackedWidget_->setCurrentIndex(stackedWidget_->currentIndex()-1);
  if (stackedWidget_->currentIndex() == 0)
    leftButton_->setEnabled(false);
  if (stackedWidget_->currentIndex()+1 != stackedWidget_->count())
    rightButton_->setEnabled(true);
  numPage_->setText(QString(tr("Page %1 of %2").
                            arg(stackedWidget_->currentIndex()+1).
                            arg(stackedWidget_->count())));
}

void NotificationWidget::markRead(int id)
{
  int read = 1;
  QSqlQuery q;
  q.exec(QString("UPDATE news SET new=0, read='%1' WHERE id=='%2'").
         arg(read).arg(id));
}
