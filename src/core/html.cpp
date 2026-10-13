#include "core/html.h"

#include "core/text.h"

extern "C" {
#include "gumbo.h"
}

#include <QSet>

namespace dq9::html {

namespace {

QString tagName(const GumboElement &el)
{
    if (el.tag != GUMBO_TAG_UNKNOWN)
        return QString::fromUtf8(gumbo_normalized_tagname(el.tag));
    GumboStringPiece piece = el.original_tag;
    gumbo_tag_from_original_text(&piece);
    return QString::fromUtf8(piece.data, int(piece.length)).toLower();
}

void convert(const GumboNode *src, Node *dst)
{
    const GumboVector *kids = nullptr;
    if (src->type == GUMBO_NODE_DOCUMENT)
        kids = &src->v.document.children;
    else if (src->type == GUMBO_NODE_ELEMENT || src->type == GUMBO_NODE_TEMPLATE)
        kids = &src->v.element.children;
    if (!kids)
        return;
    for (unsigned i = 0; i < kids->length; ++i) {
        const auto *child = static_cast<const GumboNode *>(kids->data[i]);
        auto node = std::make_unique<Node>();
        node->parent = dst;
        switch (child->type) {
        case GUMBO_NODE_ELEMENT:
        case GUMBO_NODE_TEMPLATE: {
            node->type = Node::Element;
            node->tag = tagName(child->v.element);
            const GumboVector &attrs = child->v.element.attributes;
            for (unsigned a = 0; a < attrs.length; ++a) {
                const auto *attr = static_cast<const GumboAttribute *>(attrs.data[a]);
                node->attrs.insert(QString::fromUtf8(attr->name).toLower(), QString::fromUtf8(attr->value));
            }
            convert(child, node.get());
            break;
        }
        case GUMBO_NODE_TEXT:
        case GUMBO_NODE_WHITESPACE:
        case GUMBO_NODE_CDATA:
            node->type = Node::Text;
            node->text = QString::fromUtf8(child->v.text.text);
            break;
        default:   // comments and anything else carry no text we want
            continue;
        }
        dst->children.push_back(std::move(node));
    }
}

const QSet<QString> &blocks()
{
    static const QSet<QString> b = {"li", "p", "div", "tr", "dd", "dt", "ul", "ol", "table",
                                    "h1", "h2", "h3", "h4"};
    return b;
}

} // namespace

bool Node::hasClass(const QString &cls) const
{
    return attr(QStringLiteral("class")).split(QLatin1Char(' '), Qt::SkipEmptyParts).contains(cls);
}

Node *Node::nextSibling() const
{
    if (!parent)
        return nullptr;
    const auto &sibs = parent->children;
    for (size_t i = 0; i + 1 < sibs.size(); ++i)
        if (sibs[i].get() == this)
            return sibs[i + 1].get();
    return nullptr;
}

std::vector<Node *> Node::nextSiblings() const
{
    std::vector<Node *> out;
    if (!parent)
        return out;
    bool after = false;
    for (const auto &c : parent->children) {
        if (after)
            out.push_back(c.get());
        else if (c.get() == this)
            after = true;
    }
    return out;
}

std::vector<Node *> Node::childElements(const QStringList &names) const
{
    std::vector<Node *> out;
    for (const auto &c : children)
        if (c->isElement() && (names.isEmpty() || names.contains(c->tag)))
            out.push_back(c.get());
    return out;
}

void Node::walk(const std::function<void(Node *)> &fn) const
{
    for (const auto &c : children) {
        fn(c.get());
        c->walk(fn);
    }
}

std::vector<Node *> Node::descendants() const
{
    std::vector<Node *> out;
    walk([&](Node *n) { out.push_back(n); });
    return out;
}

std::vector<Node *> Node::findAll(const QStringList &names) const
{
    return findAll([&](const Node *n) { return n->isElement() && (names.isEmpty() || names.contains(n->tag)); });
}

std::vector<Node *> Node::findAll(const std::function<bool(const Node *)> &pred) const
{
    std::vector<Node *> out;
    walk([&](Node *n) {
        if (pred(n))
            out.push_back(n);
    });
    return out;
}

Node *Node::find(const QStringList &names) const
{
    for (const auto &c : children) {
        if (c->isElement() && names.contains(c->tag))
            return c.get();
        if (Node *hit = c->find(names))
            return hit;
    }
    return nullptr;
}

Node *Node::findById(const QString &id) const
{
    for (const auto &c : children) {
        if (c->isElement() && c->attr(QStringLiteral("id")) == id)
            return c.get();
        if (Node *hit = c->findById(id))
            return hit;
    }
    return nullptr;
}

Node *Node::findParent(const QString &name) const
{
    for (Node *p = parent; p; p = p->parent)
        if (p->is(name))
            return p;
    return nullptr;
}

std::vector<Node *> Node::strings() const
{
    return findAll([](const Node *n) { return n->isText(); });
}

QString Node::getText(const QString &separator, bool strip) const
{
    if (isText())
        return strip ? text.trimmed() : text;
    QStringList parts;
    for (const Node *s : strings()) {
        const QString t = strip ? s->text.trimmed() : s->text;
        if (strip && t.isEmpty())
            continue;
        parts << t;
    }
    return parts.join(separator);
}

Document::Document(const QString &html)
    : m_root(std::make_unique<Node>())
{
    m_root->type = Node::Document;
    const QByteArray utf8 = html.toUtf8();
    GumboOutput *out = gumbo_parse_with_options(&kGumboDefaultOptions, utf8.constData(), size_t(utf8.size()));
    convert(out->document, m_root.get());
    gumbo_destroy_output(&kGumboDefaultOptions, out);
}

Node *Document::body() const
{
    return m_root->find({QStringLiteral("body")});
}

QString flatten(const Node *el, bool cellBreaks)
{
    if (!el)
        return QString();
    if (el->isText())
        return el->text;
    if (el->is(QStringLiteral("script")) || el->is(QStringLiteral("style")))
        return QString();
    if (el->is(QStringLiteral("br")))
        return QStringLiteral("\n");
    QString inner;
    for (const auto &c : el->children)
        inner += flatten(c.get(), cellBreaks);
    if (cellBreaks && (el->is(QStringLiteral("td")) || el->is(QStringLiteral("th"))))
        inner += QLatin1Char('\n');
    if (el->isElement() && blocks().contains(el->tag))
        return QLatin1Char('\n') + inner + QLatin1Char('\n');
    return inner;
}

QStringList cleanLines(const QString &text)
{
    QStringList out;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const QString t = squash(line);
        if (!t.isEmpty())
            out << t;
    }
    return out;
}

} // namespace dq9::html
