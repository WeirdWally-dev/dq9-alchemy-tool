#include "core/robots.h"

#include "core/text.h"

#include <QUrl>

namespace dq9 {

Robots Robots::allowAll()
{
    Robots r;
    r.m_allowAll = true;
    return r;
}

Robots Robots::disallowAll()
{
    Robots r;
    r.m_disallowAll = true;
    return r;
}

Robots Robots::parse(const QString &text)
{
    Robots robots;
    int state = 0;
    Entry entry;
    auto addEntry = [&](const Entry &e) {
        if (e.agents.contains(QStringLiteral("*"))) {
            if (!robots.m_hasDefault) {   // the first "*" entry is the default
                robots.m_default = e;
                robots.m_hasDefault = true;
            }
        } else {
            robots.m_entries.append(e);
        }
    };
    for (QString line : text.split(QLatin1Char('\n'))) {
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        if (line.isEmpty()) {
            if (state == 1) {
                entry = Entry();
                state = 0;
            } else if (state == 2) {
                addEntry(entry);
                entry = Entry();
                state = 0;
            }
        }
        const qsizetype hash = line.indexOf(QLatin1Char('#'));
        if (hash >= 0)
            line = line.left(hash);
        line = line.trimmed();
        if (line.isEmpty())
            continue;
        const qsizetype colon = line.indexOf(QLatin1Char(':'));
        if (colon < 0)
            continue;
        const QString field = line.left(colon).trimmed().toLower();
        const QString value = pyUnquote(line.mid(colon + 1).trimmed());
        if (field == QStringLiteral("user-agent")) {
            if (state == 2) {
                addEntry(entry);
                entry = Entry();
            }
            entry.agents << value;
            state = 1;
        } else if (field == QStringLiteral("disallow") || field == QStringLiteral("allow")) {
            if (state != 0) {
                Rule rule;
                rule.allowance = field == QStringLiteral("allow");
                if (value.isEmpty() && !rule.allowance)
                    rule.allowance = true;   // "Disallow:" with no path allows everything
                rule.path = pyQuote(value);
                entry.rules << rule;
                state = 2;
            }
        }
    }
    if (state == 2)
        addEntry(entry);
    return robots;
}

bool Robots::Entry::appliesTo(const QString &userAgent) const
{
    const QString ua = userAgent.section(QLatin1Char('/'), 0, 0).toLower();
    for (const QString &agent : agents) {
        if (agent == QStringLiteral("*"))
            return true;
        if (ua.contains(agent.toLower()))
            return true;
    }
    return false;
}

bool Robots::Entry::allowance(const QString &filename) const
{
    for (const Rule &r : rules)
        if (r.appliesTo(filename))
            return r.allowance;
    return true;
}

bool Robots::canFetch(const QString &userAgent, const QString &url) const
{
    if (m_disallowAll)
        return false;
    if (m_allowAll)
        return true;
    // Path, params, query and fragment of the url, unquoted then quoted again (as robotparser does).
    const QString plain = pyUnquote(url);
    QString rest = plain;
    const qsizetype scheme = plain.indexOf(QStringLiteral("://"));
    if (scheme >= 0) {
        const qsizetype slash = plain.indexOf(QLatin1Char('/'), scheme + 3);
        const qsizetype query = plain.indexOf(QLatin1Char('?'), scheme + 3);
        qsizetype start = slash;
        if (start < 0 || (query >= 0 && query < start))
            start = query;
        rest = start < 0 ? QString() : plain.mid(start);
    }
    QString filename = pyQuote(rest);
    if (filename.isEmpty())
        filename = QStringLiteral("/");
    for (const Entry &e : m_entries)
        if (e.appliesTo(userAgent))
            return e.allowance(filename);
    if (m_hasDefault)
        return m_default.allowance(filename);
    return true;
}

} // namespace dq9
