#include "core/xlsx.h"

#include "core/parsers.h"
#include "core/text.h"

#include <QHash>
#include <QXmlStreamReader>

#include "miniz.h"

#include <memory>

namespace dq9 {

namespace {

class Zip {
public:
    explicit Zip(const QByteArray &data) : m_data(data)
    {
        mz_zip_zero_struct(&m_zip);
        m_ok = mz_zip_reader_init_mem(&m_zip, m_data.constData(), size_t(m_data.size()), 0);
    }
    ~Zip()
    {
        if (m_ok)
            mz_zip_reader_end(&m_zip);
    }
    bool ok() const { return m_ok; }
    // nullopt if the entry doesn't exist
    std::optional<QByteArray> read(const QString &name)
    {
        const int idx = mz_zip_reader_locate_file(&m_zip, name.toUtf8().constData(), nullptr, 0);
        if (idx < 0)
            return std::nullopt;
        size_t size = 0;
        void *p = mz_zip_reader_extract_to_heap(&m_zip, mz_uint(idx), &size, 0);
        if (!p)
            return std::nullopt;
        QByteArray out(static_cast<const char *>(p), qsizetype(size));
        mz_free(p);
        return out;
    }

private:
    QByteArray m_data;
    mz_zip_archive m_zip;
    bool m_ok = false;
};

// "AB12" -> 27 (zero-based column index), -1 if there is no column part.
int columnIndex(const QString &ref)
{
    int col = 0, letters = 0;
    for (const QChar c : ref) {
        if (!c.isLetter())
            break;
        col = col * 26 + (c.toUpper().unicode() - 'A' + 1);
        ++letters;
    }
    return letters ? col - 1 : -1;
}

int rowIndex(const QString &ref)
{
    qsizetype i = 0;
    while (i < ref.size() && ref.at(i).isLetter())
        ++i;
    bool ok = false;
    const int r = ref.mid(i).toInt(&ok);
    return ok ? r : -1;
}

// Text of an <si> or <is> element: plain <t>, or the <t>s of its rich-text runs (phonetic <rPh> skipped).
QString readRichText(QXmlStreamReader &xml)
{
    const QString end = xml.name().toString();
    QString text;
    int skipDepth = 0;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            if (xml.name() == u"rPh" || skipDepth)
                ++skipDepth;
            else if (xml.name() == u"t")
                text += xml.readElementText();
        } else if (xml.isEndElement()) {
            if (skipDepth)
                --skipDepth;
            else if (xml.name() == end)
                break;
        }
    }
    return text;
}

QStringList sharedStrings(Zip &zip)
{
    QStringList out;
    const auto data = zip.read(QStringLiteral("xl/sharedStrings.xml"));
    if (!data)
        return out;
    QXmlStreamReader xml(*data);
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == u"si")
            out << readRichText(xml);
    }
    return out;
}

// Sheet XML paths in workbook order.
QStringList sheetPaths(Zip &zip)
{
    QHash<QString, QString> rels;
    if (const auto data = zip.read(QStringLiteral("xl/_rels/workbook.xml.rels"))) {
        QXmlStreamReader xml(*data);
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement() && xml.name() == u"Relationship") {
                QString target = xml.attributes().value(u"Target").toString();
                if (target.startsWith(QLatin1Char('/')))
                    target = target.mid(1);
                else
                    target = QStringLiteral("xl/") + target;
                rels.insert(xml.attributes().value(u"Id").toString(), target);
            }
        }
    }
    QStringList out;
    if (const auto data = zip.read(QStringLiteral("xl/workbook.xml"))) {
        QXmlStreamReader xml(*data);
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement() && xml.name() == u"sheet") {
                // r:id, whatever the relationships namespace prefix is
                for (const auto &a : xml.attributes())
                    if (a.name() == u"id" && rels.contains(a.value().toString()))
                        out << rels.value(a.value().toString());
            }
        }
    }
    return out;
}

SheetRows readSheet(const QByteArray &data, const QStringList &strings)
{
    QMap<int, QStringList> rows;   // row number -> cells
    QXmlStreamReader xml(data);
    int rowNum = 0;
    int nextCol = 0;
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement())
            continue;
        if (xml.name() == u"row") {
            const int r = xml.attributes().value(u"r").toInt();
            rowNum = r > 0 ? r : rowNum + 1;
            nextCol = 0;
            rows[rowNum];
        } else if (xml.name() == u"c") {
            const QString ref = xml.attributes().value(u"r").toString();
            const QString type = xml.attributes().value(u"t").toString();
            int col = columnIndex(ref);
            if (col < 0)
                col = nextCol;
            nextCol = col + 1;
            if (const int r = rowIndex(ref); r > 0)
                rowNum = r;
            QString value;
            bool have = false;
            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.isEndElement() && xml.name() == u"c")
                    break;
                if (!xml.isStartElement())
                    continue;
                if (xml.name() == u"v") {
                    value = xml.readElementText();
                    have = true;
                } else if (xml.name() == u"is") {
                    value = readRichText(xml);
                    have = true;
                }
            }
            if (!have)
                continue;
            if (type == u"s") {
                const int i = value.toInt();
                value = (i >= 0 && i < strings.size()) ? strings[i] : QString();
            } else if (type == u"b") {
                value = value == u"1" ? QStringLiteral("True") : QStringLiteral("False");
            }
            QStringList &cells = rows[rowNum];
            while (cells.size() <= col)
                cells << QString();
            cells[col] = value.isNull() ? QStringLiteral("") : value;
        }
    }
    SheetRows out;
    if (rows.isEmpty())
        return out;
    int width = 0;
    for (const auto &r : rows)
        width = std::max(width, int(r.size()));
    for (int r = rows.firstKey(); r <= rows.lastKey(); ++r) {
        QStringList cells = rows.value(r);
        while (cells.size() < width)
            cells << QString();
        out << cells;
    }
    return out;
}

// "Weapon Name" -> "Weapon"; "" when the header isn't "<word> name".
QString groupOf(const QString &header)
{
    static const QRegularExpression re = rx(QStringLiteral("^(\\w+)\\s+name$"));
    static const QHash<QString, QString> words = {
        {"weapon", "Weapon"}, {"armor", "Armor"}, {"armour", "Armor"}, {"accessory", "Accessory"}, {"item", "Item"}};
    const auto m = re.match(squash(header).toLower());
    if (!m.hasMatch())
        return QString();
    return words.value(m.captured(1), titleCase(m.captured(1)));
}

} // namespace

QList<SheetRows> readXlsx(const QByteArray &fileData)
{
    Zip zip(fileData);
    if (!zip.ok())
        throw ParseError(QStringLiteral("That file isn't an .xlsx workbook (it couldn't be opened as one)."));
    const QStringList strings = sharedStrings(zip);
    QList<SheetRows> sheets;
    for (const QString &path : sheetPaths(zip))
        if (const auto data = zip.read(path))
            sheets << readSheet(*data, strings);
    if (sheets.isEmpty())
        throw ParseError(QStringLiteral("That workbook has no readable sheets."));
    return sheets;
}

QList<Recipe> parseRecipeSheet(const QByteArray &fileData)
{
    SheetColumns columns;
    auto columnFor = [&](const QString &group) -> QList<QPair<QString, QString>> & {
        for (auto &c : columns)
            if (c.first == group)
                return c.second;
        columns.append({group, {}});
        return columns.last().second;
    };
    QStringList firstRows;
    for (const SheetRows &rows : readXlsx(fileData)) {
        bool used = false;
        for (int h = 0; h < rows.size() && h < 10 && !used; ++h) {
            const QStringList &header = rows[h];
            QList<QPair<int, QString>> found;
            for (int c = 0; c < header.size(); ++c) {
                const QString group = groupOf(header[c]);
                const QString next = c + 1 < header.size() ? header[c + 1].toLower() : QString();
                if (!group.isEmpty() && (next.contains(QStringLiteral("material")) || next.contains(QStringLiteral("ingredient"))))
                    found.append({c, group});
            }
            if (found.isEmpty())
                continue;
            for (const auto &[c, group] : found) {
                for (int r = h + 1; r < rows.size(); ++r) {
                    const QString name = c < rows[r].size() ? rows[r][c] : QString();
                    const QString mats = c + 1 < rows[r].size() ? rows[r][c + 1] : QString();
                    if (!name.trimmed().isEmpty())
                        columnFor(group).append({name, mats.isNull() ? QStringLiteral("") : mats});
                }
            }
            used = true;
        }
        if (!used) {
            QStringList first;
            for (const QString &x : rows.value(0))
                if (!x.isNull())
                    first << x;
            firstRows << first.join(QStringLiteral(" | ")).left(100);
        }
    }
    const QList<Recipe> recipes = recipesFromColumns(columns);
    if (recipes.isEmpty())
        throw ParseError(QStringLiteral("No recipes found. The spreadsheet needs column pairs headed like \"Weapon Name\" / "
                                        "\"Materials Required\". First row(s) seen: ")
                         + (firstRows.isEmpty() ? QStringLiteral("(empty)") : firstRows.join(QStringLiteral("; "))));
    return recipes;
}

} // namespace dq9
