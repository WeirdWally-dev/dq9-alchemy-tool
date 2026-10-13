// A small DOM built from the gumbo HTML5 parser, with just the BeautifulSoup-style queries the
// scrapers need (find_all, get_text, next_siblings, descendants, ...).
#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <vector>

namespace dq9::html {

struct Node {
    enum Type { Document, Element, Text };
    Type type = Element;
    QString tag;                       // lower-case element name ("" for text and document)
    QHash<QString, QString> attrs;
    QString text;                      // text nodes only
    Node *parent = nullptr;
    std::vector<std::unique_ptr<Node>> children;

    bool isElement() const { return type == Element; }
    bool isText() const { return type == Text; }
    bool is(const QString &name) const { return type == Element && tag == name; }
    QString attr(const QString &name) const { return attrs.value(name); }
    bool hasClass(const QString &cls) const;

    // Children / siblings
    Node *nextSibling() const;
    std::vector<Node *> nextSiblings() const;
    std::vector<Node *> childElements(const QStringList &names = {}) const;   // recursive=False

    // Pre-order walk over every node below this one (not including this one).
    void walk(const std::function<void(Node *)> &fn) const;
    std::vector<Node *> descendants() const;

    // Elements below this one whose tag is in names (all elements when names is empty).
    std::vector<Node *> findAll(const QStringList &names = {}) const;
    std::vector<Node *> findAll(const std::function<bool(const Node *)> &pred) const;
    Node *find(const QStringList &names) const;
    Node *findById(const QString &id) const;
    Node *findParent(const QString &name) const;

    // BeautifulSoup get_text(separator, strip)
    QString getText(const QString &separator = QString(), bool strip = false) const;
    // Every text node below this one, in order.
    std::vector<Node *> strings() const;
};

class Document {
public:
    explicit Document(const QString &html);
    Node *root() const { return m_root.get(); }
    Node *body() const;
    Node *findById(const QString &id) const { return m_root->findById(id); }

private:
    std::unique_ptr<Node> m_root;
};

// Text of an element, with <br> and block elements turned into newlines (scripts and styles
// skipped). cellBreaks also ends every <td>/<th> with a newline, keeping a table's label and
// value apart.
QString flatten(const Node *el, bool cellBreaks = false);

// Split flattened text into trimmed, whitespace-collapsed, non-empty lines.
QStringList cleanLines(const QString &text);

} // namespace dq9::html
