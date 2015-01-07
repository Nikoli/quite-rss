/* ============================================================
* QuiteRSS is a open-source cross-platform RSS/Atom news feeds reader
* Copyright (C) 2011-2015 QuiteRSS Team <quiterssteam@gmail.com>
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
#include "networkmanager.h"

#include "mainapplication.h"
#include "authenticationdialog.h"
#include "adblockmanager.h"
#include "webpage.h"
#include "sslerrordialog.h"

#include <QNetworkReply>
#include <QDebug>

NetworkManager::NetworkManager(bool isThread, QObject* parent)
  : QNetworkAccessManager(parent)
  , adblockManager_(0)
{
  setCookieJar(mainApp->cookieJar());
  // CookieJar is shared between NetworkManagers
  mainApp->cookieJar()->setParent(0);

#ifndef QT_NO_NETWORKPROXY
  qRegisterMetaType<QNetworkProxy>("QNetworkProxy");
  qRegisterMetaType<QList<QSslError> >("QList<QSslError>");
#endif

  if (isThread) {
    connect(this, SIGNAL(authenticationRequired(QNetworkReply*,QAuthenticator*)),
            mainApp->networkManager(), SLOT(slotAuthentication(QNetworkReply*,QAuthenticator*)),
            Qt::BlockingQueuedConnection);
    connect(this, SIGNAL(proxyAuthenticationRequired(QNetworkProxy,QAuthenticator*)),
            mainApp->networkManager(), SLOT(slotProxyAuthentication(QNetworkProxy,QAuthenticator*)),
            Qt::BlockingQueuedConnection);
    connect(this, SIGNAL(sslErrors(QNetworkReply*, QList<QSslError>)),
            mainApp->networkManager(), SLOT(slotSslError(QNetworkReply*, QList<QSslError>)),
            Qt::BlockingQueuedConnection);
  } else {
    connect(this, SIGNAL(authenticationRequired(QNetworkReply*,QAuthenticator*)),
            SLOT(slotAuthentication(QNetworkReply*,QAuthenticator*)));
    connect(this, SIGNAL(proxyAuthenticationRequired(QNetworkProxy,QAuthenticator*)),
            SLOT(slotProxyAuthentication(QNetworkProxy,QAuthenticator*)));
    connect(this, SIGNAL(sslErrors(QNetworkReply*, QList<QSslError>)),
            SLOT(slotSslError(QNetworkReply*, QList<QSslError>)));
  }
}

NetworkManager::~NetworkManager()
{
}

/** @brief Request authentification
 *---------------------------------------------------------------------------*/
void NetworkManager::slotAuthentication(QNetworkReply *reply, QAuthenticator *auth)
{
  AuthenticationDialog *authenticationDialog =
      new AuthenticationDialog(reply->url(), auth);

  if (!authenticationDialog->save_->isChecked())
    authenticationDialog->exec();

  delete authenticationDialog;
}
/** @brief Request proxy authentification
 *---------------------------------------------------------------------------*/
void NetworkManager::slotProxyAuthentication(const QNetworkProxy &proxy, QAuthenticator *auth)
{
  AuthenticationDialog *authenticationDialog =
      new AuthenticationDialog(proxy.hostName(), auth);

  if (!authenticationDialog->save_->isChecked())
    authenticationDialog->exec();

  delete authenticationDialog;
}

static inline uint qHash(const QSslCertificate &cert)
{
  return qHash(cert.toPem());
}

void NetworkManager::slotSslError(QNetworkReply *reply, QList<QSslError> errors)
{
  if (mainApp->isNetIgnoreWarnings() || reply->property("downloadReply").toBool() ||
      reply->property("feedReply").toBool()) {
    reply->ignoreSslErrors(errors);
    return;
  }

  QNetworkRequest request = reply->request();
  QVariant v = request.attribute((QNetworkRequest::Attribute)(QNetworkRequest::User + 100));
  WebPage* webPage = static_cast<WebPage*>(v.value<void*>());
  if (!WebPage::isPointerSafeToUse(webPage)) {
    reply->ignoreSslErrors(errors);
    return;
  }

  QHash<QSslCertificate, QStringList> errorHash;
  foreach (const QSslError &error, errors) {
    // Weird behavior on Windows
    if (error.error() == QSslError::NoError) {
      continue;
    }

    QSslCertificate cert = error.certificate();

    if (errorHash.contains(cert)) {
      errorHash[cert].append(error.errorString());
    }
    else {
      errorHash.insert(cert, QStringList(error.errorString()));
    }
  }

  // User already rejected those certs on this page
  if (webPage->containsRejectedCerts(errorHash.keys())) {
    return;
  }

  QString title = tr("SSL Certificate Error!");
  QString text1 = tr("The page you are trying to access has the following errors in the SSL certificate:");

  QString certs;

  QHash<QSslCertificate, QStringList>::const_iterator i = errorHash.constBegin();
  while (i != errorHash.constEnd()) {
    const QSslCertificate cert = i.key();
    const QStringList errors = i.value();

    if (localCerts_.contains(cert) || tempAllowedCerts_.contains(cert) || errors.isEmpty()) {
      ++i;
      continue;
    }

    certs += "<ul><li>";
    certs += tr("<b>Organization: </b>") +
        SslErrorDialog::clearCertSpecialSymbols(cert.subjectInfo(QSslCertificate::Organization));
    certs += "</li><li>";
    certs += tr("<b>Domain Name: </b>") +
        SslErrorDialog::clearCertSpecialSymbols(cert.subjectInfo(QSslCertificate::CommonName));
    certs += "</li><li>";
    certs += tr("<b>Expiration Date: </b>") +
        cert.expiryDate().toString("hh:mm:ss dddd d. MMMM yyyy");
    certs += "</li></ul>";

    certs += "<ul>";
    foreach (const QString &error, errors) {
      certs += "<li>";
      certs += tr("<b>Error: </b>") + error;
      certs += "</li>";
    }
    certs += "</ul>";

    ++i;
  }

  QString text2 = tr("Would you like to continue to the server?");
  QString message = QString("<b>%1</b><p>%2</p>%3<p>%4</p><br>").arg(title, text1, certs, text2);

  if (!certs.isEmpty())  {
    SslErrorDialog dialog(webPage->view());
    dialog.setText(message);
    int result = dialog.exec();
    if (result == QDialog::Accepted) {
      foreach (const QSslCertificate &cert, errorHash.keys()) {
        if (!tempAllowedCerts_.contains(cert)) {
          tempAllowedCerts_.append(cert);
        }
      }
    } else {
      // To prevent asking user more than once for the same certificate
      webPage->addRejectedCerts(errorHash.keys());
      return;
    }
  }

  reply->ignoreSslErrors(errors);
}

QNetworkReply *NetworkManager::createRequest(QNetworkAccessManager::Operation op,
                                             const QNetworkRequest &request,
                                             QIODevice *outgoingData)
{
  if (mainApp->networkManager() == this) {
    QNetworkReply *reply = 0;

    // Adblock
    if (op == QNetworkAccessManager::GetOperation) {
      if (!adblockManager_) {
        adblockManager_ = AdBlockManager::instance();
      }

      reply = adblockManager_->block(request);
      if (reply) {
        return reply;
      }
    }
  }

  return QNetworkAccessManager::createRequest(op, request, outgoingData);
}
