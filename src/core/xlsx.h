// Just enough of the .xlsx format to read the recipe spreadsheet: every sheet's cell values as text.
#pragma once

#include "core/models.h"

#include <QByteArray>
#include <QList>
#include <QStringList>

namespace dq9 {

// One sheet: rows from its first used row to its last, each a dense list of cell texts
// (a null QString for an empty cell). Throws ParseError if the file isn't a readable workbook.
using SheetRows = QList<QStringList>;
QList<SheetRows> readXlsx(const QByteArray &fileData);

// The "All Alchemy Recipes" spreadsheet -> recipes. It has column pairs headed like
// "Weapon Name" / "Materials Required"; the weapon and armour types are worked out from the
// alphabetical runs (see recipesFromColumns).
QList<Recipe> parseRecipeSheet(const QByteArray &fileData);

} // namespace dq9
