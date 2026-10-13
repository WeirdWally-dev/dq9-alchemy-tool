#include "core/http.h"

#include "core/models.h"

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QThread>
#include <QThreadStorage>
#include <QUrl>

namespace dq9 {

namespace {
constexpr double MIN_GAP = 1.0;
constexpr int TIMEOUT_MS = 30000;

QNetworkAccessManager &threadManager()
{
    // QNetworkAccessManager belongs to the thread that made it, so each worker gets its own.
    static QThreadStorage<QNetworkAccessManager *> managers;
    if (!managers.hasLocalData())
        managers.setLocalData(new QNetworkAccessManager);
    return *managers.localData();
}
} // namespace

QByteArray Http::fetch(const QString &url, int *status, QString *error)
{
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(UA));
    req.setTransferTimeout(TIMEOUT_MS);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = threadManager().get(req);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    *status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    *error = reply->error() == QNetworkReply::NoError ? QString() : reply->errorString();
    const QByteArray body = reply->readAll();
    delete reply;
    return body;
}

Robots Http::robotsFor(const QString &url)
{
    const QUrl u(url);
    const QString base = u.scheme() + QStringLiteral("://") + u.authority();
    QMutexLocker lock(&m_robotsLock);
    auto it = m_robots.find(base);
    if (it == m_robots.end()) {
        int status = 0;
        QString error;
        const QByteArray body = fetch(base + QStringLiteral("/robots.txt"), &status, &error);
        Robots r;
        if (status == 401 || status == 403)
            r = Robots::disallowAll();
        else if (status >= 400 && status < 500)
            r = Robots::allowAll();
        else if (status >= 200 && status < 300)
            r = Robots::parse(QString::fromUtf8(body));
        else
            throw HttpError(status, QStringLiteral("Couldn't read %1/robots.txt: %2").arg(base, error.isEmpty() ? QString::number(status) : error));
        it = m_robots.insert(base, r);
    }
    return it.value();
}

bool Http::allowed(const QString &url)
{
    return robotsFor(url).canFetch(QString::fromLatin1(UA), url);
}

QByteArray Http::rateLimited(const QString &url, QMutex &lock, std::chrono::steady_clock::time_point &last,
                             double gapSeconds)
{
    if (!allowed(url))
        throw RobotsDisallowed(url);
    int status = 0;
    QString error;
    QByteArray body;
    {
        QMutexLocker guard(&lock);
        const auto gap = std::chrono::duration<double>(gapSeconds);
        const auto wait = gap - (std::chrono::steady_clock::now() - last);
        if (wait.count() > 0)
            QThread::msleep(static_cast<unsigned long>(wait.count() * 1000));
        body = fetch(url, &status, &error);
        last = std::chrono::steady_clock::now();
    }
    if (status < 200 || status >= 300)
        throw HttpError(status, status ? QStringLiteral("HTTP %1 for %2").arg(status).arg(url)
                                       : QStringLiteral("%1 (%2)").arg(error, url));
    return body;
}

QString Http::get(const QString &url)
{
    return QString::fromUtf8(rateLimited(url, m_pageLock, m_lastPage, MIN_GAP));
}

QByteArray Http::getBytes(const QString &url, double gapSeconds)
{
    return rateLimited(url, m_imgLock, m_lastImg, gapSeconds);
}

} // namespace dq9
