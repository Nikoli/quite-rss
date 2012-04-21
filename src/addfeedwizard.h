#ifndef ADDFEEDDIALOG_H
#define ADDFEEDDIALOG_H

#include <QtGui>
#include <QtSql>
#include "lineedit.h"
#include "parsethread.h"
#include "updatethread.h"

class AddFeedWizard : public QWizard
{
  Q_OBJECT
private:
  void addFeed();
  void deleteFeed();
  void showProgressBar();
  void finish();

  QSqlDatabase *db_;
  UpdateThread *persistentUpdateThread_;
  ParseThread *persistentParseThread_;
  QByteArray data_;
  QUrl url_;
  QWizardPage *createUrlFeedPage();
  QWizardPage *createNameFeedPage();
  QCheckBox *titleFeedAsName_;
  QWidget *warningWidjet_;
  QProgressBar *progressBar_;
  bool selectedPage;
  bool finishOn;

public:
  explicit AddFeedWizard(QWidget *parent, QSqlDatabase *db);
  ~AddFeedWizard();

  LineEdit *nameFeedEdit_;
  LineEdit *urlFeedEdit_;
  QString htmlUrl_;

protected:
  virtual bool validateCurrentPage();
  virtual void done(int result);

signals:
  void startGetUrlTimer();
  void xmlReadyParse(const QByteArray &data, const QUrl &url);

public slots:
  void slotUpdateFeed(const QUrl &url);

private slots:
  void urlFeedEditChanged(const QString&);
  void nameFeedEditChanged(const QString&);
  void backButtonClicked();
  void nextButtonClicked();
  void finishButtonClicked();
  void slotCurrentIdChanged(int);
  void titleFeedAsNameStateChanged(int);
  void slotProgressBarUpdate();
  void receiveXml(const QByteArray &data, const QUrl &url);
  void getUrlDone(const int&, const QDateTime &dtReply);

};

#endif // ADDFEEDDIALOG_H
