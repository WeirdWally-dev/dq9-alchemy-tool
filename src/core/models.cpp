#include "core/models.h"

#include "core/text.h"

#include <QCryptographicHash>
#include <QHash>

namespace dq9 {

QString errorText(const std::exception &e)
{
    return QString::fromStdString(e.what());
}

QString pairsValue(const Pairs &p, const QString &key, const QString &def)
{
    for (const auto &kv : p)
        if (kv.first == key)
            return kv.second;
    return def;
}

void pairsSet(Pairs &p, const QString &key, const QString &value)
{
    for (auto &kv : p) {
        if (kv.first == key) {
            kv.second = value;
            return;
        }
    }
    p.append({key, value});
}

QString Drop::oddsText() const
{
    if (!den)
        return note.isEmpty() ? QStringLiteral("?") : note;
    return QStringLiteral("%1/%2 (%3%)").arg(num).arg(den).arg(prob() * 100, 0, 'f', 2);
}

QJsonObject Drop::toJson() const
{
    return {{"item", item}, {"kind", kind}, {"num", num}, {"den", den}, {"title", title}, {"note", note}};
}

Drop Drop::fromJson(const QJsonObject &o)
{
    Drop d;
    d.item = o.value("item").toString();
    d.kind = o.value("kind").toString();
    d.num = o.value("num").toInt();
    d.den = o.value("den").toInt();
    d.title = o.value("title").toString();
    d.note = o.value("note").toString();
    return d;
}

QJsonObject Enemy::toJson() const
{
    QJsonArray ds;
    for (const auto &d : drops)
        ds.append(d.toJson());
    return {{"number", number}, {"name", name}, {"url", url}, {"gold", gold}, {"exp", exp}, {"drops", ds}};
}

Enemy Enemy::fromJson(const QJsonObject &o)
{
    Enemy e;
    e.number = o.value("number").toInt();
    e.name = o.value("name").toString();
    e.url = o.value("url").toString();
    e.gold = o.value("gold").toString();
    e.exp = o.value("exp").toString();
    for (const auto &v : o.value("drops").toArray())
        e.drops.append(Drop::fromJson(v.toObject()));
    return e;
}

QJsonObject Recipe::toJson() const
{
    QJsonArray ings;
    for (const auto &i : ingredients)
        ings.append(QJsonArray{i.name, i.qty});
    QJsonObject ex;
    for (const auto &kv : extra)
        ex.insert(kv.first, kv.second);
    return {{"item", item}, {"category", category}, {"ingredients", ings}, {"extra", ex}, {"source", source}};
}

Recipe Recipe::fromJson(const QJsonObject &o)
{
    Recipe r;
    r.item = o.value("item").toString();
    r.category = o.value("category").toString();
    for (const auto &v : o.value("ingredients").toArray()) {
        const QJsonArray a = v.toArray();
        r.ingredients.append({a.at(0).toString(), a.at(1).toInt(1)});
    }
    const QJsonObject ex = o.value("extra").toObject();
    for (auto it = ex.begin(); it != ex.end(); ++it)
        r.extra.append({it.key(), it.value().toString()});
    r.source = o.value("source").toString();
    return r;
}

bool Recipe::operator==(const Recipe &o) const
{
    return item == o.item && category == o.category && ingredients == o.ingredients && extra == o.extra
        && source == o.source;
}

QString wikiImageUrl(const QString &fileName)
{
    static const QRegularExpression prefix = rx(QStringLiteral("^(File|Image):"), true);
    QString n = sub(fileName.trimmed(), prefix, QString()).replace(QLatin1Char(' '), QLatin1Char('_'));
    if (n.isEmpty())
        return QString();
    n = n.left(1).toUpper() + n.mid(1);
    const QString h = QString::fromLatin1(QCryptographicHash::hash(n.toUtf8(), QCryptographicHash::Md5).toHex());
    return QStringLiteral("%1/%2/%3/%4").arg(WIKI_IMAGES, h.left(1), h.left(2), pyQuote(n, QStringLiteral("_()!,-.~")));
}

QString Weapon::spriteUrl() const
{
    return image.isEmpty() ? QString() : wikiImageUrl(image);
}

QStringList Weapon::equipNames() const
{
    static const QHash<QString, QString> voc = {
        {"WA", "Warrior"}, {"PR", "Priest"}, {"MG", "Mage"}, {"MA", "Martial Artist"}, {"TH", "Thief"},
        {"MI", "Minstrel"}, {"GL", "Gladiator"}, {"PL", "Paladin"}, {"PA", "Paladin"}, {"AR", "Armamentalist"},
        {"RA", "Ranger"}, {"SA", "Sage"}, {"LU", "Luminary"},
    };
    QStringList out;
    for (const auto &v : equip)
        out << voc.value(v, v);
    return out;
}

QJsonObject Details::toJson() const
{
    if (isEmpty())
        return {};
    QJsonArray fs;
    for (const auto &f : fields)
        fs.append(QJsonArray{f.first, f.second});
    return {{"description", description}, {"fields", fs}};
}

Details Details::fromJson(const QJsonObject &o)
{
    Details d;
    d.description = o.value("description").toString();
    for (const auto &v : o.value("fields").toArray()) {
        const QJsonArray a = v.toArray();
        d.fields.append({a.at(0).toString(), a.at(1).toString()});
    }
    return d;
}

} // namespace dq9
