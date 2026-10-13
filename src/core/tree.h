// Turns an item into a left-to-right branching tree of "bubbles" (no widgets in here, so it can be
// tested on its own): item -> its recipe(s) -> ingredients -> (their recipes ...) -> monsters / shops.
// Also flattens the same chain into a totalled material list.
#pragma once

#include "core/render.h"

#include <functional>
#include <memory>

namespace dq9 {

constexpr double NODE_W = 176, NODE_H = 54, GAP_X = 64, GAP_Y = 14;
constexpr int MAX_NODES = 700;   // stop unfolding sub-recipes past this, so a huge chain stays drawable

using FoundFn = std::function<QStringList(const QString &)>;

// kind: item | craft | base | recipe | monster | shop | found | more | unknown
struct Node {
    QString kind;
    QString label;
    QString sub;
    QString edge;          // text on the line coming into this bubble ("x3", "drop", ...)
    QString key;           // registry item this bubble opens ("" = none)
    EnemyPtr enemy;        // monster bubbles open the monster's page
    QString tip;
    QString sprite;        // item name to look up an icon for ("" = none)
    int more = 0;          // monsters beyond the ones drawn (a "+" badge is shown if > 0)
    QString moreKey;       // item whose full monster list the badge opens
    QList<std::shared_ptr<Node>> children;
    double x = 0, y = 0;
};
using NodePtr = std::shared_ptr<Node>;

// prefer: "alchemy" or "drops". An item that has both a recipe and monster drops shows only the
// preferred one. foundFn(name) gives extra ways to get an item (its wiki page's "Where to find");
// it is only asked about the chosen item and things that would otherwise have no known source.
NodePtr buildTree(const Registry &reg, const ItemInfo &info, int perN, const Shops &shops,
                  const QString &prefer = QStringLiteral("alchemy"), const FoundFn &foundFn = nullptr);

// Fills in x, y (top-left of each bubble). Returns (width, height) of the whole tree.
QPair<double, double> layout(const NodePtr &root);
QList<NodePtr> walk(const NodePtr &root);

// ---- material list -----------------------------------------------------------------------
struct ListRow {
    QString name;
    QString key;           // registry key ("" if the name matches no known item)
    int qty = 0;
    bool craftable = false;   // has a recipe, so it can be broken down
    bool expanded = false;    // broken down (its ingredients are counted instead of it)
    bool loop = false;        // can't be broken down here: its recipe needs an item above it
    QString how;              // one line on where to get it
};

struct MaterialList {
    QString target;
    int count = 1;
    QList<ListRow> materials;   // what to gather / buy, totalled
    QList<ListRow> crafted;     // what gets crafted along the way, in order
    QStringList notes;
};

bool defaultBreakdown(const ItemInfo &it, const QString &prefer = QStringLiteral("alchemy"));
QString howToGet(const ItemInfo *it, const QString &name, const Shops &shops, const FoundFn &foundFn = nullptr);
MaterialList materialList(const Registry &reg, const ItemInfo &info, const Shops &shops,
                          const QString &prefer = QStringLiteral("alchemy"),
                          const QHash<QString, bool> &breakdown = {}, int count = 1, int recipeIndex = 0,
                          const FoundFn &foundFn = nullptr);
QString materialListText(const MaterialList &ml);

} // namespace dq9
