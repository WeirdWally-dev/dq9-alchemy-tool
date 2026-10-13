// Parsers for the pages the app reads: the wiki's enemy list and monster pages, woodus' recipe
// table, dqnine.wikidot.com item pages, the alchemy-ingredients icon page, and the recipe
// spreadsheet's columns.
#pragma once

#include "core/models.h"

#include <QHash>
#include <QList>
#include <QString>

#include <optional>

namespace dq9 {

QString wikiUrl(const QString &href);

QList<Enemy> parseEnemyList(const QString &html);
QStringList parseEnemyLocations(const QString &html);

Ings parseIngredients(const QString &text);
QList<Recipe> parseRecipes(const QString &html);

// ---- recipe spreadsheet ------------------------------------------------------------------
Ings parseSheetIngredients(const QString &text);
QList<QList<int>> alphabeticalRuns(const QStringList &names);
// columns: group ("Weapon", "Armor", ...) -> [(item name, materials text)] in sheet order.
using SheetColumns = QList<QPair<QString, QList<QPair<QString, QString>>>>;
QList<Recipe> recipesFromColumns(const SheetColumns &columns);

// ---- item pages (dqnine.wikidot.com) -----------------------------------------------------
extern const QStringList DETAIL_SHOWN;
std::optional<Details> parseItemPage(const QString &html, const QString &name = QString());
QStringList detailsFound(const std::optional<Details> &det);
QHash<QString, QString> parseWikiIndex(const QString &html);

// ---- icons (dragon-quest.org) ------------------------------------------------------------
QHash<QString, QString> parseSprites(const QString &html);

// ---- categories for shop-only items ------------------------------------------------------
QString shopCategory(const QString &name, const QString &hint = QString());

// Ordered (category, regex) lists used to guess weapon/armour types from names.
using PatternList = QList<QPair<QString, QString>>;
const PatternList &subtypePatterns(const QString &group);

} // namespace dq9
