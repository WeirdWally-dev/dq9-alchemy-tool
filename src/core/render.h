// HTML write-ups for the info pages (an item's recipes, drops and shops; a monster's drops and
// locations), shown in a QTextBrowser.
#pragma once

#include "core/registry.h"

#include <functional>

namespace dq9 {

constexpr int MAX_NEST = 6;   // how many levels of "this ingredient is itself crafted" to unfold

struct LocState {
    QString state;            // ok | loading | unknown | error
    QStringList locations;
};
using LocFn = std::function<LocState(const EnemyPtr &)>;

struct DetailState {
    QString state;            // ok | loading | none
    std::optional<Details> details;
};
using DetailFn = std::function<DetailState(const QString &)>;

using Shops = QHash<QString, QStringList>;

struct Rendered {
    QString html;
    QList<EnemyPtr> needed;   // monsters whose locations the page wants
};

Rendered renderItemHtml(const Registry &reg, const ItemInfo &info, int perN, const LocFn &locFn,
                        const Shops &shops, const DetailFn &detailFn = nullptr);
Rendered renderEnemyHtml(const EnemyPtr &enemy, const LocFn &locFn);

} // namespace dq9
