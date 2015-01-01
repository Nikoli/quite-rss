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
/* ============================================================
* QupZilla - WebKit based browser
* Copyright (C) 2010-2014  David Rosca <nowrep@gmail.com>
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
/**
 * Copyright (c) 2009, Benjamin C. Meyer <ben@meyerhome.net>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Benjamin Meyer nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "adblocksubscription.h"
#include "adblockmanager.h"
#include "adblocksearchtree.h"
#include "followredirectreply.h"
#include "mainapplication.h"
#include "networkmanager.h"
#include "common.h"

#include <QFile>
#include <QTimer>
#include <QNetworkReply>
#include <QDebug>
#include <QWebPage>

AdBlockSubscription::AdBlockSubscription(const QString &title, QObject* parent)
  : QObject(parent)
  , m_reply(0)
  , m_title(title)
  , m_updated(false)
{
}

QString AdBlockSubscription::title() const
{
  return m_title;
}

QString AdBlockSubscription::filePath() const
{
  return m_filePath;
}

void AdBlockSubscription::setFilePath(const QString &path)
{
  m_filePath = path;
}

QUrl AdBlockSubscription::url() const
{
  return m_url;
}

void AdBlockSubscription::setUrl(const QUrl &url)
{
  m_url = url;
}

void AdBlockSubscription::loadSubscription(const QStringList &disabledRules)
{
  QFile file(m_filePath);

  if (!file.exists()) {
    QTimer::singleShot(0, this, SLOT(updateSubscription()));
    return;
  }

  if (!file.open(QFile::ReadOnly)) {
    qWarning() << "AdBlockSubscription::" << __FUNCTION__ << "Unable to open adblock file for reading" << m_filePath;
    QTimer::singleShot(0, this, SLOT(updateSubscription()));
    return;
  }

  QTextStream textStream(&file);
  textStream.setCodec("UTF-8");
  // Header is on 3rd line
  textStream.readLine(1024);
  textStream.readLine(1024);
  QString header = textStream.readLine(1024);

  if (!header.startsWith(QLatin1String("[Adblock")) || m_title.isEmpty()) {
    qWarning() << "AdBlockSubscription::" << __FUNCTION__ << "invalid format of adblock file" << m_filePath;
    QTimer::singleShot(0, this, SLOT(updateSubscription()));
    return;
  }

  m_rules.clear();

  while (!textStream.atEnd()) {
    AdBlockRule* rule = new AdBlockRule(textStream.readLine(), this);

    if (disabledRules.contains(rule->filter())) {
      rule->setEnabled(false);
    }

    m_rules.append(rule);
  }

  populateCache();

  // Initial update
  if (m_rules.isEmpty() && !m_updated) {
    QTimer::singleShot(0, this, SLOT(updateSubscription()));
  }
}

void AdBlockSubscription::saveSubscription()
{
}

void AdBlockSubscription::updateSubscription()
{
  if (m_reply || !m_url.isValid()) {
    return;
  }

  m_reply = new FollowRedirectReply(m_url, mainApp->networkManager());

  connect(m_reply, SIGNAL(finished()), this, SLOT(subscriptionDownloaded()));
}

void AdBlockSubscription::subscriptionDownloaded()
{
  if (m_reply != qobject_cast<FollowRedirectReply*>(sender())) {
    return;
  }

  QByteArray response = QString::fromUtf8(m_reply->readAll()).toUtf8();

  if (m_reply->error() == QNetworkReply::NoError && response.startsWith("[Adblock")) {
    // Prepend subscription info
    response.prepend(QString("Title: %1\nUrl: %2\n").arg(title(), url().toString()).toUtf8());

    saveDownloadedData(response);

    loadSubscription(AdBlockManager::instance()->disabledRules());
    emit subscriptionUpdated();
  }
  else {
    emit subscriptionError(tr("Cannot load subscription!"));
  }

  m_reply->deleteLater();
  m_reply = 0;
}

void AdBlockSubscription::saveDownloadedData(const QByteArray &data)
{
  QFile file(m_filePath);

  if (!file.open(QFile::ReadWrite | QFile::Truncate)) {
    qWarning() << "AdBlockSubscription::" << __FUNCTION__ << "Unable to open adblock file for writing:" << m_filePath;
    return;
  }

  if (AdBlockManager::instance()->useLimitedEasyList() && m_url == QUrl(ADBLOCK_EASYLIST_URL)) {
    // Third-party advertisers rules are with start domain (||) placeholder which needs regexps
    // So we are ignoring it for keeping good performance
    // But we will use whitelist rules at the end of list

    QByteArray part1 = data.left(data.indexOf(QLatin1String("!-----------------------------Third-party adverts-----------------------------!")));
    QByteArray part2 = data.mid(data.indexOf(QLatin1String("!---------------------------------Whitelists----------------------------------!")));

    file.write(part1 + part2);
    file.close();
    return;
  }

  file.write(data);
  file.close();
}

const AdBlockRule* AdBlockSubscription::match(const QNetworkRequest &request, const QString &urlDomain, const QString &urlString) const
{
  // Exception rules
  if (m_networkExceptionTree.find(request, urlDomain, urlString)) {
    return 0;
  }

  int count = m_networkExceptionRules.count();
  for (int i = 0; i < count; ++i) {
    const AdBlockRule* rule = m_networkExceptionRules.at(i);
    if (rule->networkMatch(request, urlDomain, urlString)) {
      return 0;
    }
  }

  // Block rules
  if (const AdBlockRule* rule = m_networkBlockTree.find(request, urlDomain, urlString)) {
    return rule;
  }

  count = m_networkBlockRules.count();
  for (int i = 0; i < count; ++i) {
    const AdBlockRule* rule = m_networkBlockRules.at(i);
    if (rule->networkMatch(request, urlDomain, urlString)) {
      return rule;
    }
  }

  return 0;
}

bool AdBlockSubscription::adBlockDisabledForUrl(const QUrl &url) const
{
  int count = m_documentRules.count();
  for (int i = 0; i < count; ++i) {
    const AdBlockRule* rule = m_documentRules.at(i);
    if (rule->urlMatch(url)) {
      return true;
    }
  }

  return false;
}

bool AdBlockSubscription::elemHideDisabledForUrl(const QUrl &url) const
{
  if (adBlockDisabledForUrl(url)) {
    return true;
  }

  int count = m_elemhideRules.count();
  for (int i = 0; i < count; ++i) {
    const AdBlockRule* rule = m_elemhideRules.at(i);
    if (rule->urlMatch(url)) {
      return true;
    }
  }

  return false;
}

QString AdBlockSubscription::elementHidingRules() const
{
  return m_elementHidingRules;
}

QString AdBlockSubscription::elementHidingRulesForDomain(const QString &domain) const
{
  QString rules;

  int addedRulesCount = 0;
  int count = m_domainRestrictedCssRules.count();
  for (int i = 0; i < count; ++i) {
    const AdBlockRule* rule = m_domainRestrictedCssRules.at(i);
    if (!rule->matchDomain(domain)) {
      continue;
    }

#if QT_VERSION >= 0x040800
    if (Q_UNLIKELY(addedRulesCount == 1000)) {
#else
    if (addedRulesCount == 1000) {
#endif
      rules.append(rule->cssSelector());
      rules.append("{display:none !important;}\n");
      addedRulesCount = 0;
    }
    else {
      rules.append(rule->cssSelector() + QLatin1Char(','));
      addedRulesCount++;
    }
  }

  if (addedRulesCount != 0) {
    rules = rules.left(rules.size() - 1);
    rules.append("{display:none !important;}\n");
  }

  return rules;
}

const AdBlockRule* AdBlockSubscription::rule(int offset) const
{
  if (!(offset >= 0 && m_rules.count() > offset)) {
    return 0;
  }

  return m_rules[offset];
}

QVector<AdBlockRule*> AdBlockSubscription::allRules() const
{
  return m_rules;
}

const AdBlockRule* AdBlockSubscription::enableRule(int offset)
{
  if (!(offset >= 0 && m_rules.count() > offset)) {
    return 0;
  }

  AdBlockRule* rule = m_rules[offset];
  rule->setEnabled(true);
  AdBlockManager::instance()->removeDisabledRule(rule->filter());

  if (rule->isCssRule()) {
    populateCache();
    mainApp->reloadUserStyleBrowser();
  }

  return rule;
}

const AdBlockRule* AdBlockSubscription::disableRule(int offset)
{
  if (!(offset >= 0 && m_rules.count() > offset)) {
    return 0;
  }

  AdBlockRule* rule = m_rules[offset];
  rule->setEnabled(false);
  AdBlockManager::instance()->addDisabledRule(rule->filter());

  if (rule->isCssRule()) {
    populateCache();
    mainApp->reloadUserStyleBrowser();
  }

  return rule;
}

bool AdBlockSubscription::canEditRules() const
{
  return false;
}

bool AdBlockSubscription::canBeRemoved() const
{
  return true;
}

int AdBlockSubscription::addRule(AdBlockRule* rule)
{
  Q_UNUSED(rule)
  return -1;
}

bool AdBlockSubscription::removeRule(int offset)
{
  Q_UNUSED(offset)
  return false;
}

const AdBlockRule* AdBlockSubscription::replaceRule(AdBlockRule* rule, int offset)
{
  Q_UNUSED(rule)
  Q_UNUSED(offset)
  return 0;
}

void AdBlockSubscription::populateCache()
{
  m_networkExceptionTree.clear();
  m_networkExceptionRules.clear();
  m_networkBlockTree.clear();
  m_networkBlockRules.clear();
  m_domainRestrictedCssRules.clear();
  m_elementHidingRules.clear();
  m_documentRules.clear();
  m_elemhideRules.clear();

  // Apparently, excessive amount of selectors for one CSS rule is not what WebKit likes.
  // (In my testings, 4931 is the number that makes it crash)
  // So let's split it by 1000 selectors...
  int hidingRulesCount = 0;

  int count = m_rules.count();
  for (int i = 0; i < count; ++i) {
    const AdBlockRule* rule = m_rules.at(i);

    // Don't add internally disabled rules to cache
    if (rule->isInternalDisabled()) {
      continue;
    }

    if (rule->isCssRule()) {
      // We will add only enabled css rules to cache, because there is no enabled/disabled
      // check on match. They are directly embedded to pages.
      if (!rule->isEnabled()) {
        continue;
      }

      if (rule->isDomainRestricted()) {
        m_domainRestrictedCssRules.append(rule);
      }
#if QT_VERSION >= 0x040800
      else if (Q_UNLIKELY(hidingRulesCount == 1000)) {
#else
      else if (hidingRulesCount == 1000) {
#endif
        m_elementHidingRules.append(rule->cssSelector());
        m_elementHidingRules.append("{display:none !important;} ");
        hidingRulesCount = 0;
      }
      else {
        m_elementHidingRules.append(rule->cssSelector() + QLatin1Char(','));
        hidingRulesCount++;
      }
    }
    else if (rule->isDocument()) {
      m_documentRules.append(rule);
    }
    else if (rule->isElemhide()) {
      m_elemhideRules.append(rule);
    }
    else if (rule->isException()) {
      if (!m_networkExceptionTree.add(rule)) {
        m_networkExceptionRules.append(rule);
      }
    }
    else {
      if (!m_networkBlockTree.add(rule)) {
        m_networkBlockRules.append(rule);
      }
    }
  }

  if (hidingRulesCount != 0) {
    m_elementHidingRules = m_elementHidingRules.left(m_elementHidingRules.size() - 1);
    m_elementHidingRules.append("{display:none !important;} ");
  }
}

AdBlockSubscription::~AdBlockSubscription()
{
  qDeleteAll(m_rules);
}

// AdBlockCustomList

AdBlockCustomList::AdBlockCustomList(QObject* parent)
  : AdBlockSubscription(tr("Custom Rules"), parent)
{
  setFilePath(mainApp->dataDir() + "/adblock/customlist.txt");
}

void AdBlockCustomList::saveSubscription()
{
  QFile file(filePath());

  if (!file.open(QFile::ReadWrite | QFile::Truncate)) {
    qWarning() << "AdBlockSubscription::" << __FUNCTION__ << "Unable to open adblock file for writing:" << filePath();
    return;
  }

  QTextStream textStream(&file);
  textStream.setCodec("UTF-8");
  textStream << "Title: " << title() << endl;
  textStream << "Url: " << url().toString() << endl;
  textStream << "[Adblock Plus 1.1.1]" << endl;

  foreach (const AdBlockRule* rule, m_rules) {
    textStream << rule->filter() << endl;
  }

  file.close();
}

bool AdBlockCustomList::canEditRules() const
{
  return true;
}

bool AdBlockCustomList::canBeRemoved() const
{
  return false;
}

bool AdBlockCustomList::containsFilter(const QString &filter) const
{
  foreach (const AdBlockRule* rule, m_rules) {
    if (rule->filter() == filter) {
      return true;
    }
  }

  return false;
}

bool AdBlockCustomList::removeFilter(const QString &filter)
{
  for (int i = 0; i < m_rules.count(); ++i) {
    const AdBlockRule* rule = m_rules.at(i);

    if (rule->filter() == filter) {
      return removeRule(i);
    }
  }

  return false;
}

int AdBlockCustomList::addRule(AdBlockRule* rule)
{
  m_rules.append(rule);
  populateCache();

  emit subscriptionEdited();

  return m_rules.count() - 1;
}

bool AdBlockCustomList::removeRule(int offset)
{
  if (!(offset >= 0 && m_rules.count() > offset)) {
    return false;
  }

  AdBlockRule* rule = m_rules.at(offset);
  const QString filter = rule->filter();

  m_rules.remove(offset);
  populateCache();

  emit subscriptionEdited();

  AdBlockManager::instance()->removeDisabledRule(filter);

  delete rule;
  return true;
}

const AdBlockRule* AdBlockCustomList::replaceRule(AdBlockRule* rule, int offset)
{
  if (!(offset >= 0 && m_rules.count() > offset)) {
    return 0;
  }

  delete m_rules.at(offset);

  m_rules[offset] = rule;
  populateCache();

  emit subscriptionEdited();

  return m_rules[offset];
}
