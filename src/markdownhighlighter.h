#pragma once

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

class MarkdownHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    explicit MarkdownHighlighter(QTextDocument *document);

    void setDarkMode(bool darkMode);
    void setColors(const QString &background, const QString &foreground, const QString &accent);
    void setSearch(const QString &query, int currentMatchStart);
    void refreshFont();

    // Live preview: Markdown markers are revealed only on the block holding
    // the caret and hidden everywhere else.
    void setActiveBlock(int blockNumber);
    int activeBlock() const { return m_activeBlock; }

    struct Span {
        int start;
        int length;
    };

    enum class InlineKind { Bold, Italic, BoldItalic, Strike, Link, Footnote };

    struct InlineMarkup {
        InlineKind kind;
        Span content;
        Span markers[2];
    };

    // Single source of truth for inline markdown spans: the highlighter uses it
    // to style content and hide markers.
    static QList<InlineMarkup> inlineMarkup(const QString &text);

    enum class BlockKind {
        Paragraph,
        Heading,
        Quote,
        ListItem,
        SceneBreak,
        PageBreak,
        Centered,
        FootnoteDefinition,
        Fence,
    };

    // Classifies one line of Markdown, ignoring fenced code (which needs the
    // surrounding blocks). The editor uses it to lay blocks out (centering,
    // quote indents) and the highlighter to style them.
    static BlockKind blockKind(const QString &text, int *headingLevel = nullptr);

protected:
    void highlightBlock(const QString &text) override;

private:
    void rebuildFormats();
    void mergeFormat(int start, int length, const QTextCharFormat &format);
    void hideMarker(int start, int length, bool active);
    void highlightBlockMarkers(const QString &text, bool active, int *inlineFrom);
    void highlightInline(const QString &text, int from, bool active);
    void highlightSearch(const QString &text);

    bool m_darkMode = true;
    int m_activeBlock = -1;
    QString m_customBackground;
    QString m_customForeground;
    QString m_customAccent;
    QTextCharFormat m_markerFormat;
    QTextCharFormat m_hiddenMarkerFormat;
    QTextCharFormat m_headingFormats[6];
    QTextCharFormat m_boldFormat;
    QTextCharFormat m_italicFormat;
    QTextCharFormat m_strikeFormat;
    QTextCharFormat m_codeFormat;
    QTextCharFormat m_quoteFormat;
    QTextCharFormat m_linkFormat;
    QTextCharFormat m_footnoteFormat;
    QTextCharFormat m_footnoteDefinitionFormat;
    QTextCharFormat m_breakFormat;
    QString m_searchQuery;
    int m_currentMatchStart = -1;
    QTextCharFormat m_searchFormat;
    QTextCharFormat m_currentSearchFormat;
};
