// robots.txt rules, matching Python's urllib.robotparser so the app is exactly as polite as the
// desktop version (first matching rule wins; "*" is only used when no named agent matches).
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace dq9 {

class Robots {
public:
    static Robots allowAll();
    static Robots disallowAll();
    static Robots parse(const QString &text);

    bool canFetch(const QString &userAgent, const QString &url) const;

private:
    struct Rule {
        QString path;
        bool allowance;
        bool appliesTo(const QString &filename) const { return path == QStringLiteral("*") || filename.startsWith(path); }
    };
    struct Entry {
        QStringList agents;
        QList<Rule> rules;
        bool appliesTo(const QString &userAgent) const;
        bool allowance(const QString &filename) const;
    };

    bool m_allowAll = false;
    bool m_disallowAll = false;
    QList<Entry> m_entries;
    bool m_hasDefault = false;
    Entry m_default;
};

} // namespace dq9
