#include "core/text.h"

#include <QHash>
#include <QUrl>

#include <algorithm>
#include <array>
#include <vector>

namespace dq9 {

QRegularExpression rx(const QString &pattern, bool caseInsensitive, bool dotAll)
{
    QRegularExpression::PatternOptions opts = QRegularExpression::UseUnicodePropertiesOption;
    if (caseInsensitive)
        opts |= QRegularExpression::CaseInsensitiveOption;
    if (dotAll)
        opts |= QRegularExpression::DotMatchesEverythingOption;
    return QRegularExpression(pattern, opts);
}

QRegularExpressionMatch matchStart(const QRegularExpression &re, const QString &s)
{
    return re.match(s, 0, QRegularExpression::NormalMatch, QRegularExpression::AnchorAtOffsetMatchOption);
}

QRegularExpressionMatch fullMatch(const QRegularExpression &re, const QString &s)
{
    const QRegularExpression whole(QRegularExpression::anchoredPattern(re.pattern()), re.patternOptions());
    return whole.match(s);
}

QString subFn(const QString &s, const QRegularExpression &re,
              const std::function<QString(const QRegularExpressionMatch &)> &fn)
{
    QString out;
    qsizetype last = 0;
    auto it = re.globalMatch(s);
    while (it.hasNext()) {
        const auto m = it.next();
        out += s.mid(last, m.capturedStart() - last);
        out += fn(m);
        last = m.capturedEnd();
    }
    out += s.mid(last);
    return out;
}

QString sub(const QString &s, const QRegularExpression &re, const QString &replacement)
{
    QString copy = s;
    return copy.replace(re, replacement);
}

QStringList splitRx(const QString &s, const QRegularExpression &re)
{
    return s.split(re);
}

QString foldAccents(const QString &s)
{
    const QString d = s.toLower().normalized(QString::NormalizationForm_KD);
    QString out;
    out.reserve(d.size());
    for (const QChar c : d)
        if (c.combiningClass() == 0)
            out += c;
    return out;
}

QString norm(const QString &s)
{
    static const QRegularExpression possessive = rx(QStringLiteral("['’]s\\b"));
    static const QRegularExpression nonAlnum = rx(QStringLiteral("[^a-z0-9]"));
    return sub(sub(foldAccents(s), possessive, QString()), nonAlnum, QString());
}

QString sortKey(const QString &name)
{
    return foldAccents(name).trimmed();
}

QString squash(const QString &s)
{
    static const QRegularExpression ws = rx(QStringLiteral("\\s+"));
    QString t = s;
    t.replace(QChar(0x00A0), QLatin1Char(' '));
    return sub(t, ws, QStringLiteral(" ")).trimmed();
}

QString skey(const QString &name)
{
    QString t = name;
    t.replace(QChar(0x2019), QLatin1Char('\''));
    static const QRegularExpression ws = rx(QStringLiteral("\\s+"));
    return sub(t.trimmed().toLower(), ws, QStringLiteral(" "));
}

QString dkey(const QString &name)
{
    static const QRegularExpression ws = rx(QStringLiteral("\\s+"));
    return sub(name.trimmed().toLower(), ws, QStringLiteral(" "));
}

QString wikidotSlug(const QString &name)
{
    static const QRegularExpression bad = rx(QStringLiteral("[^a-z0-9]+"));
    return stripChars(sub(foldAccents(name), bad, QStringLiteral("-")), QStringLiteral("-"));
}

QString titleCase(const QString &s)
{
    QString out;
    out.reserve(s.size());
    bool prevCased = false;
    for (const QChar c : s) {
        const bool cased = c.isLetter() && (c.isUpper() || c.isLower() || c.isTitleCase());
        if (cased) {
            out += prevCased ? c.toLower() : c.toUpper();
        } else {
            out += c;
        }
        prevCased = cased;
    }
    return out;
}

QString stripChars(const QString &s, const QString &chars)
{
    qsizetype a = 0, b = s.size();
    while (a < b && chars.contains(s.at(a)))
        ++a;
    while (b > a && chars.contains(s.at(b - 1)))
        --b;
    return s.mid(a, b - a);
}

// ---- difflib.SequenceMatcher -------------------------------------------------------------
namespace {
struct Block { int a, b, size; };

Block longestMatch(const QString &a, const QString &b, const QHash<QChar, std::vector<int>> &b2j,
                   int alo, int ahi, int blo, int bhi)
{
    int besti = alo, bestj = blo, bestsize = 0;
    QHash<int, int> j2len;
    for (int i = alo; i < ahi; ++i) {
        QHash<int, int> newj2len;
        const auto it = b2j.constFind(a.at(i));
        if (it != b2j.constEnd()) {
            for (int j : *it) {
                if (j < blo)
                    continue;
                if (j >= bhi)
                    break;
                const int k = j2len.value(j - 1, 0) + 1;
                newj2len[j] = k;
                if (k > bestsize) {
                    besti = i - k + 1;
                    bestj = j - k + 1;
                    bestsize = k;
                }
            }
        }
        j2len = std::move(newj2len);
    }
    Q_UNUSED(b);
    return {besti, bestj, bestsize};
}
} // namespace

double sequenceRatio(const QString &a, const QString &b)
{
    const int la = int(a.size()), lb = int(b.size());
    if (la + lb == 0)
        return 1.0;
    QHash<QChar, std::vector<int>> b2j;
    for (int j = 0; j < lb; ++j)
        b2j[b.at(j)].push_back(j);
    // Strings here are short item names, so difflib's "autojunk" (only for len(b) >= 200) never applies.
    int matches = 0;
    std::vector<std::array<int, 4>> queue{{0, la, 0, lb}};
    while (!queue.empty()) {
        const auto q = queue.back();
        queue.pop_back();
        const Block m = longestMatch(a, b, b2j, q[0], q[1], q[2], q[3]);
        if (m.size) {
            matches += m.size;
            if (q[0] < m.a && q[2] < m.b)
                queue.push_back({q[0], m.a, q[2], m.b});
            if (m.a + m.size < q[1] && m.b + m.size < q[3])
                queue.push_back({m.a + m.size, q[1], m.b + m.size, q[3]});
        }
    }
    return 2.0 * matches / (la + lb);
}

bool similar(const QString &a, const QString &b)
{
    const QString na = norm(a), nb = norm(b);
    if (na.isEmpty() || nb.isEmpty())
        return false;
    return na.contains(nb) || nb.contains(na) || sequenceRatio(na, nb) >= 0.75;
}

// ---- URL quoting -------------------------------------------------------------------------
QString pyQuote(const QString &s, const QString &safe)
{
    static const QByteArray always = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.-~";
    const QByteArray safeBytes = safe.toUtf8();
    QString out;
    for (const char ch : s.toUtf8()) {
        if (always.contains(ch) || safeBytes.contains(ch))
            out += QLatin1Char(ch);
        else
            out += QStringLiteral("%%1").arg(uint(uchar(ch)), 2, 16, QLatin1Char('0')).toUpper();
    }
    return out;
}

QString pyUnquote(const QString &s)
{
    return QUrl::fromPercentEncoding(s.toUtf8());
}

// ---- categories --------------------------------------------------------------------------
QString canonCategory(const QString &raw)
{
    static const QHash<QString, QString> cats = {
        {"sword", "Swords"}, {"spear", "Spears"}, {"axe", "Axes"}, {"staff", "Staves"}, {"stave", "Staves"},
        {"wand", "Wands"}, {"whip", "Whips"}, {"claw", "Claws"}, {"knife", "Knives"}, {"dagger", "Knives"},
        {"bow", "Bows"}, {"hammer", "Hammers"}, {"fan", "Fans"}, {"boomerang", "Boomerangs"},
        {"shield", "Shields"}, {"head", "Head"}, {"helmet", "Head"}, {"hat", "Head"}, {"headgear", "Head"},
        {"headwear", "Head"}, {"torso", "Torso"}, {"body", "Torso"}, {"bodyarmour", "Torso"},
        {"bodyarmor", "Torso"}, {"armour", "Torso"}, {"armor", "Torso"}, {"arms", "Arms"}, {"arm", "Arms"},
        {"gloves", "Arms"}, {"handwear", "Arms"}, {"legs", "Legs"}, {"leg", "Legs"}, {"legwear", "Legs"},
        {"feet", "Feet"}, {"foot", "Feet"}, {"shoes", "Feet"}, {"footwear", "Feet"}, {"boots", "Feet"},
        {"accessory", "Accessories"}, {"accessories", "Accessories"},
        {"item", "Ingredients & items"}, {"consumable", "Ingredients & items"},
        {"tool", "Ingredients & items"}, {"material", "Ingredients & items"},
        {"ingredient", "Ingredients & items"},
    };
    static const QRegularExpression nonAlpha = rx(QStringLiteral("[^a-z]"));
    const QString k = sub(raw.toLower(), nonAlpha, QString());
    if (cats.contains(k))
        return cats.value(k);
    for (const QString suffix : {QStringLiteral("es"), QStringLiteral("s")}) {
        if (k.endsWith(suffix)) {
            const QString stem = k.left(k.size() - suffix.size());
            if (cats.contains(stem))
                return cats.value(stem);
        }
    }
    const QString t = titleCase(raw.trimmed());
    return t.isEmpty() ? QStringLiteral("Other") : t;
}

std::optional<QPair<int, int>> parseRate(const QString &text)
{
    static const QRegularExpression rate = rx(QStringLiteral("(\\d+)\\s*[/⁄]\\s*(\\d+)"));
    static const QRegularExpression typo = rx(QStringLiteral("^\\s*1(\\d+)\\s*[/⁄]\\s*$"));
    auto m = rate.match(text);
    if (m.hasMatch())
        return QPair<int, int>(m.captured(1).toInt(), m.captured(2).toInt());
    m = typo.match(text);
    if (m.hasMatch())
        return QPair<int, int>(1, m.captured(1).toInt());
    return std::nullopt;
}

QString esc(const QString &s)
{
    QString out;
    out.reserve(s.size());
    for (const QChar c : s) {
        switch (c.unicode()) {
        case '&': out += QStringLiteral("&amp;"); break;
        case '<': out += QStringLiteral("&lt;"); break;
        case '>': out += QStringLiteral("&gt;"); break;
        case '"': out += QStringLiteral("&quot;"); break;
        case '\'': out += QStringLiteral("&#x27;"); break;
        default: out += c;
        }
    }
    return out;
}

QString thousands(long long v)
{
    QString digits = QString::number(v < 0 ? -v : v);
    for (qsizetype i = digits.size() - 3; i > 0; i -= 3)
        digits.insert(i, QLatin1Char(','));
    return v < 0 ? QLatin1Char('-') + digits : digits;
}

} // namespace dq9
