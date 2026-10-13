// Polite blocking HTTP for worker threads: honours robots.txt, one page request at a time at
// least MIN_GAP apart, and a shorter gap for images.
#pragma once

#include "core/robots.h"

#include <QByteArray>
#include <QHash>
#include <QMutex>
#include <QString>

#include <chrono>

namespace dq9 {

class Http {
public:
    static constexpr const char *UA = "DQ9AlchemyHelper/1.0 (personal-use fan tool)";

    virtual ~Http() = default;

    bool allowed(const QString &url);
    // Throws RobotsDisallowed or HttpError.
    virtual QString get(const QString &url);
    virtual QByteArray getBytes(const QString &url, double gapSeconds = 0.5);

    // How long to wait before the next request, in milliseconds (0 if the gap has already passed).
    static long long waitMs(double gapSeconds, std::chrono::steady_clock::duration sinceLast);

protected:
    // One request on the calling thread. status 0 = no answer (offline, timeout, TLS failure).
    static QByteArray fetch(const QString &url, int *status, QString *error);

private:
    Robots robotsFor(const QString &url);
    QByteArray rateLimited(const QString &url, QMutex &lock, std::chrono::steady_clock::time_point &last,
                           double gapSeconds);

    QMutex m_robotsLock;
    QHash<QString, Robots> m_robots;
    QMutex m_pageLock;
    std::chrono::steady_clock::time_point m_lastPage{};
    QMutex m_imgLock;
    std::chrono::steady_clock::time_point m_lastImg{};
};

} // namespace dq9
