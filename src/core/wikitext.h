// The wiki's weapon list, read from its wikitext source (clean and compact compared to the page).
#pragma once

#include "core/models.h"

#include <QHash>

namespace dq9 {

// Plain text of a wikitext cell. {{Boomerang W}} -> Boomerang, [[A|b]] -> b, [[A]]x -> Ax.
// keepLinkTrail=false gives [[Iron nail]]s -> Iron nail (the linked name, for recipes).
QString wtText(const QString &s, bool keepLinkTrail = true);

struct WikiTable {
    QStringList header;
    QList<QStringList> rows;
};
QList<WikiTable> wtTables(const QString &text);

// Every weapon in the wiki's weapon-list table. The page is hand-edited, so this tolerates its
// quirks: N/A cells, prices with or without 'g', towns or quest notes inside the price cell,
// free-text recipe cells, unclosed links, stray pipes and one row with two columns swapped.
QList<Weapon> parseWeaponWikitext(const QString &text);

// {skey(name): image URL} for every weapon with a picture.
QHash<QString, QString> weaponSprites(const QList<WeaponPtr> &weapons);

QString dropMarker(const QString &marker);

} // namespace dq9
