// Background threads: the first load, and one-at-a-time look-ups of monster locations, item
// details and icons (each polite, each cached).
#pragma once

#include "core/registry.h"
#include "core/render.h"
#include "core/store.h"

#include <QMutex>
#include <QQueue>
#include <QSet>
#include <QThread>
#include <QWaitCondition>

#include <memory>
#include <optional>

using RegistryPtr = std::shared_ptr<const dq9::Registry>;
Q_DECLARE_METATYPE(RegistryPtr)

class LoadWorker : public QThread {
    Q_OBJECT
public:
    LoadWorker(dq9::Store &store, bool force, QObject *parent = nullptr);

signals:
    void status(const QString &text);
    void ready(RegistryPtr registry, const QString &note);
    void failed(const QString &message);

protected:
    void run() override;

private:
    dq9::Store &m_store;
    bool m_force;
};

// A worker thread fed by a queue of strings; an empty optional stops it.
class QueueWorker : public QThread {
    Q_OBJECT
public:
    explicit QueueWorker(dq9::Store &store, QObject *parent = nullptr) : QThread(parent), m_store(store) {}
    void shutdown();

signals:
    void done(const QString &what);

protected:
    void enqueue(const QString &item);
    void run() override;
    virtual void process(const QString &item) = 0;

    dq9::Store &m_store;
    QMutex m_lock;

private:
    QWaitCondition m_wake;
    QQueue<std::optional<QString>> m_queue;
};

// Monster locations, by the monster's wiki URL.
class LocWorker : public QueueWorker {
    Q_OBJECT
public:
    using QueueWorker::QueueWorker;
    dq9::LocState state(const dq9::EnemyPtr &enemy);
    void request(const dq9::EnemyPtr &enemy);
    void resetFailures();

protected:
    void process(const QString &url) override;

private:
    QHash<QString, dq9::EnemyPtr> m_enemies;
    QSet<QString> m_pending, m_empty, m_errors;
};

// Item facts (description, sell price, where to find) from dqnine.wikidot.com, by item name.
class DetailWorker : public QueueWorker {
    Q_OBJECT
public:
    using QueueWorker::QueueWorker;
    dq9::DetailState state(const QString &name);   // starts the look-up if it isn't cached
    void resetFailures();

protected:
    void process(const QString &name) override;

private:
    QSet<QString> m_pending, m_failed;
};

// Item icons, downloaded to the cache folder (drawing happens on the UI thread).
class SpriteWorker : public QueueWorker {
    Q_OBJECT
public:
    using QueueWorker::QueueWorker;
    void request(const QString &name);

protected:
    void process(const QString &name) override;

private:
    QSet<QString> m_asked;
};
