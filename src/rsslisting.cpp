﻿#include <QtCore>

#if defined(Q_OS_WIN)
#include <windows.h>
#include <psapi.h>
#endif

#include "aboutdialog.h"
#include "addfeedwizard.h"
#include "addfolderdialog.h"
#include "authenticationdialog.h"
#include "customizetoolbardialog.h"
#include "db_func.h"
#include "delegatewithoutfocus.h"
#include "feedpropertiesdialog.h"
#include "filterrulesdialog.h"
#include "newsfiltersdialog.h"
#include "optionsdialog.h"
#include "rsslisting.h"
#include "webpage.h"

/*! \brief Обработка сообщений полученных из запущщеной копии программы *******/
void RSSListing::receiveMessage(const QString& message)
{
  qDebug() << QString("Received message: '%1'").arg(message);
  if (!message.isEmpty()){
    QStringList params = message.split('\n');
    foreach (QString param, params) {
      if (param == "--show") {
        if (closeApp_) return;
        slotShowWindows();
      }
      if (param == "--exit") slotClose();
      if (param.contains("feed:", Qt::CaseInsensitive)) {
        QClipboard *clipboard = QApplication::clipboard();
        if (param.contains("https://", Qt::CaseInsensitive)) {
          param.remove(0, 5);
          clipboard->setText(param);
        } else {
          param.remove(0, 7);
          clipboard->setText("http://" + param);
        }
        activateWindow();
        addFeed();
      }
    }
  }
}

/*!****************************************************************************/
RSSListing::RSSListing(QSettings *settings, const QString &dataDirPath, QWidget *parent)
  : QMainWindow(parent),
    settings_(settings),
    dataDirPath_(dataDirPath)
{
  setWindowTitle("QuiteRSS");
  setContextMenuPolicy(Qt::CustomContextMenu);

  closeApp_ = false;

#if defined(Q_OS_WIN) || defined(Q_OS_OS2)
  appDataDirPath_ = QCoreApplication::applicationDirPath();
#else
  appDataDirPath_ = QString("/usr/share/quiterss");
#endif

  dbFileName_ = dataDirPath_ + QDir::separator() + kDbName;
  QString versionDB = initDB(dbFileName_, settings_);
  settings_->setValue("VersionDB", versionDB);

  storeDBMemory_ = settings_->value("Settings/storeDBMemory", true).toBool();
  storeDBMemoryT_ = storeDBMemory_;

  db_ = QSqlDatabase::addDatabase("QSQLITE");
  if (storeDBMemory_)
    db_.setDatabaseName(":memory:");
  else
    db_.setDatabaseName(dbFileName_);
  db_.open();

  if (storeDBMemory_) {
    dbMemFileThread_ = new DBMemFileThread(dbFileName_, this);
    dbMemFileThread_->sqliteDBMemFile(false);
    while(dbMemFileThread_->isRunning()) qApp->processEvents();
  }

  if (settings_->value("Settings/createLastFeed", false).toBool())
    lastFeedPath_ = dataDirPath_;

  networkManager_ = new NetworkManager(parent);
  connect(networkManager_, SIGNAL(authenticationRequired(QNetworkReply*,QAuthenticator*)),
          this, SLOT(slotAuthentication(QNetworkReply*,QAuthenticator*)));

  cookieJar_ = new CookieJar(dataDirPath_, this);
  networkManager_->setCookieJar(cookieJar_);

  diskCache_ = NULL;
  bool useDiskCache = settings_->value("Settings/useDiskCache", true).toBool();
  if (useDiskCache) {
    diskCache_ = new QNetworkDiskCache(this);
    QString dirDiskCache = settings_->value(
          "Settings/dirDiskCache", QDir::toNativeSeparators(dataDirPath_+ "/cache")).toString();
    diskCache_->setCacheDirectory(dirDiskCache);
    int maxDiskCache = settings_->value("Settings/maxDiskCache", 50).toInt();
    diskCache_->setMaximumCacheSize(maxDiskCache*1024*1024);

    networkManager_->setCache(diskCache_);
  }

  int requestTimeout = settings_->value("Settings/requestTimeout", 30).toInt();
  persistentUpdateThread_ = new UpdateThread(this, requestTimeout);

  persistentParseThread_ = new ParseThread(this, lastFeedPath_);

  faviconThread_ = new FaviconThread(this);

  cleanUp();

  currentNewsTab = NULL;
  newsView_ = NULL;
  notificationWidget = NULL;
  feedIdOld_ = -2;
  openingLink_ = false;
  openNewsTab_ = 0;
  indexClickedTab = -1;

  createFeedsWidget();
  createToolBarNull();

  createActions();
  createShortcut();
  createMenu();
  createToolBar();

  createStatusBar();
  createTray();

  createTabBar();

  QHBoxLayout *tabBarLayout = new QHBoxLayout();
  tabBarLayout->setContentsMargins(5, 0, 0, 0);
  tabBarLayout->setSpacing(0);
  tabBarLayout->addWidget(tabBar_);

  QWidget *tabBarWidget = new QWidget(this);
  tabBarWidget->setObjectName("tabBarWidget");
  tabBarWidget->setLayout(tabBarLayout);

  stackedWidget_ = new QStackedWidget(this);
  stackedWidget_->setObjectName("stackedWidget_");
  stackedWidget_->setFrameStyle(QFrame::NoFrame);

  mainSplitter_ = new QSplitter(this);
  mainSplitter_ ->setFrameStyle(QFrame::NoFrame);
  mainSplitter_->setHandleWidth(1);
  mainSplitter_->setStyleSheet(
              QString("QSplitter::handle {background: qlineargradient("
                      "x1: 0, y1: 0, x2: 0, y2: 1,"
                      "stop: 0 %1, stop: 0.07 %2);}").
              arg(feedsPanel_->palette().background().color().name()).
              arg(qApp->palette().color(QPalette::Dark).name()));
  mainSplitter_->setChildrenCollapsible(false);
  mainSplitter_->addWidget(feedsWidget_);
  mainSplitter_->addWidget(stackedWidget_);
  mainSplitter_->setStretchFactor(1, 1);

  #define FEEDS_WIDTH 180
  QList <int> sizes;
  sizes << FEEDS_WIDTH << QApplication::desktop()->width();
  mainSplitter_->setSizes(sizes);

  QHBoxLayout *mainLayout1 = new QHBoxLayout();
  mainLayout1->addWidget(pushButtonNull_);
  mainLayout1->addWidget(mainSplitter_, 1);

  QVBoxLayout *mainLayout = new QVBoxLayout();
  mainLayout->setMargin(0);
  mainLayout->setSpacing(0);
  mainLayout->addWidget(tabBarWidget);
  mainLayout->addLayout(mainLayout1, 1);

  QWidget *centralWidget = new QWidget(this);
  centralWidget->setLayout(mainLayout);

  setCentralWidget(centralWidget);

  connect(this, SIGNAL(signalCloseApp()),
          SLOT(slotCloseApp()), Qt::QueuedConnection);
  connect(qApp, SIGNAL(commitDataRequest(QSessionManager&)),
          this, SLOT(slotCommitDataRequest(QSessionManager&)));

  connect(this, SIGNAL(signalShowNotification()),
          SLOT(showNotification()), Qt::QueuedConnection);
  connect(this, SIGNAL(signalRefreshInfoTray()),
          SLOT(slotRefreshInfoTray()), Qt::QueuedConnection);

  updateDelayer_ = new UpdateDelayer(this);
  connect(updateDelayer_, SIGNAL(signalUpdateNeeded(int, bool, int)),
          this, SLOT(slotUpdateFeedDelayed(int, bool, int)));
  connect(this, SIGNAL(signalNextUpdate()),
          updateDelayer_, SLOT(slotNextUpdateFeed()));
  connect(updateDelayer_, SIGNAL(signalUpdateModel(bool)),
          this, SLOT(feedsModelReload(bool)));

  connect(&timerLinkOpening_, SIGNAL(timeout()),
          this, SLOT(slotTimerLinkOpening()));

  loadSettingsFeeds();

  setStyleSheet("QMainWindow::separator { width: 1px; }");

  readSettings();

  importFeedStart_ = false;
  updateFeedsStart_ = false;
  updateFeedsCount_ = 0;
  if (autoUpdatefeedsStartUp_) slotGetAllFeeds();

  updateFeedsTimer_ = new QTimer(this);
  connect(updateFeedsTimer_, SIGNAL(timeout()),
          this, SLOT(slotTimerUpdateFeeds()));

  int updateFeedsTime = autoUpdatefeedsTime_*1000;
  if (autoUpdatefeedsInterval_ == 0)
    updateFeedsTime = updateFeedsTime*60;
  else if (autoUpdatefeedsInterval_ == 1)
    updateFeedsTime = updateFeedsTime*60*60;
  updateFeedsTimer_->start(updateFeedsTime);

  QTimer::singleShot(10000, this, SLOT(slotUpdateAppCheck()));

  translator_ = new QTranslator(this);
  appInstallTranslator();

  installEventFilter(this);
}

/*!****************************************************************************/
RSSListing::~RSSListing()
{
  qDebug("App_Closing");
}

void RSSListing::slotCommitDataRequest(QSessionManager &manager)
{
  slotClose();
  manager.release();
}

/*! \brief Обработка событий закрытия окна ************************************/
/*virtual*/ void RSSListing::closeEvent(QCloseEvent* event)
{
  event->ignore();

  if (closingTray_ && showTrayIcon_) {
    oldState = windowState();
    emit signalPlaceToTray();
  } else {
    slotClose();
  }
}

/*! \brief Обработка события выхода из приложения *****************************/
void RSSListing::slotClose()
{
  closeApp_ = true;

  traySystem->hide();
  hide();
  writeSettings();
  cookieJar_->saveCookies();

  persistentUpdateThread_->quit();
  persistentParseThread_->quit();
  faviconThread_->quit();

  db_.transaction();
  QSqlQuery q;

  q.exec("UPDATE news SET new=0 WHERE new==1");
  q.exec("UPDATE news SET read=2 WHERE read==1");

  // Запускаем Cleanup всех лент, исключая категории
  q.exec("SELECT id FROM feeds WHERE xmlUrl!=''");
  while (q.next()) {
    feedsCleanUp(q.value(0).toString());
  }

  bool cleanUpDB = false;
  q.exec("SELECT value FROM info WHERE name='cleanUpAllDB_0.10.0'");
  if (q.next()) cleanUpDB = q.value(0).toBool();
  else q.exec("INSERT INTO info(name, value) VALUES ('cleanUpAllDB_0.10.0', 'true')");

  QString qStr = QString("UPDATE news SET description='', content='', received='', "
                         "author_name='', author_uri='', author_email='', "
                         "category='', new='', read='', starred='', label='', "
                         "deleteDate='', feedParentId='', deleted=2 ");
  if (cleanUpDB) qStr.append("WHERE deleted==1");
  else qStr.append("WHERE deleted!=0");
  q.exec(qStr);

  // Запускаем пересчёт всех категорий, т.к. при чистке лент могли измениться
  // их счетчики
  QList<int> categoriesList;
  q.exec("SELECT id FROM feeds WHERE (xmlUrl='' OR xmlUrl IS NULL)");
  while (q.next()) {
    categoriesList << q.value(0).toInt();
  }
  recountFeedCategories(categoriesList);

  q.exec("UPDATE feeds SET newCount=0 WHERE newCount!=0");

  q.finish();
  db_.commit();

  db_.exec("VACUUM");

  if (storeDBMemory_) {
    dbMemFileThread_->sqliteDBMemFile(true);
    while(dbMemFileThread_->isRunning());
  }

  while (persistentUpdateThread_->isRunning());
  while (persistentParseThread_->isRunning());
  while (faviconThread_->isRunning());

  db_.close();

  QSqlDatabase::removeDatabase(QString());

  emit signalCloseApp();
}

/*! \brief Завершение приложения **********************************************/
void RSSListing::slotCloseApp()
{
  qApp->quit();
}

/*!****************************************************************************/
bool RSSListing::eventFilter(QObject *obj, QEvent *event)
{
  static int deactivateState = 0;

  static bool tabFixed = false;
  if (obj == feedsTreeView_->viewport()) {
    if (event->type() == QEvent::ToolTip) {
      return true;
    }
    return false;
  } else if (obj == tabBar_) {
    if (event->type() == QEvent::MouseButtonPress) {
      QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
      if (mouseEvent->button() & Qt::MiddleButton) {
        slotTabCloseRequested(tabBar_->tabAt(mouseEvent->pos()));
      } else if (mouseEvent->button() & Qt::LeftButton) {
        if (tabBar_->tabAt(QPoint(mouseEvent->pos().x(), 0)) == 0)
          tabFixed = true;
        else
          tabFixed = false;
      }
    } else if (event->type() == QEvent::MouseMove) {
      QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
      if (mouseEvent->buttons() & Qt::LeftButton) {
        if ((tabBar_->tabAt(QPoint(mouseEvent->pos().x()-78, 0)) <= 0) || tabFixed)
          return true;
      }
    }
    return false;
  } else if (obj == statusBar()) {
    if (event->type() == QEvent::MouseButtonRelease) {
      if (windowState() & Qt::WindowMaximized) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
        if ((mouseEvent->pos().x() > (statusBar()->width()-statusBar()->height())) &&
            (mouseEvent->pos().y() > 0)) {
          setFullScreen();
        }
      }
    }
    return false;
  } else if (obj == categoriesLabel_) {
    if (event->type() == QEvent::MouseButtonRelease) {
      showNewsCategoriesTree();
    }
    return false;
  }
  // Обработка открытия ссылки во внешнем браузере в фоне
  else if (event->type() == QEvent::WindowDeactivate) {
    if (openingLink_ && openLinkInBackground_) {
      openingLink_ = false;
      timerLinkOpening_.start(openingLinkTimeout_);
      deactivateState = 1;
    }
    activationStateChangedTime_ = QDateTime::currentMSecsSinceEpoch();
  }
  // Отрисовалась деактивация
  else if ((event->type() == QEvent::Paint) && (deactivateState == 1)) {
    deactivateState = 2;
  }
  // Деактивация произведена. Переактивируемся
  else if ((deactivateState == 2) && timerLinkOpening_.isActive()) {
    deactivateState = 3;
    if (!isActiveWindow()) {
      setWindowState(windowState() & ~Qt::WindowActive);
      show();
      raise();
      activateWindow();
    }
  }
  // Отрисовалась активация
  else if ((deactivateState == 3) && (event->type() == QEvent::Paint)) {
    deactivateState = 0;
  }
  // pass the event on to the parent class
  return QMainWindow::eventFilter(obj, event);
}

/** @brief Отбработка передачи ссылки во внешний браузер фоном
 *----------------------------------------------------------------------------*/
void RSSListing::slotTimerLinkOpening()
{
  timerLinkOpening_.stop();
  if (!isActiveWindow()) {
    setWindowState(windowState() & ~Qt::WindowActive);
    show();
    raise();
    activateWindow();
  }
}

/*! \brief Обработка события изменения состояния окна *************************/
/*virtual*/ void RSSListing::changeEvent(QEvent *event)
{
  if(event->type() == QEvent::WindowStateChange) {
    if(isMinimized()) {
      oldState = ((QWindowStateChangeEvent*)event)->oldState();
      if (minimizingTray_ && showTrayIcon_) {
        event->ignore();
        emit signalPlaceToTray();
        return;
      }
    } else {
      oldState = windowState();
    }
  } else if(event->type() == QEvent::ActivationChange) {
    if (isActiveWindow() && (behaviorIconTray_ == CHANGE_ICON_TRAY)) {
#if defined(QT_NO_DEBUG_OUTPUT)
      traySystem->setIcon(QIcon(":/images/quiterss16"));
#else
      traySystem->setIcon(QIcon(":/images/quiterssDebug"));
#endif
    }
  } else if(event->type() == QEvent::LanguageChange) {
    retranslateStrings();
  }
  QMainWindow::changeEvent(event);
}

/*! \brief Обработка события помещения программы в трей ***********************/
void RSSListing::slotPlaceToTray()
{
  hide();
  if (emptyWorking_)
    QTimer::singleShot(10000, this, SLOT(myEmptyWorkingSet()));
  if (markReadMinimize_)
    setFeedRead(currentNewsTab->feedId_, FeedReadPlaceToTray);
  if (clearStatusNew_)
    markAllFeedsOld();
  idFeedList_.clear();
  cntNewNewsList_.clear();

  writeSettings();
  cookieJar_->saveCookies();

  if (storeDBMemory_) {
    db_.commit();
    dbMemFileThread_->sqliteDBMemFile(true, QThread::LowestPriority);
  }
}

/*! \brief Обработка событий трея *********************************************/
void RSSListing::slotActivationTray(QSystemTrayIcon::ActivationReason reason)
{
  bool activated = false;

  switch (reason) {
  case QSystemTrayIcon::Unknown:
    break;
  case QSystemTrayIcon::Context:
    trayMenu_->activateWindow();
    break;
  case QSystemTrayIcon::DoubleClick:
    if (!singleClickTray_) {
      if ((QDateTime::currentMSecsSinceEpoch() - activationStateChangedTime_ < 300) ||
          isActiveWindow())
        activated = true;
      slotShowWindows(activated);
    }
    break;
  case QSystemTrayIcon::Trigger:
    if (singleClickTray_) {
      if ((QDateTime::currentMSecsSinceEpoch() - activationStateChangedTime_ < 200) ||
          isActiveWindow())
        activated = true;
      slotShowWindows(activated);
    }
    break;
  case QSystemTrayIcon::MiddleClick:
    break;
  }
}

/*! \brief Отображение окна по событию ****************************************/
void RSSListing::slotShowWindows(bool trayClick)
{
  if (!trayClick || isHidden()){
    if (oldState & Qt::WindowFullScreen) {
      show();
    } else if (oldState & Qt::WindowMaximized) {
      showMaximized();
    } else {
      showNormal();
      restoreGeometry(settings_->value("GeometryState").toByteArray());
    }
    activateWindow();
  } else {
    if (minimizingTray_)
      emit signalPlaceToTray();
    else
      showMinimized();
  }
}

void RSSListing::createFeedsWidget()
{
  feedsTreeModel_ = new FeedsTreeModel("feeds",
      QStringList() << "" << "" << "" << "" << "" << "",
      QStringList() << "id" << "text" << "unread" << "undeleteCount" << "parentId" << "updated",
      0,
      "text");

  feedsTreeView_ = new FeedsTreeView(this);
  feedsTreeView_->setFrameStyle(QFrame::NoFrame);
  feedsTreeView_->setModel(feedsTreeModel_);
  for (int i = 0; i < feedsTreeModel_->columnCount(); ++i)
    feedsTreeView_->hideColumn(i);
  feedsTreeView_->showColumn(feedsTreeModel_->proxyColumnByOriginal("text"));
  feedsTreeView_->header()->setResizeMode(feedsTreeModel_->proxyColumnByOriginal("text"), QHeaderView::Stretch);
  feedsTreeView_->header()->setResizeMode(feedsTreeModel_->proxyColumnByOriginal("unread"), QHeaderView::ResizeToContents);
  feedsTreeView_->header()->setResizeMode(feedsTreeModel_->proxyColumnByOriginal("undeleteCount"), QHeaderView::ResizeToContents);
  feedsTreeView_->header()->setResizeMode(feedsTreeModel_->proxyColumnByOriginal("updated"), QHeaderView::ResizeToContents);

  feedsTreeView_->sortByColumn(feedsTreeView_->columnIndex("rowToParent"),Qt::AscendingOrder);
  feedsTreeView_->setColumnHidden("id", true);
  feedsTreeView_->setColumnHidden("parentId", true);

  feedsToolBar_ = new QToolBar(this);
  feedsToolBar_->setObjectName("feedsToolBar");
  feedsToolBar_->setStyleSheet("QToolBar { border: none; padding: 0px; }");
  feedsToolBar_->setIconSize(QSize(18, 18));

  QHBoxLayout *feedsPanelLayout = new QHBoxLayout();
  feedsPanelLayout->setMargin(2);
  feedsPanelLayout->addWidget(feedsToolBar_, 1);

  feedsPanel_ = new QWidget(this);
  feedsPanel_->setObjectName("feedsPanel_");
  feedsPanel_->setStyleSheet(
        QString("#feedsPanel_ {border-bottom: 1px solid %1;}").
        arg(qApp->palette().color(QPalette::Dark).name()));
  feedsPanel_->setLayout(feedsPanelLayout);

  findFeeds_ = new FindFeed(this);
  QVBoxLayout *findFeedsLayout = new QVBoxLayout();
  findFeedsLayout->setMargin(2);
  findFeedsLayout->addWidget(findFeeds_);
  findFeedsWidget_ = new QWidget(this);
  findFeedsWidget_->hide();
  findFeedsWidget_->setLayout(findFeedsLayout);

  newsCategoriesTree_ = new QTreeWidget(this);
  newsCategoriesTree_->setObjectName("newsCategoriesTree_");
  newsCategoriesTree_->setFrameStyle(QFrame::NoFrame);
  newsCategoriesTree_->setStyleSheet(
        QString("#newsCategoriesTree_ {border-top: 1px solid %1;}").
        arg(qApp->palette().color(QPalette::Dark).name()));
  newsCategoriesTree_->setColumnCount(4);
  newsCategoriesTree_->setColumnHidden(1, true);
  newsCategoriesTree_->setColumnHidden(2, true);
  newsCategoriesTree_->setColumnHidden(3, true);
  newsCategoriesTree_->header()->hide();

  DelegateWithoutFocus *itemDelegate = new DelegateWithoutFocus(this);
  newsCategoriesTree_->setItemDelegate(itemDelegate);

  QStringList treeItem;
  treeItem.clear();
  treeItem << "Categories" << "Type" << "Id" << "CurrentNews";
  newsCategoriesTree_->setHeaderLabels(treeItem);

  treeItem.clear();
  treeItem << tr("Unread") << QString::number(TAB_CAT_UNREAD) << "-1";
  QTreeWidgetItem *treeWidgetItem = new QTreeWidgetItem(treeItem);
  treeWidgetItem->setIcon(0, QIcon(":/images/bulletUnread"));
  newsCategoriesTree_->addTopLevelItem(treeWidgetItem);
  treeItem.clear();
  treeItem << tr("Starred") << QString::number(TAB_CAT_STAR) << "-1";
  treeWidgetItem = new QTreeWidgetItem(treeItem);
  treeWidgetItem->setIcon(0, QIcon(":/images/starOn"));
  newsCategoriesTree_->addTopLevelItem(treeWidgetItem);
  treeItem.clear();
  treeItem << tr("Deleted") << QString::number(TAB_CAT_DEL) << "-1";
  treeWidgetItem = new QTreeWidgetItem(treeItem);
  treeWidgetItem->setIcon(0, QIcon(":/images/images/trash.png"));
  newsCategoriesTree_->addTopLevelItem(treeWidgetItem);
  treeItem.clear();
  treeItem << tr("Labels") << QString::number(TAB_CAT_LABEL) << "0";
  treeWidgetItem = new QTreeWidgetItem(treeItem);
  treeWidgetItem->setIcon(0, QIcon(":/images/label_3"));
  newsCategoriesTree_->addTopLevelItem(treeWidgetItem);

  QSqlQuery q;
  q.exec("SELECT id, name, image, currentNews FROM labels ORDER BY num");
  while (q.next()) {
    int idLabel = q.value(0).toInt();
    QString nameLabel = q.value(1).toString();
    QByteArray byteArray = q.value(2).toByteArray();
    QString currentNews = q.value(3).toString();
    QPixmap imageLabel;
    if (!byteArray.isNull())
      imageLabel.loadFromData(byteArray);
    treeItem.clear();
    treeItem << nameLabel << QString::number(TAB_CAT_LABEL)
             << QString::number(idLabel) << currentNews;
    QTreeWidgetItem *childItem = new QTreeWidgetItem(treeItem);
    childItem->setIcon(0, QIcon(imageLabel));
    treeWidgetItem->addChild(childItem);
  }
  newsCategoriesTree_->expandAll();

  categoriesLabel_ = new QLabel(this);
  categoriesLabel_->setObjectName("categoriesLabel_");

  showCategoriesButton_ = new QToolButton(this);
  showCategoriesButton_->setFocusPolicy(Qt::NoFocus);
  showCategoriesButton_->setMaximumSize(16, 16);
  showCategoriesButton_->setAutoRaise(true);

  QHBoxLayout *categoriesPanelLayout = new QHBoxLayout();
  categoriesPanelLayout->setMargin(2);
  categoriesPanelLayout->addSpacing(2);
  categoriesPanelLayout->addWidget(categoriesLabel_, 1);
  categoriesPanelLayout->addWidget(showCategoriesButton_);

  categoriesPanel_ = new QWidget(this);
  categoriesPanel_->setObjectName("categoriesPanel_");
  categoriesPanel_->setLayout(categoriesPanelLayout);

  QVBoxLayout *categoriesLayout = new QVBoxLayout();
  categoriesLayout->setMargin(0);
  categoriesLayout->setSpacing(0);
  categoriesLayout->addWidget(categoriesPanel_);
  categoriesLayout->addWidget(newsCategoriesTree_, 1);

  categoriesWidget_ = new QWidget(this);
  categoriesWidget_->setLayout(categoriesLayout);

  feedsSplitter_ = new QSplitter(Qt::Vertical);
  feedsSplitter_->setChildrenCollapsible(false);
  feedsSplitter_->setHandleWidth(1);
  feedsSplitter_->setStyleSheet(
        QString("QSplitter::handle {background: %1;}").
        arg(qApp->palette().color(QPalette::Dark).name()));
  feedsSplitter_->addWidget(feedsTreeView_);
  feedsSplitter_->addWidget(categoriesWidget_);
  feedsSplitter_->setStretchFactor(0, 1);

  #define CATEGORIES_HEIGHT 210
  QList <int> sizes;
  sizes << QApplication::desktop()->height() << CATEGORIES_HEIGHT;
  feedsSplitter_->setSizes(sizes);

  QVBoxLayout *feedsLayout = new QVBoxLayout();
  feedsLayout->setMargin(0);
  feedsLayout->setSpacing(0);
  feedsLayout->addWidget(feedsPanel_);
  feedsLayout->addWidget(findFeedsWidget_);
  feedsLayout->addWidget(feedsSplitter_, 1);

  feedsWidget_ = new QFrame(this);
  feedsWidget_->setFrameStyle(QFrame::NoFrame);
  feedsWidget_->setLayout(feedsLayout);

  connect(feedsTreeView_, SIGNAL(pressed(QModelIndex)),
          this, SLOT(slotFeedClicked(QModelIndex)));
  connect(feedsTreeView_, SIGNAL(signalMiddleClicked()),
          this, SLOT(slotOpenFeedNewTab()));
  connect(feedsTreeView_, SIGNAL(signalDoubleClicked()),
          this, SLOT(slotGetFeed()));
  connect(feedsTreeView_, SIGNAL(pressKeyUp()), this, SLOT(slotFeedUpPressed()));
  connect(feedsTreeView_, SIGNAL(pressKeyDown()), this, SLOT(slotFeedDownPressed()));
  connect(feedsTreeView_, SIGNAL(pressKeyHome()), this, SLOT(slotFeedHomePressed()));
  connect(feedsTreeView_, SIGNAL(pressKeyEnd()), this, SLOT(slotFeedEndPressed()));
  connect(feedsTreeView_, SIGNAL(signalDropped(QModelIndex&,QModelIndex&,int)),
          this, SLOT(slotMoveIndex(QModelIndex&,QModelIndex&,int)));
  connect(feedsTreeView_, SIGNAL(customContextMenuRequested(QPoint)),
          this, SLOT(showContextMenuFeed(const QPoint &)));

  connect(findFeeds_, SIGNAL(textChanged(QString)),
          this, SLOT(slotFindFeeds(QString)));
  connect(findFeeds_, SIGNAL(signalSelectFind()),
          this, SLOT(slotSelectFind()));
  connect(findFeeds_, SIGNAL(returnPressed()),
          this, SLOT(slotSelectFind()));

  connect(newsCategoriesTree_, SIGNAL(itemClicked(QTreeWidgetItem*,int)),
          this, SLOT(slotCategoriesClicked(QTreeWidgetItem*,int)));
  connect(showCategoriesButton_, SIGNAL(clicked()),
          this, SLOT(showNewsCategoriesTree()));
  connect(feedsSplitter_, SIGNAL(splitterMoved(int,int)),
          this, SLOT(feedsSplitterMoved(int,int)));

  feedsTreeView_->viewport()->installEventFilter(this);
  categoriesLabel_->installEventFilter(this);
}

void RSSListing::createToolBarNull()
{
  pushButtonNull_ = new QPushButton(this);
  pushButtonNull_->setObjectName("pushButtonNull");
  pushButtonNull_->setIcon(QIcon(":/images/images/triangleR.png"));
  pushButtonNull_->setFixedWidth(6);
  pushButtonNull_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  pushButtonNull_->setFocusPolicy(Qt::NoFocus);
  pushButtonNull_->setStyleSheet("background: #E8E8E8; border: none; padding: 0px;");
}

void RSSListing::createNewsTab(int index)
{
  currentNewsTab = (NewsTabWidget*)stackedWidget_->widget(index);
  currentNewsTab->setSettings();
  currentNewsTab->retranslateStrings();
  currentNewsTab->setBrowserPosition();

  newsModel_ = currentNewsTab->newsModel_;
  newsView_ = currentNewsTab->newsView_;  
}

void RSSListing::createStatusBar()
{
  progressBar_ = new QProgressBar(this);
  progressBar_->setObjectName("progressBar_");
  progressBar_->setFormat("%p%");
  progressBar_->setAlignment(Qt::AlignCenter);
  progressBar_->setFixedWidth(100);
  progressBar_->setFixedHeight(15);
  progressBar_->setMinimum(0);
  progressBar_->setMaximum(0);
  progressBar_->setValue(0);
  progressBar_->setVisible(false);
  statusBar()->setMinimumHeight(22);

  QToolButton *loadImagesButton = new QToolButton(this);
  loadImagesButton->setFocusPolicy(Qt::NoFocus);
  loadImagesButton->setDefaultAction(autoLoadImagesToggle_);
  loadImagesButton->setStyleSheet("QToolButton { border: none; padding: 0px; }");

  QToolButton *fullScreenButton = new QToolButton(this);
  fullScreenButton->setFocusPolicy(Qt::NoFocus);
  fullScreenButton->setDefaultAction(fullScreenAct_);
  fullScreenButton->setStyleSheet("QToolButton { border: none; padding: 0px; }");
  statusBar()->installEventFilter(this);

  statusBar()->addPermanentWidget(progressBar_);
  statusUnread_ = new QLabel(this);
  statusUnread_->hide();
  statusBar()->addPermanentWidget(statusUnread_);
  statusAll_ = new QLabel(this);
  statusAll_->hide();
  statusBar()->addPermanentWidget(statusAll_);
  statusBar()->addPermanentWidget(loadImagesButton);
  statusBar()->addPermanentWidget(fullScreenButton);
  statusBar()->setVisible(true);
}

void RSSListing::createTray()
{
#if defined(QT_NO_DEBUG_OUTPUT)
    traySystem = new QSystemTrayIcon(QIcon(":/images/quiterss16"), this);
#else
  traySystem = new QSystemTrayIcon(QIcon(":/images/quiterssDebug"), this);
#endif
  connect(traySystem,SIGNAL(activated(QSystemTrayIcon::ActivationReason)),
          this, SLOT(slotActivationTray(QSystemTrayIcon::ActivationReason)));
  connect(this, SIGNAL(signalPlaceToTray()),
          this, SLOT(slotPlaceToTray()), Qt::QueuedConnection);
  traySystem->setToolTip("QuiteRSS");
  createTrayMenu();
}

/** @brief Создание виджета вкладок
 *----------------------------------------------------------------------------*/
void RSSListing::createTabBar()
{
  tabBar_ = new QTabBar();
  tabBar_->addTab("");
  tabBar_->setIconSize(QSize(16, 16));
  tabBar_->setMovable(true);
  tabBar_->setExpanding(false);
  tabBar_->setFocusPolicy(Qt::NoFocus);
  tabBar_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(tabBar_, SIGNAL(tabCloseRequested(int)),
          this, SLOT(slotTabCloseRequested(int)));
  connect(tabBar_, SIGNAL(currentChanged(int)),
          this, SLOT(slotTabCurrentChanged(int)));
  connect(tabBar_, SIGNAL(tabMoved(int,int)),
          SLOT(slotTabMoved(int,int)));
  connect(this, SIGNAL(signalSetCurrentTab(int,bool)),
          SLOT(setCurrentTab(int,bool)), Qt::QueuedConnection);
  connect(tabBar_, SIGNAL(customContextMenuRequested(QPoint)),
          this, SLOT(showContextMenuTabBar(const QPoint &)));
  tabBar_->installEventFilter(this);
}

/*! \brief Создание действий **************************************************
 * \details Которые будут использоваться в главном меню и ToolBar
 ******************************************************************************/
void RSSListing::createActions()
{
  addAct_ = new QAction(this);
  addAct_->setObjectName("newAct");
  addAct_->setIcon(QIcon(":/images/add"));
  this->addAction(addAct_);
  connect(addAct_, SIGNAL(triggered()), this, SLOT(addFeed()));

  addFeedAct_ = new QAction(this);
  addFeedAct_->setObjectName("addFeedAct");
  addFeedAct_->setIcon(QIcon(":/images/feed"));
  this->addAction(addFeedAct_);
  connect(addFeedAct_, SIGNAL(triggered()), this, SLOT(addFeed()));

  addFolderAct_ = new QAction(this);
  addFolderAct_->setObjectName("addFolderAct");
  addFolderAct_->setIcon(QIcon(":/images/folder"));
  this->addAction(addFolderAct_);
  connect(addFolderAct_, SIGNAL(triggered()), this, SLOT(addFolder()));

  openFeedNewTabAct_ = new QAction(this);
  openFeedNewTabAct_->setObjectName("openNewTabAct");
  this->addAction(openFeedNewTabAct_);
  connect(openFeedNewTabAct_, SIGNAL(triggered()), this, SLOT(slotOpenFeedNewTab()));

  deleteFeedAct_ = new QAction(this);
  deleteFeedAct_->setObjectName("deleteFeedAct");
  deleteFeedAct_->setIcon(QIcon(":/images/delete"));
  this->addAction(deleteFeedAct_);
  connect(deleteFeedAct_, SIGNAL(triggered()), this, SLOT(deleteItemFeedsTree()));

  importFeedsAct_ = new QAction(this);
  importFeedsAct_->setObjectName("importFeedsAct");
  importFeedsAct_->setIcon(QIcon(":/images/importFeeds"));
  this->addAction(importFeedsAct_);
  connect(importFeedsAct_, SIGNAL(triggered()), this, SLOT(slotImportFeeds()));

  exportFeedsAct_ = new QAction(this);
  exportFeedsAct_->setObjectName("exportFeedsAct");
  exportFeedsAct_->setIcon(QIcon(":/images/exportFeeds"));
  this->addAction(exportFeedsAct_);
  connect(exportFeedsAct_, SIGNAL(triggered()), this, SLOT(slotExportFeeds()));

  exitAct_ = new QAction(this);
  exitAct_->setObjectName("exitAct");
  this->addAction(exitAct_);
  connect(exitAct_, SIGNAL(triggered()), this, SLOT(slotClose()));

  mainToolbarToggle_ = new QAction(this);
  mainToolbarToggle_->setCheckable(true);
  feedsToolbarToggle_ = new QAction(this);
  feedsToolbarToggle_->setCheckable(true);
  newsToolbarToggle_ = new QAction(this);
  newsToolbarToggle_->setCheckable(true);
  browserToolbarToggle_ = new QAction(this);
  browserToolbarToggle_->setCheckable(true);
  categoriesPanelToggle_ = new QAction(this);
  categoriesPanelToggle_->setCheckable(true);

  connect(feedsToolbarToggle_, SIGNAL(toggled(bool)),
          feedsPanel_, SLOT(setVisible(bool)));
  connect(categoriesPanelToggle_, SIGNAL(toggled(bool)),
          categoriesWidget_, SLOT(setVisible(bool)));

  customizeMainToolbarAct_ = new QAction(this);
  customizeMainToolbarAct_->setObjectName("customizeMainToolbarAct");
  customizeMainToolbarAct2_ = new QAction(this);
  connect(customizeMainToolbarAct2_, SIGNAL(triggered()),
          this, SLOT(customizeMainToolbar()));

  toolBarLockAct_ = new QAction(this);
  toolBarLockAct_->setCheckable(true);
  toolBarHideAct_ = new QAction(this);

  customizeFeedsToolbarAct_ = new QAction(this);
  customizeFeedsToolbarAct_->setObjectName("customizeFeedsToolbarAct");

  customizeNewsToolbarAct_ = new QAction(this);
  customizeNewsToolbarAct_->setObjectName("customizeNewsToolbarAct");

  systemStyle_ = new QAction(this);
  systemStyle_->setObjectName("systemStyle_");
  systemStyle_->setCheckable(true);
  system2Style_ = new QAction(this);
  system2Style_->setObjectName("system2Style_");
  system2Style_->setCheckable(true);
  greenStyle_ = new QAction(this);
  greenStyle_->setObjectName("greenStyle_");
  greenStyle_->setCheckable(true);
  greenStyle_->setChecked(true);
  orangeStyle_ = new QAction(this);
  orangeStyle_->setObjectName("orangeStyle_");
  orangeStyle_->setCheckable(true);
  purpleStyle_ = new QAction(this);
  purpleStyle_->setObjectName("purpleStyle_");
  purpleStyle_->setCheckable(true);
  pinkStyle_ = new QAction(this);
  pinkStyle_->setObjectName("pinkStyle_");
  pinkStyle_->setCheckable(true);
  grayStyle_ = new QAction(this);
  grayStyle_->setObjectName("grayStyle_");
  grayStyle_->setCheckable(true);

  topBrowserPositionAct_ = new QAction(this);
  topBrowserPositionAct_->setCheckable(true);
  topBrowserPositionAct_->setData(TOP_POSITION);
  bottomBrowserPositionAct_ = new QAction(this);
  bottomBrowserPositionAct_->setCheckable(true);
  bottomBrowserPositionAct_->setData(BOTTOM_POSITION);
  rightBrowserPositionAct_ = new QAction(this);
  rightBrowserPositionAct_->setCheckable(true);
  rightBrowserPositionAct_->setData(RIGHT_POSITION);
  leftBrowserPositionAct_ = new QAction(this);
  leftBrowserPositionAct_->setCheckable(true);
  leftBrowserPositionAct_->setData(LEFT_POSITION);

  autoLoadImagesToggle_ = new QAction(this);
  autoLoadImagesToggle_->setObjectName("autoLoadImagesToggle");
  autoLoadImagesToggle_->setIcon(QIcon(":/images/imagesOn"));
  this->addAction(autoLoadImagesToggle_);
  connect(autoLoadImagesToggle_, SIGNAL(triggered()),
          this, SLOT(setAutoLoadImages()));

  printAct_ = new QAction(this);
  printAct_->setObjectName("printAct");
  printAct_->setIcon(QIcon(":/images/printer"));
  this->addAction(printAct_);
  connect(printAct_, SIGNAL(triggered()), this, SLOT(slotPrint()));
  printPreviewAct_ = new QAction(this);
  printPreviewAct_->setObjectName("printPreviewAct");
  printPreviewAct_->setIcon(QIcon(":/images/printer"));
  this->addAction(printPreviewAct_);
  connect(printPreviewAct_, SIGNAL(triggered()), this, SLOT(slotPrintPreview()));

  savePageAsAct_ = new QAction(this);
  savePageAsAct_->setObjectName("savePageAsAct");
  savePageAsAct_->setIcon(QIcon(":/images/save_as"));
  this->addAction(savePageAsAct_);
  connect(savePageAsAct_, SIGNAL(triggered()), this, SLOT(slotSavePageAs()));

  zoomInAct_ = new QAction(this);
  zoomInAct_->setObjectName("zoomInAct");
  zoomInAct_->setIcon(QIcon(":/images/zoomIn"));
  this->addAction(zoomInAct_);
  zoomOutAct_ = new QAction(this);
  zoomOutAct_->setObjectName("zoomOutAct");
  zoomOutAct_->setIcon(QIcon(":/images/zoomOut"));
  this->addAction(zoomOutAct_);
  zoomTo100Act_ = new QAction(this);
  zoomTo100Act_->setObjectName("zoomTo100Act");
  this->addAction(zoomTo100Act_);

  updateFeedAct_ = new QAction(this);
  updateFeedAct_->setObjectName("updateFeedAct");
  updateFeedAct_->setIcon(QIcon(":/images/updateFeed"));
  this->addAction(updateFeedAct_);
  connect(updateFeedAct_, SIGNAL(triggered()), this, SLOT(slotGetFeed()));

  updateAllFeedsAct_ = new QAction(this);
  updateAllFeedsAct_->setObjectName("updateAllFeedsAct");
  updateAllFeedsAct_->setIcon(QIcon(":/images/updateAllFeeds"));
  this->addAction(updateAllFeedsAct_);
  connect(updateAllFeedsAct_, SIGNAL(triggered()), this, SLOT(slotGetAllFeeds()));

  markAllFeedsRead_ = new QAction(this);
  markAllFeedsRead_->setObjectName("markAllFeedRead");
  markAllFeedsRead_->setIcon(QIcon(":/images/markReadAll"));
  this->addAction(markAllFeedsRead_);
  connect(markAllFeedsRead_, SIGNAL(triggered()), this, SLOT(markAllFeedsRead()));

  indentationFeedsTreeAct_ = new QAction(this);
  indentationFeedsTreeAct_->setCheckable(true);
  connect(indentationFeedsTreeAct_, SIGNAL(triggered()),
          this, SLOT(slotIndentationFeedsTree()));

  sortedByTitleFeedsTreeAct_ = new QAction(this);
  connect(sortedByTitleFeedsTreeAct_, SIGNAL(triggered()),
          this, SLOT(sortedByTitleFeedsTree()));

  collapseAllFoldersAct_ = new QAction(this);
  collapseAllFoldersAct_->setObjectName("collapseAllFolderAct");
  collapseAllFoldersAct_->setIcon(QIcon(":/images/bulletMinus"));
  this->addAction(collapseAllFoldersAct_);
  connect(collapseAllFoldersAct_, SIGNAL(triggered()),
          feedsTreeView_, SLOT(collapseAll()));

  expandAllFoldersAct_ = new QAction(this);
  expandAllFoldersAct_->setObjectName("expandAllFolderAct");
  expandAllFoldersAct_->setIcon(QIcon(":/images/bulletPlus"));
  this->addAction(expandAllFoldersAct_);
  connect(expandAllFoldersAct_, SIGNAL(triggered()),
          feedsTreeView_, SLOT(expandAll()));

  markNewsRead_ = new QAction(this);
  markNewsRead_->setObjectName("markNewsRead");
  markNewsRead_->setIcon(QIcon(":/images/markRead"));
  this->addAction(markNewsRead_);

  markAllNewsRead_ = new QAction(this);
  markAllNewsRead_->setObjectName("markAllNewsRead");
  markAllNewsRead_->setIcon(QIcon(":/images/markReadAll"));
  this->addAction(markAllNewsRead_);

  setNewsFiltersAct_ = new QAction(this);
  setNewsFiltersAct_->setObjectName("setNewsFiltersAct");
  setNewsFiltersAct_->setIcon(QIcon(":/images/filterOff"));
  this->addAction(setNewsFiltersAct_);
  connect(setNewsFiltersAct_, SIGNAL(triggered()), this, SLOT(showNewsFiltersDlg()));
  setFilterNewsAct_ = new QAction(this);
  setFilterNewsAct_->setObjectName("setFilterNewsAct");
  setFilterNewsAct_->setIcon(QIcon(":/images/filterOff"));
  this->addAction(setFilterNewsAct_);
  connect(setFilterNewsAct_, SIGNAL(triggered()), this, SLOT(showFilterRulesDlg()));

  optionsAct_ = new QAction(this);
  optionsAct_->setObjectName("optionsAct");
  optionsAct_->setIcon(QIcon(":/images/options"));
  this->addAction(optionsAct_);
  connect(optionsAct_, SIGNAL(triggered()), this, SLOT(showOptionDlg()));

  feedsFilter_ = new QAction(this);
  feedsFilter_->setObjectName("feedsFilter");
  feedsFilter_->setIcon(QIcon(":/images/filterOff"));
  this->addAction(feedsFilter_);
  filterFeedsAll_ = new QAction(this);
  filterFeedsAll_->setObjectName("filterFeedsAll_");
  filterFeedsAll_->setCheckable(true);
  filterFeedsAll_->setChecked(true);
  filterFeedsNew_ = new QAction(this);
  filterFeedsNew_->setObjectName("filterFeedsNew_");
  filterFeedsNew_->setCheckable(true);
  filterFeedsUnread_ = new QAction(this);
  filterFeedsUnread_->setObjectName("filterFeedsUnread_");
  filterFeedsUnread_->setCheckable(true);
  filterFeedsStarred_ = new QAction(this);
  filterFeedsStarred_->setObjectName("filterFeedsStarred_");
  filterFeedsStarred_->setCheckable(true);

  newsFilter_ = new QAction(this);
  newsFilter_->setObjectName("newsFilter");
  newsFilter_->setIcon(QIcon(":/images/filterOff"));
  this->addAction(newsFilter_);
  filterNewsAll_ = new QAction(this);
  filterNewsAll_->setObjectName("filterNewsAll_");
  filterNewsAll_->setCheckable(true);
  filterNewsAll_->setChecked(true);
  filterNewsNew_ = new QAction(this);
  filterNewsNew_->setObjectName("filterNewsNew_");
  filterNewsNew_->setCheckable(true);
  filterNewsUnread_ = new QAction(this);
  filterNewsUnread_->setObjectName("filterNewsUnread_");
  filterNewsUnread_->setCheckable(true);
  filterNewsStar_ = new QAction(this);
  filterNewsStar_->setObjectName("filterNewsStar_");
  filterNewsStar_->setCheckable(true);
  filterNewsNotStarred_ = new QAction(this);
  filterNewsNotStarred_->setObjectName("filterNewsNotStarred_");
  filterNewsNotStarred_->setCheckable(true);
  filterNewsUnreadStar_ = new QAction(this);
  filterNewsUnreadStar_->setObjectName("filterNewsUnreadStar_");
  filterNewsUnreadStar_->setCheckable(true);
  filterNewsLastDay_ = new QAction(this);
  filterNewsLastDay_->setObjectName("filterNewsLastDay_");
  filterNewsLastDay_->setCheckable(true);
  filterNewsLastWeek_ = new QAction(this);
  filterNewsLastWeek_->setObjectName("filterNewsLastWeek_");
  filterNewsLastWeek_->setCheckable(true);

  aboutAct_ = new QAction(this);
  aboutAct_->setObjectName("AboutAct_");
  connect(aboutAct_, SIGNAL(triggered()), this, SLOT(slotShowAboutDlg()));

  updateAppAct_ = new QAction(this);
  updateAppAct_->setObjectName("UpdateApp_");
  connect(updateAppAct_, SIGNAL(triggered()), this, SLOT(slotShowUpdateAppDlg()));

  reportProblemAct_ = new QAction(this);
  reportProblemAct_->setObjectName("reportProblemAct_");
  connect(reportProblemAct_, SIGNAL(triggered()), this, SLOT(slotReportProblem()));

  openInBrowserAct_ = new QAction(this);
  openInBrowserAct_->setObjectName("openInBrowserAct");
  this->addAction(openInBrowserAct_);

  openInExternalBrowserAct_ = new QAction(this);
  openInExternalBrowserAct_->setObjectName("openInExternalBrowserAct");
  openInExternalBrowserAct_->setIcon(QIcon(":/images/openBrowser"));
  this->addAction(openInExternalBrowserAct_);

  openNewsNewTabAct_ = new QAction(this);
  openNewsNewTabAct_->setObjectName("openInNewTabAct");
  openNewsNewTabAct_->setIcon(QIcon(":/images/images/tab_go.png"));
  this->addAction(openNewsNewTabAct_);
  openNewsBackgroundTabAct_ = new QAction(this);
  openNewsBackgroundTabAct_->setObjectName("openInBackgroundTabAct");
  this->addAction(openNewsBackgroundTabAct_);

  markStarAct_ = new QAction(this);
  markStarAct_->setObjectName("markStarAct");
  markStarAct_->setIcon(QIcon(":/images/starOn"));
  this->addAction(markStarAct_);

  deleteNewsAct_ = new QAction(this);
  deleteNewsAct_->setObjectName("deleteNewsAct");
  deleteNewsAct_->setIcon(QIcon(":/images/delete"));
  this->addAction(deleteNewsAct_);
  deleteAllNewsAct_ = new QAction(this);
  deleteAllNewsAct_->setObjectName("deleteAllNewsAct");
  this->addAction(deleteAllNewsAct_);

  restoreNewsAct_ = new QAction(this);
  restoreNewsAct_->setObjectName("restoreNewsAct");
  restoreNewsAct_->setIcon(QIcon(":/images/images/arrow_turn_left.png"));

  restoreLastNewsAct_ = new QAction(this);
  restoreLastNewsAct_->setObjectName("restoreLastNewsAct");
  restoreLastNewsAct_->setIcon(QIcon(":/images/images/arrow_turn_left.png"));
  this->addAction(restoreLastNewsAct_);
  connect(restoreLastNewsAct_, SIGNAL(triggered()), this, SLOT(restoreLastNews()));

  markFeedRead_ = new QAction(this);
  markFeedRead_->setObjectName("markFeedRead");
  markFeedRead_->setIcon(QIcon(":/images/markRead"));
  this->addAction(markFeedRead_);
  connect(markFeedRead_, SIGNAL(triggered()), this, SLOT(markFeedRead()));

  feedProperties_ = new QAction(this);
  feedProperties_->setObjectName("feedProperties");
  feedProperties_->setIcon(QIcon(":/images/preferencesFeed"));
  this->addAction(feedProperties_);
  connect(feedProperties_, SIGNAL(triggered()), this, SLOT(showFeedPropertiesDlg()));

  feedKeyUpAct_ = new QAction(this);
  feedKeyUpAct_->setObjectName("feedKeyUp");
  connect(feedKeyUpAct_, SIGNAL(triggered()), this, SLOT(slotFeedPrevious()));
  this->addAction(feedKeyUpAct_);

  feedKeyDownAct_ = new QAction(this);
  feedKeyDownAct_->setObjectName("feedKeyDownAct");
  connect(feedKeyDownAct_, SIGNAL(triggered()), this, SLOT(slotFeedNext()));
  this->addAction(feedKeyDownAct_);

  newsKeyUpAct_ = new QAction(this);
  newsKeyUpAct_->setObjectName("newsKeyUpAct");
  this->addAction(newsKeyUpAct_);

  newsKeyDownAct_ = new QAction(this);
  newsKeyDownAct_->setObjectName("newsKeyDownAct");
  this->addAction(newsKeyDownAct_);

  switchFocusAct_ = new QAction(this);
  switchFocusAct_->setObjectName("switchFocusAct");
  connect(switchFocusAct_, SIGNAL(triggered()), this, SLOT(slotSwitchFocus()));
  this->addAction(switchFocusAct_);
  switchFocusPrevAct_ = new QAction(this);
  switchFocusPrevAct_->setObjectName("switchFocusPrevAct");
  connect(switchFocusPrevAct_, SIGNAL(triggered()), this, SLOT(slotSwitchPrevFocus()));
  this->addAction(switchFocusPrevAct_);

  feedsWidgetVisibleAct_ = new QAction(this);
  feedsWidgetVisibleAct_->setObjectName("visibleFeedsWidgetAct");
  feedsWidgetVisibleAct_->setCheckable(true);
  connect(feedsWidgetVisibleAct_, SIGNAL(triggered()), this, SLOT(slotVisibledFeedsWidget()));
  connect(pushButtonNull_, SIGNAL(clicked()), feedsWidgetVisibleAct_, SLOT(trigger()));
  this->addAction(feedsWidgetVisibleAct_);

  showUnreadCount_ = new QAction(this);
  showUnreadCount_->setData(feedsTreeModel_->proxyColumnByOriginal("unread"));
  showUnreadCount_->setCheckable(true);
  showUndeleteCount_ = new QAction(this);
  showUndeleteCount_->setData(feedsTreeModel_->proxyColumnByOriginal("undeleteCount"));
  showUndeleteCount_->setCheckable(true);
  showLastUpdated_ = new QAction(this);
  showLastUpdated_->setData(feedsTreeModel_->proxyColumnByOriginal("updated"));
  showLastUpdated_->setCheckable(true);

  openDescriptionNewsAct_ = new QAction(this);
  openDescriptionNewsAct_->setObjectName("openDescriptionNewsAct");
  connect(openDescriptionNewsAct_, SIGNAL(triggered()),
          this, SLOT(slotOpenNewsWebView()));
  this->addAction(openDescriptionNewsAct_);

  findTextAct_ = new QAction(this);
  findTextAct_->setObjectName("findTextAct");
  connect(findTextAct_, SIGNAL(triggered()),
          this, SLOT(findText()));
  this->addAction(findTextAct_);

  placeToTrayAct_ = new QAction(this);
  placeToTrayAct_->setObjectName("placeToTrayAct");
  connect(placeToTrayAct_, SIGNAL(triggered()), this, SLOT(slotPlaceToTray()));
  this->addAction(placeToTrayAct_);

  findFeedAct_ = new QAction(this);
  findFeedAct_->setObjectName("findFeedAct");
  findFeedAct_->setCheckable(true);
  findFeedAct_->setChecked(false);
  findFeedAct_->setIcon(QIcon(":/images/images/findFeed.png"));
  this->addAction(findFeedAct_);
  connect(findFeedAct_, SIGNAL(triggered(bool)),
          this, SLOT(findFeedVisible(bool)));

  fullScreenAct_ = new QAction(this);
  fullScreenAct_->setObjectName("fullScreenAct");
  fullScreenAct_->setIcon(QIcon(":/images/images/fullScreen.png"));
  this->addAction(fullScreenAct_);
  connect(fullScreenAct_, SIGNAL(triggered()),
          this, SLOT(setFullScreen()));

  stayOnTopAct_ = new QAction(this);
  stayOnTopAct_->setObjectName("stayOnTopAct");
  stayOnTopAct_->setCheckable(true);
  this->addAction(stayOnTopAct_);
  connect(stayOnTopAct_, SIGNAL(triggered()),
          this, SLOT(setStayOnTop()));

  newsLabelGroup_ = new QActionGroup(this);
  newsLabelGroup_->setExclusive(false);
  QSqlQuery q;
  q.exec("SELECT id, name, image FROM labels ORDER BY num");
  while (q.next()) {
    int idLabel = q.value(0).toInt();
    QString nameLabel = q.value(1).toString();
    QByteArray byteArray = q.value(2).toByteArray();
    QPixmap imageLabel;
    if (!byteArray.isNull())
      imageLabel.loadFromData(byteArray);
    QAction *action = new QAction(QIcon(imageLabel), nameLabel, this);
    action->setObjectName(QString("labelAction_%1").arg(idLabel));
    action->setCheckable(true);
    action->setData(idLabel);
    newsLabelGroup_->addAction(action);
  }
  this->addActions(newsLabelGroup_->actions());

  newsLabelAction_ = new QAction(this);
  newsLabelAction_->setObjectName("newsLabelAction");
  this->addAction(newsLabelAction_);
  if (newsLabelGroup_->actions().count()) {
    newsLabelAction_->setIcon(newsLabelGroup_->actions().at(0)->icon());
    newsLabelAction_->setToolTip(newsLabelGroup_->actions().at(0)->text());
    newsLabelAction_->setData(newsLabelGroup_->actions().at(0)->data());
  }
  connect(newsLabelAction_, SIGNAL(triggered()),
          this, SLOT(setDefaultLabelNews()));
  connect(newsLabelGroup_, SIGNAL(triggered(QAction*)),
          this, SLOT(setLabelNews(QAction*)));

  closeTabAct_ = new QAction(this);
  closeTabAct_->setObjectName("closeTabAct");
  this->addAction(closeTabAct_);
  connect(closeTabAct_, SIGNAL(triggered()), this, SLOT(slotCloseTab()));

  closeAllTabAct_ = new QAction(this);
  closeAllTabAct_->setObjectName("closeAllTabAct");
  this->addAction(closeAllTabAct_);
  connect(closeAllTabAct_, SIGNAL(triggered()),
          this, SLOT(slotCloseAllTabButCurrent()));

  nextTabAct_ = new QAction(this);
  nextTabAct_->setObjectName("nextTabAct");
  this->addAction(nextTabAct_);
  connect(nextTabAct_, SIGNAL(triggered()), this, SLOT(slotNextTab()));

  prevTabAct_ = new QAction(this);
  prevTabAct_->setObjectName("prevTabAct");
  this->addAction(prevTabAct_);
  connect(prevTabAct_, SIGNAL(triggered()), this, SLOT(slotPrevTab()));

  reduceNewsListAct_ = new QAction(this);
  reduceNewsListAct_->setObjectName("reduceNewsListAct");
  this->addAction(reduceNewsListAct_);
  connect(reduceNewsListAct_, SIGNAL(triggered()),
          this, SLOT(reduceNewsList()));
  increaseNewsListAct_ = new QAction(this);
  increaseNewsListAct_->setObjectName("increaseNewsListAct");
  this->addAction(increaseNewsListAct_);
  connect(increaseNewsListAct_, SIGNAL(triggered()),
          this, SLOT(increaseNewsList()));

  nextUnreadNewsAct_ = new QAction(this);
  nextUnreadNewsAct_->setObjectName("nextUnreadNewsAct");
  nextUnreadNewsAct_->setIcon(QIcon(":/images/moveDown"));
  this->addAction(nextUnreadNewsAct_);
  connect(nextUnreadNewsAct_, SIGNAL(triggered()), this, SLOT(nextUnreadNews()));
  prevUnreadNewsAct_ = new QAction(this);
  prevUnreadNewsAct_->setObjectName("prevUnreadNewsAct");
  prevUnreadNewsAct_->setIcon(QIcon(":/images/moveUp"));
  this->addAction(prevUnreadNewsAct_);
  connect(prevUnreadNewsAct_, SIGNAL(triggered()), this, SLOT(prevUnreadNews()));

  openHomeFeedAct_ = new QAction(this);
  openHomeFeedAct_->setObjectName("openHomeFeedsAct");
  openHomeFeedAct_->setIcon(QIcon(":/images/homePage"));
  this->addAction(openHomeFeedAct_);
  connect(openHomeFeedAct_, SIGNAL(triggered()), this, SLOT(slotOpenHomeFeed()));

  copyLinkAct_ = new QAction(this);
  copyLinkAct_->setObjectName("copyLinkAct");
  copyLinkAct_->setIcon(QIcon(":/images/copy"));
  this->addAction(copyLinkAct_);
  connect(copyLinkAct_, SIGNAL(triggered()), this, SLOT(slotCopyLinkNews()));

  shareGroup_ = new QActionGroup(this);
  shareGroup_->setExclusive(false);

  emailShareAct_ = new QAction(this);
  emailShareAct_->setObjectName("emailShareAct");
  emailShareAct_->setText("Email");
  emailShareAct_->setIcon(QIcon(":/images/images/email.png"));
  shareGroup_->addAction(emailShareAct_);

  evernoteShareAct_ = new QAction(this);
  evernoteShareAct_->setObjectName("evernoteShareAct");
  evernoteShareAct_->setText("Evernote");
  evernoteShareAct_->setIcon(QIcon(":/images/images/share_evernote.png"));
  shareGroup_->addAction(evernoteShareAct_);

  facebookShareAct_ = new QAction(this);
  facebookShareAct_->setObjectName("facebookShareAct");
  facebookShareAct_->setText("Facebook");
  facebookShareAct_->setIcon(QIcon(":/images/images/share_facebook.png"));
  shareGroup_->addAction(facebookShareAct_);

  livejournalShareAct_ = new QAction(this);
  livejournalShareAct_->setObjectName("livejournalShareAct");
  livejournalShareAct_->setText("LiveJournal");
  livejournalShareAct_->setIcon(QIcon(":/images/images/share_livejournal.png"));
  shareGroup_->addAction(livejournalShareAct_);

  pocketShareAct_ = new QAction(this);
  pocketShareAct_->setObjectName("pocketShareAct");
  pocketShareAct_->setText("Pocket");
  pocketShareAct_->setIcon(QIcon(":/images/images/share_pocket.png"));
  shareGroup_->addAction(pocketShareAct_);

  twitterShareAct_ = new QAction(this);
  twitterShareAct_->setObjectName("twitterShareAct");
  twitterShareAct_->setText("Twitter");
  twitterShareAct_->setIcon(QIcon(":/images/images/share_twitter.png"));
  shareGroup_->addAction(twitterShareAct_);

  vkShareAct_ = new QAction(this);
  vkShareAct_->setObjectName("vkShareAct");
  vkShareAct_->setText("VK");
  vkShareAct_->setIcon(QIcon(":/images/images/share_vk.png"));
  shareGroup_->addAction(vkShareAct_);

  this->addActions(shareGroup_->actions());
  connect(shareGroup_, SIGNAL(triggered(QAction*)),
          this, SLOT(slotShareNews(QAction*)));


  connect(markNewsRead_, SIGNAL(triggered()),
          this, SLOT(markNewsRead()));
  connect(markAllNewsRead_, SIGNAL(triggered()),
          this, SLOT(markAllNewsRead()));
  connect(markStarAct_, SIGNAL(triggered()),
          this, SLOT(markNewsStar()));
  connect(deleteNewsAct_, SIGNAL(triggered()),
          this, SLOT(deleteNews()));
  connect(deleteAllNewsAct_, SIGNAL(triggered()),
          this, SLOT(deleteAllNewsList()));
  connect(restoreNewsAct_, SIGNAL(triggered()),
          this, SLOT(restoreNews()));

  connect(newsKeyUpAct_, SIGNAL(triggered()),
          this, SLOT(slotNewsUpPressed()));
  connect(newsKeyDownAct_, SIGNAL(triggered()),
          this, SLOT(slotNewsDownPressed()));

  connect(openInBrowserAct_, SIGNAL(triggered()),
          this, SLOT(openInBrowserNews()));
  connect(openInExternalBrowserAct_, SIGNAL(triggered()),
          this, SLOT(openInExternalBrowserNews()));
  connect(openNewsNewTabAct_, SIGNAL(triggered()),
          this, SLOT(slotOpenNewsNewTab()));
  connect(openNewsBackgroundTabAct_, SIGNAL(triggered()),
          this, SLOT(slotOpenNewsBackgroundTab()));
}

void RSSListing::createShortcut()
{
  addFeedAct_->setShortcut(QKeySequence(QKeySequence::New));
  listActions_.append(addFeedAct_);
  addFolderAct_->setShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_N));
  listActions_.append(addFolderAct_);
  listActions_.append(deleteFeedAct_);
  exitAct_->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_Q));  // standart on other OS
  listActions_.append(exitAct_);
  updateFeedAct_->setShortcut(QKeySequence(Qt::Key_F5));
  listActions_.append(updateFeedAct_);
  updateAllFeedsAct_->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_F5));
  listActions_.append(updateAllFeedsAct_);
  listActions_.append(openHomeFeedAct_);
  optionsAct_->setShortcut(QKeySequence(Qt::Key_F8));
  listActions_.append(optionsAct_);
  deleteNewsAct_->setShortcut(QKeySequence(Qt::Key_Delete));
  listActions_.append(deleteNewsAct_);
  deleteAllNewsAct_->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_Delete));
  listActions_.append(deleteAllNewsAct_);
  feedProperties_->setShortcut(QKeySequence(Qt::CTRL+Qt::Key_E));
  listActions_.append(feedProperties_);
  feedKeyUpAct_->setShortcut(QKeySequence(Qt::CTRL+Qt::Key_Up));
  listActions_.append(feedKeyUpAct_);
  feedKeyDownAct_->setShortcut(QKeySequence(Qt::CTRL+Qt::Key_Down));
  listActions_.append(feedKeyDownAct_);
  newsKeyUpAct_->setShortcut(QKeySequence(Qt::Key_Left));
  listActions_.append(newsKeyUpAct_);
  newsKeyDownAct_->setShortcut(QKeySequence(Qt::Key_Right));
  listActions_.append(newsKeyDownAct_);

  listActions_.append(nextUnreadNewsAct_);
  listActions_.append(prevUnreadNewsAct_);

  listActions_.append(importFeedsAct_);
  listActions_.append(exportFeedsAct_);
  listActions_.append(autoLoadImagesToggle_);
  listActions_.append(markAllFeedsRead_);
  listActions_.append(markFeedRead_);
  listActions_.append(markNewsRead_);
  listActions_.append(markAllNewsRead_);
  listActions_.append(markStarAct_);
  listActions_.append(collapseAllFoldersAct_);
  listActions_.append(expandAllFoldersAct_);

  listActions_.append(openDescriptionNewsAct_);
  openDescriptionNewsAct_->setShortcut(QKeySequence(Qt::Key_Return));
  listActions_.append(openInBrowserAct_);
  openInBrowserAct_->setShortcut(QKeySequence(Qt::Key_Space));
  listActions_.append(openInExternalBrowserAct_);
  openInExternalBrowserAct_->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_O));
  openNewsNewTabAct_->setShortcut(QKeySequence(Qt::Key_T));
  listActions_.append(openNewsNewTabAct_);
  openNewsBackgroundTabAct_->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_T));
  listActions_.append(openNewsBackgroundTabAct_);

  switchFocusAct_->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_Tab));
  listActions_.append(switchFocusAct_);
  switchFocusPrevAct_->setShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_Tab));
  listActions_.append(switchFocusPrevAct_);

  feedsWidgetVisibleAct_->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_D));
  listActions_.append(feedsWidgetVisibleAct_);

  listActions_.append(placeToTrayAct_);

  zoomInAct_->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_Plus));
  listActions_.append(zoomInAct_);
  zoomOutAct_->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_Minus));
  listActions_.append(zoomOutAct_);
  zoomTo100Act_->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_0));
  listActions_.append(zoomTo100Act_);

  printAct_->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_P));
  listActions_.append(printAct_);
  printPreviewAct_->setShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_P));
  listActions_.append(printPreviewAct_);

  savePageAsAct_->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_S));
  listActions_.append(savePageAsAct_);

  fullScreenAct_->setShortcut(QKeySequence(Qt::Key_F11));
  listActions_.append(fullScreenAct_);

  stayOnTopAct_->setShortcut(QKeySequence(Qt::Key_F10));
  listActions_.append(stayOnTopAct_);

  closeTabAct_->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_W));
  listActions_.append(closeTabAct_);
  listActions_.append(closeAllTabAct_);
  listActions_.append(nextTabAct_);
  listActions_.append(prevTabAct_);

  reduceNewsListAct_->setShortcut(QKeySequence(Qt::ALT+ Qt::Key_Up));
  listActions_.append(reduceNewsListAct_);
  increaseNewsListAct_->setShortcut(QKeySequence(Qt::ALT + Qt::Key_Down));
  listActions_.append(increaseNewsListAct_);

  restoreLastNewsAct_->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_Z));
  listActions_.append(restoreLastNewsAct_);

  findTextAct_->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_F));
  listActions_.append(findTextAct_);

  listActions_.append(copyLinkAct_);

  listActions_.append(shareGroup_->actions());

  //! Действия меток добавлять последними
  listActions_.append(newsLabelGroup_->actions());

  loadActionShortcuts();
}

void RSSListing::loadActionShortcuts()
{
  settings_->beginGroup("/Shortcuts");

  QListIterator<QAction *> iter(listActions_);
  while (iter.hasNext()) {
    QAction *pAction = iter.next();
    if (pAction->objectName().isEmpty())
      continue;

    listDefaultShortcut_.append(pAction->shortcut());

    const QString& sKey = '/' + pAction->objectName();
    const QString& sValue = settings_->value('/' + sKey, pAction->shortcut().toString()).toString();
    pAction->setShortcut(QKeySequence(sValue));
  }

  settings_->endGroup();
}

void RSSListing::saveActionShortcuts()
{
  settings_->beginGroup("/Shortcuts/");

  QListIterator<QAction *> iter(listActions_);
  while (iter.hasNext()) {
    QAction *pAction = iter.next();
    if (pAction->objectName().isEmpty())
      continue;

    const QString& sKey = '/' + pAction->objectName();
    const QString& sValue = QString(pAction->shortcut());
    settings_->setValue(sKey, sValue);
  }

  settings_->endGroup();
}

/*! \brief Создание главного меню *********************************************/
void RSSListing::createMenu()
{
  newMenu_ = new QMenu(this);
  newMenu_->addAction(addFeedAct_);
  newMenu_->addAction(addFolderAct_);
  addAct_->setMenu(newMenu_);

  fileMenu_ = new QMenu(this);
  fileMenu_->addAction(addAct_);
  fileMenu_->addSeparator();
  fileMenu_->addAction(importFeedsAct_);
  fileMenu_->addAction(exportFeedsAct_);
  fileMenu_->addSeparator();
  fileMenu_->addAction(exitAct_);
  menuBar()->addMenu(fileMenu_);

  editMenu_ = new QMenu(this);
  editMenu_->setVisible(false);
//  menuBar()->addMenu(editMenu_);

  toolbarsMenu_ = new QMenu(this);
  toolbarsMenu_->addAction(mainToolbarToggle_);
  toolbarsMenu_->addAction(feedsToolbarToggle_);
  toolbarsMenu_->addAction(newsToolbarToggle_);
  toolbarsMenu_->addAction(browserToolbarToggle_);
  toolbarsMenu_->addAction(categoriesPanelToggle_);

  customizeToolbarGroup_ = new QActionGroup(this);
  customizeToolbarGroup_->addAction(customizeMainToolbarAct_);
  customizeToolbarGroup_->addAction(customizeFeedsToolbarAct_);
  customizeToolbarGroup_->addAction(customizeNewsToolbarAct_);
  connect(customizeToolbarGroup_, SIGNAL(triggered(QAction*)),
          this, SLOT(showCustomizeToolbarDlg(QAction*)));
  customizeToolbarMenu_ = new QMenu(this);
  customizeToolbarMenu_->addActions(customizeToolbarGroup_->actions());

  styleGroup_ = new QActionGroup(this);
  styleGroup_->addAction(systemStyle_);
  styleGroup_->addAction(system2Style_);
  styleGroup_->addAction(greenStyle_);
  styleGroup_->addAction(orangeStyle_);
  styleGroup_->addAction(purpleStyle_);
  styleGroup_->addAction(pinkStyle_);
  styleGroup_->addAction(grayStyle_);
  connect(styleGroup_, SIGNAL(triggered(QAction*)),
          this, SLOT(setStyleApp(QAction*)));

  styleMenu_ = new QMenu(this);
  styleMenu_->addActions(styleGroup_->actions());

  browserPositionGroup_ = new QActionGroup(this);
  browserPositionGroup_->addAction(topBrowserPositionAct_);
  browserPositionGroup_->addAction(bottomBrowserPositionAct_);
  browserPositionGroup_->addAction(rightBrowserPositionAct_);
  browserPositionGroup_->addAction(leftBrowserPositionAct_);
  connect(browserPositionGroup_, SIGNAL(triggered(QAction*)),
          this, SLOT(setBrowserPosition(QAction*)));

  browserPositionMenu_ = new QMenu(this);
  browserPositionMenu_->addActions(browserPositionGroup_->actions());

  viewMenu_  = new QMenu(this);
  viewMenu_->addMenu(toolbarsMenu_);
  viewMenu_->addMenu(customizeToolbarMenu_);
  viewMenu_->addSeparator();
  viewMenu_->addMenu(browserPositionMenu_);
  viewMenu_->addMenu(styleMenu_);
  viewMenu_->addSeparator();
  viewMenu_->addAction(stayOnTopAct_);
  viewMenu_->addAction(fullScreenAct_);
  menuBar()->addMenu(viewMenu_);

  feedMenu_ = new QMenu(this);
  feedMenu_->addAction(updateFeedAct_);
  feedMenu_->addAction(updateAllFeedsAct_);
  feedMenu_->addSeparator();
  feedMenu_->addAction(markFeedRead_);
  feedMenu_->addAction(markAllFeedsRead_);
  feedMenu_->addSeparator();
  menuBar()->addMenu(feedMenu_);

  feedsFilterGroup_ = new QActionGroup(this);
  feedsFilterGroup_->setExclusive(true);
  feedsFilterGroup_->addAction(filterFeedsAll_);
  feedsFilterGroup_->addAction(filterFeedsNew_);
  feedsFilterGroup_->addAction(filterFeedsUnread_);
  feedsFilterGroup_->addAction(filterFeedsStarred_);
  connect(feedsFilterGroup_, SIGNAL(triggered(QAction*)),
          this, SLOT(setFeedsFilter(QAction*)));

  feedsFilterMenu_ = new QMenu(this);
  feedsFilterMenu_->addActions(feedsFilterGroup_->actions());
  feedsFilterMenu_->insertSeparator(filterFeedsNew_);

  feedsFilter_->setMenu(feedsFilterMenu_);
  feedMenu_->addAction(feedsFilter_);
  feedsFilterAction_ = NULL;
  connect(feedsFilter_, SIGNAL(triggered()), this, SLOT(slotFeedsFilter()));

  feedsColumnsGroup_ = new QActionGroup(this);
  feedsColumnsGroup_->setExclusive(false);
  feedsColumnsGroup_->addAction(showUnreadCount_);
  feedsColumnsGroup_->addAction(showUndeleteCount_);
  feedsColumnsGroup_->addAction(showLastUpdated_);
  connect(feedsColumnsGroup_, SIGNAL(triggered(QAction*)),
          this, SLOT(feedsColumnVisible(QAction*)));

  feedsColumnsMenu_ = new QMenu(this);
  feedsColumnsMenu_->addActions(feedsColumnsGroup_->actions());
  feedMenu_->addMenu(feedsColumnsMenu_);

  feedMenu_->addAction(sortedByTitleFeedsTreeAct_);
  feedMenu_->addAction(indentationFeedsTreeAct_);

  feedMenu_->addSeparator();
  feedMenu_->addAction(deleteFeedAct_);
  feedMenu_->addSeparator();
  feedMenu_->addAction(feedProperties_);
  feedMenu_->addSeparator();
//  feedMenu_->addAction(editFeedsTree_);
  connect(feedMenu_, SIGNAL(aboutToShow()), this, SLOT(slotFeedMenuShow()));

  newsMenu_ = new QMenu(this);
  newsMenu_->addAction(markNewsRead_);
  newsMenu_->addAction(markAllNewsRead_);
  newsMenu_->addSeparator();
  newsMenu_->addAction(markStarAct_);
  menuBar()->addMenu(newsMenu_);

  newsLabelMenu_ = new QMenu(this);
  newsLabelMenu_->addActions(newsLabelGroup_->actions());
  newsLabelMenuAction_ = new QAction(this);
  newsLabelMenuAction_->setIcon(QIcon(":/images/label_3"));
  newsLabelAction_->setMenu(newsLabelMenu_);
  newsLabelMenuAction_->setMenu(newsLabelMenu_);
  newsMenu_->addAction(newsLabelMenuAction_);
  connect(newsLabelMenu_, SIGNAL(aboutToShow()),
          this, SLOT(getLabelNews()));

  shareMenu_ = new QMenu(this);
  shareMenu_->addActions(shareGroup_->actions());
  shareMenuAct_ = new QAction(this);
  shareMenuAct_->setObjectName("shareMenuAct");
  shareMenuAct_->setIcon(QIcon(":/images/images/share.png"));
  shareMenuAct_->setMenu(shareMenu_);
  newsMenu_->addAction(shareMenuAct_);
  this->addAction(shareMenuAct_);

  connect(shareMenuAct_, SIGNAL(triggered()),
          this, SLOT(showMenuShareNews()));

  newsMenu_->addSeparator();

  newsFilterGroup_ = new QActionGroup(this);
  newsFilterGroup_->setExclusive(true);
  newsFilterGroup_->addAction(filterNewsAll_);
  newsFilterGroup_->addAction(filterNewsNew_);
  newsFilterGroup_->addAction(filterNewsUnread_);
  newsFilterGroup_->addAction(filterNewsStar_);
  newsFilterGroup_->addAction(filterNewsNotStarred_);
  newsFilterGroup_->addAction(filterNewsUnreadStar_);
  newsFilterGroup_->addAction(filterNewsLastDay_);
  newsFilterGroup_->addAction(filterNewsLastWeek_);
  connect(newsFilterGroup_, SIGNAL(triggered(QAction*)),
          this, SLOT(setNewsFilter(QAction*)));

  newsFilterMenu_ = new QMenu(this);
  newsFilterMenu_->addActions(newsFilterGroup_->actions());
  newsFilterMenu_->insertSeparator(filterNewsNew_);
  newsFilterMenu_->insertSeparator(filterNewsLastDay_);

  newsFilter_->setMenu(newsFilterMenu_);
  newsMenu_->addAction(newsFilter_);
  newsFilterAction_ = NULL;
  connect(newsFilter_, SIGNAL(triggered()), this, SLOT(slotNewsFilter()));

  newsMenu_->addSeparator();
  newsMenu_->addAction(deleteNewsAct_);
  newsMenu_->addAction(deleteAllNewsAct_);

  browserMenu_ = new QMenu(this);
  menuBar()->addMenu(browserMenu_);

  browserZoomGroup_ = new QActionGroup(this);
  browserZoomGroup_->addAction(zoomInAct_);
  browserZoomGroup_->addAction(zoomOutAct_);
  browserZoomGroup_->addAction(zoomTo100Act_);
  connect(browserZoomGroup_, SIGNAL(triggered(QAction*)),
          this, SLOT(browserZoom(QAction*)));

  browserZoomMenu_ = new QMenu(this);
  browserZoomMenu_->setIcon(QIcon(":/images/zoom"));
  browserZoomMenu_->addActions(browserZoomGroup_->actions());
  browserZoomMenu_->insertSeparator(zoomTo100Act_);

  browserMenu_->addAction(autoLoadImagesToggle_);
  browserMenu_->addMenu(browserZoomMenu_);
  browserMenu_->addSeparator();
  browserMenu_->addAction(printAct_);
  browserMenu_->addAction(printPreviewAct_);
  browserMenu_->addSeparator();
  browserMenu_->addAction(savePageAsAct_);

  toolsMenu_ = new QMenu(this);
  toolsMenu_->addAction(setNewsFiltersAct_);
  toolsMenu_->addSeparator();
  toolsMenu_->addAction(optionsAct_);
  menuBar()->addMenu(toolsMenu_);

  helpMenu_ = new QMenu(this);
  helpMenu_->addAction(updateAppAct_);
  helpMenu_->addSeparator();
  helpMenu_->addAction(reportProblemAct_);
  helpMenu_->addAction(aboutAct_);
  menuBar()->addMenu(helpMenu_);
}

/*! \brief Создание ToolBar ***************************************************/
void RSSListing::createToolBar()
{
  mainToolbar_ = new QToolBar(this);
  mainToolbar_->setObjectName("ToolBar_General");
  mainToolbar_->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
  mainToolbar_->setContextMenuPolicy(Qt::CustomContextMenu);
  addToolBar(mainToolbar_);

  connect(mainToolbarToggle_, SIGNAL(toggled(bool)),
          mainToolbar_, SLOT(setVisible(bool)));
  connect(mainToolbar_, SIGNAL(customContextMenuRequested(QPoint)),
          this, SLOT(showContextMenuToolBar(const QPoint &)));

  connect(toolBarLockAct_, SIGNAL(toggled(bool)), this, SLOT(lockMainToolbar(bool)));
  connect(toolBarHideAct_, SIGNAL(triggered()), this, SLOT(hideMainToolbar()));
}

/*! \brief Чтение настроек из ini-файла ***************************************/
void RSSListing::readSettings()
{
  settings_->beginGroup("/Settings");

  showSplashScreen_ = settings_->value("showSplashScreen", true).toBool();
  reopenFeedStartup_ = settings_->value("reopenFeedStartup", true).toBool();

  showTrayIcon_ = settings_->value("showTrayIcon", true).toBool();
  startingTray_ = settings_->value("startingTray", false).toBool();
  minimizingTray_ = settings_->value("minimizingTray", true).toBool();
  closingTray_ = settings_->value("closingTray", false).toBool();
  singleClickTray_ = settings_->value("singleClickTray", false).toBool();
  clearStatusNew_ = settings_->value("clearStatusNew", true).toBool();
  emptyWorking_ = settings_->value("emptyWorking", true).toBool();

  QString strLang;
  QString strLocalLang = QLocale::system().name();
  bool findLang = false;
  QDir langDir = appDataDirPath_ + "/lang";
  foreach (QString file, langDir.entryList(QStringList("*.qm"), QDir::Files)) {
    strLang = file.section('.', 0, 0).section('_', 1);
    if (strLocalLang == strLang) {
      strLang = strLocalLang;
      findLang = true;
      break;
    }
  }
  if (!findLang) {
    strLocalLang = strLocalLang.left(2);
    foreach (QString file, langDir.entryList(QStringList("*.qm"), QDir::Files)) {
      strLang = file.section('.', 0, 0).section('_', 1);
      if (strLocalLang.contains(strLang, Qt::CaseInsensitive)) {
        strLang = strLocalLang;
        findLang = true;
        break;
      }
    }
  }
  if (!findLang) strLang = "en";

  langFileName_ = settings_->value("langFileName", strLang).toString();

  QString fontFamily = settings_->value("/feedsFontFamily", qApp->font().family()).toString();
  int fontSize = settings_->value("/feedsFontSize", 8).toInt();
  feedsTreeView_->setFont(QFont(fontFamily, fontSize));
  feedsTreeModel_->font_ = feedsTreeView_->font();

  newsListFontFamily_ = settings_->value("/newsFontFamily", qApp->font().family()).toString();
  newsListFontSize_ = settings_->value("/newsFontSize", 8).toInt();
  newsTitleFontFamily_ = settings_->value("/newsTitleFontFamily", qApp->font().family()).toString();
  newsTitleFontSize_ = settings_->value("/newsTitleFontSize", 10).toInt();
  newsTextFontFamily_ = settings_->value("/newsTextFontFamily", qApp->font().family()).toString();
  newsTextFontSize_ = settings_->value("/newsTextFontSize", 10).toInt();
  notificationFontFamily_ = settings_->value("/notificationFontFamily", qApp->font().family()).toString();
  notificationFontSize_ = settings_->value("/notificationFontSize", 8).toInt();
  browserMinFontSize_ = settings_->value("/browserMinFontSize", 0).toInt();
  browserMinLogFontSize_ = settings_->value("/browserMinLogFontSize", 0).toInt();

  QWebSettings::globalSettings()->setFontFamily(
        QWebSettings::StandardFont, newsTextFontFamily_);
  QWebSettings::globalSettings()->setFontSize(
        QWebSettings::MinimumFontSize, browserMinFontSize_);
  QWebSettings::globalSettings()->setFontSize(
        QWebSettings::MinimumLogicalFontSize, browserMinLogFontSize_);

  autoUpdatefeedsStartUp_ = settings_->value("autoUpdatefeedsStartUp", false).toBool();
  autoUpdatefeeds_ = settings_->value("autoUpdatefeeds", false).toBool();
  autoUpdatefeedsTime_ = settings_->value("autoUpdatefeedsTime", 10).toInt();
  autoUpdatefeedsInterval_ = settings_->value("autoUpdatefeedsInterval", 0).toInt();

  openingFeedAction_ = settings_->value("openingFeedAction", 0).toInt();
  openNewsWebViewOn_ = settings_->value("openNewsWebViewOn", true).toBool();

  markNewsReadOn_ = settings_->value("markNewsReadOn", true).toBool();
  markCurNewsRead_ = settings_->value("markCurNewsRead", true).toBool();
  markNewsReadTime_ = settings_->value("markNewsReadTime", 0).toInt();
  markPrevNewsRead_= settings_->value("markPrevNewsRead", false).toBool();
  markReadSwitchingFeed_ = settings_->value("markReadSwitchingFeed", false).toBool();
  markReadClosingTab_ = settings_->value("markReadClosingTab", false).toBool();
  markReadMinimize_ = settings_->value("markReadMinimize", false).toBool();

  showDescriptionNews_ = settings_->value("showDescriptionNews", true).toBool();

  formatDate_ = settings_->value("formatData", "dd.MM.yy").toString();
  formatTime_ = settings_->value("formatTime", "hh:mm").toString();
  feedsTreeModel_->formatDate_ = formatDate_;
  feedsTreeModel_->formatTime_ = formatTime_;

  alternatingRowColorsNews_ = settings_->value("alternatingColorsNews", false).toBool();
  changeBehaviorActionNUN_ = settings_->value("changeBehaviorActionNUN", false).toBool();
  simplifiedDateTime_ = settings_->value("simplifiedDateTime", true).toBool();
  notDeleteStarred_ = settings_->value("notDeleteStarred", false).toBool();
  notDeleteLabeled_ = settings_->value("notDeleteLabeled", false).toBool();

  maxDayCleanUp_ = settings_->value("maxDayClearUp", 30).toInt();
  maxNewsCleanUp_ = settings_->value("maxNewsClearUp", 200).toInt();
  dayCleanUpOn_ = settings_->value("dayClearUpOn", true).toBool();
  newsCleanUpOn_ = settings_->value("newsClearUpOn", true).toBool();
  readCleanUp_ = settings_->value("readClearUp", false).toBool();
  neverUnreadCleanUp_ = settings_->value("neverUnreadClearUp", true).toBool();
  neverStarCleanUp_ = settings_->value("neverStarClearUp", true).toBool();
  neverLabelCleanUp_ = settings_->value("neverLabelClearUp", true).toBool();

  externalBrowserOn_ = settings_->value("externalBrowserOn", 0).toInt();
  externalBrowser_ = settings_->value("externalBrowser", "").toString();
  javaScriptEnable_ = settings_->value("javaScriptEnable", true).toBool();
  pluginsEnable_ = settings_->value("pluginsEnable", true).toBool();
  maxPagesInCache_ = settings_->value("maxPagesInCache", 3).toInt();

  QWebSettings::globalSettings()->setAttribute(
        QWebSettings::JavascriptEnabled, javaScriptEnable_);
  QWebSettings::globalSettings()->setAttribute(
        QWebSettings::PluginsEnabled, pluginsEnable_);
  QWebSettings::globalSettings()->setMaximumPagesInCache(maxPagesInCache_);

  soundNewNews_ = settings_->value("soundNewNews", true).toBool();
  QString soundNotifyPathStr = appDataDirPath_ + "/sound/notification.wav";
  soundNotifyPath_ = settings_->value("soundNotifyPath", soundNotifyPathStr).toString();
  showNotifyOn_ = settings_->value("showNotifyOn", true).toBool();
  countShowNewsNotify_ = settings_->value("countShowNewsNotify", 10).toInt();
  widthTitleNewsNotify_ = settings_->value("widthTitleNewsNotify", 300).toInt();
  timeShowNewsNotify_ = settings_->value("timeShowNewsNotify", 10).toInt();
  onlySelectedFeeds_ = settings_->value("onlySelectedFeeds", false).toBool();

  toolBarLockAct_->setChecked(settings_->value("mainToolbarLock", true).toBool());
  lockMainToolbar(toolBarLockAct_->isChecked());

  mainToolbarToggle_->setChecked(settings_->value("mainToolbarShow", true).toBool());
  feedsToolbarToggle_->setChecked(settings_->value("feedsToolbarShow", true).toBool());
  newsToolbarToggle_->setChecked(settings_->value("newsToolbarShow", true).toBool());
  browserToolbarToggle_->setChecked(settings_->value("browserToolbarShow", true).toBool());
  categoriesPanelToggle_->setChecked(settings_->value("categoriesPanelShow", true).toBool());
  categoriesWidget_->setVisible(categoriesPanelToggle_->isChecked());

  if (!mainToolbarToggle_->isChecked())
    mainToolbar_->hide();
  if (!feedsToolbarToggle_->isChecked())
    feedsPanel_->hide();

  QString str = settings_->value("mainToolBar",
                                 "newAct,Separator,updateFeedAct,updateAllFeedsAct,"
                                 "Separator,markFeedRead,Separator,autoLoadImagesToggle").toString();

  foreach (QString actionStr, str.split(",", QString::SkipEmptyParts)) {
    if (actionStr == "Separator") {
      mainToolbar_->addSeparator();
    } else {
      QListIterator<QAction *> iter(actions());
      while (iter.hasNext()) {
        QAction *pAction = iter.next();
        if (!pAction->icon().isNull()) {
          if (pAction->objectName() == actionStr) {
            mainToolbar_->addAction(pAction);
            break;
          }
        }
      }
    }
  }

  str = settings_->value("feedsToolBar", "findFeedAct,feedsFilter").toString();

  foreach (QString actionStr, str.split(",", QString::SkipEmptyParts)) {
    if (actionStr == "Separator") {
      feedsToolBar_->addSeparator();
    } else {
      QListIterator<QAction *> iter(actions());
      while (iter.hasNext()) {
        QAction *pAction = iter.next();
        if (!pAction->icon().isNull()) {
          if (pAction->objectName() == actionStr) {
            feedsToolBar_->addAction(pAction);
            break;
          }
        }
      }
    }
  }

  setToolBarStyle(settings_->value("toolBarStyle", "toolBarStyleTuI_").toString());
  setToolBarIconSize(settings_->value("toolBarIconSize", "toolBarIconNormal_").toString());

  str = settings_->value("styleApplication", "defaultStyle_").toString();
  QList<QAction*> listActions = styleGroup_->actions();
  foreach(QAction *action, listActions) {
    if (action->objectName() == str) {
      action->setChecked(true);
      break;
    }
  }

  showUnreadCount_->setChecked(settings_->value("showUnreadCount", true).toBool());
  showUndeleteCount_->setChecked(settings_->value("showUndeleteCount", false).toBool());
  showLastUpdated_->setChecked(settings_->value("showLastUpdated", false).toBool());
  feedsColumnVisible(showUnreadCount_);
  feedsColumnVisible(showUndeleteCount_);
  feedsColumnVisible(showLastUpdated_);

  indentationFeedsTreeAct_->setChecked(settings_->value("indentationFeedsTree", true).toBool());
  slotIndentationFeedsTree();

  browserPosition_ = settings_->value("browserPosition", BOTTOM_POSITION).toInt();
  switch (browserPosition_) {
  case TOP_POSITION:   topBrowserPositionAct_->setChecked(true); break;
  case RIGHT_POSITION: rightBrowserPositionAct_->setChecked(true); break;
  case LEFT_POSITION:  leftBrowserPositionAct_->setChecked(true); break;
  default: bottomBrowserPositionAct_->setChecked(true);
  }

  openLinkInBackground_ = settings_->value("openLinkInBackground", true).toBool();
  openLinkInBackgroundEmbedded_ = settings_->value("openLinkInBackgroundEmbedded", true).toBool();
  openingLinkTimeout_ = settings_->value("openingLinkTimeout", 1000).toInt();

  stayOnTopAct_->setChecked(settings_->value("stayOnTop", false).toBool());
  if (stayOnTopAct_->isChecked())
    setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
  else
    setWindowFlags(windowFlags() & ~Qt::WindowStaysOnTopHint);

  updateCheckTech_ = settings_->value("updateCheck", true).toBool();
  updateCheckEnabled_ = settings_->value("updateCheckEnabled", true).toBool();

  hideFeedsOpenTab_ = settings_->value("hideFeedsOpenTab", true).toBool();

  defaultIconFeeds_ = settings_->value("defaultIconFeeds", false).toBool();
  feedsTreeModel_->defaultIconFeeds_ = defaultIconFeeds_;
  feedsTreeView_->autocollapseFolder_ =
      settings_->value("autocollapseFolder", false).toBool();

  settings_->endGroup();

  settings_->beginGroup("ClickToFlash");
  c2fWhitelist_ = settings_->value("whitelist", QStringList()).toStringList();
  c2fEnabled_ = settings_->value("enabled", true).toBool();
  settings_->endGroup();

  resize(800, 600);
  restoreGeometry(settings_->value("GeometryState").toByteArray());
  restoreState(settings_->value("ToolBarsState").toByteArray());

  mainSplitter_->restoreState(settings_->value("MainSplitterState").toByteArray());
  feedsWidgetVisibleAct_->setChecked(settings_->value("FeedsWidgetVisible", true).toBool());
  slotVisibledFeedsWidget();

  feedsWidgetSplitterState_ = settings_->value("FeedsWidgetSplitterState").toByteArray();
  bool showCategories = settings_->value("NewsCategoriesTreeVisible", true).toBool();
  newsCategoriesTree_->setVisible(showCategories);
  if (showCategories) {
    showCategoriesButton_->setIcon(QIcon(":/images/images/panel_hide.png"));
    showCategoriesButton_->setToolTip(tr("Hide Categories"));
    feedsSplitter_->restoreState(feedsWidgetSplitterState_);
  } else {
    showCategoriesButton_->setIcon(QIcon(":/images/images/panel_show.png"));
    showCategoriesButton_->setToolTip(tr("Show Categories"));
    QList <int> sizes;
    sizes << QApplication::desktop()->height() << 20;
    feedsSplitter_->setSizes(sizes);
  }

  if (isFullScreen())
    menuBar()->hide();

  networkProxy_.setType(static_cast<QNetworkProxy::ProxyType>(
                          settings_->value("networkProxy/type", QNetworkProxy::DefaultProxy).toInt()));
  networkProxy_.setHostName(settings_->value("networkProxy/hostName", "").toString());
  networkProxy_.setPort(    settings_->value("networkProxy/port",     "").toUInt());
  networkProxy_.setUser(    settings_->value("networkProxy/user",     "").toString());
  networkProxy_.setPassword(settings_->value("networkProxy/password", "").toString());
  setProxy(networkProxy_);
}

/*! \brief Запись настроек в ini-файл *****************************************/
void RSSListing::writeSettings()
{
  settings_->beginGroup("/Settings");

  settings_->setValue("showSplashScreen", showSplashScreen_);
  settings_->setValue("reopenFeedStartup", reopenFeedStartup_);

  settings_->setValue("storeDBMemory", storeDBMemoryT_);

  settings_->setValue("createLastFeed", !lastFeedPath_.isEmpty());

  settings_->setValue("showTrayIcon", showTrayIcon_);
  settings_->setValue("startingTray", startingTray_);
  settings_->setValue("minimizingTray", minimizingTray_);
  settings_->setValue("closingTray", closingTray_);
  settings_->setValue("behaviorIconTray", behaviorIconTray_);
  settings_->setValue("singleClickTray", singleClickTray_);
  settings_->setValue("clearStatusNew", clearStatusNew_);
  settings_->setValue("emptyWorking", emptyWorking_);

  settings_->setValue("langFileName", langFileName_);

  QString fontFamily = feedsTreeView_->font().family();
  settings_->setValue("/feedsFontFamily", fontFamily);
  int fontSize = feedsTreeView_->font().pointSize();
  settings_->setValue("/feedsFontSize", fontSize);

  settings_->setValue("/newsFontFamily", newsListFontFamily_);
  settings_->setValue("/newsFontSize", newsListFontSize_);
  settings_->setValue("/newsTitleFontFamily", newsTitleFontFamily_);
  settings_->setValue("/newsTitleFontSize", newsTitleFontSize_);
  settings_->setValue("/newsTextFontFamily", newsTextFontFamily_);
  settings_->setValue("/newsTextFontSize", newsTextFontSize_);
  settings_->setValue("/notificationFontFamily", notificationFontFamily_);
  settings_->setValue("/notificationFontSize", notificationFontSize_);
  settings_->setValue("/browserMinFontSize", browserMinFontSize_);
  settings_->setValue("/browserMinLogFontSize", browserMinLogFontSize_);

  settings_->setValue("autoUpdatefeedsStartUp", autoUpdatefeedsStartUp_);
  settings_->setValue("autoUpdatefeeds", autoUpdatefeeds_);
  settings_->setValue("autoUpdatefeedsTime", autoUpdatefeedsTime_);
  settings_->setValue("autoUpdatefeedsInterval", autoUpdatefeedsInterval_);

  settings_->setValue("openingFeedAction", openingFeedAction_);
  settings_->setValue("openNewsWebViewOn", openNewsWebViewOn_);

  settings_->setValue("markNewsReadOn", markNewsReadOn_);
  settings_->setValue("markCurNewsRead", markCurNewsRead_);
  settings_->setValue("markNewsReadTime", markNewsReadTime_);
  settings_->setValue("markPrevNewsRead", markPrevNewsRead_);
  settings_->setValue("markReadSwitchingFeed", markReadSwitchingFeed_);
  settings_->setValue("markReadClosingTab", markReadClosingTab_);
  settings_->setValue("markReadMinimize", markReadMinimize_);

  settings_->setValue("showDescriptionNews", showDescriptionNews_);

  settings_->setValue("formatData", formatDate_);
  settings_->setValue("formatTime", formatTime_);

  settings_->setValue("alternatingColorsNews", alternatingRowColorsNews_);
  settings_->setValue("changeBehaviorActionNUN", changeBehaviorActionNUN_);
  settings_->setValue("simplifiedDateTime", simplifiedDateTime_);
  settings_->setValue("notDeleteStarred_", notDeleteStarred_);
  settings_->setValue("notDeleteLabeled_", notDeleteLabeled_);

  settings_->setValue("maxDayClearUp", maxDayCleanUp_);
  settings_->setValue("maxNewsClearUp", maxNewsCleanUp_);
  settings_->setValue("dayClearUpOn", dayCleanUpOn_);
  settings_->setValue("newsClearUpOn", newsCleanUpOn_);
  settings_->setValue("readClearUp", readCleanUp_);
  settings_->setValue("neverUnreadClearUp", neverUnreadCleanUp_);
  settings_->setValue("neverStarClearUp", neverStarCleanUp_);
  settings_->setValue("neverLabelClearUp", neverLabelCleanUp_);

  settings_->setValue("externalBrowserOn", externalBrowserOn_);
  settings_->setValue("externalBrowser", externalBrowser_);
  settings_->setValue("javaScriptEnable", javaScriptEnable_);
  settings_->setValue("pluginsEnable", pluginsEnable_);
  settings_->setValue("maxPagesInCache", maxPagesInCache_);

  settings_->setValue("soundNewNews", soundNewNews_);
  settings_->setValue("soundNotifyPath", soundNotifyPath_);
  settings_->setValue("showNotifyOn", showNotifyOn_);
  settings_->setValue("countShowNewsNotify", countShowNewsNotify_);
  settings_->setValue("widthTitleNewsNotify", widthTitleNewsNotify_);
  settings_->setValue("timeShowNewsNotify", timeShowNewsNotify_);
  settings_->setValue("onlySelectedFeeds", onlySelectedFeeds_);

  settings_->setValue("mainToolbarLock", toolBarLockAct_->isChecked());

  settings_->setValue("mainToolbarShow", mainToolbarToggle_->isChecked());
  settings_->setValue("feedsToolbarShow", feedsToolbarToggle_->isChecked());
  settings_->setValue("newsToolbarShow", newsToolbarToggle_->isChecked());
  settings_->setValue("browserToolbarShow", browserToolbarToggle_->isChecked());
  settings_->setValue("categoriesPanelShow", categoriesPanelToggle_->isChecked());

  settings_->setValue("styleApplication",
                      styleGroup_->checkedAction()->objectName());

  settings_->setValue("showUnreadCount", showUnreadCount_->isChecked());
  settings_->setValue("showUndeleteCount", showUndeleteCount_->isChecked());
  settings_->setValue("showLastUpdated", showLastUpdated_->isChecked());

  settings_->setValue("indentationFeedsTree", indentationFeedsTreeAct_->isChecked());

  settings_->setValue("browserPosition", browserPosition_);

  settings_->setValue("openLinkInBackground", openLinkInBackground_);
  settings_->setValue("openLinkInBackgroundEmbedded", openLinkInBackgroundEmbedded_);
  settings_->setValue("openingLinkTimeout", openingLinkTimeout_);

  settings_->setValue("stayOnTop", stayOnTopAct_->isChecked());

  settings_->setValue("updateCheck", updateCheckTech_);
  settings_->setValue("updateCheckEnabled", updateCheckEnabled_);

  settings_->setValue("hideFeedsOpenTab", hideFeedsOpenTab_);

  settings_->setValue("defaultIconFeeds", defaultIconFeeds_);
  settings_->setValue("autocollapseFolder", feedsTreeView_->autocollapseFolder_);

  settings_->endGroup();

  settings_->beginGroup("ClickToFlash");
  settings_->setValue("whitelist", c2fWhitelist_);
  settings_->setValue("enabled", c2fEnabled_);
  settings_->endGroup();

  settings_->setValue("GeometryState", saveGeometry());
  settings_->setValue("ToolBarsState", saveState());

  settings_->setValue("MainSplitterState", mainSplitter_->saveState());
  settings_->setValue("FeedsWidgetVisible", feedsWidgetVisibleAct_->isChecked());

  bool newsCategoriesTreeVisible = true;
  if (categoriesWidget_->height() <= (categoriesPanel_->height()+2)) {
    newsCategoriesTreeVisible = false;
    settings_->setValue("FeedsWidgetSplitterState", feedsWidgetSplitterState_);
  } else {
    settings_->setValue("FeedsWidgetSplitterState", feedsSplitter_->saveState());
  }
  settings_->setValue("NewsCategoriesTreeVisible", newsCategoriesTreeVisible);

  if (stackedWidget_->count() && (currentNewsTab->type_ == TAB_FEED)) {
    settings_->setValue("NewsHeaderGeometry",
                        currentNewsTab->newsHeader_->saveGeometry());
    settings_->setValue("NewsHeaderState",
                        currentNewsTab->newsHeader_->saveState());

    settings_->setValue("NewsTabSplitterGeometry",
                        currentNewsTab->newsTabWidgetSplitter_->saveGeometry());
    settings_->setValue("NewsTabSplitterState",
                        currentNewsTab->newsTabWidgetSplitter_->saveState());
  }

  settings_->setValue("networkProxy/type",     networkProxy_.type());
  settings_->setValue("networkProxy/hostName", networkProxy_.hostName());
  settings_->setValue("networkProxy/port",     networkProxy_.port());
  settings_->setValue("networkProxy/user",     networkProxy_.user());
  settings_->setValue("networkProxy/password", networkProxy_.password());

  NewsTabWidget* widget = (NewsTabWidget*)stackedWidget_->widget(TAB_WIDGET_PERMANENT);
  settings_->setValue("feedSettings/currentId", widget->feedId_);
  settings_->setValue("feedSettings/currentParId", widget->feedParId_);
  settings_->setValue("feedSettings/filterName",
                      feedsFilterGroup_->checkedAction()->objectName());
  settings_->setValue("newsSettings/filterName",
                      newsFilterGroup_->checkedAction()->objectName());
}

void RSSListing::setProxy(const QNetworkProxy proxy)
{
  networkProxy_ = proxy;
  if (QNetworkProxy::DefaultProxy == networkProxy_.type())
    QNetworkProxyFactory::setUseSystemConfiguration(true);
  else
    QNetworkProxy::setApplicationProxy(networkProxy_);
}

/*! \brief Добавление ленты в список лент *************************************/
void RSSListing::addFeed()
{
  int curFolderId = 0;
  QPersistentModelIndex curIndex = feedsTreeView_->selectIndex();
  if (feedsTreeModel_->isFolder(curIndex)) {
    curFolderId = feedsTreeModel_->getIdByIndex(curIndex);
  } else {
    curFolderId = feedsTreeModel_->getParidByIndex(curIndex);
  }

  AddFeedWizard *addFeedWizard = new AddFeedWizard(this, lastFeedPath_, curFolderId);
  addFeedWizard->restoreGeometry(settings_->value("addFeedWizard/geometry").toByteArray());

  int result = addFeedWizard->exec();
  settings_->setValue("addFeedWizard/geometry", addFeedWizard->saveGeometry());
  if (result == QDialog::Rejected) {
    delete addFeedWizard;
    return;
  }

  emit faviconRequestUrl(addFeedWizard->htmlUrlString_, addFeedWizard->feedUrlString_);

  QList<int> categoriesList;
  categoriesList << addFeedWizard->feedParentId_;
  recountFeedCategories(categoriesList);

  QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
  feedsTreeView_->setCurrentIndex(QModelIndex());
  feedsModelReload();
  QModelIndex index = feedsTreeModel_->getIndexById(addFeedWizard->feedId_,
                                                    addFeedWizard->feedParentId_);
  feedsTreeView_->selectIdEn_ = true;
  feedsTreeView_->setCurrentIndex(index);
  slotFeedClicked(feedsTreeView_->currentIndex());
  QApplication::restoreOverrideCursor();

  slotUpdateFeedDelayed(addFeedWizard->feedId_, true, addFeedWizard->newCount_);

  delete addFeedWizard;
}

void RSSListing::addFolder()
{
  int curFolderId = 0;
  QPersistentModelIndex curIndex = feedsTreeView_->selectIndex();
  if (feedsTreeModel_->isFolder(curIndex)) {
    curFolderId = feedsTreeModel_->getIdByIndex(curIndex);
  } else {
    curFolderId = feedsTreeModel_->getParidByIndex(curIndex);
  }

  AddFolderDialog *addFolderDialog = new AddFolderDialog(this, curFolderId);

  if (addFolderDialog->exec() == QDialog::Rejected) {
    delete addFolderDialog;
    return;
  }

  QString folderText = addFolderDialog->nameFeedEdit_->text();
  int parentId = addFolderDialog->foldersTree_->currentItem()->text(1).toInt();

  QSqlQuery q;

  // Вычисляем номер ряда для папки
  int rowToParent = 0;
  q.exec(QString("SELECT count(id) FROM feeds WHERE parentId='%1'").
         arg(parentId));
  if (q.next()) rowToParent = q.value(0).toInt();

  // Добавляем папку
  q.prepare("INSERT INTO feeds(text, created, parentId, rowToParent) "
            "VALUES (:text, :feedCreateTime, :parentId, :rowToParent)");
  q.bindValue(":text", folderText);
  q.bindValue(":feedCreateTime",
              QLocale::c().toString(QDateTime::currentDateTimeUtc(), "yyyy-MM-ddTHH:mm:ss"));
  q.bindValue(":parentId", parentId);
  q.bindValue(":rowToParent", rowToParent);
  q.exec();

  delete addFolderDialog;

  QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
  feedsModelReload();
  QApplication::restoreOverrideCursor();
}

/** @brief Удаление элемента из дерева лент с подтверждением
 *----------------------------------------------------------------------------*/
void RSSListing::deleteItemFeedsTree()
{
  if (!feedsTreeView_->selectIndex().isValid()) return;

  QPersistentModelIndex index = feedsTreeView_->selectIndex();
  int feedDeleteId = feedsTreeModel_->getIdByIndex(index);
  int feedParentId = feedsTreeModel_->getParidByIndex(index);

  QPersistentModelIndex currentIndex = feedsTreeView_->currentIndex();
  int feedCurrentId = feedsTreeModel_->getIdByIndex(currentIndex);

  QMessageBox msgBox;
  msgBox.setIcon(QMessageBox::Question);
  if (feedsTreeModel_->isFolder(index)) {
    msgBox.setWindowTitle(tr("Delete Folder"));
    msgBox.setText(QString(tr("Are you sure to delete the folder '%1'?")).
                   arg(feedsTreeModel_->dataField(index, "text").toString()));
  } else {
    msgBox.setWindowTitle(tr("Delete Feed"));
    msgBox.setText(QString(tr("Are you sure to delete the feed '%1'?")).
                   arg(feedsTreeModel_->dataField(index, "text").toString()));
  }
  msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
  msgBox.setDefaultButton(QMessageBox::No);

  if (msgBox.exec() == QMessageBox::No) return;

  QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

  db_.transaction();
  QSqlQuery q;
  QString idStr(QString("id='%1'").arg(feedDeleteId));
  QString feedIdStr(QString("feedId='%1'").arg(feedDeleteId));
  QQueue<int> parentIds;
  parentIds.enqueue(feedDeleteId);
  while (!parentIds.empty()) {
    int parentId = parentIds.dequeue();
    q.exec(QString("SELECT id FROM feeds WHERE parentId='%1'").arg(parentId));
    while (q.next()) {
      int feedId = q.value(0).toInt();

      idStr.append(QString(" OR id='%1'").arg(feedId));
      feedIdStr.append(QString(" OR feedId='%1'").arg(feedId));

      parentIds.enqueue(feedId);
    }
  }

  q.exec(QString("DELETE FROM feeds WHERE %1").arg(idStr));
  q.exec(QString("DELETE FROM news WHERE %1").arg(feedIdStr));

  // Correction row
  QList<int> idList;
  q.exec(QString("SELECT id FROM feeds WHERE parentId='%1' ORDER BY rowToParent").
         arg(feedParentId));
  while (q.next()) {
    idList << q.value(0).toInt();
  }
  for (int i = 0; i < idList.count(); i++) {
    q.exec(QString("UPDATE feeds SET rowToParent='%1' WHERE id=='%2'").
           arg(i).arg(idList.at(i)));
  }
  db_.commit();

  QList<int> categoriesList;
  categoriesList << feedParentId;
  recountFeedCategories(categoriesList);

  // Если удаляется лента на которой стоит фокус и эта лента последняя,
  // то курсор нужно ставить на предыдущую ленту, чтобы не курсор пропадал.
  // Иначе курсор ставим на ранее сфокусированную ленту.
  // Сравниваем идентификаторы, т.к. сам selectedIndex после скрытия
  // всплывающего меню устанавливается на currentIndex()
  if (feedCurrentId == feedDeleteId) {
    QModelIndex index = feedsTreeView_->indexBelow(currentIndex);
    if (!index.isValid())
      index = feedsTreeView_->indexAbove(currentIndex);
    currentIndex = index;
  }
  feedsTreeView_->setCurrentIndex(currentIndex);
  feedsModelReload();
  slotFeedClicked(feedsTreeView_->currentIndex());

  QApplication::restoreOverrideCursor();
}

/**
 * @brief Импорт лент из OPML-файла
 *
 *  ВЫзывает диалог выбора файла *.opml. Добавляет все ленты из файла в базу,
 *  сохраняя дерево папок. Если URL ленты уже есть в базе, лента не добавляется
 *----------------------------------------------------------------------------*/
void RSSListing::slotImportFeeds()
{
  QString fileName = QFileDialog::getOpenFileName(this, tr("Select OPML-File"),
                                                  QDir::homePath(),
                                                  tr("OPML-Files (*.opml *.xml)"));

  if (fileName.isNull()) {
    statusBar()->showMessage(tr("Import canceled"), 3000);
    return;
  }

  qDebug() << "import file:" << fileName;

  QFile file(fileName);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    statusBar()->showMessage(tr("Import: can't open a file"), 3000);
    return;
  }

  updateFeedsStart_ = true;
  importFeedStart_ = true;

  timer_.start();
//  qCritical() << "Start update";
//  qCritical() << "------------------------------------------------------------";

  db_.transaction();

  QXmlStreamReader xml(&file);

  int elementCount = 0;
  int outlineCount = 0;
  QSqlQuery q;

  //* Хранит иерархию outline'ов. Каждый следующий outline может быть вложенным.
  //* Поэтому при нахождении очередного outline'а мы складываем его в стек. А
  //* когда этот outline заканчивается, то достаем его из стека. Верхний элемент
  //* стека всегда является родителем текущего outline'а
  QStack<int> parentIdsStack;
  parentIdsStack.push(0);
  while (!xml.atEnd()) {
    xml.readNext();
    if (xml.isStartElement()) {
      statusBar()->showMessage(QVariant(elementCount).toString(), 3000);
      // Выбираем одни outline'ы
      if (xml.name() == "outline") {
        qDebug() << outlineCount << "+:" << xml.prefix().toString()
                 << ":" << xml.name().toString();

        QString textString(xml.attributes().value("text").toString());
        QString titleString(xml.attributes().value("title").toString());
        QString xmlUrlString(xml.attributes().value("xmlUrl").toString());
        if (textString.isEmpty()) textString = titleString;

        // Найдена папка
        if (xmlUrlString.isEmpty()) {

          // Если такая папка уже есть в базе, то ленты добавляем в нее
          bool isFolderDuplicated = false;
          q.prepare("SELECT id FROM feeds WHERE text=:text AND (xmlUrl='' OR xmlUrl IS NULL)");
          q.bindValue(":text", textString);
          q.exec();
          if (q.next()) {
            isFolderDuplicated = true;
            parentIdsStack.push(q.value(0).toInt());
          }

          // Если такой папки еще нет, создаем ее
          if (!isFolderDuplicated) {
            int rowToParent = 0;
            q.exec(QString("SELECT count(id) FROM feeds WHERE parentId='%1'").
                   arg(parentIdsStack.top()));
            if (q.next()) rowToParent = q.value(0).toInt();

            q.prepare("INSERT INTO feeds(text, title, xmlUrl, created, f_Expanded, parentId, rowToParent) "
                      "VALUES (:text, :title, :xmlUrl, :feedCreateTime, 0, :parentId, :rowToParent)");
            q.bindValue(":text", textString);
            q.bindValue(":title", textString);
            q.bindValue(":xmlUrl", "");
            q.bindValue(":feedCreateTime",
                        QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
            q.bindValue(":parentId", parentIdsStack.top());
            q.bindValue(":rowToParent", rowToParent);
            q.exec();
            parentIdsStack.push(q.lastInsertId().toInt());
//            qDebug() << q.lastQuery() << q.boundValues() << q.lastInsertId();
//            qDebug() << q.lastError().number() << ": " << q.lastError().text();
          }
        }
        // Найдена лента
        else {

          bool isFeedDuplicated = false;
          q.prepare("SELECT id FROM feeds WHERE xmlUrl LIKE :xmlUrl");
          q.bindValue(":xmlUrl", xmlUrlString);
          q.exec();
          if (q.next())
            isFeedDuplicated = true;

          if (isFeedDuplicated) {
            qDebug() << "duplicate feed:" << xmlUrlString << textString;
          } else {
            int rowToParent = 0;
            q.exec(QString("SELECT count(id) FROM feeds WHERE parentId='%1'").
                   arg(parentIdsStack.top()));
            if (q.next()) rowToParent = q.value(0).toInt();

            q.prepare("INSERT INTO feeds(text, title, description, xmlUrl, htmlUrl, created, parentId, rowToParent) "
                      "VALUES(?, ?, ?, ?, ?, ?, ?, ?)");
            q.addBindValue(textString);
            q.addBindValue(xml.attributes().value("title").toString());
            q.addBindValue(xml.attributes().value("description").toString());
            q.addBindValue(xmlUrlString);
            q.addBindValue(xml.attributes().value("htmlUrl").toString());
            q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
            q.addBindValue(parentIdsStack.top());
            q.addBindValue(rowToParent);
            q.exec();
//            qDebug() << q.lastQuery() << q.boundValues();
//            qDebug() << q.lastError().number() << ": " << q.lastError().text();

            updateFeedsCount_ = updateFeedsCount_ + 2;
            emit signalRequestUrl(xmlUrlString, QDateTime(), "");
          }
          parentIdsStack.push(q.lastInsertId().toInt());
        }
      }
    } else if (xml.isEndElement()) {
      if (xml.name() == "outline") {
        parentIdsStack.pop();
        ++outlineCount;
      }
      ++elementCount;
    }
    qDebug() << parentIdsStack;
  }
  if (xml.error()) {
    statusBar()->showMessage(QString("Import error: Line=%1, ErrorString=%2").
                             arg(xml.lineNumber()).arg(xml.errorString()), 3000);
  } else {
    statusBar()->showMessage(QString("Import: file read done"), 3000);
  }
  db_.commit();

  file.close();

  showProgressBar(updateFeedsCount_);

//  qCritical() << "Start update: " << timer_.elapsed();
  feedsModelReload();
//  qCritical() << "Start update: " << timer_.elapsed() << updateFeedsCount_;
//  qCritical() << "------------------------------------------------------------";
}
/*! Экспорт ленты в OPML-файл *************************************************/
void RSSListing::slotExportFeeds()
{
  QString fileName = QFileDialog::getSaveFileName(this, tr("Select OPML-File"),
                                                  QDir::homePath(),
                                                  tr("OPML-Files (*.opml)"));

  if (fileName.isNull()) {
    statusBar()->showMessage(tr("Export canceled"), 3000);
    return;
  }

  QFile file(fileName);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    statusBar()->showMessage(tr("Export: can't open a file"), 3000);
    return;
  }

  QXmlStreamWriter xml(&file);
  xml.setAutoFormatting(true);
  xml.writeStartDocument();
  xml.writeStartElement("opml");
  xml.writeAttribute("version", "2.0");
  xml.writeStartElement("head");
  xml.writeTextElement("title", "QuiteRSS");
  xml.writeTextElement("dateModified", QDateTime::currentDateTime().toString());
  xml.writeEndElement(); // </head>

  xml.writeStartElement("body");
  // Создаем модель и представление для экспорта.
  // Раскрываем представление, чтобы пройтись по всем веткам
  FeedsTreeModel exportTreeModel("feeds",
      QStringList() << "" << "" << "",
      QStringList() << "id" << "text" << "parentId",
      0,
      "text");
  FeedsTreeView exportTreeView(this);
  exportTreeView.setModel(&exportTreeModel);
  exportTreeView.sortByColumn(exportTreeView.columnIndex("rowToParent"), Qt::AscendingOrder);
  exportTreeView.expandAll();

  QModelIndex index = exportTreeModel.index(0, 0);
  QStack<int> parentIdsStack;
  parentIdsStack.push(0);
  while (index.isValid()) {
    int feedId = exportTreeModel.getIdByIndex(index);
    int feedParId = exportTreeModel.getParidByIndex(index);

    // Родитель отличается от предыдущего, закрываем категорию
    if (feedParId != parentIdsStack.top()) {
      xml.writeEndElement();  // "outline"
      parentIdsStack.pop();
    }

    // Нашли категорию. Открываем ее
    if (exportTreeModel.isFolder(index)) {
      parentIdsStack.push(feedId);
      xml.writeStartElement("outline");  // Начало категории
      xml.writeAttribute("text", exportTreeModel.dataField(index, "text").toString());
    }
    // Нашли ленту. Сохраняем
    else {
      xml.writeEmptyElement("outline");
      xml.writeAttribute("text",    exportTreeModel.dataField(index, "text").toString());
      xml.writeAttribute("htmlUrl", exportTreeModel.dataField(index, "htmlUrl").toString());
      xml.writeAttribute("xmlUrl",  exportTreeModel.dataField(index, "xmlUrl").toString());
    }

    index = exportTreeView.indexBelow(index);
  }

  xml.writeEndElement(); // </body>

  xml.writeEndElement(); // </opml>
  xml.writeEndDocument();

  file.close();
}

/*! \brief Обработка окончания запроса ****************************************/
void RSSListing::getUrlDone(const int &result, const QString &feedUrlStr,
                            const QByteArray &data, const QDateTime &dtReply)
{
  qDebug() << "getUrl result = " << result << "url: " << feedUrlStr;

  if (updateFeedsCount_ > 0) {
    updateFeedsCount_--;
    progressBar_->setValue(progressBar_->maximum() - updateFeedsCount_);
  }

  if (!data.isEmpty()) {
    emit xmlReadyParse(data, feedUrlStr, dtReply);
  } else {
    slotUpdateFeed(0, false, 0);
  }
}

/** @brief Обновление счётчиков ленты и всех родительский категорий
 *
 *  Обновляются поля: количество непрочитанных новостей,
 *  количество новых новостей, дата последнего обновления у категорий
 *  Обновление производится только для лент, категории исключаются
 *  Обновление производится напрямую в базу, но если лента отображается в
 *  дереве, то производится обновление отображения
 * @param feedId идентификатор ленты
 *----------------------------------------------------------------------------*/
void RSSListing::recountFeedCounts(int feedId, bool update)
{
  QSqlQuery q;
  QString qStr;

  db_.transaction();

  int feedParId = 0;
  bool isFolder = false;
  qStr = QString("SELECT parentId, xmlUrl FROM feeds WHERE id=='%1'").
      arg(feedId);
  q.exec(qStr);
  if (q.next()) {
    feedParId = q.value(0).toInt();
    if (q.value(1).toString().isEmpty())
      isFolder = true;
  }

  QModelIndex index = feedsTreeModel_->getIndexById(feedId, feedParId);
  QModelIndex indexParent = QModelIndex();
  QModelIndex indexUnread;
  QModelIndex indexNew;
  QModelIndex indexUndelete;
  QModelIndex indexUpdated;
  int undeleteCount = 0;
  int unreadCount = 0;
  int newCount = 0;

  if (!isFolder) {
    // Подсчет всех новостей (не помеченных удаленными)
    qStr = QString("SELECT count(id) FROM news WHERE feedId=='%1' AND deleted==0").
        arg(feedId);
    q.exec(qStr);
    if (q.next()) undeleteCount = q.value(0).toInt();

    // Подсчет непрочитанных новостей
    qStr = QString("SELECT count(read) FROM news WHERE feedId=='%1' AND read==0 AND deleted==0").
        arg(feedId);
    q.exec(qStr);
    if (q.next()) unreadCount = q.value(0).toInt();

    // Подсчет новых новостей
    qStr = QString("SELECT count(new) FROM news WHERE feedId=='%1' AND new==1 AND deleted==0").
        arg(feedId);
    q.exec(qStr);
    if (q.next()) newCount = q.value(0).toInt();

    int unreadCountOld = 0;
    int newCountOld = 0;
    int undeleteCountOld = 0;
    qStr = QString("SELECT unread, newCount, undeleteCount FROM feeds WHERE id=='%1'").
        arg(feedId);
    q.exec(qStr);
    if (q.next()) {
      unreadCountOld = q.value(0).toInt();
      newCountOld = q.value(1).toInt();
      undeleteCountOld = q.value(2).toInt();
    }

    if ((unreadCount == unreadCountOld) && (newCount == newCountOld) &&
        (undeleteCount == undeleteCountOld)) {
      db_.commit();
      return;
    }

    // Установка количества непрочитанных новостей в ленту
    // Установка количества новых новостей в ленту
    qStr = QString("UPDATE feeds SET unread='%1', newCount='%2', undeleteCount='%3' "
                   "WHERE id=='%4'").
        arg(unreadCount).arg(newCount).arg(undeleteCount).arg(feedId);
    q.exec(qStr);

    // Обновление отображения ленты, если оно существует
    if (index.isValid()) {
      indexUnread   = index.sibling(index.row(), feedsTreeModel_->proxyColumnByOriginal("unread"));
      indexNew      = index.sibling(index.row(), feedsTreeModel_->proxyColumnByOriginal("newCount"));
      indexUndelete = index.sibling(index.row(), feedsTreeModel_->proxyColumnByOriginal("undeleteCount"));
      feedsTreeModel_->setData(indexUnread, unreadCount);
      feedsTreeModel_->setData(indexNew, newCount);
      feedsTreeModel_->setData(indexUndelete, undeleteCount);
    }
  } else {
    bool changed = false;
    QList<int> idParList;
    QList<int> idList = getIdFeedsInList(feedId);
    if (idList.count()) {
      foreach (int id, idList) {
        int parId = 0;
        q.exec(QString("SELECT parentId FROM feeds WHERE id=='%1'").arg(id));
        if (q.next())
          parId = q.value(0).toInt();

        if (parId) {
          if (idParList.indexOf(parId) == -1) {
            idParList.append(parId);
          }
        }

        // Подсчет всех новостей (не помеченных удаленными)
        qStr = QString("SELECT count(id) FROM news WHERE feedId=='%1' AND deleted==0").
            arg(id);
        q.exec(qStr);
        if (q.next()) undeleteCount = q.value(0).toInt();

        // Подсчет непрочитанных новостей
        qStr = QString("SELECT count(read) FROM news WHERE feedId=='%1' AND read==0 AND deleted==0").
            arg(id);
        q.exec(qStr);
        if (q.next()) unreadCount = q.value(0).toInt();

        // Подсчет новых новостей
        qStr = QString("SELECT count(new) FROM news WHERE feedId=='%1' AND new==1 AND deleted==0").
            arg(id);
        q.exec(qStr);
        if (q.next()) newCount = q.value(0).toInt();

        int unreadCountOld = 0;
        int newCountOld = 0;
        int undeleteCountOld = 0;
        qStr = QString("SELECT unread, newCount, undeleteCount FROM feeds WHERE id=='%1'").
            arg(id);
        q.exec(qStr);
        if (q.next()) {
          unreadCountOld = q.value(0).toInt();
          newCountOld = q.value(1).toInt();
          undeleteCountOld = q.value(2).toInt();
        }

        if ((unreadCount == unreadCountOld) && (newCount == newCountOld) &&
            (undeleteCount == undeleteCountOld)) {
          continue;
        }
        changed = true;

        // Установка количества непрочитанных новостей в ленту
        // Установка количества новых новостей в ленту
        qStr = QString("UPDATE feeds SET unread='%1', newCount='%2', undeleteCount='%3' "
                       "WHERE id=='%4'").
            arg(unreadCount).arg(newCount).arg(undeleteCount).arg(id);
        q.exec(qStr);

        // Обновление отображения ленты, если оно существует
        QModelIndex index1 = feedsTreeModel_->getIndexById(id, parId);
        if (index1.isValid()) {
          indexUnread   = index1.sibling(index1.row(), feedsTreeModel_->proxyColumnByOriginal("unread"));
          indexNew      = index1.sibling(index1.row(), feedsTreeModel_->proxyColumnByOriginal("newCount"));
          indexUndelete = index1.sibling(index1.row(), feedsTreeModel_->proxyColumnByOriginal("undeleteCount"));
          feedsTreeModel_->setData(indexUnread, unreadCount);
          feedsTreeModel_->setData(indexNew, newCount);
          feedsTreeModel_->setData(indexUndelete, undeleteCount);
        }
      }

      if (!changed) {
        db_.commit();
        return;
      }

      foreach (int l_feedParId, idParList) {
        while (l_feedParId) {
          QString updated;

          qStr = QString("SELECT sum(unread), sum(newCount), sum(undeleteCount), "
                         "max(updated) FROM feeds WHERE parentId=='%1'").
              arg(l_feedParId);
          q.exec(qStr);
          if (q.next()) {
            unreadCount   = q.value(0).toInt();
            newCount      = q.value(1).toInt();
            undeleteCount = q.value(2).toInt();
            updated       = q.value(3).toString();
          }
          qStr = QString("UPDATE feeds SET unread='%1', newCount='%2', undeleteCount='%3', "
                         "updated='%4' WHERE id=='%5'").
              arg(unreadCount).arg(newCount).arg(undeleteCount).arg(updated).
              arg(l_feedParId);
          q.exec(qStr);

          int parId = 0;
          q.exec(QString("SELECT parentId FROM feeds WHERE id=='%1'").arg(l_feedParId));
          if (q.next())
            parId = q.value(0).toInt();
          QModelIndex index1 = feedsTreeModel_->getIndexById(l_feedParId, parId);

          // Обновление отображения ленты, если оно существует
          if (index1.isValid()) {
            indexUnread   = index1.sibling(index1.row(), feedsTreeModel_->proxyColumnByOriginal("unread"));
            indexNew      = index1.sibling(index1.row(), feedsTreeModel_->proxyColumnByOriginal("newCount"));
            indexUndelete = index1.sibling(index1.row(), feedsTreeModel_->proxyColumnByOriginal("undeleteCount"));
            indexUpdated  = index1.sibling(index1.row(), feedsTreeModel_->proxyColumnByOriginal("updated"));
            feedsTreeModel_->setData(indexUnread, unreadCount);
            feedsTreeModel_->setData(indexNew, newCount);
            feedsTreeModel_->setData(indexUndelete, undeleteCount);
            feedsTreeModel_->setData(indexUpdated, updated);
          }

          if (feedId == l_feedParId) break;
          q.exec(QString("SELECT parentId FROM feeds WHERE id==%1").arg(l_feedParId));
          if (q.next()) l_feedParId = q.value(0).toInt();
        }
      }
    }
  }

  // Пересчитываем счетчики для всех родителей
  indexParent = index.parent();
  int l_feedParId = feedParId;
  while (l_feedParId) {
    QString updated;

    qStr = QString("SELECT sum(unread), sum(newCount), sum(undeleteCount), "
                   "max(updated) FROM feeds WHERE parentId=='%1'").
        arg(l_feedParId);
    q.exec(qStr);
    if (q.next()) {
      unreadCount   = q.value(0).toInt();
      newCount      = q.value(1).toInt();
      undeleteCount = q.value(2).toInt();
      updated       = q.value(3).toString();
    }
    qStr = QString("UPDATE feeds SET unread='%1', newCount='%2', undeleteCount='%3', "
                   "updated='%4' WHERE id=='%5'").
        arg(unreadCount).arg(newCount).arg(undeleteCount).arg(updated).
        arg(l_feedParId);
    q.exec(qStr);

    // Обновление отображения ленты, если оно существует
    if (indexParent.isValid()) {
      indexUnread   = indexParent.sibling(indexParent.row(), feedsTreeModel_->proxyColumnByOriginal("unread"));
      indexNew      = indexParent.sibling(indexParent.row(), feedsTreeModel_->proxyColumnByOriginal("newCount"));
      indexUndelete = indexParent.sibling(indexParent.row(), feedsTreeModel_->proxyColumnByOriginal("undeleteCount"));
      indexUpdated  = indexParent.sibling(indexParent.row(), feedsTreeModel_->proxyColumnByOriginal("updated"));
      feedsTreeModel_->setData(indexUnread, unreadCount);
      feedsTreeModel_->setData(indexNew, newCount);
      feedsTreeModel_->setData(indexUndelete, undeleteCount);
      feedsTreeModel_->setData(indexUpdated, updated);
      indexParent = indexParent.parent();
    }

    q.exec(QString("SELECT parentId FROM feeds WHERE id==%1").arg(l_feedParId));
    if (q.next()) l_feedParId = q.value(0).toInt();
  }
  db_.commit();

  if (update) {
    feedsTreeView_->viewport()->update();
    feedsTreeView_->header()->setResizeMode(feedsTreeModel_->proxyColumnByOriginal("unread"), QHeaderView::ResizeToContents);
    feedsTreeView_->header()->setResizeMode(feedsTreeModel_->proxyColumnByOriginal("undeleteCount"), QHeaderView::ResizeToContents);
    feedsTreeView_->header()->setResizeMode(feedsTreeModel_->proxyColumnByOriginal("updated"), QHeaderView::ResizeToContents);
  }
}

void RSSListing::slotFeedCountsUpdate(FeedCountStruct counts)
{
  QModelIndex index = feedsTreeModel_->getIndexById(counts.feedId, counts.parentId);
  if (index.isValid()) {
    QModelIndex indexUnread   = index.sibling(index.row(), feedsTreeModel_->proxyColumnByOriginal("unread"));
    QModelIndex indexNew      = index.sibling(index.row(), feedsTreeModel_->proxyColumnByOriginal("newCount"));
    QModelIndex indexUndelete = index.sibling(index.row(), feedsTreeModel_->proxyColumnByOriginal("undeleteCount"));
    feedsTreeModel_->setData(indexUnread, counts.unreadCount);
    feedsTreeModel_->setData(indexNew, counts.newCount);
    feedsTreeModel_->setData(indexUndelete, counts.undeleteCount);

    if (!counts.updated.isEmpty()) {
      QModelIndex indexUpdated  = index.sibling(index.row(), feedsTreeModel_->proxyColumnByOriginal("updated"));
      feedsTreeModel_->setData(indexUpdated, counts.updated);
    }

    if (!counts.htmlUrl.isEmpty()) {
      QModelIndex indexHtmlUrl  = index.sibling(index.row(), feedsTreeModel_->proxyColumnByOriginal("htmlUrl"));
      feedsTreeModel_->setData(indexHtmlUrl, counts.htmlUrl);
    }

    if (!counts.title.isEmpty()) {
      QModelIndex indexTitle  = index.sibling(index.row(), feedsTreeModel_->proxyColumnByOriginal("title"));
      feedsTreeModel_->setData(indexTitle, counts.title);
    }
  }

  if (importFeedStart_ && !counts.xmlUrl.isEmpty()) {
    emit faviconRequestUrl(counts.htmlUrl, counts.xmlUrl);
  }
}

/**
 * @brief Пересчёт счетчиков для указанных категорий
 * @details Пересчет производится прямо в базе. Необходим "reselect" модели
 * @param categoriesList - список идентификаторов категорий для обработки
 ******************************************************************************/
void RSSListing::recountFeedCategories(const QList<int> &categoriesList)
{
  QSqlQuery q;
  QString qStr;

  foreach (int categoryIdStart, categoriesList) {
    if (categoryIdStart < 1) continue;

    int categoryId = categoryIdStart;
    // Пересчет всех родителей
    while (0 < categoryId) {
      int unreadCount = -1;
      int undeleteCount = -1;
      int newCount = -1;

      // Подсчет суммы для всех лент c одним родителем
      qStr = QString("SELECT sum(unread), sum(undeleteCount), sum(newCount) "
                     "FROM feeds WHERE parentId=='%1'").arg(categoryId);
      q.exec(qStr);
      if (q.next()) {
        unreadCount   = q.value(0).toInt();
        undeleteCount = q.value(1).toInt();
        newCount = q.value(2).toInt();
      }

      if (unreadCount != -1) {
        qStr = QString("UPDATE feeds SET unread='%1', undeleteCount='%2', newCount='%3' WHERE id=='%4'").
            arg(unreadCount).arg(undeleteCount).arg(newCount).arg(categoryId);
        q.exec(qStr);
      }

      // Переходим к предыдущему родителю
      qStr = QString("SELECT parentId FROM feeds WHERE id=='%1'").
          arg(categoryId);
      categoryId = 0;
      q.exec(qStr);
      if (q.next()) categoryId = q.value(0).toInt();
    }
  }
}

/** @brief Обработка сигнала на обновление отображения ленты
 *
 *  Производится после обновления ленты и после добавления ленты
 *  В действительности производится задержка обновления
 * @param url URL-адрес обновляемой ленты
 * @param changed Признак того, что лента действительно была обновлена
 *---------------------------------------------------------------------------*/
void RSSListing::slotUpdateFeed(int feedId, const bool &changed, int newCount)
{
  updateDelayer_->delayUpdate(feedId, changed, newCount);
}

/** @brief Обновление отображения ленты
 *
 *  Слот вызывается по сигналу от UpdateDelayer'а после некоторой задержки
 * @param feedId Id обновляемой ленты
 * @param changed Признак того, что лента действительно была обновлена
 *---------------------------------------------------------------------------*/
void RSSListing::slotUpdateFeedDelayed(int feedId, const bool &changed, int newCount)
{
  if (updateFeedsCount_ > 0) {
    updateFeedsCount_--;
    progressBar_->setValue(progressBar_->maximum() - updateFeedsCount_);
  }
  if (updateFeedsCount_ <= 0) {
    emit signalShowNotification();
    progressBar_->hide();
    progressBar_->setValue(0);
    progressBar_->setMaximum(0);
    updateFeedsCount_ = 0;
    updateFeedsStart_ = false;
    importFeedStart_ = false;
//    qCritical() << "Stop update: " << timer_.elapsed();
//    qCritical() << "------------------------------------------------------------";
  }

  if (!changed) {
    emit signalNextUpdate();
    return;
  }

  // Действия после получения новых новостей: трей, звук
  if (!isActiveWindow() && (newCount > 0) &&
      (behaviorIconTray_ == CHANGE_ICON_TRAY)) {
    traySystem->setIcon(QIcon(":/images/quiterss16_NewNews"));
  }
  emit signalRefreshInfoTray();
  if (newCount > 0) {
    playSoundNewNews();
  }

  // Управление уведомлениями
  if (isActiveWindow()) {
    idFeedList_.clear();
    cntNewNewsList_.clear();
  }
  if ((newCount > 0) && !isActiveWindow()) {
    int feedIdIndex = idFeedList_.indexOf(feedId);
    if (onlySelectedFeeds_) {
      QSqlQuery q;
      q.exec(QString("SELECT value FROM feeds_ex WHERE feedId='%1' AND name='showNotification'").
             arg(feedId));
      if (q.next()) {
        if (q.value(0).toInt() == 1) {
          if (-1 < feedIdIndex) {
            cntNewNewsList_[feedIdIndex] = newCount;
          } else {
            idFeedList_.append(feedId);
            cntNewNewsList_.append(newCount);
          }
        }
      }
    } else {
      if (-1 < feedIdIndex) {
        cntNewNewsList_[feedIdIndex] = newCount;
      } else {
        idFeedList_.append(feedId);
        cntNewNewsList_.append(newCount);
      }
    }
  }

  if (currentNewsTab->type_ == TAB_FEED) {
    bool folderUpdate = false;
    int feedParentId = 0;
    QSqlQuery q;
    q.exec(QString("SELECT parentId FROM feeds WHERE id==%1").arg(feedId));
    if (q.next()) {
      feedParentId = q.value(0).toInt();
      if (feedParentId == currentNewsTab->feedId_) folderUpdate = true;
    }

    while (feedParentId && !folderUpdate) {
      q.exec(QString("SELECT parentId FROM feeds WHERE id==%1").arg(feedParentId));
      if (q.next()) {
        feedParentId = q.value(0).toInt();
        if (feedParentId == currentNewsTab->feedId_) folderUpdate = true;
      }
    }

    // если обновлена просматриваемая лента, кликаем по ней, чтобы обновить просмотр
    if ((feedId == currentNewsTab->feedId_) || folderUpdate) {
      slotUpdateNews();
      int unreadCount = 0;
      int allCount = 0;
      q.exec(QString("SELECT unread, undeleteCount FROM feeds WHERE id=='%1'").arg(feedId));
      if (q.next()) {
        unreadCount = q.value(0).toInt();
        allCount    = q.value(1).toInt();
      }
      statusUnread_->setText(QString(" " + tr("Unread: %1") + " ").arg(unreadCount));
      statusAll_->setText(QString(" " + tr("All: %1") + " ").arg(allCount));
    }
  }

  emit signalNextUpdate();
}

//! Обновление списка новостей
void RSSListing::slotUpdateNews()
{
  int newsId = newsModel_->index(
        newsView_->currentIndex().row(), newsModel_->fieldIndex("id")).data(Qt::EditRole).toInt();

  newsModel_->select();

  if (!newsModel_->rowCount()) return;

  while (newsModel_->canFetchMore())
    newsModel_->fetchMore();

  QModelIndex index = newsModel_->index(0, newsModel_->fieldIndex("id"));
  QModelIndexList indexList = newsModel_->match(index, Qt::EditRole, newsId);
  if (indexList.count()) {
    int newsRow = indexList.first().row();
    newsView_->setCurrentIndex(newsModel_->index(newsRow, newsModel_->fieldIndex("title")));
  } else {
    currentNewsTab->currentNewsIdOld = newsId;
    currentNewsTab->hideWebContent();
  }
}

/*! \brief Обработка нажатия в дереве лент ************************************/
void RSSListing::slotFeedClicked(QModelIndex index)
{
  int feedIdCur = feedsTreeModel_->getIdByIndex(index);
  int feedParIdCur = feedsTreeModel_->getParidByIndex(index);

  // Поиск уже открытого таба с этой лентой
  int indexTab = -1;
  for (int i = 0; i < stackedWidget_->count(); i++) {
    NewsTabWidget *widget = (NewsTabWidget*)stackedWidget_->widget(i);
    if (widget->feedId_ == feedIdCur) {
      indexTab = i;
      break;
    }
  }

  if (indexTab == -1) {
    if (tabBar_->currentIndex() != TAB_WIDGET_PERMANENT) {
      updateCurrentTab_ = false;
      tabBar_->setCurrentIndex(TAB_WIDGET_PERMANENT);
      updateCurrentTab_ = true;
      feedsTreeView_->setCurrentIndex(feedsTreeModel_->getIndexById(feedIdCur, feedParIdCur));

      currentNewsTab = (NewsTabWidget*)stackedWidget_->widget(TAB_WIDGET_PERMANENT);
      newsModel_ = currentNewsTab->newsModel_;
      newsView_ = currentNewsTab->newsView_;
    }
    //! При переходе на другую ленту метим старую просмотренной
    setFeedRead(feedIdOld_, FeedReadTypeSwitchingFeed);

    slotFeedSelected(feedsTreeModel_->getIndexById(feedIdCur, feedParIdCur));
    feedsTreeView_->repaint();
  } else if (indexTab != -1) {
    tabBar_->setCurrentIndex(indexTab);
  }
  feedIdOld_ = feedIdCur;
}

/** @brief Обработка самого выбора ленты **************************************/
void RSSListing::slotFeedSelected(QModelIndex index, bool createTab)
{
  QElapsedTimer timer;
  timer.start();
  qDebug() << "--------------------------------";
  qDebug() << __PRETTY_FUNCTION__ << __LINE__ << timer.elapsed();

  int feedId = feedsTreeModel_->getIdByIndex(index);
  int feedParId = feedsTreeModel_->getParidByIndex(index);

  // Открытие или создание вкладки с лентой
  if (!stackedWidget_->count() || createTab) {
    NewsTabWidget *widget = new NewsTabWidget(this, TAB_FEED, feedId, feedParId);
    int indexTab = addTab(widget);
    createNewsTab(indexTab);

    if (indexTab == 0)
      currentNewsTab->closeButton_->setVisible(false);
    if (!index.isValid())
      currentNewsTab->setVisible(false);

    emit signalSetCurrentTab(indexTab);
  } else {
    currentNewsTab->feedId_ = feedId;
    currentNewsTab->feedParId_ = feedParId;
    currentNewsTab->setSettings(false);
    currentNewsTab->setVisible(index.isValid());
  }

  statusUnread_->setVisible(index.isValid());
  statusAll_->setVisible(index.isValid());

  //! Устанавливаем иконку для открытой вкладки
  bool isFeed = (index.isValid() && feedsTreeModel_->isFolder(index)) ? false : true;

  QPixmap iconTab;
  QByteArray byteArray = feedsTreeModel_->dataField(index, "image").toByteArray();
  if (!isFeed) {
    iconTab.load(":/images/folder");
  } else {
    if (byteArray.isNull() || defaultIconFeeds_) {
      iconTab.load(":/images/feed");
    } else if (isFeed) {
      iconTab.loadFromData(QByteArray::fromBase64(byteArray));
    }
  }
  currentNewsTab->newsIconTitle_->setPixmap(iconTab);

  //! Устанавливаем текст для открытой вкладки
  int padding = 15;
  if (tabBar_->currentIndex() == TAB_WIDGET_PERMANENT)
    padding = 0;
  currentNewsTab->setTextTab(feedsTreeModel_->dataField(index, "text").toString(),
                             currentNewsTab->newsTextTitle_->width() - padding);

  feedProperties_->setEnabled(index.isValid());

  qDebug() << __PRETTY_FUNCTION__ << __LINE__ << timer.elapsed();

  // Переустанавливаем фильтр, чтобы текущая лента не исчезала при изменении фильтра лент
  setFeedsFilter(feedsFilterGroup_->checkedAction(), false);

  qDebug() << __PRETTY_FUNCTION__ << __LINE__ << timer.elapsed();

  setNewsFilter(newsFilterGroup_->checkedAction(), false);

  qDebug() << __PRETTY_FUNCTION__ << __LINE__ << timer.elapsed();

  // Поиск новости ленты, отображамой ранее
  int newsRow = -1;
  if (openingFeedAction_ == 0) {
    QModelIndex feedIndex = feedsTreeModel_->getIndexById(feedId, feedParId);
    int newsIdCur = feedsTreeModel_->dataField(feedIndex, "currentNews").toInt();
    QModelIndex index = newsModel_->index(0, newsModel_->fieldIndex("id"));
    QModelIndexList indexList = newsModel_->match(index, Qt::EditRole, newsIdCur);

    if (!indexList.isEmpty()) newsRow = indexList.first().row();
  } else if (openingFeedAction_ == 1) {
    newsRow = 0;
  } else if ((openingFeedAction_ == 3) || (openingFeedAction_ == 4)) {
    QModelIndex index = newsModel_->index(0, newsModel_->fieldIndex("read"));
    QModelIndexList indexList;
    if ((newsView_->header()->sortIndicatorOrder() == Qt::DescendingOrder) &&
        (openingFeedAction_ != 4))
      indexList = newsModel_->match(index, Qt::EditRole, 0, -1);
    else
      indexList = newsModel_->match(index, Qt::EditRole, 0);

    if (!indexList.isEmpty()) newsRow = indexList.last().row();
  }

  qDebug() << __PRETTY_FUNCTION__ << __LINE__ << timer.elapsed();

  // Выбор новости ленты, отображамой ранее
  newsView_->setCurrentIndex(newsModel_->index(newsRow, newsModel_->fieldIndex("title")));
  if (newsRow == -1) newsView_->verticalScrollBar()->setValue(newsRow);

  qDebug() << __PRETTY_FUNCTION__ << __LINE__ << timer.elapsed();

  if ((openingFeedAction_ != 2) && openNewsWebViewOn_) {
    currentNewsTab->slotNewsViewSelected(newsModel_->index(newsRow, newsModel_->fieldIndex("title")));
    qDebug() << __PRETTY_FUNCTION__ << __LINE__ << timer.elapsed();
  } else {
    currentNewsTab->slotNewsViewSelected(newsModel_->index(-1, newsModel_->fieldIndex("title")));
    qDebug() << __PRETTY_FUNCTION__ << __LINE__ << timer.elapsed();
    QSqlQuery q;
    int newsId = newsModel_->index(newsRow, newsModel_->fieldIndex("id")).data(Qt::EditRole).toInt();
    QString qStr = QString("UPDATE feeds SET currentNews='%1' WHERE id=='%2'").arg(newsId).arg(feedId);
    q.exec(qStr);

    QModelIndex feedIndex = feedsTreeModel_->getIndexById(feedId, feedParId);
    feedsTreeModel_->setData(
          feedIndex.sibling(feedIndex.row(), feedsTreeModel_->proxyColumnByOriginal("currentNews")),
          newsId);
    qDebug() << __PRETTY_FUNCTION__ << __LINE__ << timer.elapsed();
  }
}

/*! \brief Вызов окна настроек ************************************************/
void RSSListing::showOptionDlg()
{
  static int index = 0;

  OptionsDialog *optionsDialog = new OptionsDialog(this);

  optionsDialog->showSplashScreen_->setChecked(showSplashScreen_);
  optionsDialog->reopenFeedStartup_->setChecked(reopenFeedStartup_);
  optionsDialog->hideFeedsOpenTab_->setChecked(hideFeedsOpenTab_);
  optionsDialog->defaultIconFeeds_->setChecked(defaultIconFeeds_);
  optionsDialog->autocollapseFolder_->setChecked(feedsTreeView_->autocollapseFolder_);

  optionsDialog->updateCheckEnabled_->setChecked(updateCheckEnabled_);
  optionsDialog->storeDBMemory_->setChecked(storeDBMemoryT_);

  optionsDialog->showTrayIconBox_->setChecked(showTrayIcon_);
  optionsDialog->startingTray_->setChecked(startingTray_);
  optionsDialog->minimizingTray_->setChecked(minimizingTray_);
  optionsDialog->closingTray_->setChecked(closingTray_);
  optionsDialog->setBehaviorIconTray(behaviorIconTray_);
  optionsDialog->singleClickTray_->setChecked(singleClickTray_);
  optionsDialog->clearStatusNew_->setChecked(clearStatusNew_);
  optionsDialog->emptyWorking_->setChecked(emptyWorking_);

  optionsDialog->setProxy(networkProxy_);

  optionsDialog->embeddedBrowserOn_->setChecked(externalBrowserOn_ <= 0);
  optionsDialog->externalBrowserOn_->setChecked(externalBrowserOn_ >= 1);
  optionsDialog->defaultExternalBrowserOn_->setChecked((externalBrowserOn_ == 0) ||
                                                       (externalBrowserOn_ == 1));
  optionsDialog->otherExternalBrowserOn_->setChecked((externalBrowserOn_ == -1) ||
                                                     (externalBrowserOn_ == 2));
  optionsDialog->otherExternalBrowserEdit_->setText(externalBrowser_);
  optionsDialog->javaScriptEnable_->setChecked(javaScriptEnable_);
  optionsDialog->pluginsEnable_->setChecked(pluginsEnable_);
  optionsDialog->openLinkInBackground_->setChecked(openLinkInBackground_);
  optionsDialog->openLinkInBackgroundEmbedded_->setChecked(openLinkInBackgroundEmbedded_);

  optionsDialog->maxPagesInCache_->setValue(maxPagesInCache_);
  bool useDiskCache = settings_->value("Settings/useDiskCache", true).toBool();
  optionsDialog->diskCacheOn_->setChecked(useDiskCache);
  QString dirDiskCache = settings_->value(
        "Settings/dirDiskCache", QDir::toNativeSeparators(dataDirPath_+ "/cache")).toString();
  optionsDialog->dirDiskCacheEdit_->setText(dirDiskCache);
  int maxDiskCache = settings_->value("Settings/maxDiskCache", 50).toInt();
  optionsDialog->maxDiskCache_->setValue(maxDiskCache);

  optionsDialog->updateFeedsStartUp_->setChecked(autoUpdatefeedsStartUp_);
  optionsDialog->updateFeeds_->setChecked(autoUpdatefeeds_);
  optionsDialog->intervalTime_->setCurrentIndex(autoUpdatefeedsInterval_+1);
  optionsDialog->updateFeedsTime_->setValue(autoUpdatefeedsTime_);

  optionsDialog->setOpeningFeed(openingFeedAction_);
  optionsDialog->openNewsWebViewOn_->setChecked(openNewsWebViewOn_);

  optionsDialog->markNewsReadOn_->setChecked(markNewsReadOn_);
  optionsDialog->markCurNewsRead_->setChecked(markCurNewsRead_);
  optionsDialog->markNewsReadTime_->setValue(markNewsReadTime_);
  optionsDialog->markPrevNewsRead_->setChecked(markPrevNewsRead_);
  optionsDialog->markReadSwitchingFeed_->setChecked(markReadSwitchingFeed_);
  optionsDialog->markReadClosingTab_->setChecked(markReadClosingTab_);
  optionsDialog->markReadMinimize_->setChecked(markReadMinimize_);

  optionsDialog->showDescriptionNews_->setChecked(showDescriptionNews_);

  for (int i = 0; i < optionsDialog->formatDate_->count(); i++) {
    if (optionsDialog->formatDate_->itemData(i).toString() == formatDate_) {
      optionsDialog->formatDate_->setCurrentIndex(i);
      break;
    }
  }
  for (int i = 0; i < optionsDialog->formatTime_->count(); i++) {
    if (optionsDialog->formatTime_->itemData(i).toString() == formatTime_) {
      optionsDialog->formatTime_->setCurrentIndex(i);
      break;
    }
  }

  optionsDialog->alternatingRowColorsNews_->setChecked(alternatingRowColorsNews_);
  optionsDialog->changeBehaviorActionNUN_->setChecked(changeBehaviorActionNUN_);
  optionsDialog->simplifiedDateTime_->setChecked(simplifiedDateTime_);
  optionsDialog->notDeleteStarred_->setChecked(notDeleteStarred_);
  optionsDialog->notDeleteLabeled_->setChecked(notDeleteLabeled_);

  optionsDialog->dayCleanUpOn_->setChecked(dayCleanUpOn_);
  optionsDialog->maxDayCleanUp_->setValue(maxDayCleanUp_);
  optionsDialog->newsCleanUpOn_->setChecked(newsCleanUpOn_);
  optionsDialog->maxNewsCleanUp_->setValue(maxNewsCleanUp_);
  optionsDialog->readCleanUp_->setChecked(readCleanUp_);
  optionsDialog->neverUnreadCleanUp_->setChecked(neverUnreadCleanUp_);
  optionsDialog->neverStarCleanUp_->setChecked(neverStarCleanUp_);
  optionsDialog->neverLabelCleanUp_->setChecked(neverLabelCleanUp_);

  optionsDialog->soundNewNews_->setChecked(soundNewNews_);
  optionsDialog->editSoundNotifer_->setText(soundNotifyPath_);
  optionsDialog->showNotifyOn_->setChecked(showNotifyOn_);
  optionsDialog->countShowNewsNotify_->setValue(countShowNewsNotify_);
  optionsDialog->widthTitleNewsNotify_->setValue(widthTitleNewsNotify_);
  optionsDialog->timeShowNewsNotify_->setValue(timeShowNewsNotify_);
  optionsDialog->onlySelectedFeeds_->setChecked(onlySelectedFeeds_);

  optionsDialog->setLanguage(langFileName_);

  QString strFont = QString("%1, %2").
      arg(feedsTreeView_->font().family()).
      arg(feedsTreeView_->font().pointSize());
  optionsDialog->fontsTree_->topLevelItem(0)->setText(2, strFont);
  strFont = QString("%1, %2").arg(newsListFontFamily_).arg(newsListFontSize_);
  optionsDialog->fontsTree_->topLevelItem(1)->setText(2, strFont);
  strFont = QString("%1, %2").arg(newsTitleFontFamily_).arg(newsTitleFontSize_);
  optionsDialog->fontsTree_->topLevelItem(2)->setText(2, strFont);
  strFont = QString("%1, %2").arg(newsTextFontFamily_).arg(newsTextFontSize_);
  optionsDialog->fontsTree_->topLevelItem(3)->setText(2, strFont);
  strFont = QString("%1, %2").arg(notificationFontFamily_).arg(notificationFontSize_);
  optionsDialog->fontsTree_->topLevelItem(4)->setText(2, strFont);

  optionsDialog->browserMinFontSize_->setValue(browserMinFontSize_);
  optionsDialog->browserMinLogFontSize_->setValue(browserMinLogFontSize_);

  optionsDialog->loadActionShortcut(listActions_, &listDefaultShortcut_);


//! Показ диалога настроек

  optionsDialog->setCurrentItem(index);
  int result = optionsDialog->exec();
  index = optionsDialog->currentIndex();

  if (result == QDialog::Rejected) {
    delete optionsDialog;
    return;
  }

//! Применение настроек

  foreach (QAction *action, listActions_) {
    QString objectName = action->objectName();
    if (objectName.contains("labelAction_")) {
      listActions_.removeOne(action);
      delete action;
    }
  }
  optionsDialog->saveActionShortcut(listActions_, newsLabelGroup_);
  listActions_.append(newsLabelGroup_->actions());
  newsLabelMenu_->addActions(newsLabelGroup_->actions());
  this->addActions(newsLabelGroup_->actions());
  if (newsLabelGroup_->actions().count()) {
    newsLabelAction_->setIcon(newsLabelGroup_->actions().at(0)->icon());
    newsLabelAction_->setToolTip(newsLabelGroup_->actions().at(0)->text());
    newsLabelAction_->setData(newsLabelGroup_->actions().at(0)->data());
  }

  if (optionsDialog->idLabels_.count()) {
    QTreeWidgetItem *labelTreeItem = newsCategoriesTree_->topLevelItem(3);
    while (labelTreeItem->childCount()) {
      labelTreeItem->removeChild(labelTreeItem->child(0));
    }

    bool closeTab = true;
    int indexTab = -1;
    int tabLabelId = -1;
    for (int i = 0; i < stackedWidget_->count(); i++) {
      NewsTabWidget *widget = (NewsTabWidget*)stackedWidget_->widget(i);
      if (widget->type_ == TAB_CAT_LABEL) {
        indexTab = i;
        tabLabelId = widget->labelId_;
        break;
      }
    }

    QSqlQuery q;
    q.exec("SELECT id, name, image, currentNews FROM labels ORDER BY num");
    while (q.next()) {
      int idLabel = q.value(0).toInt();
      QString nameLabel = q.value(1).toString();
      QByteArray byteArray = q.value(2).toByteArray();
      QString currentNews = q.value(3).toString();
      QPixmap imageLabel;
      if (!byteArray.isNull())
        imageLabel.loadFromData(byteArray);
      QStringList dataItem;
      dataItem << nameLabel << QString::number(TAB_CAT_LABEL)
               << QString::number(idLabel) << currentNews;
      QTreeWidgetItem *childItem = new QTreeWidgetItem(dataItem);
      childItem->setIcon(0, QIcon(imageLabel));
      labelTreeItem->addChild(childItem);

      if (idLabel == tabLabelId) {
        closeTab = false;
        NewsTabWidget *widget = (NewsTabWidget*)stackedWidget_->widget(indexTab);
        //! Устанавливаем иконку и текст для открытой вкладки
        widget->newsIconTitle_->setPixmap(imageLabel);
        widget->setTextTab(nameLabel);
      }
    }

    if (closeTab && (indexTab > 0) && (tabLabelId > 0)) {
      slotTabCloseRequested(indexTab);
    }
    if ((tabBar_->currentIndex() == indexTab) && (indexTab > 0) && (tabLabelId == 0)) {
      slotUpdateNews();
    }
  }

  showSplashScreen_ = optionsDialog->showSplashScreen_->isChecked();
  reopenFeedStartup_ = optionsDialog->reopenFeedStartup_->isChecked();
  hideFeedsOpenTab_ = optionsDialog->hideFeedsOpenTab_->isChecked();
  defaultIconFeeds_ = optionsDialog->defaultIconFeeds_->isChecked();
  feedsTreeModel_->defaultIconFeeds_ = defaultIconFeeds_;
  feedsTreeView_->autocollapseFolder_ = optionsDialog->autocollapseFolder_->isChecked();

  updateCheckEnabled_ = optionsDialog->updateCheckEnabled_->isChecked();
  storeDBMemoryT_ = optionsDialog->storeDBMemory_->isChecked();

  showTrayIcon_ = optionsDialog->showTrayIconBox_->isChecked();
  startingTray_ = optionsDialog->startingTray_->isChecked();
  minimizingTray_ = optionsDialog->minimizingTray_->isChecked();
  closingTray_ = optionsDialog->closingTray_->isChecked();
  behaviorIconTray_ = optionsDialog->behaviorIconTray();
  if (behaviorIconTray_ > CHANGE_ICON_TRAY) {
    emit signalRefreshInfoTray();
  } else {
#if defined(QT_NO_DEBUG_OUTPUT)
    traySystem->setIcon(QIcon(":/images/quiterss16"));
#else
    traySystem->setIcon(QIcon(":/images/quiterssDebug"));
#endif
  }
  singleClickTray_ = optionsDialog->singleClickTray_->isChecked();
  clearStatusNew_ = optionsDialog->clearStatusNew_->isChecked();
  emptyWorking_ = optionsDialog->emptyWorking_->isChecked();
  if (showTrayIcon_) traySystem->show();
  else traySystem->hide();

  networkProxy_ = optionsDialog->proxy();
  setProxy(networkProxy_);

  if (optionsDialog->embeddedBrowserOn_->isChecked()) {
    if (optionsDialog->defaultExternalBrowserOn_->isChecked())
      externalBrowserOn_ = 0;
    else
      externalBrowserOn_ = -1;
  } else {
    if (optionsDialog->defaultExternalBrowserOn_->isChecked())
      externalBrowserOn_ = 1;
    else
      externalBrowserOn_ = 2;
  }

  externalBrowser_ = optionsDialog->otherExternalBrowserEdit_->text();
  javaScriptEnable_ = optionsDialog->javaScriptEnable_->isChecked();
  pluginsEnable_ = optionsDialog->pluginsEnable_->isChecked();
  openLinkInBackground_ = optionsDialog->openLinkInBackground_->isChecked();
  openLinkInBackgroundEmbedded_ = optionsDialog->openLinkInBackgroundEmbedded_->isChecked();
  maxPagesInCache_ = optionsDialog->maxPagesInCache_->value();

  QWebSettings::globalSettings()->setAttribute(
        QWebSettings::JavascriptEnabled, javaScriptEnable_);
  QWebSettings::globalSettings()->setAttribute(
        QWebSettings::PluginsEnabled, pluginsEnable_);
  QWebSettings::globalSettings()->setMaximumPagesInCache(maxPagesInCache_);

  useDiskCache = optionsDialog->diskCacheOn_->isChecked();
  settings_->setValue("Settings/useDiskCache", useDiskCache);
  maxDiskCache = optionsDialog->maxDiskCache_->value();
  settings_->setValue("Settings/maxDiskCache", maxDiskCache);

  if (dirDiskCache != optionsDialog->dirDiskCacheEdit_->text()) {
    if (diskCache_ != NULL)
      diskCache_->clear();
  }
  dirDiskCache = optionsDialog->dirDiskCacheEdit_->text();
  settings_->setValue("Settings/dirDiskCache", dirDiskCache);

  if (useDiskCache) {
    if (diskCache_ == NULL) {
      diskCache_ = new QNetworkDiskCache(this);
      networkManager_->setCache(diskCache_);
    }
    diskCache_->setCacheDirectory(dirDiskCache);
    diskCache_->setMaximumCacheSize(maxDiskCache*1024*1024);
  } else {
    if (diskCache_ != NULL) {
      diskCache_->setMaximumCacheSize(0);
      diskCache_->clear();
    }
  }

  autoUpdatefeedsStartUp_ = optionsDialog->updateFeedsStartUp_->isChecked();
  autoUpdatefeeds_ = optionsDialog->updateFeeds_->isChecked();
  int updateTime = optionsDialog->updateFeedsTime_->value();
  int updateInterval = optionsDialog->intervalTime_->currentIndex()-1;

  if (!autoUpdatefeeds_)
    updateFeedsTimer_->stop();

  if (autoUpdatefeeds_ && ((updateTime != autoUpdatefeedsTime_) ||
      (updateInterval != autoUpdatefeedsInterval_))) {
    updateFeedsTimer_->stop();
    int updateFeedsTime = updateTime*1000;
    if (updateInterval == 0)
      updateFeedsTime = updateFeedsTime*60;
    else if (updateInterval == 1)
      updateFeedsTime = updateFeedsTime*60*60;
    updateFeedsTimer_->start(updateFeedsTime);
  }
  autoUpdatefeedsTime_ = updateTime;
  autoUpdatefeedsInterval_ = updateInterval;

  openingFeedAction_ = optionsDialog->getOpeningFeed();
  openNewsWebViewOn_ = optionsDialog->openNewsWebViewOn_->isChecked();

  markNewsReadOn_ = optionsDialog->markNewsReadOn_->isChecked();
  markCurNewsRead_ = optionsDialog->markCurNewsRead_->isChecked();
  markNewsReadTime_ = optionsDialog->markNewsReadTime_->value();
  markPrevNewsRead_ = optionsDialog->markPrevNewsRead_->isChecked();
  markReadSwitchingFeed_ = optionsDialog->markReadSwitchingFeed_->isChecked();
  markReadClosingTab_ = optionsDialog->markReadClosingTab_->isChecked();
  markReadMinimize_ = optionsDialog->markReadMinimize_->isChecked();

  showDescriptionNews_ = optionsDialog->showDescriptionNews_->isChecked();

  formatDate_ = optionsDialog->formatDate_->itemData(
        optionsDialog->formatDate_->currentIndex()).toString();
  feedsTreeModel_->formatDate_ = formatDate_;
  formatTime_ = optionsDialog->formatTime_->itemData(
        optionsDialog->formatTime_->currentIndex()).toString();
  feedsTreeModel_->formatTime_ = formatTime_;

  alternatingRowColorsNews_ = optionsDialog->alternatingRowColorsNews_->isChecked();
  changeBehaviorActionNUN_ = optionsDialog->changeBehaviorActionNUN_->isChecked();
  simplifiedDateTime_ = optionsDialog->simplifiedDateTime_->isChecked();
  notDeleteStarred_ = optionsDialog->notDeleteStarred_->isChecked();
  notDeleteLabeled_ = optionsDialog->notDeleteLabeled_->isChecked();

  dayCleanUpOn_ = optionsDialog->dayCleanUpOn_->isChecked();
  maxDayCleanUp_ = optionsDialog->maxDayCleanUp_->value();
  newsCleanUpOn_ = optionsDialog->newsCleanUpOn_->isChecked();
  maxNewsCleanUp_ = optionsDialog->maxNewsCleanUp_->value();
  readCleanUp_ = optionsDialog->readCleanUp_->isChecked();
  neverUnreadCleanUp_ = optionsDialog->neverUnreadCleanUp_->isChecked();
  neverStarCleanUp_ = optionsDialog->neverStarCleanUp_->isChecked();
  neverLabelCleanUp_ = optionsDialog->neverLabelCleanUp_->isChecked();

  soundNewNews_ = optionsDialog->soundNewNews_->isChecked();
  soundNotifyPath_ = optionsDialog->editSoundNotifer_->text();
  showNotifyOn_ = optionsDialog->showNotifyOn_->isChecked();
  countShowNewsNotify_ = optionsDialog->countShowNewsNotify_->value();
  widthTitleNewsNotify_ = optionsDialog->widthTitleNewsNotify_->value();
  timeShowNewsNotify_ = optionsDialog->timeShowNewsNotify_->value();
  onlySelectedFeeds_ = optionsDialog->onlySelectedFeeds_->isChecked();

  if (langFileName_ != optionsDialog->language()) {
    langFileName_ = optionsDialog->language();
    appInstallTranslator();
  }

  QFont font = feedsTreeView_->font();
  font.setFamily(
        optionsDialog->fontsTree_->topLevelItem(0)->text(2).section(", ", 0, 0));
  font.setPointSize(
        optionsDialog->fontsTree_->topLevelItem(0)->text(2).section(", ", 1).toInt());
  feedsTreeView_->setFont(font);
  feedsTreeModel_->font_ = font;

  newsListFontFamily_ = optionsDialog->fontsTree_->topLevelItem(1)->text(2).section(", ", 0, 0);
  newsListFontSize_ = optionsDialog->fontsTree_->topLevelItem(1)->text(2).section(", ", 1).toInt();
  newsTitleFontFamily_ = optionsDialog->fontsTree_->topLevelItem(2)->text(2).section(", ", 0, 0);
  newsTitleFontSize_ = optionsDialog->fontsTree_->topLevelItem(2)->text(2).section(", ", 1).toInt();
  newsTextFontFamily_ = optionsDialog->fontsTree_->topLevelItem(3)->text(2).section(", ", 0, 0);
  newsTextFontSize_ = optionsDialog->fontsTree_->topLevelItem(3)->text(2).section(", ", 1).toInt();
  notificationFontFamily_ = optionsDialog->fontsTree_->topLevelItem(4)->text(2).section(", ", 0, 0);
  notificationFontSize_ = optionsDialog->fontsTree_->topLevelItem(4)->text(2).section(", ", 1).toInt();

  browserMinFontSize_ = optionsDialog->browserMinFontSize_->value();
  browserMinLogFontSize_ = optionsDialog->browserMinLogFontSize_->value();

  QWebSettings::globalSettings()->setFontFamily(
        QWebSettings::StandardFont, newsTextFontFamily_);
  QWebSettings::globalSettings()->setFontSize(
        QWebSettings::MinimumFontSize, browserMinFontSize_);
  QWebSettings::globalSettings()->setFontSize(
        QWebSettings::MinimumLogicalFontSize, browserMinLogFontSize_);

  delete optionsDialog;

  if (currentNewsTab != NULL)
    currentNewsTab->setSettings();

  writeSettings();
  saveActionShortcuts();
}

/*! \brief Создание меню трея *************************************************/
void RSSListing::createTrayMenu()
{
  trayMenu_ = new QMenu(this);
  showWindowAct_ = new QAction(this);
  connect(showWindowAct_, SIGNAL(triggered()), this, SLOT(slotShowWindows()));
  QFont font_ = showWindowAct_->font();
  font_.setBold(true);
  showWindowAct_->setFont(font_);
  trayMenu_->addAction(showWindowAct_);
  trayMenu_->addAction(updateAllFeedsAct_);
  trayMenu_->addSeparator();

  trayMenu_->addAction(optionsAct_);
  trayMenu_->addSeparator();

  trayMenu_->addAction(exitAct_);
  traySystem->setContextMenu(trayMenu_);
}

/*! \brief Освобождение памяти ************************************************/
void RSSListing::myEmptyWorkingSet()
{
#if defined(Q_WS_WIN)
  if (isHidden())
    EmptyWorkingSet(GetCurrentProcess());
#endif
}

/*! \brief Показ статус бара после запрос обновления ленты ********************/
void RSSListing::showProgressBar(int addToMaximum)
{
  if (addToMaximum == 0) {
    updateFeedsStart_ = false;
    importFeedStart_ = false;
    return;
  }
  playSoundNewNews_ = false;

  progressBar_->setMaximum(addToMaximum);
  progressBar_->show();
  QTimer::singleShot(150, this, SLOT(slotProgressBarUpdate()));
}

/*! \brief Обновление ленты (действие) ****************************************/
void RSSListing::slotGetFeed()
{
  if (updateFeedsStart_) return;
  updateFeedsStart_ = true;

  QPersistentModelIndex index = feedsTreeView_->selectIndex();
  if (feedsTreeModel_->isFolder(index)) {
    QSqlQuery q;
    QString str = getIdFeedsString(feedsTreeModel_->dataField(index, "id").toInt());
    str.replace("feedId", "id");
    QString qStr = QString("SELECT xmlUrl, lastBuildDate, authentication FROM feeds WHERE (%1)").
        arg(str);
    q.exec(qStr);
    while (q.next()) {
      updateFeedsCount_ = updateFeedsCount_ + 2;
      QString userInfo = getUserInfo(q.record().value(0).toString(),
                                     q.record().value(2).toInt());
      emit signalRequestUrl(q.record().value(0).toString(),
                            q.record().value(1).toDateTime(),
                            userInfo);
    }
  } else {
    updateFeedsCount_ = updateFeedsCount_ + 2;
    QString userInfo = getUserInfo(feedsTreeModel_->dataField(index, "xmlUrl").toString(),
                                   feedsTreeModel_->dataField(index, "authentication").toInt());
    emit signalRequestUrl(
          feedsTreeModel_->dataField(index, "xmlUrl").toString(),
          QDateTime::fromString(feedsTreeModel_->dataField(index, "lastBuildDate").toString(), Qt::ISODate),
          userInfo);
  }

  showProgressBar(updateFeedsCount_);
}

/*! \brief Обновление ленты (действие) ****************************************/
void RSSListing::slotGetAllFeeds()
{
  if (updateFeedsStart_) return;
  updateFeedsStart_ = true;

  QSqlQuery q;
  q.exec("SELECT xmlUrl, lastBuildDate, authentication FROM feeds WHERE xmlUrl!=''");
//  qDebug() << q.lastError();
  while (q.next()) {
    updateFeedsCount_ = updateFeedsCount_ + 2;
    QString userInfo = getUserInfo(q.record().value(0).toString(),
                                   q.record().value(2).toInt());
    emit signalRequestUrl(q.record().value(0).toString(),
                          q.record().value(1).toDateTime(),
                          userInfo);
  }

  showProgressBar(updateFeedsCount_);

  timer_.start();
//  qCritical() << "Start update";
//  qCritical() << "------------------------------------------------------------";
}

void RSSListing::slotProgressBarUpdate()
{
  progressBar_->update();

  if (progressBar_->isVisible())
    QTimer::singleShot(150, this, SLOT(slotProgressBarUpdate()));
}

void RSSListing::slotVisibledFeedsWidget()
{
  if (tabBar_->currentIndex() == TAB_WIDGET_PERMANENT) {
    showFeedsTabPermanent_ = feedsWidgetVisibleAct_->isChecked();
  }

  feedsWidget_->setVisible(feedsWidgetVisibleAct_->isChecked());
  updateIconToolBarNull(feedsWidgetVisibleAct_->isChecked());
}

void RSSListing::updateIconToolBarNull(bool feedsWidgetVisible)
{
  if (feedsWidgetVisible)
    pushButtonNull_->setIcon(QIcon(":/images/images/triangleR.png"));
  else
    pushButtonNull_->setIcon(QIcon(":/images/images/triangleL.png"));
}


void RSSListing::markFeedRead()
{
  bool openFeed = false;
  QString qStr;
  QPersistentModelIndex index = feedsTreeView_->selectIndex();
  bool isFolder = feedsTreeModel_->isFolder(index);
  int id = feedsTreeModel_->getIdByIndex(index);
  if (currentNewsTab->feedId_ == id)
    openFeed = true;

  db_.transaction();
  QSqlQuery q;
  if (isFolder) {
    if (currentNewsTab->feedParId_ == id)
      openFeed = true;

    qStr = QString("UPDATE news SET read=2 WHERE read!=2 AND deleted==0 AND (%1)").
        arg(getIdFeedsString(id));
    q.exec(qStr);
    qStr = QString("UPDATE news SET new=0 WHERE new==1 AND (%1)").
        arg(getIdFeedsString(id));
    q.exec(qStr);
  } else {
    if (openFeed) {
      qStr = QString("UPDATE news SET read=2 WHERE feedId=='%1' AND read!=2 AND deleted==0").
          arg(id);
      q.exec(qStr);
    } else {
      QString qStr = QString("UPDATE news SET read=1 WHERE feedId=='%1' AND read==0").
          arg(id);
      q.exec(qStr);
    }
    qStr = QString("UPDATE news SET new=0 WHERE feedId=='%1' AND new==1").
        arg(id);
    q.exec(qStr);
  }
  db_.commit();
  // Обновляем ленту, на которой стоит фокус
  if (openFeed) {
    if ((tabBar_->currentIndex() == TAB_WIDGET_PERMANENT) &&
        !isFolder) {
      QModelIndex indexNextUnread =
          feedsTreeView_->indexNextUnread(feedsTreeView_->currentIndex());
      feedsTreeView_->setCurrentIndex(indexNextUnread);
      slotFeedClicked(indexNextUnread);
    } else {
      int currentRow = newsView_->currentIndex().row();

      newsModel_->select();
      while (newsModel_->canFetchMore())
        newsModel_->fetchMore();

      newsView_->setCurrentIndex(newsModel_->index(currentRow, newsModel_->fieldIndex("title")));

      slotUpdateStatus(id);
    }
  }
  // Обновляем ленту, на которой нет фокуса
  else {
    slotUpdateStatus(id);
  }
}

/*! \brief Обновление статуса либо выбранной ленты, либо ленты текущей вкладки*/
void RSSListing::slotUpdateStatus(int feedId, bool changed)
{
  if (changed) {
    recountFeedCounts(feedId);
  }

  emit signalRefreshInfoTray();

  if ((feedId > 0) && (feedId == currentNewsTab->feedId_)) {
    QSqlQuery q;
    int unreadCount = 0;
    int allCount = 0;
    QString qStr = QString("SELECT unread, undeleteCount FROM feeds WHERE id=='%1'").
        arg(feedId);
    q.exec(qStr);
    if (q.next()) {
      unreadCount = q.value(0).toInt();
      allCount    = q.value(1).toInt();
    }
    statusUnread_->setText(QString(" " + tr("Unread: %1") + " ").arg(unreadCount));
    statusAll_->setText(QString(" " + tr("All: %1") + " ").arg(allCount));
  }
  if ((currentNewsTab->type_ != TAB_WEB) && (currentNewsTab->type_ != TAB_FEED)) {
    QSqlQuery q;
    int allCount = 0;
    QString qStr = QString("SELECT count(id) FROM news WHERE %1").
        arg(currentNewsTab->categoryFilterStr_);
    q.exec(qStr);
    if (q.next()) allCount = q.value(0).toInt();

    statusAll_->setText(QString(" " + tr("All: %1") + " ").arg(allCount));
  }
}

/**
 * @brief Установка фильтра для отображения лент и их категорий
 * @param pAct тип выбранного фильтра
 * @param clicked Установка фильтра пользователем или вызов функции из программы:
 *    true  - метод вызван непосредственно после действий пользователя
 *    false - метод вызван внутрипрограммно
 ******************************************************************************/
void RSSListing::setFeedsFilter(QAction* pAct, bool clicked)
{
  QModelIndex index = feedsTreeView_->currentIndex();
  int feedId = feedsTreeModel_->getIdByIndex(index);
  int feedParId = feedsTreeModel_->getParidByIndex(index);
  int newCount = feedsTreeModel_->dataField(index, "newCount").toInt();
  int unRead   = feedsTreeModel_->dataField(index, "unread").toInt();

  QList<int> parentIdList;  //*< Список всех родителей ленты
  while (index.parent().isValid()) {
    parentIdList << feedsTreeModel_->getParidByIndex(index);
    index = index.parent();
  }

  // Создаем фильтр лент из "фильтра"
  QString strFilter;
  if (pAct->objectName() == "filterFeedsAll_") {
    strFilter = "";
  } else if (pAct->objectName() == "filterFeedsNew_") {
    if (clicked && !newCount) {
      strFilter = QString("newCount > 0");
    } else {
      strFilter = QString("(newCount > 0 OR id=='%1'").arg(feedId);
      foreach (int parentId, parentIdList)
        strFilter.append(QString(" OR id=='%1'").arg(parentId));
      strFilter.append(")");
    }
  } else if (pAct->objectName() == "filterFeedsUnread_") {
    if (clicked && !unRead) {
      strFilter = QString("unread > 0");
    } else {
      strFilter = QString("(unread > 0 OR id=='%1'").arg(feedId);
      foreach (int parentId, parentIdList)
        strFilter.append(QString(" OR id=='%1'").arg(parentId));
      strFilter.append(")");
    }
  } else if (pAct->objectName() == "filterFeedsStarred_") {
    strFilter = QString("label LIKE '%starred%'");
  }

  // ... добавляем фильтр из "поиска"
  if (findFeedsWidget_->isVisible()) {
    if (pAct->objectName() != "filterFeedsAll_")
      strFilter.append(" AND ");

    // обязательно добавляем отображение категорий, чтобы найденные внутри
    // ленты смогли отображаться
    strFilter.append("(");
    strFilter.append(QString("((xmlUrl = '') OR (xmlUrl IS NULL)) OR "));
    if (findFeeds_->findGroup_->checkedAction()->objectName() == "findNameAct") {
      strFilter.append(QString("text LIKE '%%1%'").arg(findFeeds_->text()));
    } else {
      strFilter.append(QString("xmlUrl LIKE '%%1%'").arg(findFeeds_->text()));
    }
    strFilter.append(")");
  }

  QElapsedTimer timer;
  timer.start();
  qDebug() << __PRETTY_FUNCTION__ << __LINE__ << timer.elapsed() << strFilter;

  static QString strFilterOld = QString();

  if (strFilterOld.compare(strFilter) == 0) {
    qDebug() << __PRETTY_FUNCTION__ << __LINE__ << timer.elapsed() << "No filter changes";
    return;
  }
  qDebug() << __PRETTY_FUNCTION__ << __LINE__ << timer.elapsed() << "Applying new filter";
  strFilterOld = strFilter;

  // Установка фильтра
  feedsTreeModel_->setFilter(strFilter);
  expandNodes();

  qDebug() << __PRETTY_FUNCTION__ << __LINE__ << timer.elapsed();

  if (pAct->objectName() == "filterFeedsAll_") feedsFilter_->setIcon(QIcon(":/images/filterOff"));
  else feedsFilter_->setIcon(QIcon(":/images/filterOn"));

  // Восстановление курсора на ранее отображаемую ленту
  QModelIndex feedIndex = feedsTreeModel_->getIndexById(feedId, feedParId);
  feedsTreeView_->setCurrentIndex(feedIndex);

  if (clicked) {
    qDebug() << __PRETTY_FUNCTION__ << __LINE__ << timer.elapsed();

    if (tabBar_->currentIndex() == TAB_WIDGET_PERMANENT) {
      slotFeedClicked(feedIndex);
    }
  }

  qDebug() << __PRETTY_FUNCTION__ << __LINE__ << timer.elapsed();

  // Сохраняем фильтр для дальнейшего использования при включении последнего
  // использованного фильтра
  if (pAct->objectName() != "filterFeedsAll_")
    feedsFilterAction_ = pAct;
}

/** @brief Установка фильтра для таблицы новостей *****************************/
void RSSListing::setNewsFilter(QAction* pAct, bool clicked)
{
  if (currentNewsTab == NULL) return;
  if (currentNewsTab->type_ == TAB_WEB) {
    filterNewsAll_->setChecked(true);
    return;
  }

  QElapsedTimer timer;
  timer.start();
  qDebug() << __FUNCTION__ << __LINE__ << timer.elapsed();

  QModelIndex index = newsView_->currentIndex();

  int feedId = currentNewsTab->feedId_;
  int feedParId = currentNewsTab->feedParId_;
  int newsId = newsModel_->index(
        index.row(), newsModel_->fieldIndex("id")).data(Qt::EditRole).toInt();

  // Помеченные новости как "Прочитанные" убираем с глаз долой
  // read=1 - отображаются не зависимо от фильтра
  // read=2 - не будут отображаться
  if (clicked) {
    QString qStr = QString("UPDATE news SET read=2 WHERE feedId='%1' AND read=1").
        arg(feedId);
    QSqlQuery q;
    q.exec(qStr);
  }

  // Создаем фильтр по котегории или по ленте
  if (feedsTreeModel_->isFolder(feedsTreeModel_->getIndexById(feedId, feedParId))) {
    newsFilterStr = QString("(%1) AND ").arg(getIdFeedsString(feedId));
  } else {
    newsFilterStr = QString("feedId=%1 AND ").arg(feedId);
  }

  // ... добавляем фильтр из "фильтра"
  if (pAct->objectName() == "filterNewsAll_") {
    newsFilterStr.append("deleted = 0");
  } else if (pAct->objectName() == "filterNewsNew_") {
    newsFilterStr.append(QString("new = 1 AND deleted = 0"));
  } else if (pAct->objectName() == "filterNewsUnread_") {
    newsFilterStr.append(QString("read < 2 AND deleted = 0"));
  } else if (pAct->objectName() == "filterNewsStar_") {
    newsFilterStr.append(QString("starred = 1 AND deleted = 0"));
  } else if (pAct->objectName() == "filterNewsNotStarred_") {
    newsFilterStr.append(QString("starred = 0 AND deleted = 0"));
  } else if (pAct->objectName() == "filterNewsUnreadStar_") {
    newsFilterStr.append(QString("(read < 2 OR starred = 1) AND deleted = 0"));
  } else if (pAct->objectName() == "filterNewsLastDay_") {
    newsFilterStr.append(QString("(published >= datetime('now', '-1 day')) AND deleted = 0"));
  } else if (pAct->objectName() == "filterNewsLastWeek_") {
    newsFilterStr.append(QString("(published >= datetime('now', '-7 day')) AND deleted = 0"));
  }

  // ... добавляем фильтр из "поиска"
  QString filterStr = newsFilterStr;
  if (currentNewsTab->findText_->findGroup_->checkedAction()->objectName() == "findInNewsAct") {
    filterStr.append(
        QString(" AND (title LIKE '%%1%' OR author_name LIKE '%%1%' OR category LIKE '%%1%')").
        arg(currentNewsTab->findText_->text()));
  }

  qDebug() << __FUNCTION__ << __LINE__ << timer.elapsed() << filterStr;
  newsModel_->setFilter(filterStr);

  while (newsModel_->canFetchMore())
    newsModel_->fetchMore();

  qDebug() << __FUNCTION__ << __LINE__ << timer.elapsed();

  if ((currentNewsTab->newsHeader_->sortIndicatorSection() == newsModel_->fieldIndex("read")) ||
      currentNewsTab->newsHeader_->sortIndicatorSection() == newsModel_->fieldIndex("starred")) {
    currentNewsTab->slotSort(currentNewsTab->newsHeader_->sortIndicatorSection(),
                             currentNewsTab->newsHeader_->sortIndicatorOrder());
  }

  // Переустановка иконки нужна при вызове слота непосредственным кликом пользователя
  if (pAct->objectName() == "filterNewsAll_") newsFilter_->setIcon(QIcon(":/images/filterOff"));
  else newsFilter_->setIcon(QIcon(":/images/filterOn"));

  // Если слот был вызван непосредственным нажатием пользователя,
  // возвращаем курсор на текущий индекс
  if (clicked) {
    QModelIndex index = newsModel_->index(0, newsModel_->fieldIndex("id"));
    QModelIndexList indexList = newsModel_->match(index, Qt::EditRole, newsId);
    if (indexList.count()) {
      int newsRow = indexList.first().row();
      newsView_->setCurrentIndex(newsModel_->index(newsRow, newsModel_->fieldIndex("title")));
    } else {
      currentNewsTab->currentNewsIdOld = newsId;
      currentNewsTab->hideWebContent();
    }
  }

  qDebug() << __FUNCTION__ << __LINE__ << timer.elapsed();

  // Запоминаем выбранный фильтр, для использования именно его при следующем включении фильтра,
  // если включается последний используемый фильтр
  if (pAct->objectName() != "filterNewsAll_")
    newsFilterAction_ = pAct;
}

//! Маркировка ленты прочитанной при клике на не отмеченной ленте
void RSSListing::setFeedRead(int feedId, FeedReedType feedReadtype)
{
  if (feedId <= -1) return;

  db_.transaction();
  QSqlQuery q;
  if (((feedReadtype == FeedReadTypeSwitchingFeed) && markReadSwitchingFeed_) ||
      ((feedReadtype == FeedReadClosingTab)        && markReadClosingTab_) ||
      ((feedReadtype == FeedReadPlaceToTray)       && markReadMinimize_)) {
    QString str = getIdFeedsString(feedId);
    if (str == "feedId=-1") {
      q.exec(QString("UPDATE news SET read=2 WHERE feedId='%1' AND read!=2").arg(feedId));
    } else {
      q.exec(QString("UPDATE news SET read=2 WHERE (%1) AND read=1").arg(str));
    }
  } else {
    QString str = getIdFeedsString(feedId);
    if (str == "feedId=-1") {
      q.exec(QString("UPDATE news SET read=2 WHERE feedId='%1' AND read=1").arg(feedId));
    } else {
      q.exec(QString("UPDATE news SET read=2 WHERE (%1) AND read=1").arg(str));
    }
  }
  QString str = getIdFeedsString(feedId);
  if (str == "feedId=-1") {
    q.exec(QString("UPDATE news SET new=0 WHERE feedId='%1' AND new=1").arg(feedId));
  } else {
    q.exec(QString("UPDATE news SET new=0 WHERE (%1) AND new=1").arg(str));
  }

  if (markNewsReadOn_ && markPrevNewsRead_)
    q.exec(QString("UPDATE news SET read=2 WHERE id IN (SELECT currentNews FROM feeds WHERE id='%1')").arg(feedId));

  db_.commit();

  recountFeedCounts(feedId, false);
  if (feedReadtype != FeedReadPlaceToTray) {
    emit signalRefreshInfoTray();
  }
}

void RSSListing::slotShowAboutDlg()
{
  AboutDialog *aboutDialog = new AboutDialog(langFileName_, this);
  aboutDialog->exec();
  delete aboutDialog;
}

/** @brief Вызов контекстного меню дерева лент
 *----------------------------------------------------------------------------*/
void RSSListing::showContextMenuFeed(const QPoint &pos)
{
  slotFeedMenuShow();

  QModelIndex index = feedsTreeView_->indexAt(pos);
  if (index.isValid()) {
    QRect rectText = feedsTreeView_->visualRect(index);
    if (pos.x() >= rectText.x()) {
      QMenu menu;
      menu.addAction(addAct_);
      menu.addSeparator();
      menu.addAction(openFeedNewTabAct_);
      menu.addSeparator();
      menu.addAction(updateFeedAct_);
      menu.addSeparator();
      menu.addAction(markFeedRead_);
      menu.addSeparator();
      menu.addAction(deleteFeedAct_);
      menu.addSeparator();
      menu.addAction(setFilterNewsAct_);
      menu.addAction(feedProperties_);

      menu.exec(feedsTreeView_->viewport()->mapToGlobal(pos));
    }
  } else {
    QMenu menu;
    menu.addAction(addAct_);

    menu.exec(feedsTreeView_->viewport()->mapToGlobal(pos));
  }

  index = feedsTreeView_->currentIndex();
  feedsTreeView_->selectId_ = feedsTreeModel_->getIdByIndex(index);
  feedsTreeView_->selectParentId_ = feedsTreeModel_->getParidByIndex(index);

  feedProperties_->setEnabled(feedsTreeView_->selectIndex().isValid());
}

void RSSListing::slotFeedMenuShow()
{
  feedProperties_->setEnabled(feedsTreeView_->selectIndex().isValid());
}

void RSSListing::setAutoLoadImages(bool set)
{
  autoLoadImages_ = !autoLoadImages_;
  if (autoLoadImages_) {
    autoLoadImagesToggle_->setText(tr("Load Images"));
    autoLoadImagesToggle_->setToolTip(tr("Auto Load Images to News View"));
    autoLoadImagesToggle_->setIcon(QIcon(":/images/imagesOn"));
  } else {
    autoLoadImagesToggle_->setText(tr("No Load Images"));
    autoLoadImagesToggle_->setToolTip(tr("No Load Images to News View"));
    autoLoadImagesToggle_->setIcon(QIcon(":/images/imagesOff"));
  }

  if (set) {
    currentNewsTab->autoLoadImages_ = autoLoadImages_;
    currentNewsTab->webView_->settings()->setAttribute(
          QWebSettings::AutoLoadImages, autoLoadImages_);
    if (autoLoadImages_) {
      if ((currentNewsTab->webView_->title() == "news_descriptions") &&
          (currentNewsTab->type_ == TAB_FEED))
        currentNewsTab->updateWebView(newsView_->currentIndex());
      else currentNewsTab->webView_->reload();
    }
  }
}

void RSSListing::loadSettingsFeeds()
{
  markCurNewsRead_ = false;
  behaviorIconTray_ = settings_->value("Settings/behaviorIconTray", NEW_COUNT_ICON_TRAY).toInt();

  QString filterName = settings_->value("feedSettings/filterName", "filterFeedsAll_").toString();
  QList<QAction*> listActions = feedsFilterGroup_->actions();
  foreach(QAction *action, listActions) {
    if (action->objectName() == filterName) {
      action->setChecked(true);
      break;
    }
  }
  filterName = settings_->value("newsSettings/filterName", "filterNewsAll_").toString();
  listActions = newsFilterGroup_->actions();
  foreach(QAction *action, listActions) {
    if (action->objectName() == filterName) {
      action->setChecked(true);
      break;
    }
  }

  setFeedsFilter(feedsFilterGroup_->checkedAction(), false);
}

/**
 * @brief Восстановление состояние лент во время запуска приложения
 *----------------------------------------------------------------------------*/
void RSSListing::restoreFeedsOnStartUp()
{
  qApp->processEvents();

  expandNodes();

  //* Восстановление текущей ленты
  QModelIndex feedIndex;
  if (reopenFeedStartup_) {
    int feedId = settings_->value("feedSettings/currentId", 0).toInt();
    int feedParId = settings_->value("feedSettings/currentParId", 0).toInt();
    feedIndex = feedsTreeModel_->getIndexById(feedId, feedParId);
  } else feedIndex = QModelIndex();
  feedsTreeView_->setCurrentIndex(feedIndex);
  updateCurrentTab_ = false;
  slotFeedClicked(feedIndex);
  updateCurrentTab_ = true;

  //* Открытие лент во вкладках
  QSqlQuery q;
  q.exec(QString("SELECT id, parentId FROM feeds WHERE displayOnStartup=1"));
  while(q.next()) {
    creatFeedTab(q.value(0).toInt(), q.value(1).toInt());
  }
}

/** @brief Разворачивание узлов, имеющих флаг развернутости в базе
 *----------------------------------------------------------------------------*/
void RSSListing::expandNodes()
{
  //* Восстановление развернутости узлов
  QSqlQuery q;
  q.exec("SELECT id, parentId FROM feeds WHERE f_Expanded=1 AND (xmlUrl='' OR xmlUrl IS NULL)");
  while (q.next()) {
    int feedId    = q.value(0).toInt();
    int feedParId = q.value(1).toInt();
    QModelIndex index = feedsTreeModel_->getIndexById(feedId, feedParId);
    feedsTreeView_->setExpanded(index, true);
  }
}

void RSSListing::slotFeedsFilter()
{
  if (feedsFilterGroup_->checkedAction()->objectName() == "filterFeedsAll_") {
    if (feedsFilterAction_ != NULL) {
      feedsFilterAction_->setChecked(true);
      setFeedsFilter(feedsFilterAction_);
    } else {
      if (mainToolbar_->widgetForAction(feedsFilter_)) {
        QWidget *widget = mainToolbar_->widgetForAction(feedsFilter_);
        if (widget->underMouse()) {
          feedsFilterMenu_->popup(widget->mapToGlobal(QPoint(0, mainToolbar_->height()-1)));
        }
      }
      if (feedsToolBar_->widgetForAction(feedsFilter_)) {
        QWidget *widget = feedsToolBar_->widgetForAction(feedsFilter_);
        if (widget->underMouse()) {
          feedsFilterMenu_->popup(widget->mapToGlobal(QPoint(0, feedsToolBar_->height()-1)));
        }
      }
      if (currentNewsTab->newsToolBar_->widgetForAction(feedsFilter_)) {
        QWidget *widget = currentNewsTab->newsToolBar_->widgetForAction(feedsFilter_);
        if (widget->underMouse()) {
          feedsFilterMenu_->popup(widget->mapToGlobal(QPoint(0, currentNewsTab->newsToolBar_->height()-1)));
        }
      }
    }
  } else {
    filterFeedsAll_->setChecked(true);
    setFeedsFilter(filterFeedsAll_);
  }
}

void RSSListing::slotNewsFilter()
{
  if (newsFilterGroup_->checkedAction()->objectName() == "filterNewsAll_") {
    if (newsFilterAction_ != NULL) {
      newsFilterAction_->setChecked(true);
      setNewsFilter(newsFilterAction_);
    } else {
      if (mainToolbar_->widgetForAction(newsFilter_)) {
        QWidget *widget = mainToolbar_->widgetForAction(newsFilter_);
        if (widget->underMouse()) {
          newsFilterMenu_->popup(widget->mapToGlobal(QPoint(0, mainToolbar_->height()-1)));
        }
      }
      if (feedsToolBar_->widgetForAction(newsFilter_)) {
        QWidget *widget = feedsToolBar_->widgetForAction(newsFilter_);
        if (widget->underMouse()) {
          newsFilterMenu_->popup(widget->mapToGlobal(QPoint(0, feedsToolBar_->height()-1)));
        }
      }
      if (currentNewsTab->newsToolBar_->widgetForAction(newsFilter_)) {
        QWidget *widget = currentNewsTab->newsToolBar_->widgetForAction(newsFilter_);
        if (widget->underMouse()) {
          newsFilterMenu_->popup(widget->mapToGlobal(QPoint(0, currentNewsTab->newsToolBar_->height()-1)));
        }
      }
    }
  } else {
    filterNewsAll_->setChecked(true);
    setNewsFilter(filterNewsAll_);
  }
}

void RSSListing::slotTimerUpdateFeeds()
{
  if (autoUpdatefeeds_) slotGetAllFeeds();
}

void RSSListing::slotShowUpdateAppDlg()
{
  UpdateAppDialog *updateAppDialog = new UpdateAppDialog(langFileName_,
                                                         settings_, this);
  updateAppDialog->activateWindow();
  updateAppDialog->exec();
  delete updateAppDialog;
}

void RSSListing::appInstallTranslator()
{
  bool translatorLoad;
  qApp->removeTranslator(translator_);
  translatorLoad = translator_->load(appDataDirPath_ +
                                     QString("/lang/quiterss_%1").arg(langFileName_));
  if (translatorLoad) qApp->installTranslator(translator_);
  else retranslateStrings();
}

void RSSListing::retranslateStrings()
{
  QString str = statusUnread_->text();
  str = str.right(str.length() - str.indexOf(':') - 1).replace(" ", "");
  statusUnread_->setText(QString(" " + tr("Unread: %1") + " ").arg(str));
  str = statusAll_->text();
  str = str.right(str.length() - str.indexOf(':') - 1).replace(" ", "");
  statusAll_->setText(QString(" " + tr("All: %1") + " ").arg(str));

  str = traySystem->toolTip();
  QString info =
      "QuiteRSS\n" +
      QString(tr("New News: %1")).arg(str.section(": ", 1).section("\n", 0, 0)) +
      QString("\n") +
      QString(tr("Unread News: %1")).arg(str.section(": ", 2));
  traySystem->setToolTip(info);

  addAct_->setText(tr("&Add"));
  addAct_->setToolTip(tr("Add New Feed"));

  addFeedAct_->setText(tr("&Feed..."));
  addFeedAct_->setToolTip(tr("Add New Feed"));

  addFolderAct_->setText(tr("F&older..."));
  addFolderAct_->setToolTip(tr("Add New Folder"));

  openFeedNewTabAct_->setText(tr("Open in New Tab"));

  deleteFeedAct_->setText(tr("&Delete..."));
  deleteFeedAct_->setToolTip(tr("Delete Selected Feed"));

  importFeedsAct_->setText(tr("&Import Feeds..."));
  importFeedsAct_->setToolTip(tr("Import Feeds from OPML File"));

  exportFeedsAct_->setText(tr("&Export Feeds..."));
  exportFeedsAct_->setToolTip(tr("Export Feeds to OPML File"));

  exitAct_->setText(tr("E&xit"));

  if (autoLoadImages_) {
    autoLoadImagesToggle_->setText(tr("Load Images"));
    autoLoadImagesToggle_->setToolTip(tr("Auto Load Images to News View"));
  } else {
    autoLoadImagesToggle_->setText(tr("No Load Images"));
    autoLoadImagesToggle_->setToolTip(tr("No Load Images to News View"));
  }

  updateFeedAct_->setText(tr("Update Feed"));
  updateFeedAct_->setToolTip(tr("Update Current Feed"));

  updateAllFeedsAct_->setText(tr("Update All"));
  updateAllFeedsAct_->setToolTip(tr("Update All Feeds"));

  markAllFeedsRead_->setText(tr("Mark All Feeds Read"));

  markNewsRead_->setText(tr("Mark Read/Unread"));
  markNewsRead_->setToolTip(tr("Mark Current News Read/Unread"));

  markAllNewsRead_->setText(tr("Mark All News Read"));
  markAllNewsRead_->setToolTip(tr("Mark All News Read"));


  setNewsFiltersAct_->setText(tr("News Filters..."));
  setFilterNewsAct_->setText(tr("Filter News..."));

  optionsAct_->setText(tr("Options..."));
  optionsAct_->setToolTip(tr("Open Options Dialog"));

  feedsFilter_->setText(tr("Filter Feeds"));
  filterFeedsAll_->setText(tr("Show All"));
  filterFeedsNew_->setText(tr("Show New"));
  filterFeedsUnread_->setText(tr("Show Unread"));
  filterFeedsStarred_->setText(tr("Show Starred Feeds"));

  newsFilter_->setText(tr("Filter News"));
  filterNewsAll_->setText(tr("Show All"));
  filterNewsNew_->setText(tr("Show New"));
  filterNewsUnread_->setText(tr("Show Unread"));
  filterNewsStar_->setText(tr("Show Starred"));
  filterNewsNotStarred_->setText(tr("Show Not Starred"));
  filterNewsUnreadStar_->setText(tr("Show Unread or Starred"));
  filterNewsLastDay_->setText(tr("Show Last Day"));
  filterNewsLastWeek_->setText(tr("Show Last 7 Days"));

  aboutAct_ ->setText(tr("About..."));
  aboutAct_->setToolTip(tr("Show 'About' Dialog"));

  updateAppAct_->setText(tr("Check for Updates..."));
  reportProblemAct_->setText(tr("Report a Problem..."));

  openDescriptionNewsAct_->setText(tr("Open News"));
  openDescriptionNewsAct_->setToolTip(tr("Open Description News"));
  openInBrowserAct_->setText(tr("Open in Browser"));
  openInExternalBrowserAct_->setText(tr("Open in External Browser"));
  openInExternalBrowserAct_->setToolTip(tr("Open News in External Browser"));
  openNewsNewTabAct_->setText(tr("Open in New Tab"));
  openNewsNewTabAct_->setToolTip(tr("Open News in New Tab"));
  openNewsBackgroundTabAct_->setText(tr("Open in Background Tab"));
  openNewsBackgroundTabAct_->setToolTip(tr("Open News in Background Tab"));
  markStarAct_->setText(tr("Star"));
  markStarAct_->setToolTip(tr("Mark News Star"));
  deleteNewsAct_->setText(tr("Delete"));
  deleteNewsAct_->setToolTip(tr("Delete Selected News"));
  deleteAllNewsAct_->setText(tr("Delete All News"));
  deleteAllNewsAct_->setToolTip(tr("Delete All News from List"));
  restoreNewsAct_->setText(tr("Restore"));
  restoreNewsAct_->setToolTip(tr("Restore News"));
  copyLinkAct_->setText(tr("Copy Link"));
  copyLinkAct_->setToolTip(tr("Copy Link News"));

  restoreLastNewsAct_->setText(tr("Restore last deleted news"));

  markFeedRead_->setText(tr("Mark Read"));
  markFeedRead_->setToolTip(tr("Mark Feed Read"));
  feedProperties_->setText(tr("Properties"));
  feedProperties_->setToolTip(tr("Properties"));

  fileMenu_->setTitle(tr("&File"));
  editMenu_->setTitle(tr("&Edit"));
  viewMenu_->setTitle(tr("&View"));
  feedMenu_->setTitle(tr("Fee&ds"));
  newsMenu_->setTitle(tr("&News"));
  browserMenu_->setTitle(tr("&Browser"));
  toolsMenu_->setTitle(tr("&Tools"));
  helpMenu_->setTitle(tr("&Help"));

  mainToolbar_->setWindowTitle(tr("Main Toolbar"));
  customizeToolbarMenu_->setTitle(tr("Customize Toolbar"));
  customizeMainToolbarAct_->setText(tr("Main Toolbar..."));
  customizeMainToolbarAct2_->setText(tr("Customize Toolbar..."));
  customizeFeedsToolbarAct_->setText(tr("Feeds Toolbar..."));
  customizeNewsToolbarAct_->setText(tr("News Toolbar..."));

  toolBarLockAct_->setText(tr("Lock Toolbar"));
  toolBarHideAct_->setText(tr("Hide Toolbar"));

  styleMenu_->setTitle(tr("Application Style"));
  systemStyle_->setText(tr("System"));
  system2Style_->setText(tr("System2"));
  greenStyle_->setText(tr("Green"));
  orangeStyle_->setText(tr("Orange"));
  purpleStyle_->setText(tr("Purple"));
  pinkStyle_->setText(tr("Pink"));
  grayStyle_->setText(tr("Gray"));

  browserPositionMenu_->setTitle(tr("Browser Position"));
  topBrowserPositionAct_->setText(tr("Top"));
  bottomBrowserPositionAct_->setText(tr("Bottom"));
  rightBrowserPositionAct_->setText(tr("Right"));
  leftBrowserPositionAct_->setText(tr("Left"));

  showWindowAct_->setText(tr("Show Window"));

  feedKeyUpAct_->setText(tr("Previous Feed"));
  feedKeyDownAct_->setText(tr("Next Feed"));
  newsKeyUpAct_->setText(tr("Previous News"));
  newsKeyDownAct_->setText(tr("Next News"));

  nextUnreadNewsAct_->setText(tr("Next Unread News"));
  prevUnreadNewsAct_->setText(tr("Previous Unread News"));

  switchFocusAct_->setText(tr("Switch Focus to Next Panel"));
  switchFocusAct_->setToolTip(
        tr("Switch Focus to Next Panel (Tree Feeds, List News, Browser)"));
  switchFocusPrevAct_->setText(tr("Switch Focus to Previous Panel"));
  switchFocusPrevAct_->setToolTip(
        tr("Switch Focus to Previous Panel (Tree Feeds, Browser, List News)"));

  feedsWidgetVisibleAct_->setText(tr("Show/Hide Tree Feeds"));

  placeToTrayAct_->setText(tr("Minimize to Tray"));
  placeToTrayAct_->setToolTip(
        tr("Minimize Application to Tray"));

  feedsColumnsMenu_->setTitle(tr("Columns"));
  showUnreadCount_->setText(tr("Count News Unread"));
  showUndeleteCount_->setText(tr("Count News All"));
  showLastUpdated_->setText(tr("Last Updated"));

  indentationFeedsTreeAct_->setText(tr("Show Indentation"));

  findFeedAct_->setText(tr("Search Feed"));
  findFeedAct_->setToolTip(tr("Search Feed"));

  browserZoomMenu_->setTitle(tr("Zoom"));
  zoomInAct_->setText(tr("Zoom In"));
  zoomInAct_->setToolTip(tr("Zoom in in browser"));
  zoomOutAct_->setText(tr("Zoom Out"));
  zoomOutAct_->setToolTip(tr("Zoom out in browser"));
  zoomTo100Act_->setText(tr("100%"));
  zoomTo100Act_->setToolTip(tr("Reset zoom in browser"));

  printAct_->setText(tr("Print..."));
  printAct_->setToolTip(tr("Print Web page"));
  printPreviewAct_->setText(tr("Print Preview..."));
  printPreviewAct_->setToolTip(tr("Preview Web page"));

  savePageAsAct_->setText(tr("Save As..."));
  savePageAsAct_->setToolTip(tr("Save Page As..."));

  toolbarsMenu_->setTitle(tr("Show/Hide"));
  mainToolbarToggle_->setText(tr("Main Toolbar"));
  feedsToolbarToggle_->setText(tr("Feeds Toolbar"));
  newsToolbarToggle_->setText(tr("News Toolbar"));
  browserToolbarToggle_->setText(tr("Browser Toolbar"));
  categoriesPanelToggle_->setText(tr("Panel Categories"));

  fullScreenAct_->setText(tr("Full Screen"));
  fullScreenAct_->setToolTip(tr("Full Screen"));

  stayOnTopAct_->setText(tr("Stay On Top"));
  stayOnTopAct_->setToolTip(tr("Stay On Top"));

  categoriesLabel_->setText(tr("Categories"));
  if (newsCategoriesTree_->isHidden())
    showCategoriesButton_->setToolTip(tr("Show Categories"));
  else
    showCategoriesButton_->setToolTip(tr("Hide Categories"));

  newsLabelMenuAction_->setText(tr("Label"));
  newsLabelAction_->setText(tr("Label"));

  closeTabAct_->setText(tr("Close tab"));
  closeAllTabAct_->setText(tr("Close other tabs"));
  nextTabAct_->setText(tr("Switch to next tab"));
  prevTabAct_->setText(tr("Switch to previous tab"));

  newsCategoriesTree_->topLevelItem(0)->setText(0, tr("Unread"));
  newsCategoriesTree_->topLevelItem(1)->setText(0, tr("Starred"));
  newsCategoriesTree_->topLevelItem(2)->setText(0, tr("Deleted"));
  newsCategoriesTree_->topLevelItem(3)->setText(0, tr("Labels"));

  reduceNewsListAct_->setText(tr("Decrease news list/increase browser"));
  increaseNewsListAct_->setText(tr("Increase news list/decrease browser"));

  findTextAct_->setText(tr("Find"));

  openHomeFeedAct_->setText(tr("Open Homepage Feed"));
  sortedByTitleFeedsTreeAct_->setText(tr("Sorted by Name"));
  collapseAllFoldersAct_->setText(tr("Collapse All Folders"));
  expandAllFoldersAct_->setText(tr("Expand All Folders"));

  shareMenuAct_->setText(tr("Share"));

  QApplication::translate("QDialogButtonBox", "Close");
  QApplication::translate("QDialogButtonBox", "Cancel");
  QApplication::translate("QDialogButtonBox", "&Yes");
  QApplication::translate("QDialogButtonBox", "&No");

  QApplication::translate("QLineEdit", "&Undo");
  QApplication::translate("QLineEdit", "&Redo");
  QApplication::translate("QLineEdit", "Cu&t");
  QApplication::translate("QLineEdit", "&Copy");
  QApplication::translate("QLineEdit", "&Paste");
  QApplication::translate("QLineEdit", "Delete");
  QApplication::translate("QLineEdit", "Select All");

  QApplication::translate("QTextControl", "&Undo");
  QApplication::translate("QTextControl", "&Redo");
  QApplication::translate("QTextControl", "Cu&t");
  QApplication::translate("QTextControl", "&Copy");
  QApplication::translate("QTextControl", "&Paste");
  QApplication::translate("QTextControl", "Delete");
  QApplication::translate("QTextControl", "Select All");
  QApplication::translate("QTextControl", "Copy &Link Location");

  QApplication::translate("QAbstractSpinBox", "&Step up");
  QApplication::translate("QAbstractSpinBox", "Step &down");
  QApplication::translate("QAbstractSpinBox", "&Select All");

  QApplication::translate("QMultiInputContext", "Select IM");

  QApplication::translate("QWizard", "Cancel");
  QApplication::translate("QWizard", "< &Back");
  QApplication::translate("QWizard", "&Finish");
  QApplication::translate("QWizard", "&Next >");

  QStringList nameLabels;
  nameLabels << "Important" << "Work" << "Personal"
             << "To Do" << "Later" << "Amusingly";
  QStringList trNameLabels;
  trNameLabels << tr("Important") << tr("Work") << tr("Personal")
               << tr("To Do") << tr("Later") << tr("Amusingly");

  QSqlQuery q;
  for (int i = 0; i < nameLabels.count(); i++) {
    QString qStr = QString("UPDATE labels SET name='%1' WHERE id <= 6 AND name == '%2'").
        arg(trNameLabels[i]).arg(nameLabels[i]);
    q.exec(qStr);
  }
  q.exec("SELECT id, name FROM labels WHERE id <= 6");
  while (q.next()) {
    int idLabel = q.value(0).toInt();
    QString nameLabel = q.value(1).toString();
    QList<QTreeWidgetItem *> treeItems;
    treeItems = newsCategoriesTree_->findItems(QString::number(idLabel),
                                               Qt::MatchFixedString|Qt::MatchRecursive,
                                               2);
    if (treeItems.count()) {
      treeItems.at(0)->setText(0, nameLabel);
    }
  }

  if (newsView_) {
    currentNewsTab->retranslateStrings();
  }
  findFeeds_->retranslateStrings();
}

void RSSListing::setToolBarStyle(const QString &styleStr)
{
  if (mainToolbar_->widgetForAction(addAct_))
    mainToolbar_->widgetForAction(addAct_)->setMinimumWidth(10);
  if (styleStr == "toolBarStyleI_") {
    mainToolbar_->setToolButtonStyle(Qt::ToolButtonIconOnly);
  } else if (styleStr == "toolBarStyleT_") {
    mainToolbar_->setToolButtonStyle(Qt::ToolButtonTextOnly);
  } else if (styleStr == "toolBarStyleTbI_") {
    mainToolbar_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  } else {
    mainToolbar_->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    if (mainToolbar_->widgetForAction(addAct_))
      mainToolbar_->widgetForAction(addAct_)->setMinimumWidth(60);
  }
}

void RSSListing::setToolBarIconSize(const QString &iconSizeStr)
{
  if (iconSizeStr == "toolBarIconBig_") {
    mainToolbar_->setIconSize(QSize(32, 32));
  } else if (iconSizeStr == "toolBarIconSmall_") {
    mainToolbar_->setIconSize(QSize(16, 16));
  } else {
    mainToolbar_->setIconSize(QSize(24, 24));
  }
}

/** @brief Вызов контекстного меню главной панели инструментов
 *----------------------------------------------------------------------------*/
void RSSListing::showContextMenuToolBar(const QPoint &pos)
{
  QMenu menu;
  menu.addAction(customizeMainToolbarAct2_);
  menu.addSeparator();
  menu.addAction(toolBarLockAct_);
  menu.addAction(toolBarHideAct_);

  menu.exec(mainToolbar_->mapToGlobal(pos));
}

void RSSListing::lockMainToolbar(bool lock)
{
  mainToolbar_->setMovable(!lock);
}

void RSSListing::hideMainToolbar()
{
  mainToolbarToggle_->setChecked(false);
  mainToolbar_->hide();
}

void RSSListing::showFeedPropertiesDlg()
{
  if (!feedsTreeView_->selectIndex().isValid()) {
    feedProperties_->setEnabled(false);
    return;
  }

  QPersistentModelIndex index = feedsTreeView_->selectIndex();
  int feedId = feedsTreeModel_->getIdByIndex(index);
  int feedParentId = feedsTreeModel_->getParidByIndex(index);
  bool isFeed = (index.isValid() && feedsTreeModel_->isFolder(index)) ? false : true;

  FeedPropertiesDialog *feedPropertiesDialog = new FeedPropertiesDialog(isFeed, this);

  QByteArray byteArray = feedsTreeModel_->dataField(index, "image").toByteArray();
  if (!byteArray.isNull()) {
    QPixmap icon;
    icon.loadFromData(QByteArray::fromBase64(byteArray));
    feedPropertiesDialog->setWindowIcon(icon);
  } else if (isFeed) {
    feedPropertiesDialog->setWindowIcon(QPixmap(":/images/feed"));
  } else {
    feedPropertiesDialog->setWindowIcon(QPixmap(":/images/folder"));
  }
  QString str(feedPropertiesDialog->windowTitle() +
              " '" +
              feedsTreeModel_->dataField(index, "text").toString() +
              "'");
  feedPropertiesDialog->setWindowTitle(str);

  FEED_PROPERTIES properties;

  properties.general.text =
      feedsTreeModel_->dataField(index, "text").toString();
  properties.general.title =
      feedsTreeModel_->dataField(index, "title").toString();
  properties.general.url =
      feedsTreeModel_->dataField(index, "xmlUrl").toString();
  properties.general.homepage =
      feedsTreeModel_->dataField(index, "htmlUrl").toString();
  properties.general.displayOnStartup =
      feedsTreeModel_->dataField(index, "displayOnStartup").toInt();
  properties.display.displayEmbeddedImages =
      feedsTreeModel_->dataField(index, "displayEmbeddedImages").toInt();
  if (feedsTreeModel_->dataField(index, "displayNews").toString().isEmpty())
    properties.display.displayNews = !showDescriptionNews_;
  else
    properties.display.displayNews =
        feedsTreeModel_->dataField(index, "displayNews").toInt();

  if (feedsTreeModel_->dataField(index, "label").toString().contains("starred"))
    properties.general.starred = true;
  else
    properties.general.starred = false;

  properties.general.duplicateNewsMode =
      feedsTreeModel_->dataField(index, "duplicateNewsMode").toBool();

  properties.authentication.on = false;
  if (feedsTreeModel_->dataField(index, "authentication").toInt() == 1) {
    properties.authentication.on = true;
  }
  QUrl url(feedsTreeModel_->dataField(index, "xmlUrl").toString());
  QSqlQuery q;
  q.prepare("SELECT username, password FROM passwords WHERE server=?");
  q.addBindValue(url.host());
  q.exec();
  if (q.next()) {
    properties.authentication.user = q.value(0).toString();
    properties.authentication.pass = QString::fromUtf8(QByteArray::fromBase64(q.value(1).toByteArray()));
  }

  QDateTime dtLocalTime = QDateTime::currentDateTime();
  QDateTime dtUTC = QDateTime(dtLocalTime.date(), dtLocalTime.time(), Qt::UTC);
  int nTimeShift = dtLocalTime.secsTo(dtUTC);

  QDateTime dt = QDateTime::fromString(
        feedsTreeModel_->dataField(index, "created").toString(),
        Qt::ISODate);
  properties.status.createdTime = dt.addSecs(nTimeShift);

  dt = QDateTime::fromString(
        feedsTreeModel_->dataField(index, "updated").toString(),
        Qt::ISODate);
  properties.status.lastUpdate = dt.addSecs(nTimeShift);

  properties.status.undeleteCount = feedsTreeModel_->dataField(index, "undeleteCount").toInt();
  properties.status.newCount      = feedsTreeModel_->dataField(index, "newCount").toInt();
  properties.status.unreadCount   = feedsTreeModel_->dataField(index, "unread").toInt();
  properties.status.description   = feedsTreeModel_->dataField(index, "description").toString();

  feedPropertiesDialog->setFeedProperties(properties);

  connect(feedPropertiesDialog, SIGNAL(signalLoadTitle(QString,QString)),
          this, SIGNAL(faviconRequestUrl(QString,QString)));

  int result = feedPropertiesDialog->exec();
  if (result == QDialog::Rejected) {
    delete feedPropertiesDialog;
    return;
  }

  properties = feedPropertiesDialog->getFeedProperties();
  delete feedPropertiesDialog;

  q.prepare("UPDATE feeds SET text = ?, xmlUrl = ?, displayOnStartup = ?, "
            "displayEmbeddedImages = ?, displayNews = ?, label = ?, "
            "duplicateNewsMode = ?, authentication = ? WHERE id == ?");
  q.addBindValue(properties.general.text);
  q.addBindValue(properties.general.url);
  q.addBindValue(properties.general.displayOnStartup);
  q.addBindValue(properties.display.displayEmbeddedImages);
  q.addBindValue(properties.display.displayNews);
  if (properties.general.starred)
    q.addBindValue("starred");
  else
    q.addBindValue("");
  q.addBindValue(properties.general.duplicateNewsMode ? 1 : 0);
  q.addBindValue(properties.authentication.on ? 1 : 0);
  q.addBindValue(feedId);
  q.exec();

  if (!(!feedsTreeModel_->dataField(index, "authentication").toInt() && !properties.authentication.on)) {
    q.prepare("SELECT * FROM passwords WHERE server=?");
    q.addBindValue(url.host());
    q.exec();
    if (q.next()) {
      q.prepare("UPDATE passwords SET username = ?, password = ? WHERE server=?");
      q.addBindValue(properties.authentication.user);
      q.addBindValue(properties.authentication.pass.toUtf8().toBase64());
      q.addBindValue(url.host());
      q.exec();
    } else if (properties.authentication.on) {
      q.prepare("INSERT INTO passwords (server, username, password) "
                "VALUES (:server, :username, :password)");
      q.bindValue(":server", url.host());
      q.bindValue(":username", properties.authentication.user);
      q.bindValue(":password", properties.authentication.pass.toUtf8().toBase64());
      q.exec();
    }
  }

  index = feedsTreeModel_->getIndexById(feedId, feedParentId);
  QPersistentModelIndex indexText    = index.sibling(index.row(), feedsTreeModel_->proxyColumnByOriginal("text"));
  QPersistentModelIndex indexUrl     = index.sibling(index.row(), feedsTreeModel_->proxyColumnByOriginal("xmlUrl"));
  QPersistentModelIndex indexStartup = index.sibling(index.row(), feedsTreeModel_->proxyColumnByOriginal("displayOnStartup"));
  QModelIndex indexImages  = index.sibling(index.row(), feedsTreeModel_->proxyColumnByOriginal("displayEmbeddedImages"));
  QModelIndex indexNews    = index.sibling(index.row(), feedsTreeModel_->proxyColumnByOriginal("displayNews"));
  QModelIndex indexLabel   = index.sibling(index.row(), feedsTreeModel_->proxyColumnByOriginal("label"));
  QModelIndex indexDuplicate = index.sibling(index.row(), feedsTreeModel_->proxyColumnByOriginal("duplicateNewsMode"));
  QModelIndex indexAuthentication = index.sibling(index.row(), feedsTreeModel_->proxyColumnByOriginal("authentication"));
  feedsTreeModel_->setData(indexText, properties.general.text);
  feedsTreeModel_->setData(indexUrl, properties.general.url);
  feedsTreeModel_->setData(indexStartup, properties.general.displayOnStartup);
  feedsTreeModel_->setData(indexImages, properties.display.displayEmbeddedImages);
  feedsTreeModel_->setData(indexNews, properties.display.displayNews);
  feedsTreeModel_->setData(indexLabel, properties.general.starred ? "starred" : "");
  feedsTreeModel_->setData(indexDuplicate, properties.general.duplicateNewsMode ? 1 : 0);
  feedsTreeModel_->setData(indexAuthentication, properties.authentication.on ? 1 : 0);

  if (feedsTreeView_->currentIndex() == index) {
    QPixmap iconTab;
    byteArray = feedsTreeModel_->dataField(index, "image").toByteArray();
    if (!isFeed) {
      iconTab.load(":/images/folder");
    } else {
      if (byteArray.isNull() || defaultIconFeeds_) {
        iconTab.load(":/images/feed");
      } else if (isFeed) {
        iconTab.loadFromData(QByteArray::fromBase64(byteArray));
      }
    }
    currentNewsTab->newsIconTitle_->setPixmap(iconTab);
    currentNewsTab->setTextTab(feedsTreeModel_->dataField(index, "text").toString());
  }
}

//! Обновление информации в трее: значок и текст подсказки
void RSSListing::slotRefreshInfoTray()
{
  if (!showTrayIcon_) return;

  // Подсчёт количества новых и прочитанных новостей
  int newCount = 0;
  int unreadCount = 0;
  QSqlQuery q;
  q.exec("SELECT sum(newCount), sum(unread) FROM feeds WHERE xmlUrl!=''");
  if (q.next()) {
    newCount    = q.value(0).toInt();
    unreadCount = q.value(1).toInt();
  }

  // Установка текста всплывающей подсказки
  QString info =
      "QuiteRSS\n" +
      QString(tr("New News: %1")).arg(newCount) +
      QString("\n") +
      QString(tr("Unread News: %1")).arg(unreadCount);
  traySystem->setToolTip(info);

  // Отображаем количество либо новых, либо непрочитанных новостей
  if (behaviorIconTray_ > CHANGE_ICON_TRAY) {
    int trayCount = (behaviorIconTray_ == UNREAD_COUNT_ICON_TRAY) ? unreadCount : newCount;
    // выводим иконку с цифрой
    if (trayCount != 0) {
      // Подготавливаем цифру
      QString trayCountStr;
      QFont font("Consolas");
      if (trayCount > 99) {
        font.setBold(false);
        if (trayCount < 1000) {
          font.setPixelSize(8);
          trayCountStr = QString::number(trayCount);
        } else {
          font.setPixelSize(11);
          trayCountStr = "#";
        }
      } else {
        font.setBold(true);
        font.setPixelSize(11);
        trayCountStr = QString::number(trayCount);
      }

      // Рисуем иконку, текст на ней, и устанавливаем разрисованную иконку в трей
      QPixmap icon = QPixmap(":/images/countNew");
      QPainter trayPainter;
      trayPainter.begin(&icon);
      trayPainter.setFont(font);
      trayPainter.setPen(Qt::white);
      trayPainter.drawText(QRect(1, 0, 15, 16), Qt::AlignVCenter | Qt::AlignHCenter,
                           trayCountStr);
      trayPainter.end();
      traySystem->setIcon(icon);
    }
    // Выводим иконку без цифры
    else {
#if defined(QT_NO_DEBUG_OUTPUT)
      traySystem->setIcon(QIcon(":/images/quiterss16"));
#else
      traySystem->setIcon(QIcon(":/images/quiterssDebug"));
#endif
    }
  }
}

//! Помечаем все ленты прочитанными
void RSSListing::markAllFeedsRead()
{
  QSqlQuery q;

  q.exec("UPDATE news SET read=2 WHERE read!=2 AND deleted==0");
  q.exec("UPDATE news SET new=0 WHERE new==1 AND deleted==0");

  q.exec("SELECT id FROM feeds WHERE unread!=0");
  while (q.next()) {
    qApp->processEvents();
    recountFeedCounts(q.value(0).toInt());
  }

  if (tabBar_->currentIndex() == TAB_WIDGET_PERMANENT) {
    QModelIndex index =
        feedsTreeModel_->index(-1, feedsTreeView_->columnIndex("text"));
    feedsTreeView_->setCurrentIndex(index);
    slotFeedClicked(index);
  } else {
    int currentRow = newsView_->currentIndex().row();

    newsModel_->select();

    while (newsModel_->canFetchMore())
      newsModel_->fetchMore();

    newsView_->setCurrentIndex(newsModel_->index(currentRow, newsModel_->fieldIndex("title")));
  }

  emit signalRefreshInfoTray();
}

//! Помечаем все ленты не новыми
void RSSListing::markAllFeedsOld()
{
  QSqlQuery q;
  q.exec("UPDATE news SET new=0 WHERE new==1 AND deleted==0");

  q.exec("SELECT id FROM feeds WHERE newCount!=0");
  while (q.next()) {
    qApp->processEvents();
    recountFeedCounts(q.value(0).toInt());
  }

  if (currentNewsTab != NULL) {
    int currentRow = newsView_->currentIndex().row();
    setNewsFilter(newsFilterGroup_->checkedAction(), false);
    newsView_->setCurrentIndex(newsModel_->index(currentRow, newsModel_->fieldIndex("title")));
  }

  emit signalRefreshInfoTray();
}

/** @brief Подготовка иконки ленты для сохранения в БД
 *----------------------------------------------------------------------------*/
void RSSListing::slotIconFeedPreparing(const QString &feedUrl, const QByteArray &byteArray)
{
  QPixmap icon;
  if (icon.loadFromData(byteArray)) {
    icon = icon.scaled(16, 16, Qt::IgnoreAspectRatio,
                       Qt::SmoothTransformation);
    QByteArray faviconData;
    QBuffer    buffer(&faviconData);
    buffer.open(QIODevice::WriteOnly);
    if (icon.save(&buffer, "ICO")) {
      emit signalIconFeedReady(feedUrl, faviconData);
    }
  }
}

/** @brief Обновление иконки ленты в модели
 *----------------------------------------------------------------------------*/
void RSSListing::slotIconFeedUpdate(int feedId, int feedParId, const QByteArray &faviconData)
{
  QModelIndex index = feedsTreeModel_->getIndexById(feedId, feedParId);
  if (index.isValid()) {
    QModelIndex indexImage = index.sibling(index.row(), feedsTreeModel_->proxyColumnByOriginal("image"));
    feedsTreeModel_->setData(indexImage, faviconData.toBase64());
    feedsTreeView_->viewport()->update();
  }

  if (defaultIconFeeds_) return;

  for (int i = 0; i < stackedWidget_->count(); i++) {
    NewsTabWidget *widget = (NewsTabWidget*)stackedWidget_->widget(i);
    if (widget->feedId_ == feedId) {
      QPixmap iconTab;
      if (!faviconData.isNull()) {
        iconTab.loadFromData(faviconData);
      } else {
        iconTab.load(":/images/feed");
      }
      widget->newsIconTitle_->setPixmap(iconTab);
      break;
    }
  }
  if (currentNewsTab->type_ != TAB_WEB)
    currentNewsTab->newsView_->viewport()->update();
}

void RSSListing::playSoundNewNews()
{
  if (!playSoundNewNews_ && soundNewNews_) {
#if defined(Q_OS_WIN) || defined(Q_OS_OS2)
    QSound::play(soundNotifyPath_);
#else
    QProcess::startDetached(QString("play %1").arg(soundNotifyPath_));
#endif
    playSoundNewNews_ = true;
  }
}

void RSSListing::showNewsFiltersDlg(bool newFilter)
{
  NewsFiltersDialog *newsFiltersDialog = new NewsFiltersDialog(this, settings_);
  if (newFilter) {
    newsFiltersDialog->filtersTree->setCurrentItem(
          newsFiltersDialog->filtersTree->topLevelItem(
            newsFiltersDialog->filtersTree->topLevelItemCount()-1));
  }

  newsFiltersDialog->exec();

  delete newsFiltersDialog;
}

void RSSListing::showFilterRulesDlg()
{
  if (!feedsTreeView_->selectIndex().isValid()) return;

  int feedId = feedsTreeView_->selectId_;

  FilterRulesDialog *filterRulesDialog = new FilterRulesDialog(
        this, -1, feedId);

  int result = filterRulesDialog->exec();
  if (result == QDialog::Rejected) {
    delete filterRulesDialog;
    return;
  }

  delete filterRulesDialog;

  showNewsFiltersDlg(true);
}

void RSSListing::slotUpdateAppCheck()
{
  if (!updateCheckTech_) return;

  updateAppDialog_ = new UpdateAppDialog(langFileName_, settings_, this, false);
  connect(updateAppDialog_, SIGNAL(signalNewVersion(QString)),
          this, SLOT(slotNewVersion(QString)), Qt::QueuedConnection);
}

void RSSListing::slotNewVersion(QString newVersion)
{
  delete updateAppDialog_;

  if (!updateCheckEnabled_) return;

  if (!newVersion.isEmpty()) {
    traySystem->showMessage(tr("Check for updates"),
                            tr("A new version of QuiteRSS..."));
    connect(traySystem, SIGNAL(messageClicked()),
            this, SLOT(slotShowUpdateAppDlg()));
  }
}

/*! \brief Обработка клавиш Key_Up в дереве лент ******************************/
void RSSListing::slotFeedUpPressed()
{
  QModelIndex indexBefore = feedsTreeView_->currentIndex();
  QModelIndex indexAfter;

  // Если нет текущего индекса устанавливаем его в конец, т.к. мы хотим "подниматься" по лентам
  if (!indexBefore.isValid())
    indexAfter = feedsTreeModel_->index(feedsTreeModel_->rowCount()-1, feedsTreeView_->columnIndex("text"));
  else
    indexAfter = feedsTreeView_->indexAbove(indexBefore);

  // Если индекса "выше" не существует
  if (!indexAfter.isValid()) return;

  feedsTreeView_->setCurrentIndex(indexAfter);
  slotFeedClicked(indexAfter);
}

/*! \brief Обработка клавиш Key_Down в дереве лент ****************************/
void RSSListing::slotFeedDownPressed()
{
  QModelIndex indexBefore = feedsTreeView_->currentIndex();
  QModelIndex indexAfter;

  // Если нет текущего индекса устанавливаем его в начало, т.к. мы хотим "опускаться" по лентам
  if (!indexBefore.isValid())
    indexAfter = feedsTreeModel_->index(0, feedsTreeView_->columnIndex("text"));
  else
    indexAfter = feedsTreeView_->indexBelow(indexBefore);

  // Если индекса "ниже" не существует
  if (!indexAfter.isValid()) return;

  feedsTreeView_->setCurrentIndex(indexAfter);
  slotFeedClicked(indexAfter);
}

/*! \brief Обработка горячей клавиши перемещения на предыдущую ленту **********/
void RSSListing::slotFeedPrevious()
{
  QModelIndex indexBefore = feedsTreeView_->currentIndex();
  QModelIndex indexAfter;

  // Если нет текущего индекса устанавливаем его в конец, т.к. мы хотим "подниматься" по лентам
  if (!indexBefore.isValid())
    indexAfter = feedsTreeModel_->index(feedsTreeModel_->rowCount()-1, feedsTreeView_->columnIndex("text"));
  else
    indexAfter = feedsTreeView_->indexPrevious(indexBefore);

  // Если индекса "выше" не существует
  if (!indexAfter.isValid()) return;

  feedsTreeView_->setCurrentIndex(indexAfter);
  slotFeedClicked(indexAfter);
}

/*! \brief Обработка горячей клавиши перемещения на следующую ленту ***********/
void RSSListing::slotFeedNext()
{
  QModelIndex indexBefore = feedsTreeView_->currentIndex();
  QModelIndex indexAfter;

  // Если нет текущего индекса устанавливаем его в начало, т.к. мы хотим "опускаться" по лентам
  if (!indexBefore.isValid())
    indexAfter = feedsTreeModel_->index(0, feedsTreeView_->columnIndex("text"));
  else
    indexAfter = feedsTreeView_->indexNext(indexBefore);

  // Если индекса "ниже" не существует
  if (!indexAfter.isValid()) return;

  feedsTreeView_->setCurrentIndex(indexAfter);
  slotFeedClicked(indexAfter);
}

/*! \brief Обработка клавиш Home/End в дереве лент *****************************/
void RSSListing::slotFeedHomePressed()
{
  QModelIndex index = feedsTreeModel_->index(
      0, feedsTreeView_->columnIndex("text"));
  feedsTreeView_->setCurrentIndex(index);
  slotFeedClicked(index);
}

void RSSListing::slotFeedEndPressed()
{
  QModelIndex index = feedsTreeModel_->index(
      feedsTreeModel_->rowCount()-1, feedsTreeView_->columnIndex("text"));
  feedsTreeView_->setCurrentIndex(index);
  slotFeedClicked(index);
}

//! Удаление новостей в ленте по критериям
void RSSListing::feedsCleanUp(QString feedId)
{
  int cntT = 0;
  int cntNews = 0;

  QSqlQuery q;
  QString qStr;
  qStr = QString("SELECT undeleteCount FROM feeds WHERE id=='%1'").
      arg(feedId);
  q.exec(qStr);
  if (q.next()) cntNews = q.value(0).toInt();

  qStr = QString("SELECT id, received FROM news WHERE feedId=='%1' AND deleted==0")
      .arg(feedId);
  if (neverUnreadCleanUp_) qStr.append(" AND read!=0");
  if (neverStarCleanUp_) qStr.append(" AND starred==0");
  if (neverLabelCleanUp_) qStr.append(" AND (label=='' OR label==',' OR label IS NULL)");
  q.exec(qStr);
  while (q.next()) {
    int newsId = q.value(0).toInt();

    if (newsCleanUpOn_ && (cntT < (cntNews - maxNewsCleanUp_))) {
        qStr = QString("UPDATE news SET deleted=1, read=2 WHERE id=='%1'").
            arg(newsId);
//        qCritical() << "*01"  << feedId << q.value(5).toString()
//                 << q.value(1).toString() << cntNews
//                 << (cntNews - maxNewsCleanUp_);
        QSqlQuery qt;
        qt.exec(qStr);
        cntT++;
        continue;
    }

    QDateTime dateTime = QDateTime::fromString(
          q.value(1).toString(),
          Qt::ISODate);
    if (dayCleanUpOn_ &&
        (dateTime.daysTo(QDateTime::currentDateTime()) > maxDayCleanUp_)) {
        qStr = QString("UPDATE news SET deleted=1, read=2 WHERE id=='%1'").
            arg(newsId);
//        qCritical() << "*02"  << feedId << q.value(5).toString()
//                 << q.value(1).toString() << cntNews
//                 << (cntNews - maxNewsCleanUp_);
        QSqlQuery qt;
        qt.exec(qStr);
        cntT++;
        continue;
    }

    if (readCleanUp_) {
      qStr = QString("UPDATE news SET deleted=1 WHERE read!=0 AND id=='%1'").
          arg(newsId);
      QSqlQuery qt;
      qt.exec(qStr);
      cntT++;
    }
  }

  int undeleteCount = 0;
  qStr = QString("SELECT count(id) FROM news WHERE feedId=='%1' AND deleted==0").
      arg(feedId);
  q.exec(qStr);
  if (q.next()) undeleteCount = q.value(0).toInt();

  int unreadCount = 0;
  qStr = QString("SELECT count(read) FROM news WHERE feedId=='%1' AND read==0 AND deleted==0").
      arg(feedId);
  q.exec(qStr);
  if (q.next()) unreadCount = q.value(0).toInt();

  qStr = QString("UPDATE feeds SET unread='%1', undeleteCount='%2' WHERE id=='%3'").
      arg(unreadCount).arg(undeleteCount).arg(feedId);
  q.exec(qStr);
}

//! Установка стиля оформления приложения
void RSSListing::setStyleApp(QAction *pAct)
{
  QString fileString(appDataDirPath_);
  if (pAct->objectName() == "systemStyle_") {
    fileString.append("/style/system.qss");
  } else if (pAct->objectName() == "system2Style_") {
    fileString.append("/style/system2.qss");
  } else if (pAct->objectName() == "orangeStyle_") {
    fileString.append("/style/orange.qss");
  } else if (pAct->objectName() == "purpleStyle_") {
    fileString.append("/style/purple.qss");
  } else if (pAct->objectName() == "pinkStyle_") {
    fileString.append("/style/pink.qss");
  } else if (pAct->objectName() == "grayStyle_") {
    fileString.append("/style/gray.qss");
  } else {
    fileString.append("/style/green.qss");
  }

  QFile file(fileString);
  if (!file.open(QFile::ReadOnly)) {
    file.setFileName(":/style/systemStyle");
    file.open(QFile::ReadOnly);
  }
  qApp->setStyleSheet(QLatin1String(file.readAll()));
  file.close();

  mainSplitter_->setStyleSheet(
              QString("QSplitter::handle {background: qlineargradient("
                      "x1: 0, y1: 0, x2: 0, y2: 1,"
                      "stop: 0 %1, stop: 0.07 %2);}").
              arg(feedsPanel_->palette().background().color().name()).
              arg(qApp->palette().color(QPalette::Dark).name()));
}

//! Переключение фокуса между деревом лент, списком новостей и браузером
void RSSListing::slotSwitchFocus()
{
  if (feedsTreeView_->hasFocus()) {
    newsView_->setFocus();
  } else if (newsView_->hasFocus()) {
    currentNewsTab->webView_->setFocus();
  } else {
    feedsTreeView_->setFocus();
  }
}

//! Переключение фокуса между деревом лент, списком новостей и браузером
void RSSListing::slotSwitchPrevFocus()
{
  if (feedsTreeView_->hasFocus()) {
    currentNewsTab->webView_->setFocus();
  } else if (currentNewsTab->webView_->hasFocus()) {
    newsView_->setFocus();
  } else {
    feedsTreeView_->setFocus();
  }
}

//! Открытие ленты в новой вкладке
void RSSListing::slotOpenFeedNewTab()
{
  feedsTreeView_->selectIdEn_ = false;
  feedsTreeView_->setCurrentIndex(feedsTreeView_->selectIndex());
  slotFeedSelected(feedsTreeView_->selectIndex(), true);
}

//! Закрытие вкладки
void RSSListing::slotTabCloseRequested(int index)
{
  if (index != 0) {
    NewsTabWidget *widget = (NewsTabWidget*)stackedWidget_->widget(index);

    if (widget->type_ == TAB_FEED) {
      setFeedRead(widget->feedId_, FeedReadClosingTab);

      settings_->setValue("NewsHeaderGeometry",
                          widget->newsHeader_->saveGeometry());
      settings_->setValue("NewsHeaderState",
                          widget->newsHeader_->saveState());

      settings_->setValue("NewsTabSplitterGeometry",
                          widget->newsTabWidgetSplitter_->saveGeometry());
      settings_->setValue("NewsTabSplitterState",
                          widget->newsTabWidgetSplitter_->saveState());
    }

    stackedWidget_->removeWidget(widget);
    tabBar_->removeTab(index);
    QWidget *newsTitleLabel = widget->newsTitleLabel_;
    delete widget;
    delete newsTitleLabel;
  }
}

//! Переключение между вкладками
void RSSListing::slotTabCurrentChanged(int index)
{
  if (!stackedWidget_->count()) return;

  NewsTabWidget *widget = (NewsTabWidget*)stackedWidget_->widget(index);
  if ((widget->type_ == TAB_FEED) || (widget->type_ == TAB_WEB))
    newsCategoriesTree_->setCurrentIndex(QModelIndex());
  if (widget->type_ != TAB_FEED)
    feedsTreeView_->setCurrentIndex(QModelIndex());

  if (index == TAB_WIDGET_PERMANENT) {
    feedsWidgetVisibleAct_->setChecked(showFeedsTabPermanent_);
    slotVisibledFeedsWidget();
  } else {
    if (hideFeedsOpenTab_) {
      feedsWidgetVisibleAct_->setChecked(false);
      slotVisibledFeedsWidget();
    }
  }

  stackedWidget_->setCurrentIndex(index);

  if (!updateCurrentTab_) return;

  if (widget->type_ == TAB_FEED) {
    if (widget->feedId_ == 0)
      widget->hide();
    createNewsTab(index);

    QModelIndex feedIndex = feedsTreeModel_->getIndexById(currentNewsTab->feedId_, currentNewsTab->feedParId_);
    feedsTreeView_->setCurrentIndex(feedIndex);

    setFeedsFilter(feedsFilterGroup_->checkedAction(), false);

    slotUpdateNews();
    if (widget->isVisible())
      newsView_->setFocus();
    else
      feedsTreeView_->setFocus();

    statusUnread_->setVisible(widget->feedId_);
    statusAll_->setVisible(widget->feedId_);
  } else if (widget->type_ == TAB_WEB) {
    statusUnread_->setVisible(false);
    statusAll_->setVisible(false);
    currentNewsTab = widget;
    currentNewsTab->setSettings();
    currentNewsTab->retranslateStrings();
    currentNewsTab->webView_->setFocus();
  } else {
    QList<QTreeWidgetItem *> treeItems;
    if (widget->type_ == TAB_CAT_LABEL) {
      treeItems = newsCategoriesTree_->findItems(QString::number(widget->labelId_),
                                                 Qt::MatchFixedString|Qt::MatchRecursive,
                                                 2);
    } else {
      treeItems = newsCategoriesTree_->findItems(QString::number(widget->type_),
                                                 Qt::MatchFixedString,
                                                 1);
    }
    newsCategoriesTree_->setCurrentItem(treeItems.at(0));

    createNewsTab(index);
    slotUpdateNews();
    newsView_->setFocus();

    QSqlQuery q;
    int allCount = 0;
    QString qStr = QString("SELECT count(id) FROM news WHERE %1").
        arg(currentNewsTab->categoryFilterStr_);
    q.exec(qStr);
    if (q.next()) allCount = q.value(0).toInt();

    statusAll_->setText(QString(" " + tr("All: %1") + " ").arg(allCount));

    statusUnread_->setVisible(false);
    statusAll_->setVisible(true);
  }

  setTextTitle(widget->newsTitleLabel_->toolTip(), widget);
}

/** @brief Перемещение вкладок
 *----------------------------------------------------------------------------*/
void RSSListing::slotTabMoved(int fromIndex, int toIndex)
{
  stackedWidget_->insertWidget(toIndex, stackedWidget_->widget(fromIndex));
}

//! Включение/отключение отображения колонок в дереве лент
void RSSListing::feedsColumnVisible(QAction *action)
{
  int idx = action->data().toInt();
  if (action->isChecked())
    feedsTreeView_->showColumn(idx);
  else
    feedsTreeView_->hideColumn(idx);
}

//! Установка позиции браузера
void RSSListing::setBrowserPosition(QAction *action)
{
  browserPosition_ = action->data().toInt();
  currentNewsTab->setBrowserPosition();
}

//! Создание вкладки только со страницей в браузере
QWebPage *RSSListing::createWebTab()
{
  NewsTabWidget *widget = new NewsTabWidget(this, TAB_WEB);
  int indexTab = addTab(widget);

  widget->setTextTab(tr("Loading..."));
  widget->setSettings();
  widget->retranslateStrings();

  if (openNewsTab_ == NEW_TAB_FOREGROUND) {
    currentNewsTab = widget;
    emit signalSetCurrentTab(indexTab);
  }

  openNewsTab_ = 0;

  return widget->webView_->page();
}

void RSSListing::creatFeedTab(int feedId, int feedParId)
{
  QSqlQuery q;
  q.exec(QString("SELECT text, image, currentNews, xmlUrl FROM feeds WHERE id=='%1'").
         arg(feedId));

  if (q.next()) {
    NewsTabWidget *widget = new NewsTabWidget(this, TAB_FEED, feedId, feedParId);
    addTab(widget);
    widget->setSettings();
    widget->retranslateStrings();
    widget->setBrowserPosition();

    bool isFeed = true;
    if (q.value(3).toString().isEmpty())
      isFeed = false;

    //! Устанавливаем иконку и текст для открытой вкладки
    QPixmap iconTab;
    QByteArray byteArray = q.value(1).toByteArray();
    if (!isFeed) {
      iconTab.load(":/images/folder");
    } else {
      if (byteArray.isNull() || defaultIconFeeds_) {
        iconTab.load(":/images/feed");
      } else if (isFeed) {
        iconTab.loadFromData(QByteArray::fromBase64(byteArray));
      }
    }
    widget->newsIconTitle_->setPixmap(iconTab);
    widget->setTextTab(q.value(0).toString());

    QString feedIdFilter;
    if (feedsTreeModel_->isFolder(feedsTreeModel_->getIndexById(feedId, feedParId))) {
      feedIdFilter = QString("(%1) AND ").arg(getIdFeedsString(feedId));
    } else {
      feedIdFilter = QString("feedId=%1 AND ").arg(feedId);
    }

    if (newsFilterGroup_->checkedAction()->objectName() == "filterNewsAll_") {
      feedIdFilter.append("deleted = 0");
    } else if (newsFilterGroup_->checkedAction()->objectName() == "filterNewsNew_") {
      feedIdFilter.append(QString("new = 1 AND deleted = 0"));
    } else if (newsFilterGroup_->checkedAction()->objectName() == "filterNewsUnread_") {
      feedIdFilter.append(QString("read < 2 AND deleted = 0"));
    } else if (newsFilterGroup_->checkedAction()->objectName() == "filterNewsStar_") {
      feedIdFilter.append(QString("starred = 1 AND deleted = 0"));
    } else if (newsFilterGroup_->checkedAction()->objectName() == "filterNewsNotStarred_") {
      feedIdFilter.append(QString("starred = 0 AND deleted = 0"));
    } else if (newsFilterGroup_->checkedAction()->objectName() == "filterNewsUnreadStar_") {
      feedIdFilter.append(QString("(read < 2 OR starred = 1) AND deleted = 0"));
    } else if (newsFilterGroup_->checkedAction()->objectName() == "filterNewsLastDay_") {
      feedIdFilter.append(QString("(published >= datetime('now', '-1 day')) AND deleted = 0"));
    } else if (newsFilterGroup_->checkedAction()->objectName() == "filterNewsLastWeek_") {
      feedIdFilter.append(QString("(published >= datetime('now', '-7 day')) AND deleted = 0"));
    }
    widget->newsModel_->setFilter(feedIdFilter);

    if (widget->newsModel_->rowCount() != 0) {
      while (widget->newsModel_->canFetchMore())
        widget->newsModel_->fetchMore();
    }

    // выбор новости ленты, отображамой ранее
    int newsRow = -1;
    int newsId = widget->newsModel_->index(newsRow, widget->newsModel_->fieldIndex("id")).data(Qt::EditRole).toInt();
    if (openingFeedAction_ == 0) {
      QModelIndex index = newsModel_->index(0, newsModel_->fieldIndex("id"));
      QModelIndexList indexList = newsModel_->match(index, Qt::EditRole, newsId);
      if (indexList.count()) newsRow = indexList.first().row();
    } else if (openingFeedAction_ == 1) newsRow = 0;

    widget->newsView_->setCurrentIndex(widget->newsModel_->index(newsRow, widget->newsModel_->fieldIndex("title")));
    if (newsRow == -1) widget->newsView_->verticalScrollBar()->setValue(newsRow);

    if ((openingFeedAction_ < 2) && openNewsWebViewOn_) {
      widget->slotNewsViewSelected(widget->newsModel_->index(newsRow, widget->newsModel_->fieldIndex("title")));
    } else {
      widget->slotNewsViewSelected(widget->newsModel_->index(-1, widget->newsModel_->fieldIndex("title")));
      QSqlQuery q;
      QString qStr = QString("UPDATE feeds SET currentNews='%1' WHERE id=='%2'").
          arg(newsId).arg(feedId);
      q.exec(qStr);
    }
  }
}

//! Открытие новости клавишей Enter
void RSSListing::slotOpenNewsWebView()
{
  if (!newsView_->hasFocus()) return;
  currentNewsTab->slotNewsViewClicked(newsView_->currentIndex());
}

void RSSListing::slotNewsUpPressed()
{
  currentNewsTab->slotNewsUpPressed();
}

void RSSListing::slotNewsDownPressed()
{
  currentNewsTab->slotNewsDownPressed();
}

void RSSListing::markNewsRead()
{
  currentNewsTab->markNewsRead();
}

void RSSListing::markAllNewsRead()
{
  currentNewsTab->markAllNewsRead();
}

void RSSListing::markNewsStar()
{
  currentNewsTab->markNewsStar();
}

void RSSListing::deleteNews()
{
  currentNewsTab->deleteNews();
}

void RSSListing::deleteAllNewsList()
{
  currentNewsTab->deleteAllNewsList();
}

void RSSListing::restoreNews()
{
  currentNewsTab->restoreNews();
}

void RSSListing::openInBrowserNews()
{
  currentNewsTab->openInBrowserNews();
}

void RSSListing::openInExternalBrowserNews()
{
  currentNewsTab->openInExternalBrowserNews();
}

void RSSListing::slotOpenNewsNewTab()
{
  openNewsTab_ = NEW_TAB_FOREGROUND;
  currentNewsTab->openNewsNewTab();
}

void RSSListing::slotOpenNewsBackgroundTab()
{
  openNewsTab_ = NEW_TAB_BACKGROUND;
  currentNewsTab->openNewsNewTab();
}

/** @brief Копировать ссылку новости
 *----------------------------------------------------------------------------*/
void RSSListing::slotCopyLinkNews()
{
  currentNewsTab->slotCopyLinkNews();
}

/**
 * @brief Полное обновление модели лент
 * @details Производит перечитывание модели, сброс прокси модели и
 *    восстановление курсора на прежнее место
 ******************************************************************************/
void RSSListing::feedsModelReload(bool checkFilter)
{
  if (checkFilter && feedsTreeModel_->filter().isEmpty()) {
    feedsTreeView_->viewport()->update();
    feedsTreeView_->header()->setResizeMode(feedsTreeModel_->proxyColumnByOriginal("unread"), QHeaderView::ResizeToContents);
    feedsTreeView_->header()->setResizeMode(feedsTreeModel_->proxyColumnByOriginal("undeleteCount"), QHeaderView::ResizeToContents);
    feedsTreeView_->header()->setResizeMode(feedsTreeModel_->proxyColumnByOriginal("updated"), QHeaderView::ResizeToContents);
    return;
  }

  int topRow = feedsTreeView_->verticalScrollBar()->value();

  int feedId = feedsTreeModel_->getIdByIndex(feedsTreeView_->currentIndex());
  int feedParId = feedsTreeModel_->getParidByIndex(feedsTreeView_->currentIndex());

  feedsTreeModel_->refresh();
  expandNodes();

  QModelIndex feedIndex = feedsTreeModel_->getIndexById(feedId, feedParId);
  feedsTreeView_->selectIdEn_ = false;
  feedsTreeView_->setCurrentIndex(feedIndex);
  feedsTreeView_->verticalScrollBar()->setValue(topRow);
}

void RSSListing::setCurrentTab(int index, bool updateCurrentTab)
{
  updateCurrentTab_ = updateCurrentTab;
  tabBar_->setCurrentIndex(index);
  updateCurrentTab_ = true;
}

//! Установить фокус в строку поиска (CTRL+F)
void RSSListing::findText()
{
  if (currentNewsTab->type_ != TAB_WEB)
    currentNewsTab->findText_->setFocus();
}

//! Показать уведовление о входящих новостях
void RSSListing::showNotification()
{
  if (idFeedList_.isEmpty() || isActiveWindow() || !showNotifyOn_) {
    idFeedList_.clear();
    cntNewNewsList_.clear();
    return;
  }

  if (notificationWidget) delete notificationWidget;
  notificationWidget = new NotificationWidget(
        idFeedList_, cntNewNewsList_,
        countShowNewsNotify_, timeShowNewsNotify_, widthTitleNewsNotify_,
        notificationFontFamily_, notificationFontSize_);

  idFeedList_.clear();
  cntNewNewsList_.clear();

  connect(notificationWidget, SIGNAL(signalShow()), this, SLOT(slotShowWindows()));
  connect(notificationWidget, SIGNAL(signalDelete()),
          this, SLOT(deleteNotification()));
  connect(notificationWidget, SIGNAL(signalOpenNews(int,int,int)),
          this, SLOT(slotOpenNew(int,int,int)));
  connect(notificationWidget, SIGNAL(signalOpenExternalBrowser(QUrl)),
          this, SLOT(slotOpenNewBrowser(QUrl)));

  notificationWidget->show();
}

//! Удалить уведовление о входящих новостях
void RSSListing::deleteNotification()
{
  notificationWidget->deleteLater();
  notificationWidget = NULL;
}

//! Показать новость при клике в окне уведомления входящих новостей
void RSSListing::slotOpenNew(int feedId, int feedParId, int newsId)
{
  deleteNotification();

  openingFeedAction_ = 0;
  openNewsWebViewOn_ = true;

  QSqlQuery q;
  QString qStr = QString("UPDATE feeds SET currentNews='%1' WHERE id=='%2'").arg(newsId).arg(feedId);
  q.exec(qStr);

  QModelIndex feedIndex = feedsTreeModel_->getIndexById(feedId, feedParId);
  feedsTreeModel_->setData(
        feedIndex.sibling(feedIndex.row(), feedsTreeModel_->proxyColumnByOriginal("currentNews")),
        newsId);

  feedsTreeView_->setCurrentIndex(feedIndex);
  slotFeedClicked(feedIndex);

  openingFeedAction_ = settings_->value("/Settings/openingFeedAction", 0).toInt();
  openNewsWebViewOn_ = settings_->value("/Settings/openNewsWebViewOn", true).toBool();
  QModelIndex index = newsView_->currentIndex();
  slotShowWindows();
  newsView_->setCurrentIndex(index);
}

/** @brief Открыть новость во внешнем браузере из окна уведомления
 *----------------------------------------------------------------------------*/
void RSSListing::slotOpenNewBrowser(const QUrl &url)
{
  currentNewsTab->openUrl(url);
}

void RSSListing::slotFindFeeds(QString)
{
  if (!findFeedsWidget_->isVisible()) return;

  setFeedsFilter(feedsFilterGroup_->checkedAction(), false);
}

void RSSListing::slotSelectFind()
{
  slotFindFeeds(findFeeds_->text());
}

void RSSListing::findFeedVisible(bool visible)
{
  findFeedsWidget_->setVisible(visible);
  if (visible) {
    findFeeds_->setFocus();
  } else {
    findFeeds_->clear();
    // ВЫзываем фильтр явно, т.к. слот не вызовется, потому что widget не виден
    setFeedsFilter(feedsFilterGroup_->checkedAction(), false);
  }
}

//! Полное удаление новостей
void RSSListing::cleanUp()
{
  QSqlQuery q;

  db_.transaction();
  bool lastBuildDateClear = false;
  q.exec("SELECT value FROM info WHERE name='lastBuildDateClear_0.10.1'");
  if (q.next()) {
    lastBuildDateClear = q.value(0).toBool();
    q.exec("UPDATE info SET value='true' WHERE name='lastBuildDateClear_0.10.1'");
  }
  else q.exec("INSERT INTO info(name, value) VALUES ('lastBuildDateClear_0.10.1', 'true')");

  if (!lastBuildDateClear) {
    QString qStr = QString("UPDATE feeds SET lastBuildDate = '%1'").
        arg(QDateTime().toString(Qt::ISODate));
    q.exec(qStr);
  }

  if (settings_->value("CleanUp", 0).toInt() != 1) return;

  q.exec("SELECT received, id FROM news WHERE deleted==2");
  while (q.next()) {
    QDateTime dateTime = QDateTime::fromString(q.value(0).toString(), Qt::ISODate);
    if (dateTime.daysTo(QDateTime::currentDateTime()) > settings_->value("DayCleanUp", 0).toInt()) {
      QString qStr = QString("DELETE FROM news WHERE id='%2'").
          arg(q.value(1).toInt());
      QSqlQuery qt;
      qt.exec(qStr);
    }
  }
  db_.commit();

  settings_->setValue("CleanUp", 0);
}

//! Масштаб в браузере
void RSSListing::browserZoom(QAction *action)
{
  if (action->objectName() == "zoomInAct") {
    currentNewsTab->webView_->setZoomFactor(currentNewsTab->webView_->zoomFactor()+0.1);
  } else if (action->objectName() == "zoomOutAct") {
    if (currentNewsTab->webView_->zoomFactor() > 0.1)
      currentNewsTab->webView_->setZoomFactor(currentNewsTab->webView_->zoomFactor()-0.1);
  } else {
    currentNewsTab->webView_->setZoomFactor(1);
  }
}

//! Сообщить о проблеме...
void RSSListing::slotReportProblem()
{
  QDesktopServices::openUrl(QUrl("http://code.google.com/p/quite-rss/issues/list"));
}

//! Печать страницы из браузера
void RSSListing::slotPrint()
{
  QPrinter printer;
  printer.setDocName(tr("Web Page"));
  QPrintDialog *printDlg = new QPrintDialog(&printer);
  connect(printDlg, SIGNAL(accepted(QPrinter*)), currentNewsTab->webView_, SLOT(print(QPrinter*)));
  printDlg->exec();
  delete printDlg;
}

//! Предварительный просмотр при печати страницы из браузера
void RSSListing::slotPrintPreview()
{
  QPrinter printer;
  printer.setDocName(tr("Web Page"));
  QPrintPreviewDialog *prevDlg = new QPrintPreviewDialog(&printer);
  prevDlg->setWindowFlags(prevDlg->windowFlags() | Qt::WindowMaximizeButtonHint);
  prevDlg->resize(650, 800);
  connect(prevDlg, SIGNAL(paintRequested(QPrinter*)), currentNewsTab->webView_, SLOT(print(QPrinter*)));
  prevDlg->exec();
  delete prevDlg;
}

void RSSListing::setFullScreen()
{
  if (!isFullScreen()) {
    // hide menu & toolbars
    mainToolbar_->hide();
    menuBar()->hide();
#ifdef Q_WS_X11
    show();
    raise();
    setWindowState(windowState() | Qt::WindowFullScreen);
#else
    setWindowState(windowState() | Qt::WindowFullScreen);
#endif
  } else {
    menuBar()->show();
    if (mainToolbarToggle_->isChecked())
      mainToolbar_->show();
    setWindowState(windowState() & ~Qt::WindowFullScreen);
  }
}

void RSSListing::setStayOnTop()
{
  int state = windowState();

  if (stayOnTopAct_->isChecked())
    setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
  else
    setWindowFlags(windowFlags() & ~Qt::WindowStaysOnTopHint);

  if ((state & Qt::WindowFullScreen) || (state & Qt::WindowMaximized)) {
    restoreGeometry(settings_->value("GeometryState").toByteArray());
  }
  setWindowState((Qt::WindowState)state);
  show();
}

/**
 * @brief Перемещение индекса после Drag & Drop
 * @param indexWhat Индекс, который перемещаем
 * @param indexWhere Индекс, куда перемещаем
 ******************************************************************************/
void RSSListing::slotMoveIndex(QModelIndex &indexWhat, QModelIndex &indexWhere, int how)
{
  feedsTreeView_->setCursor(Qt::WaitCursor);

  int feedIdWhat = feedsTreeModel_->getIdByIndex(indexWhat);
  int feedParIdWhat = feedsTreeModel_->getParidByIndex(indexWhat);
  int feedIdWhere = feedsTreeModel_->getIdByIndex(indexWhere);
  int feedParIdWhere = feedsTreeModel_->getParidByIndex(indexWhere);

  // Исправляем rowToParent
  QSqlQuery q;
  if (how == 2) {
    // Перемещение в другую папку
    QList<int> idList;
    q.exec(QString("SELECT id FROM feeds WHERE parentId='%1' ORDER BY rowToParent").
           arg(feedParIdWhat));
    while (q.next()) {
      if (feedIdWhat != q.value(0).toInt())
        idList << q.value(0).toInt();
    }
    for (int i = 0; i < idList.count(); i++) {
      q.exec(QString("UPDATE feeds SET rowToParent='%1' WHERE id=='%2'").
             arg(i).arg(idList.at(i)));
    }

    int rowToParent = 0;
    q.exec(QString("SELECT count(id) FROM feeds WHERE parentId='%1'").
           arg(feedIdWhere));
    if (q.next()) rowToParent = q.value(0).toInt();

    q.exec(QString("UPDATE feeds SET parentId='%1', rowToParent='%2' WHERE id=='%3'").
           arg(feedIdWhere).arg(rowToParent).arg(feedIdWhat));

    QList<int> categoriesList;
    categoriesList << feedParIdWhat << feedIdWhere;
    recountFeedCategories(categoriesList);

    feedParIdWhere = feedIdWhere;
  } else if (feedParIdWhat == feedParIdWhere) {
    // Перемещение между лентами в одной папке
    QList<int> idList;
    q.exec(QString("SELECT id FROM feeds WHERE parentId='%1' ORDER BY rowToParent").
           arg(feedParIdWhat));
    while (q.next()) {
      idList << q.value(0).toInt();
    }

    int rowWhat = feedsTreeModel_->dataField(indexWhat, "rowToParent").toInt();
    int rowWhere = feedsTreeModel_->dataField(indexWhere, "rowToParent").toInt();
    if ((rowWhat < rowWhere) && (how != 1)) rowWhere--;
    else if (how == 1) rowWhere++;
    idList.insert(rowWhere, idList.takeAt(rowWhat));

    for (int i = 0; i < idList.count(); i++) {
      q.exec(QString("UPDATE feeds SET rowToParent='%1' WHERE id=='%2'").
             arg(i).arg(idList.at(i)));
    }
  } else {
    // Перемещение в другую папку рядом с лентами
    QList<int> idList;
    q.exec(QString("SELECT id FROM feeds WHERE parentId='%1' ORDER BY rowToParent").
           arg(feedParIdWhat));
    while (q.next()) {
      if (feedIdWhat != q.value(0).toInt())
        idList << q.value(0).toInt();
    }
    for (int i = 0; i < idList.count(); i++) {
      q.exec(QString("UPDATE feeds SET rowToParent='%1' WHERE id=='%2'").
             arg(i).arg(idList.at(i)));
    }

    //

    idList.clear();
    q.exec(QString("SELECT id FROM feeds WHERE parentId='%1' ORDER BY rowToParent").
           arg(feedParIdWhere));
    while (q.next()) {
      idList << q.value(0).toInt();
    }

    int rowWhere = feedsTreeModel_->dataField(indexWhere, "rowToParent").toInt();
    if (how == 1) rowWhere++;
    idList.insert(rowWhere, feedIdWhat);

    for (int i = 0; i < idList.count(); i++) {
      q.exec(QString("UPDATE feeds SET rowToParent='%1' WHERE id=='%2'").
             arg(i).arg(idList.at(i)));
    }

    q.exec(QString("UPDATE feeds SET parentId='%1' WHERE id=='%2'").
           arg(feedParIdWhere).arg(feedIdWhat));

    QList<int> categoriesList;
    categoriesList << feedParIdWhat << feedParIdWhere;
    recountFeedCategories(categoriesList);
  }

  feedsTreeModel_->refresh();
  expandNodes();

  QModelIndex feedIndex = feedsTreeModel_->getIndexById(feedIdWhat, feedParIdWhere);
  feedsTreeView_->setCurrentIndex(feedIndex);

  feedsTreeView_->setCursor(Qt::ArrowCursor);
}

/**
 * @brief Обработка нажатия в дереве категорий
 * @param item пункт по которому кликаем
 ******************************************************************************/
void RSSListing::slotCategoriesClicked(QTreeWidgetItem *item, int)
{
  int type = item->text(1).toInt();

  int indexTab = -1;
  for (int i = 0; i < stackedWidget_->count(); i++) {
    NewsTabWidget *widget = (NewsTabWidget*)stackedWidget_->widget(i);
    if (widget->type_ == type) {
      indexTab = i;
      break;
    }
  }
  if ((indexTab == -1) || (type == TAB_CAT_LABEL)) {
    if (indexTab == -1) {
      NewsTabWidget *widget = new NewsTabWidget(this, type);
      indexTab = addTab(widget);
    }
    createNewsTab(indexTab);

    //! Устанавливаем иконку и текст для открытой вкладки
    currentNewsTab->newsIconTitle_->setPixmap(item->icon(0).pixmap(16,16));
    currentNewsTab->setTextTab(item->text(0));

    currentNewsTab->labelId_ = item->text(2).toInt();

    switch (type) {
    case TAB_CAT_UNREAD:
      currentNewsTab->categoryFilterStr_ = "feedId > 0 AND deleted = 0 AND read < 2";
      break;
    case TAB_CAT_STAR:
      currentNewsTab->categoryFilterStr_ = "feedId > 0 AND deleted = 0 AND starred = 1";
      break;
    case TAB_CAT_DEL:
      currentNewsTab->categoryFilterStr_ = "feedId > 0 AND deleted = 1";
      break;
    case TAB_CAT_LABEL:
      if (currentNewsTab->labelId_ != 0) {
        currentNewsTab->categoryFilterStr_ =
            QString("feedId > 0 AND deleted = 0 AND label LIKE '%,%1,%'").
            arg(currentNewsTab->labelId_);
      } else {
        currentNewsTab->categoryFilterStr_ =
            QString("feedId > 0 AND deleted = 0 AND label!='' AND label!=','");
      }
      break;
    }
    newsModel_->setFilter(currentNewsTab->categoryFilterStr_);

    if (newsModel_->rowCount() != 0) {
      while (newsModel_->canFetchMore())
        newsModel_->fetchMore();
    }
    if (type == TAB_CAT_DEL){
      currentNewsTab->newsHeader_->setSortIndicator(newsModel_->fieldIndex("deleteDate"),
                                                    Qt::DescendingOrder);
    } else {
      if ((currentNewsTab->newsHeader_->sortIndicatorSection() == newsModel_->fieldIndex("read")) ||
          currentNewsTab->newsHeader_->sortIndicatorSection() == newsModel_->fieldIndex("starred")) {
        currentNewsTab->slotSort(currentNewsTab->newsHeader_->sortIndicatorSection(),
                                 currentNewsTab->newsHeader_->sortIndicatorOrder());
      }
    }

    // Поиск новости ленты, отображамой ранее
    int newsRow = -1;
    if (openingFeedAction_ == 0) {
      int newsIdCur = item->text(3).toInt();
      QModelIndex index = newsModel_->index(0, newsModel_->fieldIndex("id"));
      QModelIndexList indexList = newsModel_->match(index, Qt::EditRole, newsIdCur);

      if (!indexList.isEmpty()) newsRow = indexList.first().row();
    } else if (openingFeedAction_ == 1) {
      newsRow = 0;
    } else if (openingFeedAction_ == 3) {
      QModelIndex index = newsModel_->index(0, newsModel_->fieldIndex("read"));
      QModelIndexList indexList;
      if (newsView_->header()->sortIndicatorOrder() == Qt::DescendingOrder)
        indexList = newsModel_->match(index, Qt::EditRole, 0, -1);
      else
        indexList = newsModel_->match(index, Qt::EditRole, 0);

      if (!indexList.isEmpty()) newsRow = indexList.last().row();
    }

    // Выбор новости ленты, отображамой ранее
    newsView_->setCurrentIndex(newsModel_->index(newsRow, newsModel_->fieldIndex("title")));
    if (newsRow == -1) newsView_->verticalScrollBar()->setValue(newsRow);

    if ((openingFeedAction_ != 2) && openNewsWebViewOn_) {
      currentNewsTab->slotNewsViewSelected(newsModel_->index(newsRow, newsModel_->fieldIndex("title")));
    } else {
      currentNewsTab->slotNewsViewSelected(newsModel_->index(-1, newsModel_->fieldIndex("title")));
    }

    emit signalSetCurrentTab(indexTab);
  } else {
    emit signalSetCurrentTab(indexTab, true);
  }

  QSqlQuery q;
  int allCount = 0;
  QString qStr = QString("SELECT count(id) FROM news WHERE %1").
      arg(currentNewsTab->categoryFilterStr_);

  q.exec(qStr);
  if (q.next()) allCount = q.value(0).toInt();

  statusAll_->setText(QString(" " + tr("All: %1") + " ").arg(allCount));

  statusUnread_->setVisible(false);
  statusAll_->setVisible(true);
}

/**
 * @brief Показ/скрытие дерева категорий
 ******************************************************************************/
void RSSListing::showNewsCategoriesTree()
{
  if (newsCategoriesTree_->isHidden()) {
    showCategoriesButton_->setIcon(QIcon(":/images/images/panel_hide.png"));
    showCategoriesButton_->setToolTip(tr("Hide Categories"));
    newsCategoriesTree_->show();
    feedsSplitter_->restoreState(feedsWidgetSplitterState_);
  } else {
    feedsWidgetSplitterState_ = feedsSplitter_->saveState();
    showCategoriesButton_->setIcon(QIcon(":/images/images/panel_show.png"));
    showCategoriesButton_->setToolTip(tr("Show Categories"));
    newsCategoriesTree_->hide();
    QList <int> sizes;
    sizes << height() << 20;
    feedsSplitter_->setSizes(sizes);
  }
}

/**
 * @brief Перемещение сплитера между деревом лент и деревом категорий
 ******************************************************************************/
void RSSListing::feedsSplitterMoved(int pos, int)
{
  if (newsCategoriesTree_->isHidden()) {
    int height = pos + categoriesPanel_->height() + 2;
    if (height < feedsSplitter_->height()) {
      showCategoriesButton_->setIcon(QIcon(":/images/images/panel_hide.png"));
      showCategoriesButton_->setToolTip(tr("Hide Categories"));
      newsCategoriesTree_->show();
    }
  }
}

/**
 * @brief Установка метки для новости
 ******************************************************************************/
void RSSListing::setLabelNews(QAction *action)
{
  if (currentNewsTab->type_ == TAB_WEB) return;

  newsLabelAction_->setIcon(action->icon());
  newsLabelAction_->setToolTip(action->text());
  newsLabelAction_->setData(action->data());

  currentNewsTab->setLabelNews(action->data().toInt());
}

/**
 * @brief Установка последней выбранной метки для новости
 ******************************************************************************/
void RSSListing::setDefaultLabelNews()
{
  if (currentNewsTab->type_ == TAB_WEB) return;

  currentNewsTab->setLabelNews(newsLabelAction_->data().toInt());
}

/**
 * @brief Получение меток назначенных выбранной новости
 ******************************************************************************/
void RSSListing::getLabelNews()
{
  for (int i = 0; i < newsLabelGroup_->actions().count(); i++) {
    newsLabelGroup_->actions().at(i)->setChecked(false);
  }

  if (currentNewsTab->type_ == TAB_WEB) return;

  QList<QModelIndex> indexes = newsView_->selectionModel()->selectedRows(
        newsModel_->fieldIndex("label"));
  if (!indexes.count()) return;

  if (indexes.count() == 1) {
    QModelIndex index = indexes.at(0);
    QStringList strLabelIdList = index.data(Qt::EditRole).toString().split(",", QString::SkipEmptyParts);
    foreach (QString strLabelId, strLabelIdList) {
      for (int i = 0; i < newsLabelGroup_->actions().count(); i++) {
        if (newsLabelGroup_->actions().at(i)->data().toString() == strLabelId)
          newsLabelGroup_->actions().at(i)->setChecked(true);
      }
    }
  } else {
    for (int i = 0; i < newsLabelGroup_->actions().count(); i++) {
      bool check = false;
      QString strLabelId = newsLabelGroup_->actions().at(i)->data().toString();
      for (int y = indexes.count()-1; y >= 0; --y) {
        QModelIndex index = indexes.at(y);
        QString strIdLabels = index.data(Qt::EditRole).toString();
        if (!strIdLabels.contains(QString(",%1,").arg(strLabelId))) {
          check = false;
          break;
        }
        check = true;
      }
      newsLabelGroup_->actions().at(i)->setChecked(check);
    }
  }
}

/** @brief Вызов контекстного меню вкладки
 *----------------------------------------------------------------------------*/
void RSSListing::showContextMenuTabBar(const QPoint &pos)
{
  int index = tabBar_->tabAt(pos);
  indexClickedTab = index;

  if (index == -1) return;

  QMenu menu;
  menu.addAction(closeTabAct_);
  menu.addAction(closeAllTabAct_);

  menu.exec(tabBar_->mapToGlobal(pos));

  indexClickedTab = -1;
}

/** @brief Закрытие выбранной вкладки
 *----------------------------------------------------------------------------*/
void RSSListing::slotCloseTab()
{
  int index = indexClickedTab;
  if (index == -1) index = tabBar_->currentIndex();

  slotTabCloseRequested(index);
}

/** @brief Закрытие всех вкладок кроме выбранной
 *----------------------------------------------------------------------------*/
void RSSListing::slotCloseAllTabButCurrent()
{
  int index = indexClickedTab;
  if (index == -1) index = tabBar_->currentIndex();

  for (int i = tabBar_->count()-1; i > 0; i--) {
    if (i == index) continue;
    slotTabCloseRequested(i);
  }
}

/** @brief Переключение на следующую вкладку
 *----------------------------------------------------------------------------*/
void RSSListing::slotNextTab()
{
  tabBar_->setCurrentIndex(tabBar_->currentIndex()+1);
}

/** @brief Переключение на предыдущую вкладку
 *----------------------------------------------------------------------------*/
void RSSListing::slotPrevTab()
{
  tabBar_->setCurrentIndex(tabBar_->currentIndex()-1);
}

/** @brief Добавление новой вкладки
 *----------------------------------------------------------------------------*/
int RSSListing::addTab(NewsTabWidget *widget)
{
  if (stackedWidget_->count()) tabBar_->addTab("");
  int indexTab = stackedWidget_->addWidget(widget);
  tabBar_->setTabButton(indexTab,
                        QTabBar::LeftSide,
                        widget->newsTitleLabel_);
  return indexTab;
}

/** @brief Запрос авторизации
 *----------------------------------------------------------------------------*/
void RSSListing::slotAuthentication(QNetworkReply *reply, QAuthenticator *auth)
{
  AuthenticationDialog *authenticationDialog =
      new AuthenticationDialog(this, reply, auth);

  if (!authenticationDialog->save_->isChecked())
    authenticationDialog->exec();

  delete authenticationDialog;
}

void RSSListing::reduceNewsList()
{
  currentNewsTab->reduceNewsList();
}

void RSSListing::increaseNewsList()
{
  currentNewsTab->increaseNewsList();
}

/** @brief Сохранение открытой страницы браузера в файл
 *----------------------------------------------------------------------------*/
void RSSListing::slotSavePageAs()
{
  QString fileName = QFileDialog::getSaveFileName(this, tr("Save As"),
                                                  QDir::homePath(),
                                                  tr("HTML-Files (*.html)")+ ";;" +
                                                  tr("Text files (*.txt)"));
  if (fileName.isNull()) return;

  QFile file(fileName);
  if (!file.open(QIODevice::WriteOnly)) {
    statusBar()->showMessage(tr("Save As: can't open a file"), 3000);
    return;
  }
  QFileInfo fileInfo(fileName);
  if (fileInfo.suffix() == "txt") {
    file.write(currentNewsTab->webView_->page()->mainFrame()->toPlainText().toUtf8());
  } else {
    file.write(currentNewsTab->webView_->page()->mainFrame()->toHtml().toUtf8());
  }
  file.close();
}

/** @brief Получение из БД пароля и логина для авторизации
 * @param url - ссылка на сайт, которому нужна авторизация
 * @param auth - флаг включения авторизации
 *----------------------------------------------------------------------------*/
QString RSSListing::getUserInfo(QUrl url, int auth)
{
  QString userInfo;
  if (auth == 1) {
    QSqlQuery q;
    q.prepare("SELECT username, password FROM passwords WHERE server=?");
    q.addBindValue(url.host());
    q.exec();
    if (q.next()) {
      userInfo = QString("%1:%2").arg(q.value(0).toString()).
          arg(QString::fromUtf8(QByteArray::fromBase64(q.value(1).toByteArray())));
    }
  }
  return userInfo;
}

/** @brief Восстановление последней удалённой новости
 *----------------------------------------------------------------------------*/
void RSSListing::restoreLastNews()
{
  QSqlQuery q;
  q.exec("SELECT id, feedId FROM news WHERE deleted=1 AND deleteDate!='' ORDER BY deleteDate DESC");
  if (q.next()) {
    QModelIndex curIndex = newsView_->currentIndex();
    int newsIdCur = newsModel_->index(curIndex.row(), newsModel_->fieldIndex("id")).data().toInt();

    int newsId = q.value(0).toInt();
    int feedId = q.value(1).toInt();
    q.exec(QString("UPDATE news SET deleted=0, deleteDate='' WHERE id=='%1'").
           arg(newsId));

    newsModel_->select();

    while (newsModel_->canFetchMore())
      newsModel_->fetchMore();

    QModelIndex index = newsModel_->index(0, newsModel_->fieldIndex("id"));
    QModelIndexList indexList = newsModel_->match(index, Qt::EditRole, newsIdCur);
    if (indexList.count()) {
      int newsRow = indexList.first().row();
      newsView_->setCurrentIndex(newsModel_->index(newsRow, newsModel_->fieldIndex("title")));
    }
    slotUpdateStatus(feedId);
  }
}

/** @brief Переход на следующую непрочитанную новость
 *----------------------------------------------------------------------------*/
void RSSListing::nextUnreadNews()
{
  if (currentNewsTab->type_ == TAB_WEB) return;

  int newsRow = currentNewsTab->findUnreadNews(true);

  if (newsRow == -1) {
    QModelIndex indexPrevUnread =
        feedsTreeView_->indexNextUnread(feedsTreeView_->currentIndex(), 1);
    if (indexPrevUnread.isValid()) {
      if (changeBehaviorActionNUN_)
        openingFeedAction_ = 4;
      else
        openingFeedAction_ = 3;
      feedsTreeView_->setCurrentIndex(indexPrevUnread);
      slotFeedClicked(indexPrevUnread);
      openingFeedAction_ = settings_->value("/Settings/openingFeedAction", 0).toInt();
    }
    return;
  }

  int value = newsView_->verticalScrollBar()->value();
  int pageStep = newsView_->verticalScrollBar()->pageStep();
  if (newsRow > (value + pageStep/2))
    newsView_->verticalScrollBar()->setValue(newsRow - pageStep/2);

  QModelIndex index = newsModel_->index(newsRow, newsModel_->fieldIndex("title"));
  newsView_->setCurrentIndex(index);
  currentNewsTab->slotNewsViewSelected(index);
}

/** @brief Переход на предыдущую непрочитанную новость
 *----------------------------------------------------------------------------*/
void RSSListing::prevUnreadNews()
{
  if (currentNewsTab->type_ == TAB_WEB) return;

  int newsRow = currentNewsTab->findUnreadNews(false);

  int newsRowCur = newsView_->currentIndex().row();
  if ((newsRow >= newsRowCur) || (newsRow == -1)) {
    QModelIndex indexNextUnread =
        feedsTreeView_->indexNextUnread(feedsTreeView_->currentIndex(), 2);
    if (indexNextUnread.isValid()) {
      openingFeedAction_ = 3;
      feedsTreeView_->setCurrentIndex(indexNextUnread);
      slotFeedClicked(indexNextUnread);
      openingFeedAction_ = settings_->value("/Settings/openingFeedAction", 0).toInt();
    }
    return;
  }

  int value = newsView_->verticalScrollBar()->value();
  int pageStep = newsView_->verticalScrollBar()->pageStep();
  if (newsRow < (value + pageStep/2))
    newsView_->verticalScrollBar()->setValue(newsRow - pageStep/2);

  QModelIndex index = newsModel_->index(newsRow, newsModel_->fieldIndex("title"));
  newsView_->setCurrentIndex(index);
  currentNewsTab->slotNewsViewSelected(index);
}

/** @brief Получение списка ИД лент находящихся в указанной папке
 *----------------------------------------------------------------------------*/
QList<int> RSSListing::getIdFeedsInList(int idFolder)
{
  QList<int> idList;
  if (idFolder <= 0) return idList;

  QSqlQuery q;
  QQueue<int> parentIds;
  parentIds.enqueue(idFolder);
  while (!parentIds.empty()) {
    int parentId = parentIds.dequeue();
    QString qStr = QString("SELECT id, xmlUrl FROM feeds WHERE parentId='%1'").
        arg(parentId);
    q.exec(qStr);
    while (q.next()) {
      int feedId = q.value(0).toInt();
      if (!q.value(1).toString().isEmpty())
        idList << feedId;
      parentIds.enqueue(feedId);
    }
  }
  return idList;
}

/** @brief Получение строки из ИД лент находящихся в указанной папке
 *----------------------------------------------------------------------------*/
QString RSSListing::getIdFeedsString(int idFolder)
{
  QList<int> idList = getIdFeedsInList(idFolder);
  if (idList.count()) {
    QString str;
    foreach (int id, idList) {
      if (!str.isEmpty()) str.append(" OR ");
      str.append(QString("feedId=%1").arg(id));
    }
    return str;
  } else {
    return QString("feedId=-1");
  }
}

/** @brief Установка текста заголовка программы
 *----------------------------------------------------------------------------*/
void RSSListing::setTextTitle(const QString &text, NewsTabWidget *widget)
{
  if (currentNewsTab != widget) return;

  if (text.isEmpty()) setWindowTitle("QuiteRSS");
  else setWindowTitle(QString("%1 - QuiteRSS").arg(text));
}

/** @brief Включение/отключение отступа в дереве лент
 *----------------------------------------------------------------------------*/
void RSSListing::slotIndentationFeedsTree()
{
  feedsTreeView_->setRootIsDecorated(indentationFeedsTreeAct_->isChecked());
}

/** @brief Настройка главной панели инструментов
 *----------------------------------------------------------------------------*/
void RSSListing::customizeMainToolbar()
{
  showCustomizeToolbarDlg(customizeMainToolbarAct2_);
}

/** @brief Настройка панелей инструментов
 *----------------------------------------------------------------------------*/
void RSSListing::showCustomizeToolbarDlg(QAction *action)
{
  QToolBar *toolbar = mainToolbar_;
  if (action->objectName() == "customizeFeedsToolbarAct") {
    toolbar = feedsToolBar_;
  } else if (action->objectName() == "customizeNewsToolbarAct") {
    toolbar = currentNewsTab->newsToolBar_;
  }

  CustomizeToolbarDialog *toolbarDlg = new CustomizeToolbarDialog(this, toolbar);

  toolbarDlg->exec();

  delete toolbarDlg;
}

/** @brief Поделиться новостью
 *----------------------------------------------------------------------------*/
void RSSListing::slotShareNews(QAction *action)
{
  currentNewsTab->slotShareNews(action);
}

void RSSListing::showMenuShareNews()
{
  if (mainToolbar_->widgetForAction(shareMenuAct_)) {
    QWidget *widget = mainToolbar_->widgetForAction(shareMenuAct_);
    if (widget->underMouse()) {
      shareMenu_->popup(widget->mapToGlobal(QPoint(0, mainToolbar_->height()-1)));
    }
  }
  if (feedsToolBar_->widgetForAction(shareMenuAct_)) {
    QWidget *widget = feedsToolBar_->widgetForAction(shareMenuAct_);
    if (widget->underMouse()) {
      shareMenu_->popup(widget->mapToGlobal(QPoint(0, feedsToolBar_->height()-1)));
    }
  }
  if (currentNewsTab->newsToolBar_->widgetForAction(shareMenuAct_)) {
    QWidget *widget = currentNewsTab->newsToolBar_->widgetForAction(shareMenuAct_);
    if (widget->underMouse()) {
      shareMenu_->popup(widget->mapToGlobal(QPoint(0, currentNewsTab->newsToolBar_->height()-1)));
    }
  }
}

/** @brief Открыть домашнюю страницу ленты во внешнем браузере
 *----------------------------------------------------------------------------*/
void RSSListing::slotOpenHomeFeed()
{
  QModelIndex index = feedsTreeView_->currentIndex();
  if (!index.isValid()) return;

  QString homePage = feedsTreeModel_->dataField(index, "htmlUrl").toString();
  QDesktopServices::openUrl(homePage);
}

/** @brief Отсортировать ленты и папки по имени
 *----------------------------------------------------------------------------*/
void RSSListing::sortedByTitleFeedsTree()
{
  QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

  QList<int> parentIdsPotential;
  parentIdsPotential << 0;
  while (!parentIdsPotential.empty()) {
    int parentId = parentIdsPotential.takeFirst();

    // Ищем детей родителя parentId
    QSqlQuery q;
    q.prepare(QString("SELECT id FROM feeds WHERE parentId=? ORDER BY text"));
    q.addBindValue(parentId);
    q.exec();

    // Каждому ребенку прописываем его rowToParent
    // ... сохраняем его в списке потенциальных родителей
    int rowToParent = 0;
    while (q.next()) {
      int parentIdNew = q.value(0).toInt();

      QSqlQuery q2;
      q2.prepare("UPDATE feeds SET rowToParent=? WHERE id=?");
      q2.addBindValue(rowToParent);
      q2.addBindValue(parentIdNew);
      q2.exec();

      parentIdsPotential << parentIdNew;
      ++rowToParent;
    }
  }

  feedsModelReload();
  QApplication::restoreOverrideCursor();
}
