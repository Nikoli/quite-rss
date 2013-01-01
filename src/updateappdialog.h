#ifndef UPDATEAPPDIALOG_H
#define UPDATEAPPDIALOG_H

#include <QtGui>
#include <QNetworkReply>
#include <QWebPage>
#include <QWebFrame>

class UpdateAppDialog : public QDialog
{
  Q_OBJECT
private:
  QString lang_;
  QSettings *settings_;
  bool showDialog_;

  QNetworkAccessManager manager_;
  QNetworkReply *reply_;
  QNetworkReply *historyReply_;

  QLabel *infoLabel;
  QTextBrowser *history_;

  QPushButton *installButton_;
  QCheckBox *remindAboutVersion_;

  QWebPage page;

public:
  explicit UpdateAppDialog(const QString &lang, QSettings *settings,
                           QWidget *parent, bool show = true);

private slots:
  void closeDialog();
  void finishUpdatesChecking();
  void slotFinishHistoryReply();
  void updaterRun();
  void renderStatistics();

signals:
  void signalNewVersion(QString newVersion);

};

#endif // UPDATEAPPDIALOG_H
