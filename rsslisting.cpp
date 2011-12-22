#include <QtDebug>
#include <QtCore>
#include <windows.h>
#include <Psapi.h>

#include "addfeeddialog.h"
#include "optionsdialog.h"
#include "rsslisting.h"
#include "VersionNo.h"

/*!****************************************************************************/
const QString kDbName = "feeds.db";

const QString kCreateNewsTableQuery(
    "create table feed_%1("
        "id integer primary key, "
        "guid varchar, "
        "description varchar, "
        "content varchar, "
        "title varchar, "
        "published varchar, "
        "modified varchar, "
        "received varchar, "
        "author varchar, "
        "category varchar, "
        "label varchar, "
        "new integer default 1, "
        "read integer default 0, "
        "sticky integer default 0, "
        "deleted integer default 0, "
        "attachment varchar, "
        "feed varchar, "
        "location varchar, "
        "link varchar"
    ")");

/*!****************************************************************************/
RSSListing::RSSListing(QWidget *parent)
    : QMainWindow(parent)
{
    QString AppFileName = qApp->applicationFilePath();
    AppFileName.replace(".exe", ".ini");
    settings_ = new QSettings(AppFileName, QSettings::IniFormat);

    QString dbFileName(qApp->applicationDirPath() + "/" + kDbName);
    db_ = QSqlDatabase::addDatabase("QSQLITE");
    db_.setDatabaseName(dbFileName);
    if (QFile(dbFileName).exists()) {
      db_.open();
    } else {  // ������������� ����
      db_.open();
      db_.exec("create table feeds(id integer primary key, text varchar,"
          " title varchar, description varchar, xmlurl varchar, "
          "htmlurl varchar, unread integer)");
      db_.exec("insert into feeds(text, xmlurl) "
          "values ('Qt Labs', 'http://labs.qt.nokia.com/blogs/feed')");
      db_.exec("insert into feeds(text, xmlurl) "
          "values ('Qt Russian Forum', 'http://www.prog.org.ru/index.php?type=rss;action=.xml')");
      db_.exec("create table info(id integer primary key, name varchar, value varchar)");
      db_.exec("insert into info(name, value) values ('version', '1.0')");
      db_.exec(kCreateNewsTableQuery.arg(1));
      db_.exec(kCreateNewsTableQuery.arg(2));
    }

    persistentUpdateThread_ = new UpdateThread(this);
    persistentUpdateThread_->setObjectName("persistentUpdateThread_");
    connect(persistentUpdateThread_, SIGNAL(readedXml(QByteArray, QUrl)),
        this, SLOT(receiveXml(QByteArray, QUrl)));
    connect(persistentUpdateThread_, SIGNAL(getUrlDone(int)),
        this, SLOT(getUrlDone(int)));

    persistentParseThread_ = new ParseThread(this, &db_);
    persistentParseThread_->setObjectName("persistentParseThread_");
    connect(this, SIGNAL(xmlReadyParse(QByteArray,QUrl)),
        persistentParseThread_, SLOT(parseXml(QByteArray,QUrl)),
        Qt::QueuedConnection);

    feedsModel_ = new FeedsModel(this);
    feedsModel_->setTable("feeds");
    feedsModel_->select();

    feedsView_ = new QTreeView();
    feedsView_->setObjectName("feedsTreeView_");
    feedsView_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    feedsView_->setModel(feedsModel_);
    feedsView_->header()->setStretchLastSection(false);
    feedsView_->header()->setResizeMode(feedsModel_->fieldIndex("id"), QHeaderView::ResizeToContents);
    feedsView_->header()->setResizeMode(feedsModel_->fieldIndex("text"), QHeaderView::Stretch);
    feedsView_->header()->setResizeMode(feedsModel_->fieldIndex("unread"), QHeaderView::ResizeToContents);

    feedsView_->header()->setVisible(false);
    feedsView_->setUniformRowHeights(true);
    feedsView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    feedsView_->hideColumn(feedsModel_->fieldIndex("title"));
    feedsView_->hideColumn(feedsModel_->fieldIndex("description"));
    feedsView_->hideColumn(feedsModel_->fieldIndex("xmlurl"));
    feedsView_->hideColumn(feedsModel_->fieldIndex("htmlurl"));
    connect(feedsView_, SIGNAL(clicked(QModelIndex)),
            this, SLOT(slotFeedsTreeClicked(QModelIndex)));
    connect(this, SIGNAL(signalFeedsTreeKeyUpDownPressed()),
            SLOT(slotFeedsTreeKeyUpDownPressed()), Qt::QueuedConnection);

    newsModel_ = new NewsModel(this);
    newsModel_->setEditStrategy(QSqlTableModel::OnFieldChange);
    newsView_ = new NewsView(this);
    newsView_->setModel(newsModel_);
    newsView_->model_ = newsModel_;

    newsHeader_ = new NewsHeader(Qt::Horizontal, newsView_);
    newsHeader_->model_ = newsModel_;
    newsHeader_->view_ = newsView_;
    newsView_->setHeader(newsHeader_);

    connect(newsView_, SIGNAL(clicked(QModelIndex)),
            this, SLOT(slotNewsViewClicked(QModelIndex)));
    connect(this, SIGNAL(signalFeedKeyUpDownPressed()),
            SLOT(slotNewsKeyUpDownPressed()), Qt::QueuedConnection);
    connect(newsView_, SIGNAL(signalSetItemRead(QModelIndex, int)),
            this, SLOT(slotSetItemRead(QModelIndex, int)));
    connect(newsView_, SIGNAL(signalDoubleClicked(QModelIndex)),
            this, SLOT(slotNewsViewDoubleClicked(QModelIndex)));

    webView_ = new QWebView();
    webView_->setObjectName("webView_");
    webViewProgress_ = new QProgressBar(this);
    webViewProgress_->setObjectName("webViewProgress_");
    webViewProgress_->setFixedHeight(15);
    webViewProgress_->setMinimum(0);
    webViewProgress_->setMaximum(100);
    webViewProgress_->setFormat(tr("Loading... (%p%)"));
    webViewProgress_->setVisible(true);
    connect(webView_, SIGNAL(loadStarted()), this, SLOT(slotLoadStarted()));
    connect(webView_, SIGNAL(loadFinished(bool)), this, SLOT(slotLoadFinished(bool)));
    connect(webView_, SIGNAL(loadProgress(int)), webViewProgress_, SLOT(setValue(int)));

    setContextMenuPolicy(Qt::CustomContextMenu);

    //! Create feeds DockWidget
    setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
    setCorner( Qt::TopRightCorner, Qt::RightDockWidgetArea );
    setDockOptions(QMainWindow::AnimatedDocks|QMainWindow::AllowNestedDocks);

    feedsDock_ = new QDockWidget(tr("Feeds"), this);
    feedsDock_->setObjectName("feedsDock");
    feedsDock_->setAllowedAreas(Qt::LeftDockWidgetArea|Qt::RightDockWidgetArea|Qt::TopDockWidgetArea);
    feedsDock_->setWidget(feedsView_);
    feedsDock_->setFeatures(QDockWidget::DockWidgetMovable);
    addDockWidget(Qt::LeftDockWidgetArea, feedsDock_);
    connect(feedsDock_, SIGNAL(dockLocationChanged(Qt::DockWidgetArea)),
        this, SLOT(slotFeedsDockLocationChanged(Qt::DockWidgetArea)));

    toolBarNull_ = new QToolBar(this);
    toolBarNull_->setObjectName("toolBarNull");
    toolBarNull_->setMovable(false);
    toolBarNull_->setFixedWidth(6);
    addToolBar(Qt::LeftToolBarArea, toolBarNull_);

    pushButtonNull_ = new QPushButton(this);
    pushButtonNull_->setObjectName("pushButtonNull");
    pushButtonNull_->setIcon(QIcon(":/images/images/triangleL.png"));
    pushButtonNull_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    toolBarNull_->addWidget(pushButtonNull_);
    connect(pushButtonNull_, SIGNAL(clicked()), this, SLOT(slotVisibledFeedsDock()));

    //! Create news DockWidget
    QSplitter *newsSplitter = new QSplitter(Qt::Vertical);
    newsSplitter->addWidget(newsView_);

    newsDock_ = new QDockWidget(tr("News"), this);
    newsDock_->setObjectName("newsDock");
    newsDock_->setFeatures(QDockWidget::DockWidgetMovable);
    newsDock_->setWidget(newsSplitter);
    addDockWidget(Qt::TopDockWidgetArea, newsDock_);
    connect(newsDock_, SIGNAL(dockLocationChanged(Qt::DockWidgetArea)),
        this, SLOT(slotNewsDockLocationChanged(Qt::DockWidgetArea)));

    //! Create web layout
    QVBoxLayout *webLayout = new QVBoxLayout();
    webLayout->setMargin(1);  // ����� ���� ����� ������� �������
    webLayout->setSpacing(0);
    webLayout->addWidget(webView_);
    webLayout->addWidget(webViewProgress_);

    webWidget_ = new QWidget();
    webWidget_->setObjectName("webWidget_");
    webWidget_->setLayout(webLayout);

    setCentralWidget(webWidget_);

    setWindowTitle(QString("QtRSS v") + QString(STRFILEVER).section('.', 0, 2));

    createActions();
    createMenu();
    createToolBar();

    feedsView_->installEventFilter(this);
    newsView_->installEventFilter(this);
    toolBarNull_->installEventFilter(this);

    //! GIU tuning
    progressBar_ = new QProgressBar();
    progressBar_->setObjectName("progressBar_");
    progressBar_->setFixedWidth(200);
    progressBar_->setFixedHeight(15);
    progressBar_->setMinimum(0);
    progressBar_->setFormat(tr("Update feeds... (%p%)"));
    progressBar_->setVisible(false);
    statusBar()->addPermanentWidget(progressBar_);
    statusUnread_ = new QLabel(tr(" Unread: "));
    statusBar()->addPermanentWidget(statusUnread_);
    statusAll_ = new QLabel(tr(" All: "));
    statusBar()->addPermanentWidget(statusAll_);
    statusBar()->setVisible(true);

    //! testing
    webView_->load(QUrl("qrc:/html/test1.html"));
    webView_->show();

    traySystem = new QSystemTrayIcon(QIcon(":/images/images/QtRSS16.png"),this);
    connect(traySystem,SIGNAL(activated(QSystemTrayIcon::ActivationReason)),
            this, SLOT(slotActivationTray(QSystemTrayIcon::ActivationReason)));
    connect(this,SIGNAL(signalPlaceToTray()),this,SLOT(slotPlaceToTray()),Qt::QueuedConnection);
    traySystem->setToolTip("QtRSS");
    createTrayMenu();
    traySystem->show();

    connect(this, SIGNAL(signalCloseApp()),
            SLOT(slotCloseApp()), Qt::QueuedConnection);
    connect(feedsView_, SIGNAL(doubleClicked(QModelIndex)),
            updateFeedAct_, SLOT(trigger()));

    feedsView_->setCurrentIndex(feedsModel_->index(0, 0));
    slotFeedsTreeClicked(feedsModel_->index(0, 0));  // �������� ��������

    readSettings();
    newsHeader_->createMenu();

    //��������� ������� � �� �������� ��� ���������
    QFont font_ = newsDock_->font();
    font_.setBold(true);
    newsDock_->setFont(font_);
}

/*!****************************************************************************/
RSSListing::~RSSListing()
{
  qDebug("App_Closing");
  persistentUpdateThread_->quit();
  persistentParseThread_->quit();

  delete newsView_;
  delete feedsView_;
  delete newsModel_;
  delete feedsModel_;

  db_.close();

  QSqlDatabase::removeDatabase(QString());
}

/*virtual*/ void RSSListing::showEvent(QShowEvent* event)
{
  connect(feedsDock_, SIGNAL(dockLocationChanged(Qt::DockWidgetArea)),
          this, SLOT(slotDockLocationChanged(Qt::DockWidgetArea)), Qt::UniqueConnection);
}

/*!****************************************************************************/
bool RSSListing::eventFilter(QObject *obj, QEvent *event)
{
  if (obj == feedsView_) {
    if (event->type() == QEvent::KeyPress) {
      QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);
      if ((keyEvent->key() == Qt::Key_Up) ||
          (keyEvent->key() == Qt::Key_Down)) {
        emit signalFeedsTreeKeyUpDownPressed();
      }
      return false;
    } else {
      return false;
    }
  } else if (obj == newsView_) {
    if (event->type() == QEvent::KeyPress) {
      QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);
      if ((keyEvent->key() == Qt::Key_Up) ||
          (keyEvent->key() == Qt::Key_Down)) {
        emit signalFeedKeyUpDownPressed();
      }
      return false;
    } else {
      return false;
    }
  } else if (obj == toolBarNull_) {
    if (event->type() == QEvent::MouseButtonRelease) {
      slotVisibledFeedsDock();
    }
    return false;
  } else {
    // pass the event on to the parent class
    return QMainWindow::eventFilter(obj, event);
  }
}

/*! \brief ��������� ������� �������� ���� ************************************/
/*virtual*/ void RSSListing::closeEvent(QCloseEvent* event)
{
  oldState = windowState();
  event->ignore();
  emit signalPlaceToTray();
}

/*! \brief ��������� ������� ������ �� ���������� *****************************/
void RSSListing::slotClose()
{
  traySystem->hide();
  writeSettings();
  emit signalCloseApp();
}

/*! \brief ���������� ���������� **********************************************/
void RSSListing::slotCloseApp()
{
  qApp->quit();
}

/*! \brief ��������� ������� ��������� ��������� ���� *************************/
/*virtual*/ void RSSListing::changeEvent(QEvent *event)
{
  if(event->type() == QEvent::WindowStateChange) {
    oldState = ((QWindowStateChangeEvent*)event)->oldState();
    if(isMinimized()) {
      event->ignore();
      emit signalPlaceToTray();
      return;
    }
  }
  QMainWindow::changeEvent(event);
}

/*! \brief ��������� ������� ��������� ��������� � ���� ***********************/
void RSSListing::slotPlaceToTray()
{
  QTimer::singleShot(10000, this, SLOT(myEmptyWorkingSet()));
  hide();
}

/*! \brief ��������� ������� ���� *********************************************/
void RSSListing::slotActivationTray(QSystemTrayIcon::ActivationReason reason)
{
  switch (reason) {
  case QSystemTrayIcon::Unknown:
    break;
  case QSystemTrayIcon::Context:
    trayMenu_->activateWindow();
    break;
  case QSystemTrayIcon::DoubleClick:
    slotShowWindows();
    break;
  case QSystemTrayIcon::Trigger:
    break;
  case QSystemTrayIcon::MiddleClick:
    break;
  }
}

/*! \brief ����������� ���� �� ������� ****************************************/
void RSSListing::slotShowWindows()
{
  if (oldState == Qt::WindowMaximized) {
    showMaximized();
  } else {
    showNormal();
  }
  activateWindow();
}

/*! \brief �������� �������� **************************************************
 * \details ������� ����� �������������� � ������� ���� � ToolBar
 ******************************************************************************/
void RSSListing::createActions()
{
  addFeedAct_ = new QAction(QIcon(":/images/addFeed"), tr("&Add..."), this);
  addFeedAct_->setStatusTip(tr("Add new feed"));
  addFeedAct_->setShortcut(QKeySequence::New);
  connect(addFeedAct_, SIGNAL(triggered()), this, SLOT(addFeed()));

  deleteFeedAct_ = new QAction(QIcon(":/images/deleteFeed"), tr("&Delete..."), this);
  deleteFeedAct_->setStatusTip(tr("Delete selected feed"));
  connect(deleteFeedAct_, SIGNAL(triggered()), this, SLOT(deleteFeed()));

  importFeedsAct_ = new QAction(tr("&Import feeds..."), this);
  importFeedsAct_->setStatusTip(tr("Import feeds from OPML file"));
  connect(importFeedsAct_, SIGNAL(triggered()), this, SLOT(importFeeds()));

  exitAct_ = new QAction(tr("E&xit"), this);
  exitAct_->setShortcut(Qt::CTRL+Qt::Key_Q);  // standart on other OS
  connect(exitAct_, SIGNAL(triggered()), this, SLOT(slotClose()));

  toolBarToggle_ = new QAction(tr("&ToolBar"), this);
  toolBarToggle_->setCheckable(true);
  toolBarToggle_->setStatusTip(tr("Show ToolBar"));

  updateFeedAct_ = new QAction(QIcon(":/images/updateFeed"), tr("Update"), this);
  updateFeedAct_->setStatusTip(tr("Update current feed"));
  updateFeedAct_->setShortcut(Qt::Key_F5);
  connect(updateFeedAct_, SIGNAL(triggered()), this, SLOT(slotGetFeed()));

  updateAllFeedsAct_ = new QAction(QIcon(":/images/updateAllFeeds"), tr("Update all"), this);
  updateAllFeedsAct_->setStatusTip(tr("Update all feeds"));
  updateAllFeedsAct_->setShortcut(Qt::CTRL + Qt::Key_F5);
  connect(updateAllFeedsAct_, SIGNAL(triggered()), this, SLOT(slotGetAllFeeds()));

  markNewsRead_ = new QAction(QIcon(":/images/markRead"), tr("Mark Read"), this);
  markNewsRead_->setStatusTip(tr("Mark current news read"));
  connect(markNewsRead_, SIGNAL(triggered()), this, SLOT(markNewsRead()));

  markAllNewsRead_ = new QAction(QIcon(":/images/markReadAll"), tr("Mark all news Read"), this);
  markAllNewsRead_->setStatusTip(tr("Mark all news read"));
  connect(markAllNewsRead_, SIGNAL(triggered()), this, SLOT(markAllNewsRead()));

  optionsAct_ = new QAction(QIcon(":/images/options"), tr("Options..."), this);
  optionsAct_->setStatusTip(tr("Open options gialog"));
  optionsAct_->setShortcut(Qt::Key_F8);
  connect(optionsAct_, SIGNAL(triggered()), this, SLOT(showOptionDlg()));

  filterFeedsAll_ = new QAction(tr("Show All"), this);
  filterFeedsAll_->setObjectName("filterFeedsAll_");
  filterFeedsAll_->setStatusTip(tr("Show All"));
  filterFeedsAll_->setCheckable(true);
  filterFeedsAll_->setChecked(true);
  filterFeedsUnread_ = new QAction(tr("Show Unread"), this);
  filterFeedsUnread_->setObjectName("filterFeedsUnread_");
  filterFeedsUnread_->setStatusTip(tr("Show Unread"));
  filterFeedsUnread_->setCheckable(true);

  filterNewsAll_ = new QAction(tr("Show All"), this);
  filterNewsAll_->setObjectName("filterNewsAll_");
  filterNewsAll_->setStatusTip(tr("Show All"));
  filterNewsAll_->setCheckable(true);
  filterNewsAll_->setChecked(true);
  filterNewsUnread_ = new QAction(tr("Show Unread"), this);
  filterNewsUnread_->setObjectName("filterNewsUnread_");
  filterNewsUnread_->setStatusTip(tr("Show Unread"));
  filterNewsUnread_->setCheckable(true);

}

/*! \brief �������� �������� ���� *********************************************/
void RSSListing::createMenu()
{
  fileMenu_ = menuBar()->addMenu(tr("&File"));
  fileMenu_->addAction(addFeedAct_);
  fileMenu_->addAction(deleteFeedAct_);
  fileMenu_->addSeparator();
  fileMenu_->addAction(importFeedsAct_);
  fileMenu_->addSeparator();
  fileMenu_->addAction(exitAct_);

  menuBar()->addMenu(tr("&Edit"));

  viewMenu_ = menuBar()->addMenu(tr("&View"));
  viewMenu_->addAction(toolBarToggle_);

  feedMenu_ = menuBar()->addMenu(tr("Fee&ds"));
  feedMenu_->addAction(updateFeedAct_);
  feedMenu_->addAction(updateAllFeedsAct_);
  feedMenu_->addSeparator();

  feedsFilterGroup_ = new QActionGroup(this);
  feedsFilterGroup_->setExclusive(true);
  connect(feedsFilterGroup_, SIGNAL(triggered(QAction*)), this, SLOT(setFeedsFilter(QAction*)));

  QMenu *feedsFilter = feedMenu_->addMenu(QIcon(":/images/filter"), tr("Filter"));
  feedsFilter->addAction(filterFeedsAll_);
  feedsFilterGroup_->addAction(filterFeedsAll_);
  feedsFilter->addAction(filterFeedsUnread_);
  feedsFilterGroup_->addAction(filterFeedsUnread_);

  newsMenu_ = menuBar()->addMenu(tr("&News"));
  newsMenu_->addAction(markNewsRead_);
  newsMenu_->addAction(markAllNewsRead_);
  newsMenu_->addSeparator();

  newsFilterGroup_ = new QActionGroup(this);
  newsFilterGroup_->setExclusive(true);
  connect(newsFilterGroup_, SIGNAL(triggered(QAction*)), this, SLOT(setNewsFilter(QAction*)));

  QMenu *newsFilter = newsMenu_->addMenu(QIcon(":/images/filter"), tr("Filter"));
  newsFilter->addAction(filterNewsAll_);
  newsFilterGroup_->addAction(filterNewsAll_);
  newsFilter->addAction(filterNewsUnread_);
  newsFilterGroup_->addAction(filterNewsUnread_);

  toolsMenu_ = menuBar()->addMenu(tr("&Tools"));
  toolsMenu_->addAction(optionsAct_);

  menuBar()->addMenu(tr("&Help"));
}

/*! \brief �������� ToolBar ***************************************************/
void RSSListing::createToolBar()
{
  toolBar_ = addToolBar(tr("ToolBar"));
  toolBar_->setObjectName("ToolBar_General");
  toolBar_->setAllowedAreas(Qt::TopToolBarArea);
  toolBar_->setMovable(false);
  toolBar_->addAction(addFeedAct_);
  toolBar_->addAction(deleteFeedAct_);
  toolBar_->addSeparator();
  toolBar_->addAction(updateFeedAct_);
  toolBar_->addAction(updateAllFeedsAct_);
  toolBar_->addSeparator();
  toolBar_->addAction(markNewsRead_);
  toolBar_->addAction(markAllNewsRead_);
  toolBar_->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
  connect(toolBar_, SIGNAL(visibilityChanged(bool)), toolBarToggle_, SLOT(setChecked(bool)));
  connect(toolBarToggle_, SIGNAL(toggled(bool)), toolBar_, SLOT(setVisible(bool)));
}

/*! \brief ������ �������� �� ini-����� ***************************************/
void RSSListing::readSettings()
{
  settings_->beginGroup("/Settings");

  QString fontFamily = settings_->value("/FontFamily", "Tahoma").toString();
  int fontSize = settings_->value("/FontSize", 8).toInt();
  qApp->setFont(QFont(fontFamily, fontSize));

  settings_->endGroup();

  restoreGeometry(settings_->value("GeometryState").toByteArray());
  restoreState(settings_->value("ToolBarsState").toByteArray());
  newsHeader_->restoreGeometry(settings_->value("NewsViewGeometry").toByteArray());
  newsHeader_->restoreState(settings_->value("NewsViewState").toByteArray());

  setProxyAct_->setChecked(settings_->value("networkProxy/Enabled", false).toBool());
  networkProxy_.setType(static_cast<QNetworkProxy::ProxyType>
      (settings_->value("networkProxy/type", QNetworkProxy::HttpProxy).toInt()));
  networkProxy_.setHostName(settings_->value("networkProxy/hostName", "10.0.0.172").toString());
  networkProxy_.setPort(    settings_->value("networkProxy/port",     3150).toUInt());
  networkProxy_.setUser(    settings_->value("networkProxy/user",     "").toString());
  networkProxy_.setPassword(settings_->value("networkProxy/password", "").toString());
  slotSetProxy();
}

/*! \brief ������ �������� � ini-���� *****************************************/
void RSSListing::writeSettings()
{
  settings_->beginGroup("/Settings");

  QString strLocalLang = QLocale::system().name();
  QString lang = settings_->value("/Lang", strLocalLang).toString();
  settings_->setValue("/Lang", lang);

  QString fontFamily = settings_->value("/FontFamily", "Tahoma").toString();
  settings_->setValue("/FontFamily", fontFamily);
  int fontSize = settings_->value("/FontSize", 8).toInt();
  settings_->setValue("/FontSize", fontSize);

  settings_->endGroup();

  settings_->setValue("GeometryState", saveGeometry());
  settings_->setValue("ToolBarsState", saveState());
  if (newsModel_->columnCount()) {
    settings_->setValue("NewsViewGeometry", newsHeader_->saveGeometry());
    settings_->setValue("NewsViewState", newsHeader_->saveState());
  }

  settings_->setValue("networkProxy/Enabled",  setProxyAct_->isChecked());
  settings_->setValue("networkProxy/type",     networkProxy_.type());
  settings_->setValue("networkProxy/hostName", networkProxy_.hostName());
  settings_->setValue("networkProxy/port",     networkProxy_.port());
  settings_->setValue("networkProxy/user",     networkProxy_.user());
  settings_->setValue("networkProxy/password", networkProxy_.password());
}

/*! \brief ���������� ����� � ������ ���� *************************************/
void RSSListing::addFeed()
{
  AddFeedDialog *addFeedDialog = new AddFeedDialog(this);
  addFeedDialog->setWindowTitle(tr("Add feed"));
  if (addFeedDialog->exec() == QDialog::Rejected) return;

  QSqlQuery q(db_);
  QString qStr = QString("insert into feeds(text, xmlurl) values (?, ?)");
  q.prepare(qStr);
  q.addBindValue(addFeedDialog->feedTitleEdit_->text());
  q.addBindValue(addFeedDialog->feedUrlEdit_->text());
  q.exec();
  q.exec(kCreateNewsTableQuery.arg(q.lastInsertId().toString()));
  q.finish();

  QModelIndex index = feedsView_->currentIndex();
  feedsModel_->select();
  feedsView_->setCurrentIndex(index);
}

/*! \brief �������� ����� �� ������ ���� � �������������� *********************/
void RSSListing::deleteFeed()
{
  if (feedsView_->currentIndex().row() >= 0) {
    QMessageBox msgBox;
    msgBox.setIcon(QMessageBox::Question);
    msgBox.setWindowTitle(tr("Delete feed"));
    msgBox.setText(QString(tr("Are you sure to delete the feed '%1'?")).
                   arg(feedsModel_->record(feedsView_->currentIndex().row()).field("text").value().toString()));
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::No);

    if (msgBox.exec() == QMessageBox::No) return;

    QSqlQuery q(db_);
    QString str = QString("delete from feeds where text='%1'").
        arg(feedsModel_->record(feedsView_->currentIndex().row()).field("text").value().toString());
    q.exec(str);
    q.exec(QString("drop table feed_%1").
        arg(feedsModel_->record(feedsView_->currentIndex().row()).field("id").value().toString()));
    q.finish();

    int row = feedsView_->currentIndex().row();
    feedsModel_->select();
    if (feedsModel_->rowCount() == row) row--;
    feedsView_->setCurrentIndex(feedsModel_->index(row, 0));
  }
}

/*! \brief ������ ���� �� OPML-����� ******************************************/
void RSSListing::importFeeds()
{
  QString fileName = QFileDialog::getOpenFileName(this, tr("Select OPML-file"),
      qApp->applicationDirPath(),
      tr("OPML-files (*.opml)"));

  if (fileName.isNull()) {
    statusBar()->showMessage(tr("Import canceled"), 3000);
    return;
  }

  qDebug() << "process file:" << fileName;

  QFile file(fileName);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    statusBar()->showMessage(tr("Import: can't open a file"), 3000);
    return;
  }

  db_.transaction();

  QXmlStreamReader xml(&file);

  int elementCount = 0;
  int outlineCount = 0;
  while (!xml.atEnd()) {
    xml.readNext();
    if (xml.isStartElement()) {
      statusBar()->showMessage(QVariant(elementCount).toString(), 3000);
      // �������� ���� outline'�
      if (xml.name() == "outline") {
        qDebug() << outlineCount << "+:" << xml.prefix().toString()
            << ":" << xml.name().toString();;
        QSqlQuery q(db_);
        QString qStr = QString("insert into feeds(text, title, description, xmlurl, htmlurl) "
                       "values(?, ?, ?, ?, ?)");
        q.prepare(qStr);
        q.addBindValue(xml.attributes().value("text").toString());
        q.addBindValue(xml.attributes().value("title").toString());
        q.addBindValue(xml.attributes().value("description").toString());
        q.addBindValue(xml.attributes().value("xmlUrl").toString());
        q.addBindValue(xml.attributes().value("htmlUrl").toString());
        q.exec();
        qDebug() << q.lastError().number() << ": " << q.lastError().text();
        q.exec(kCreateNewsTableQuery.arg(q.lastInsertId().toString()));
        q.finish();
      }
    } else if (xml.isEndElement()) {
      if (xml.name() == "outline") {
        ++outlineCount;
      }
      ++elementCount;
    }
  }
  if (xml.error()) {
    statusBar()->showMessage(QString("Import error: Line=%1, ErrorString=%2").
        arg(xml.lineNumber()).arg(xml.errorString()), 3000);
  } else {
    statusBar()->showMessage(QString("Import: file read done"), 3000);
  }
  db_.commit();

  QModelIndex index = feedsView_->currentIndex();
  feedsModel_->select();
  feedsView_->setCurrentIndex(index);
}

/*! \brief ���� xml-����� ****************************************************/
void RSSListing::receiveXml(const QByteArray &data, const QUrl &url)
{
  url_ = url;
  data_.append(data);
}

/*! \brief ��������� ��������� ������� ****************************************/
void RSSListing::getUrlDone(const int &result)
{
  qDebug() << "getUrl result =" << result;

  if (!url_.isEmpty()) {
    qDebug() << "emit xmlReadyParse: before <<" << url_;
//    persistentParseThread_->parseXml(data_, url_);
    emit xmlReadyParse(data_, url_);
    qDebug() << "emit xmlReadyParse: after  <<" << url_;
    data_.clear();
    url_.clear();
  }

  // ������� �������� �����
  if (0 == result) {
    updateAllFeedsAct_->setEnabled(true);
    progressBar_->hide();
    statusBar()->showMessage(QString("Update done"), 3000);
  }
  // � ������� �������� �������� _result_ ��������
  else if (0 < result) {
    progressBar_->setValue(progressBar_->maximum() - result);
  }
}

void RSSListing::slotUpdateFeed(const QUrl &url)
{
  // ����� �������������� ����� � ������� ����
  int parseFeedId = 0;
  QSqlQuery q(db_);
  q.exec(QString("select id from feeds where xmlurl like '%1'").
      arg(url.toString()));
  while (q.next()) {
    parseFeedId = q.value(q.record().indexOf("id")).toInt();
  }

  int unreadCount = 0;
  QString qStr = QString("select count(read) from feed_%1 where read==0").
      arg(parseFeedId);
  q.exec(qStr);
  if (q.next()) unreadCount = q.value(0).toInt();

  qStr = QString("update feeds set unread='%1' where id=='%2'").
      arg(unreadCount).arg(parseFeedId);
  q.exec(qStr);

  QModelIndex index = feedsView_->currentIndex();

  // ���� ��������� ��������������� �����, ������� �� ���
  if (parseFeedId ==
      feedsModel_->index(index.row(), feedsModel_->fieldIndex("id")).data().toInt()) {
    slotFeedsTreeClicked(feedsModel_->index(index.row(), 0));
  }
  // ����� ��������� ������ ����
  else {
    feedsModel_->select();
    feedsView_->setCurrentIndex(index);
  }
}

/*! \brief ��������� ������� � ������ ���� ************************************/
void RSSListing::slotFeedsTreeClicked(QModelIndex index)
{
  bool initNo = false;
  if (!newsModel_->columnCount()) initNo = true;
  newsModel_->setTable(QString("feed_%1").arg(feedsModel_->index(index.row(), 0).data().toString()));
  newsModel_->select();
  setNewsFilter(newsFilterGroup_->checkedAction());

  newsHeader_->overload();

  if (initNo) {
    newsHeader_->initColumns();
    newsHeader_->createMenu();
  }

//  newsView_->setCurrentIndex(newsModel_->index(-1, 0));
  slotNewsViewClicked(newsModel_->index(-1, 0));

  newsDock_->setWindowTitle(feedsModel_->index(index.row(), 1).data().toString());
}

/*! \brief ������ ���������� ����� ********************************************/
void RSSListing::getFeed(QString urlString)
{
  persistentUpdateThread_->getUrl(urlString);

  progressBar_->setValue(progressBar_->minimum());
  progressBar_->show();
  QTimer::singleShot(150, this, SLOT(slotProgressBarUpdate()));
}

/*! \brief ��������� ������� � ������ �������� ********************************/
void RSSListing::slotNewsViewClicked(QModelIndex index)
{
  static QModelIndex indexOld = index;
  if (!index.isValid()) {
    webView_->setHtml("");
    slotUpdateStatus();
    return;
  }
  QModelIndex indexNew = index;
  if (!((index.row() == indexOld.row()) &&
         newsModel_->index(index.row(), newsModel_->fieldIndex("read")).data(Qt::EditRole).toInt() == 1)) {
    QString content = newsModel_->record(index.row()).field("content").value().toString();
    if (content.isEmpty())
      webView_->setHtml(
            newsModel_->record(index.row()).field("description").value().toString());
    else
      webView_->setHtml(content);
    if (newsView_->currentIndex().row() != indexOld.row()) {
      slotSetItemRead(indexOld, 1);
    }
    slotSetItemRead(index, 1);
  }
  indexOld = indexNew;
}

/*! \brief ��������� ������ Up/Down � ������ ���� *****************************/
void RSSListing::slotFeedsTreeKeyUpDownPressed()
{
  slotFeedsTreeClicked(feedsView_->currentIndex());
}

/*! \brief ��������� ������ Up/Down � ������ �������� *************************/
void RSSListing::slotNewsKeyUpDownPressed()
{
  slotNewsViewClicked(newsView_->currentIndex());
}

/*! \brief ����� ���� �������� ************************************************/
void RSSListing::showOptionDlg()
{
  OptionsDialog *optionsDialog = new OptionsDialog(this);
  optionsDialog->setWindowTitle(tr("Options"));
  optionsDialog->restoreGeometry(settings_->value("options/geometry").toByteArray());
  optionsDialog->setProxy(networkProxy_);
  int result = optionsDialog->exec();
  settings_->setValue("options/geometry", optionsDialog->saveGeometry());

  if (result == QDialog::Rejected) return;

  networkProxy_ = optionsDialog->proxy();
  setProxyAct_->setChecked(networkProxy_.type() == QNetworkProxy::HttpProxy);
}

/*! \brief ��������� ��������� ���������� �� ���������� ����� ��������� *******/
void RSSListing::receiveMessage(const QString& message)
{
  qDebug() << QString("Received message: '%1'").arg(message);
  if (!message.isEmpty()){
    QStringList params = message.split('\n');
    foreach (QString param, params) {
      if (param == "--show") slotShowWindows();
      if (param == "--exit") slotClose();
    }
  }
}

/*! \brief �������� ���� ���� *************************************************/
void RSSListing::createTrayMenu()
{
  trayMenu_ = new QMenu(this);
  QAction *showWindowAct_ = new QAction(tr("Show window"), this);
  connect(showWindowAct_, SIGNAL(triggered()), this, SLOT(slotShowWindows()));
  QFont font_ = showWindowAct_->font();
  font_.setBold(true);
  showWindowAct_->setFont(font_);
  trayMenu_->addAction(showWindowAct_);
  trayMenu_->addAction(updateAllFeedsAct_);
  trayMenu_->addSeparator();

  setProxyAct_ = new QAction(this);
  setProxyAct_->setText(tr("Proxy enabled"));
  setProxyAct_->setCheckable(true);
  setProxyAct_->setChecked(false);
  connect(setProxyAct_, SIGNAL(toggled(bool)), this, SLOT(slotSetProxy()));
  trayMenu_->addAction(setProxyAct_);
  trayMenu_->addSeparator();

  trayMenu_->addAction(exitAct_);
  traySystem->setContextMenu(trayMenu_);
//  QNetworkProxy::NoProxy
}

/*! \brief ������������ ������ ************************************************/
void RSSListing::myEmptyWorkingSet()
{
  EmptyWorkingSet(GetCurrentProcess());
}

/*! \brief ��������� ������������ ������������� ������ ������� ****************/
void RSSListing::slotSetProxy()
{
  bool on = setProxyAct_->isChecked();
  if (on) {
    networkProxy_.setType(QNetworkProxy::HttpProxy);
  } else {
    networkProxy_.setType(QNetworkProxy::NoProxy);
  }
  persistentUpdateThread_->setProxy(networkProxy_);
}

/*! \brief ���������� ����� (��������) ****************************************/
void RSSListing::slotGetFeed()
{
  progressBar_->setMaximum(1);
  getFeed(feedsModel_->record(feedsView_->currentIndex().row()).
      field("xmlurl").value().toString());
}

/*! \brief ���������� ����� (��������) ****************************************/
void RSSListing::slotGetAllFeeds()
{
  updateAllFeedsAct_->setEnabled(false);

  int feedCount = 0;

  QSqlQuery q(db_);
  q.exec("select xmlurl from feeds where xmlurl is not null");
  while (q.next()) {
    getFeed(q.record().value(0).toString());
    ++feedCount;
  }

  if (0 == feedCount) {
    updateAllFeedsAct_->setEnabled(true);
    return;
  }

  progressBar_->setMaximum(feedCount-1);
}

void RSSListing::slotProgressBarUpdate()
{
  progressBar_->update();

  if (progressBar_->isVisible())
    QTimer::singleShot(150, this, SLOT(slotProgressBarUpdate()));
}

void RSSListing::slotVisibledFeedsDock()
{
  feedsDock_->setVisible(!feedsDock_->isVisible());
  if (newsDockArea_ == feedsDockArea_)
    newsDock_->setVisible(feedsDock_->isVisible());
}

void RSSListing::slotDockLocationChanged(Qt::DockWidgetArea area)
{
  if (area == Qt::LeftDockWidgetArea) {
    pushButtonNull_->setIcon(QIcon(":/images/images/triangleL.png"));
    toolBarNull_->show();
    addToolBar(Qt::LeftToolBarArea, toolBarNull_);
  } else if (area == Qt::RightDockWidgetArea) {
    toolBarNull_->show();
    pushButtonNull_->setIcon(QIcon(":/images/images/triangleR.png"));
    addToolBar(Qt::RightToolBarArea, toolBarNull_);
  } else {
    toolBarNull_->hide();
  }
}

void RSSListing::slotSetItemRead(QModelIndex index, int read)
{
  if (!index.isValid()) return;

  int readOld = newsModel_->index(index.row(), newsModel_->fieldIndex("read")).data(Qt::EditRole).toInt();
  QModelIndex curIndex = newsView_->currentIndex();
  if ((curIndex != index) && (read == 1)) read = 2;
  newsModel_->setData(
      newsModel_->index(index.row(), newsModel_->fieldIndex("read")),
      read);
  newsView_->setCurrentIndex(curIndex);
  slotUpdateStatus();
  qDebug() << index.row() << curIndex.row() << readOld << read;
}

void RSSListing::markNewsRead()
{
  QModelIndex index = newsView_->currentIndex();
  if (newsModel_->index(index.row(), newsModel_->fieldIndex("read")).data(Qt::EditRole).toInt() == 0) {
    slotSetItemRead(index, 1);
  } else {
    slotSetItemRead(index, 0);
  }
}

void RSSListing::markAllNewsRead()
{
  QString qStr = QString("update %1 set read=1").
      arg(newsModel_->tableName());
  QSqlQuery q(db_);
  q.exec(qStr);
  newsModel_->select();
  setNewsFilter(newsFilterGroup_->checkedAction());
  slotUpdateStatus();
}

void RSSListing::slotUpdateStatus()
{
  int allCount = 0;
  QString qStr = QString("select count(id) from %1").
      arg(newsModel_->tableName());
  QSqlQuery q(db_);
  q.exec(qStr);
  if (q.next()) allCount = q.value(0).toInt();

  int unreadCount = 0;
  qStr = QString("select count(read) from %1 where read==0").
      arg(newsModel_->tableName());
  q.exec(qStr);
  if (q.next()) unreadCount = q.value(0).toInt();

  qStr = QString("update feeds set unread='%1' where id=='%2'").
      arg(unreadCount).arg(newsModel_->tableName().remove("feed_"));
  q.exec(qStr);

  QModelIndex index = feedsView_->currentIndex();
  feedsModel_->select();
  feedsView_->setCurrentIndex(index);

  statusUnread_->setText(tr(" Unread: ") + QString::number(unreadCount) + " ");

  statusAll_->setText(tr(" All: ") + QString::number(allCount) + " ");

//  static int updateCount = 0;
//  qDebug() << "updateStatus()" << ++updateCount;
}

void RSSListing::slotLoadStarted()
{
  if (newsView_->currentIndex().isValid()) {
    webViewProgress_->setValue(0);
    webViewProgress_->show();
  }
}

void RSSListing::slotLoadFinished(bool ok)
{
  if (!ok) statusBar()->showMessage(tr("Error loading to WebVeiw"), 3000);
  webViewProgress_->hide();
}

void RSSListing::setFeedsFilter(QAction* pAct)
{
  if (pAct->objectName() == "filterFeedsAll_") {
    feedsModel_->setFilter("");
  } else if (pAct->objectName() == "filterFeedsUnread_") {
    feedsModel_->setFilter(QString("unread > 0"));
  }
}

void RSSListing::setNewsFilter(QAction* pAct)
{
  if (pAct->objectName() == "filterNewsAll_") {
    newsModel_->setFilter("");
  } else if (pAct->objectName() == "filterNewsUnread_") {
    newsModel_->setFilter(QString("read < 2"));
  }
}

void RSSListing::slotFeedsDockLocationChanged(Qt::DockWidgetArea area)
{
  feedsDockArea_ = area;
}

void RSSListing::slotNewsDockLocationChanged(Qt::DockWidgetArea area)
{
  newsDockArea_ = area;
}

void RSSListing::slotNewsViewDoubleClicked(QModelIndex index)
{
  QDesktopServices::openUrl(
        QUrl(newsModel_->index(index.row(), newsModel_->fieldIndex("link")).data(Qt::EditRole).toString()));
}
