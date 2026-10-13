#include "app/workers.h"

#include "core/text.h"
#include "core/wikitext.h"

#include <QDebug>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcLoad, "dq9.load")

using namespace dq9;

// --------------------------------------------------------------------------- first load
LoadWorker::LoadWorker(Store &store, bool force, QObject *parent)
    : QThread(parent), m_store(store), m_force(force)
{
}

void LoadWorker::run()
{
    QList<EnemyPtr> enemies;
    try {
        emit status(tr("Loading enemy list..."));
        enemies = m_store.loadEnemies(m_force);
        qCInfo(lcLoad) << "enemies:" << enemies.size() << "cache:" << m_store.dir();
    } catch (const std::exception &e) {
        qCWarning(lcLoad) << "enemy list failed:" << errorText(e);
        emit failed(tr("Couldn't load the enemy list:\n%1").arg(errorText(e)));
        return;
    }
    try {
        emit status(tr("Loading weapon list..."));
        m_store.loadWeapons(m_force);
        qCInfo(lcLoad) << "weapons:" << m_store.weapons.size();
    } catch (const std::exception &e) {
        qCWarning(lcLoad) << "weapon list failed:" << errorText(e);
        // the weapon list adds detail; the app works without it
    }
    QList<Recipe> woodus;
    QString note;
    try {
        emit status(tr("Loading alchemy recipes..."));
        const auto loaded = m_store.loadRecipes(m_force);
        woodus = loaded.first;
        note = loaded.second;
    } catch (const RobotsDisallowed &) {
        note = QStringLiteral("robots");
    } catch (const std::exception &e) {
        note = tr("Couldn't load recipes: %1").arg(errorText(e));
    }
    const QList<Recipe> sheet = m_store.loadSheetRecipes();
    try {
        emit status(tr("Loading item icons..."));
        qCInfo(lcLoad) << "icons:" << m_store.loadSpriteIndex(m_force).size();
    } catch (const std::exception &e) {
        qCWarning(lcLoad) << "icon index failed:" << errorText(e);
        // icons are optional
    }
    m_store.registerSprites(weaponSprites(m_store.weapons));
    if (note == QStringLiteral("robots") && (!woodus.isEmpty() || !sheet.isEmpty()))
        note.clear();   // woodus.com is optional once the spreadsheet (or a saved copy) is there
    emit status(tr("Matching items across sources..."));
    auto reg = std::make_shared<const Registry>(enemies, woodus + sheet, m_store.shopItems(), m_store.weapons);
    qCInfo(lcLoad) << "ready:" << enemies.size() << "enemies," << reg->recipeCount() << "recipes, note:" << note;
    emit ready(reg, note);
}

// --------------------------------------------------------------------------- queue worker
void QueueWorker::enqueue(const QString &item)
{
    QMutexLocker lock(&m_lock);
    m_queue.enqueue(item);
    m_wake.wakeOne();
}

void QueueWorker::shutdown()
{
    {
        QMutexLocker lock(&m_lock);
        m_queue.enqueue(std::nullopt);
        m_wake.wakeOne();
    }
    wait(3000);
}

void QueueWorker::run()
{
    for (;;) {
        std::optional<QString> item;
        {
            QMutexLocker lock(&m_lock);
            while (m_queue.isEmpty())
                m_wake.wait(&m_lock);
            item = m_queue.dequeue();
        }
        if (!item)
            return;
        process(*item);
    }
}

// --------------------------------------------------------------------------- locations
LocState LocWorker::state(const EnemyPtr &enemy)
{
    if (const auto cached = m_store.cachedLocations(*enemy))
        return {QStringLiteral("ok"), *cached};
    QMutexLocker lock(&m_lock);
    if (m_errors.contains(enemy->url))
        return {QStringLiteral("error"), {}};
    if (m_empty.contains(enemy->url))
        return {QStringLiteral("unknown"), {}};
    return {QStringLiteral("loading"), {}};
}

void LocWorker::request(const EnemyPtr &enemy)
{
    if (m_store.cachedLocations(*enemy))
        return;
    {
        QMutexLocker lock(&m_lock);
        if (m_pending.contains(enemy->url) || m_errors.contains(enemy->url) || m_empty.contains(enemy->url))
            return;
        m_pending.insert(enemy->url);
        m_enemies.insert(enemy->url, enemy);
    }
    enqueue(enemy->url);
}

void LocWorker::resetFailures()
{
    QMutexLocker lock(&m_lock);
    m_errors.clear();
    m_empty.clear();
}

void LocWorker::process(const QString &url)
{
    EnemyPtr enemy;
    {
        QMutexLocker lock(&m_lock);
        enemy = m_enemies.value(url);
    }
    if (enemy) {
        try {
            if (m_store.locations(*enemy).isEmpty()) {
                QMutexLocker lock(&m_lock);
                m_empty.insert(url);
            }
        } catch (const std::exception &e) {
            qCWarning(lcLoad) << "locations failed:" << url << errorText(e);
            QMutexLocker lock(&m_lock);
            m_errors.insert(url);
        }
    }
    {
        QMutexLocker lock(&m_lock);
        m_pending.remove(url);
    }
    emit done(url);
}

// --------------------------------------------------------------------------- item details
DetailState DetailWorker::state(const QString &name)
{
    const QString key = dkey(name);
    if (const auto cached = m_store.cachedDetails(name)) {
        if (cached->isEmpty())
            return {QStringLiteral("none"), std::nullopt};
        return {QStringLiteral("ok"), cached};
    }
    bool start = false;
    {
        QMutexLocker lock(&m_lock);
        if (m_failed.contains(key))
            return {QStringLiteral("none"), std::nullopt};
        if (!m_pending.contains(key)) {
            m_pending.insert(key);
            start = true;
        }
    }
    if (start)
        enqueue(name);
    return {QStringLiteral("loading"), std::nullopt};
}

void DetailWorker::resetFailures()
{
    QMutexLocker lock(&m_lock);
    m_failed.clear();
}

void DetailWorker::process(const QString &name)
{
    try {
        m_store.itemDetails(name);
    } catch (const std::exception &e) {   // offline, blocked, etc.: just show nothing
        qCWarning(lcLoad) << "item details failed:" << name << errorText(e);
        QMutexLocker lock(&m_lock);
        m_failed.insert(dkey(name));
    }
    {
        QMutexLocker lock(&m_lock);
        m_pending.remove(dkey(name));
    }
    emit done(name);
}

// --------------------------------------------------------------------------- icons
void SpriteWorker::request(const QString &name)
{
    {
        QMutexLocker lock(&m_lock);
        const QString key = skey(name);
        if (m_asked.contains(key))
            return;
        m_asked.insert(key);
    }
    enqueue(name);
}

void SpriteWorker::process(const QString &name)
{
    try {
        if (!m_store.fetchSprite(name).isEmpty())
            emit done(name);
    } catch (const std::exception &) {
        // no icon is fine; the bubble just has no picture
    }
}
