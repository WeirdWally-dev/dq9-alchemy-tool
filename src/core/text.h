// Text helpers shared by the data layer: name matching keys, regex conveniences, URL quoting.
#pragma once

#include <QList>
#include <QPair>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

namespace dq9 {

// Matching key for an item name: ignores case, spaces, punctuation and a possessive 's
// ("Dragon's scale" == "Dragon scale"). Deliberately NOT fuzzy: DQ9 has many distinct items one
// letter apart (Razer-wing / Razor-wing / Erazor-wing boomerang, Safest / Safety shoes), and
// merging those would show the wrong recipe. Accents are folded first ("Über" == "Uber").
QString norm(const QString &s);

// Lowercase with accents removed (NFKD, combining marks dropped).
QString foldAccents(const QString &s);

// Case-, accent- and spacing-insensitive sort key ("Über war hammer" files under U).
QString sortKey(const QString &name);

// Sprite-index key: lowercase, curly quotes straightened, spaces collapsed.
QString skey(const QString &name);

// Cache key for item details: exact name, lowercase, spaces collapsed.
QString dkey(const QString &name);

// "Warrior's sword" -> "warrior-s-sword" (dqnine.wikidot.com page names).
QString wikidotSlug(const QString &name);

// Python's str.title(): first letter of every letter run upper-cased, the rest lower-cased.
QString titleCase(const QString &s);

// Python's str.strip(chars).
QString stripChars(const QString &s, const QString &chars);

// Collapse whitespace runs (including U+00A0) to one space and trim.
QString squash(const QString &s);

// difflib.SequenceMatcher(None, a, b).ratio()
double sequenceRatio(const QString &a, const QString &b);

// True if two names are plausibly the same item (substring or >= 0.75 similar).
bool similar(const QString &a, const QString &b);

// Python's urllib.parse.quote(s, safe=...) on the UTF-8 bytes of s.
QString pyQuote(const QString &s, const QString &safe = QStringLiteral("/"));
QString pyUnquote(const QString &s);

// Canonical category for a raw type label ("Sword" -> "Swords", "helmet" -> "Head").
QString canonCategory(const QString &raw);

// "1/16 (6.25%)" -> (1, 16). Also handles U+2044 and the wiki's "116/" typo.
std::optional<QPair<int, int>> parseRate(const QString &text);

// ---- regex conveniences ------------------------------------------------------------------
// Compiled regex with Unicode-aware \w, \b, \s (Python 3 semantics).
QRegularExpression rx(const QString &pattern, bool caseInsensitive = false, bool dotAll = false);

// re.match: match anchored at the start of the string.
QRegularExpressionMatch matchStart(const QRegularExpression &re, const QString &s);
// re.fullmatch
QRegularExpressionMatch fullMatch(const QRegularExpression &re, const QString &s);
// re.search
inline QRegularExpressionMatch search(const QRegularExpression &re, const QString &s) { return re.match(s); }
inline bool contains(const QRegularExpression &re, const QString &s) { return re.match(s).hasMatch(); }

// re.sub with a function for the replacement.
QString subFn(const QString &s, const QRegularExpression &re,
              const std::function<QString(const QRegularExpressionMatch &)> &fn);
// re.sub with a fixed replacement (supports \1 style back-references).
QString sub(const QString &s, const QRegularExpression &re, const QString &replacement);
// re.split
QStringList splitRx(const QString &s, const QRegularExpression &re);

// html.escape(s, quote=True)
QString esc(const QString &s);

// 9900 -> "9,900"
QString thousands(long long v);

} // namespace dq9
