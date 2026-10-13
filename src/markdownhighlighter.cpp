#include "markdownhighlighter.h"

#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QTextBlock>
#include <QTextDocument>

#include <algorithm>

namespace {

constexpr int insideFenceState = 1;
constexpr qreal headingScales[6] = {1.6, 1.35, 1.15, 1.0, 1.0, 1.0};

const QRegularExpression &headingRe() {
    static const QRegularExpression re(QStringLiteral("^(#{1,6})(\\s+)(.*)$"));
    return re;
}

const QRegularExpression &quoteRe() {
    static const QRegularExpression re(QStringLiteral("^(\\s{0,3}>+\\s?)(.*)$"));
    return re;
}

const QRegularExpression &listRe() {
    static const QRegularExpression re(QStringLiteral("^(\\s*(?:[-+*]|\\d+[.)])\\s+)(.*)$"));
    return re;
}

const QRegularExpression &ruleRe() {
    static const QRegularExpression re(QStringLiteral("^\\s{0,3}([-*_])(?:\\s*\\1){2,}\\s*$"));
    return re;
}

const QRegularExpression &pageBreakRe() {
    static const QRegularExpression re(QStringLiteral("^(\\s*\\\\)(pagebreak|newpage)\\s*$"));
    return re;
}

const QRegularExpression &centeredRe() {
    static const QRegularExpression re(QStringLiteral("^(\\s*->\\s?)(.*?)(\\s?<-\\s*)$"));
    return re;
}

const QRegularExpression &footnoteDefinitionRe() {
    static const QRegularExpression re(QStringLiteral("^(\\[\\^[^\\]\\s]+\\]:)(\\s*)(.*)$"));
    return re;
}

const QRegularExpression &fenceRe() {
    static const QRegularExpression re(QStringLiteral("^\\s{0,3}(```|~~~)"));
    return re;
}

QColor blend(const QColor &a, const QColor &b, qreal amountOfA) {
    return QColor::fromRgbF(a.redF() * amountOfA + b.redF() * (1 - amountOfA),
                            a.greenF() * amountOfA + b.greenF() * (1 - amountOfA),
                            a.blueF() * amountOfA + b.blueF() * (1 - amountOfA));
}

} // namespace

MarkdownHighlighter::MarkdownHighlighter(QTextDocument *document)
    : QSyntaxHighlighter(document) {
    rebuildFormats();
}

void MarkdownHighlighter::setDarkMode(bool darkMode) {
    if (m_darkMode == darkMode)
        return;

    m_darkMode = darkMode;
    rebuildFormats();
    rehighlight();
}

// The hidden marker spacing and heading sizes are measured from the document's
// default font, so they have to be remeasured whenever the editor font changes.
void MarkdownHighlighter::refreshFont() {
    rebuildFormats();
    rehighlight();
}

void MarkdownHighlighter::setColors(const QString &background, const QString &foreground,
                                    const QString &accent) {
    if (m_customBackground == background && m_customForeground == foreground
            && m_customAccent == accent)
        return;

    m_customBackground = background;
    m_customForeground = foreground;
    m_customAccent = accent;
    rebuildFormats();
    rehighlight();
}

void MarkdownHighlighter::setSearch(const QString &query, int currentMatchStart) {
    if (m_searchQuery == query && m_currentMatchStart == currentMatchStart)
        return;
    m_searchQuery = query;
    m_currentMatchStart = currentMatchStart;
    rehighlight();
}

void MarkdownHighlighter::setActiveBlock(int blockNumber) {
    if (m_activeBlock == blockNumber)
        return;

    const int previous = m_activeBlock;
    m_activeBlock = blockNumber;
    if (!document())
        return;

    const QTextBlock previousBlock = document()->findBlockByNumber(previous);
    if (previousBlock.isValid())
        rehighlightBlock(previousBlock);
    const QTextBlock activeBlock = document()->findBlockByNumber(blockNumber);
    if (activeBlock.isValid())
        rehighlightBlock(activeBlock);
}

void MarkdownHighlighter::rebuildFormats() {
    const QColor marker = m_darkMode ? QColor(QStringLiteral("#4f525a"))
                                     : QColor(QStringLiteral("#aeb1b5"));
    const QColor background = !m_customBackground.isEmpty() ? QColor(m_customBackground)
        : (m_darkMode ? QColor(QStringLiteral("#101010")) : QColor(QStringLiteral("#ffffff")));
    const QColor text = !m_customForeground.isEmpty() ? QColor(m_customForeground)
        : (m_darkMode ? QColor(QStringLiteral("#eeeeee")) : QColor(QStringLiteral("#222324")));
    const QColor link = !m_customAccent.isEmpty() ? QColor(m_customAccent)
        : (m_darkMode ? QColor(QStringLiteral("#5584aa")) : QColor(QStringLiteral("#2077b2")));
    const QColor softText = blend(text, background, 0.72);
    const QColor codeBackground = m_darkMode ? QColor(QStringLiteral("#1c1a1a"))
                                             : QColor(QStringLiteral("#f8f8f8"));

    const QFont baseFont = document() ? document()->defaultFont() : QFont();
    const auto scaledFormat = [&baseFont](qreal scale) {
        QTextCharFormat format;
        if (baseFont.pixelSize() > 0)
            format.setProperty(QTextFormat::FontPixelSize,
                               qRound(baseFont.pixelSize() * scale));
        else if (baseFont.pointSizeF() > 0)
            format.setFontPointSize(baseFont.pointSizeF() * scale);
        return format;
    };

    m_markerFormat = QTextCharFormat();
    m_markerFormat.setForeground(marker);

    // A sub-pixel font size combined with a stretch factor used to make these
    // markers occupy (close to) zero space, but that combination deadlocks Qt's
    // font metrics engine on some platforms. Instead, use a normal font size and
    // cancel out its advance width with negative absolute letter-spacing.
    m_hiddenMarkerFormat = QTextCharFormat();
    m_hiddenMarkerFormat.setForeground(background);
    m_hiddenMarkerFormat.setFontPointSize(1.0);

    QFont hiddenFont = baseFont;
    hiddenFont.setPointSizeF(1.0);
    const qreal charWidth = QFontMetricsF(hiddenFont).horizontalAdvance(QLatin1Char('['));

    m_hiddenMarkerFormat.setFontLetterSpacingType(QFont::AbsoluteSpacing);
    m_hiddenMarkerFormat.setFontLetterSpacing(-charWidth);

    for (int level = 0; level < 6; ++level) {
        m_headingFormats[level] = scaledFormat(headingScales[level]);
        m_headingFormats[level].setForeground(text);
        m_headingFormats[level].setFontWeight(QFont::Bold);
    }

    m_boldFormat = QTextCharFormat();
    m_boldFormat.setFontWeight(QFont::Bold);
    m_boldFormat.setForeground(text);

    m_italicFormat = QTextCharFormat();
    m_italicFormat.setFontItalic(true);

    m_strikeFormat = QTextCharFormat();
    m_strikeFormat.setFontStrikeOut(true);
    m_strikeFormat.setForeground(softText);

    m_codeFormat = QTextCharFormat();
    m_codeFormat.setForeground(text);
    m_codeFormat.setBackground(codeBackground);

    m_quoteFormat = QTextCharFormat();
    m_quoteFormat.setForeground(softText);

    m_linkFormat = QTextCharFormat();
    m_linkFormat.setForeground(link);
    m_linkFormat.setFontUnderline(true);

    m_footnoteFormat = QTextCharFormat();
    m_footnoteFormat.setForeground(link);
    m_footnoteFormat.setVerticalAlignment(QTextCharFormat::AlignSuperScript);

    m_footnoteDefinitionFormat = scaledFormat(0.85);
    m_footnoteDefinitionFormat.setForeground(softText);

    m_breakFormat = QTextCharFormat();
    m_breakFormat.setForeground(marker);
    m_breakFormat.setFontLetterSpacingType(QFont::PercentageSpacing);
    m_breakFormat.setFontLetterSpacing(160);

    m_searchFormat = QTextCharFormat();
    m_searchFormat.setBackground(m_darkMode ? QColor(QStringLiteral("#725b18"))
                                            : QColor(QStringLiteral("#ffe58a")));
    m_currentSearchFormat = QTextCharFormat();
    m_currentSearchFormat.setBackground(m_darkMode ? QColor(QStringLiteral("#b36b20"))
                                                   : QColor(QStringLiteral("#ffad42")));
}

MarkdownHighlighter::BlockKind MarkdownHighlighter::blockKind(const QString &text,
                                                              int *headingLevel) {
    int first = 0;
    while (first < text.length() && text.at(first).isSpace())
        ++first;
    if (first >= text.length())
        return BlockKind::Paragraph;

    const QChar c = text.at(first);
    if (c == QLatin1Char('#') && first == 0) {
        const QRegularExpressionMatch heading = headingRe().match(text);
        if (heading.hasMatch()) {
            if (headingLevel)
                *headingLevel = int(heading.capturedLength(1));
            return BlockKind::Heading;
        }
    }
    if (c == QLatin1Char('`') || c == QLatin1Char('~')) {
        if (fenceRe().match(text).hasMatch())
            return BlockKind::Fence;
    }
    if (c == QLatin1Char('>') && quoteRe().match(text).hasMatch())
        return BlockKind::Quote;
    if ((c == QLatin1Char('-') || c == QLatin1Char('*') || c == QLatin1Char('_'))
            && ruleRe().match(text).hasMatch())
        return BlockKind::SceneBreak;
    if (c == QLatin1Char('-') && centeredRe().match(text).hasMatch())
        return BlockKind::Centered;
    if ((c == QLatin1Char('-') || c == QLatin1Char('+') || c == QLatin1Char('*')
         || c.isDigit()) && listRe().match(text).hasMatch())
        return BlockKind::ListItem;
    if (c == QLatin1Char('\\') && pageBreakRe().match(text).hasMatch())
        return BlockKind::PageBreak;
    if (c == QLatin1Char('[') && first == 0 && footnoteDefinitionRe().match(text).hasMatch())
        return BlockKind::FootnoteDefinition;
    return BlockKind::Paragraph;
}

void MarkdownHighlighter::mergeFormat(int start, int length, const QTextCharFormat &format) {
    // QSyntaxHighlighter::setFormat replaces whatever was there, so nested
    // styles (italic inside a heading, bold inside a quote) merge per run.
    const int end = start + length;
    int runStart = start;
    while (runStart < end) {
        const QTextCharFormat runFormat = this->format(runStart);
        int runEnd = runStart + 1;
        while (runEnd < end && this->format(runEnd) == runFormat)
            ++runEnd;
        QTextCharFormat merged = runFormat;
        merged.merge(format);
        setFormat(runStart, runEnd - runStart, merged);
        runStart = runEnd;
    }
}

void MarkdownHighlighter::hideMarker(int start, int length, bool active) {
    if (length <= 0)
        return;
    if (active)
        mergeFormat(start, length, m_markerFormat);
    else
        setFormat(start, length, m_hiddenMarkerFormat);
}

void MarkdownHighlighter::highlightBlock(const QString &text) {
    const bool insideFence = previousBlockState() == insideFenceState;
    const bool fenceLine = fenceRe().match(text).hasMatch();
    if (insideFence || fenceLine) {
        // A fence line opens a block outside one and closes it inside one.
        setCurrentBlockState(insideFence != fenceLine ? insideFenceState : 0);
        if (!text.isEmpty())
            setFormat(0, text.length(), fenceLine ? m_markerFormat : m_codeFormat);
        highlightSearch(text);
        return;
    }
    setCurrentBlockState(0);

    if (!text.isEmpty()) {
        const bool active = currentBlock().blockNumber() == m_activeBlock;
        int inlineFrom = 0;
        highlightBlockMarkers(text, active, &inlineFrom);
        if (inlineFrom >= 0)
            highlightInline(text, inlineFrom, active);
    }
    highlightSearch(text);
}

void MarkdownHighlighter::highlightSearch(const QString &text) {
    if (m_searchQuery.isEmpty())
        return;

    int from = 0;
    while ((from = text.indexOf(m_searchQuery, from, Qt::CaseInsensitive)) >= 0) {
        const int documentStart = currentBlock().position() + from;
        mergeFormat(from, m_searchQuery.length(),
                    documentStart == m_currentMatchStart ? m_currentSearchFormat
                                                         : m_searchFormat);
        from += qMax(1, m_searchQuery.length());
    }
}

void MarkdownHighlighter::highlightBlockMarkers(const QString &text, bool active,
                                                int *inlineFrom) {
    int level = 0;
    switch (blockKind(text, &level)) {
    case BlockKind::Heading: {
        const QRegularExpressionMatch heading = headingRe().match(text);
        setFormat(heading.capturedStart(3), heading.capturedLength(3),
                  m_headingFormats[qBound(1, level, 6) - 1]);
        hideMarker(0, heading.capturedLength(1) + heading.capturedLength(2), active);
        *inlineFrom = int(heading.capturedStart(3));
        return;
    }
    case BlockKind::Quote: {
        const QRegularExpressionMatch quote = quoteRe().match(text);
        setFormat(quote.capturedStart(2), quote.capturedLength(2), m_quoteFormat);
        hideMarker(0, quote.capturedLength(1), active);
        *inlineFrom = int(quote.capturedStart(2));
        return;
    }
    case BlockKind::ListItem: {
        const QRegularExpressionMatch list = listRe().match(text);
        setFormat(0, list.capturedLength(1), m_markerFormat);
        *inlineFrom = int(list.capturedStart(2));
        return;
    }
    case BlockKind::SceneBreak:
        setFormat(0, text.length(), m_breakFormat);
        *inlineFrom = -1;
        return;
    case BlockKind::PageBreak: {
        const QRegularExpressionMatch pageBreak = pageBreakRe().match(text);
        QTextCharFormat label = m_breakFormat;
        if (!active)
            label.setFontCapitalization(QFont::AllUppercase);
        setFormat(0, text.length(), label);
        hideMarker(0, pageBreak.capturedLength(1), active);
        *inlineFrom = -1;
        return;
    }
    case BlockKind::Centered: {
        const QRegularExpressionMatch centered = centeredRe().match(text);
        hideMarker(0, centered.capturedLength(1), active);
        hideMarker(centered.capturedStart(3), centered.capturedLength(3), active);
        *inlineFrom = int(centered.capturedStart(2));
        return;
    }
    case BlockKind::FootnoteDefinition: {
        const QRegularExpressionMatch definition = footnoteDefinitionRe().match(text);
        setFormat(0, text.length(), m_footnoteDefinitionFormat);
        mergeFormat(0, definition.capturedLength(1), m_markerFormat);
        *inlineFrom = int(definition.capturedStart(3));
        return;
    }
    case BlockKind::Fence:
    case BlockKind::Paragraph:
        *inlineFrom = 0;
        return;
    }
}

void MarkdownHighlighter::highlightInline(const QString &text, int from, bool active) {
    const QList<InlineMarkup> markup = inlineMarkup(text);
    for (const InlineMarkup &item : markup) {
        if (item.markers[0].start < from)
            continue;

        switch (item.kind) {
        case InlineKind::Bold:
            mergeFormat(item.content.start, item.content.length, m_boldFormat);
            break;
        case InlineKind::Italic:
            mergeFormat(item.content.start, item.content.length, m_italicFormat);
            break;
        case InlineKind::BoldItalic:
            mergeFormat(item.content.start, item.content.length, m_boldFormat);
            mergeFormat(item.content.start, item.content.length, m_italicFormat);
            break;
        case InlineKind::Strike:
            mergeFormat(item.content.start, item.content.length, m_strikeFormat);
            break;
        case InlineKind::Link:
            mergeFormat(item.content.start, item.content.length, m_linkFormat);
            break;
        case InlineKind::Footnote:
            mergeFormat(item.content.start, item.content.length, m_footnoteFormat);
            break;
        }
        for (const Span &marker : item.markers)
            hideMarker(marker.start, marker.length, active);
    }

    // Code spans are literal: no emphasis inside, and their backticks hide too.
    static const QRegularExpression codeRe(QStringLiteral("(`)([^`]+)(`)"));
    QRegularExpressionMatchIterator codeMatches = codeRe.globalMatch(text, from);
    while (codeMatches.hasNext()) {
        const QRegularExpressionMatch match = codeMatches.next();
        setFormat(match.capturedStart(2), match.capturedLength(2), m_codeFormat);
        hideMarker(match.capturedStart(1), 1, active);
        hideMarker(match.capturedStart(3), 1, active);
    }
}

QList<MarkdownHighlighter::InlineMarkup> MarkdownHighlighter::inlineMarkup(const QString &text) {
    QList<InlineMarkup> markup;
    static const QRegularExpression interestingRe(QStringLiteral("[*_\\[~]"));
    if (!text.contains(interestingRe))
        return markup;

    // Characters already claimed by a marker or a code span; a candidate whose
    // markers overlap them is rejected, which keeps `***x***` from also being
    // read as bold plus a stray asterisk and keeps emphasis out of code.
    QList<bool> claimed(text.length(), false);
    static const QRegularExpression codeRe(QStringLiteral("`[^`]+`"));
    QRegularExpressionMatchIterator codeMatches = codeRe.globalMatch(text);
    while (codeMatches.hasNext()) {
        const QRegularExpressionMatch match = codeMatches.next();
        for (int i = int(match.capturedStart()); i < match.capturedEnd(); ++i)
            claimed[i] = true;
    }

    const auto span = [](const QRegularExpressionMatch &match, int group) {
        return Span{int(match.capturedStart(group)), int(match.capturedLength(group))};
    };
    const auto tryAdd = [&](InlineKind kind, Span content, Span open, Span close) {
        for (const Span &marker : {open, close}) {
            for (int i = marker.start; i < marker.start + marker.length; ++i) {
                if (claimed[i])
                    return;
            }
        }
        for (const Span &marker : {open, close}) {
            for (int i = marker.start; i < marker.start + marker.length; ++i)
                claimed[i] = true;
        }
        markup.append({kind, content, {open, close}});
    };
    const auto addDelimited = [&](const QRegularExpression &re, InlineKind kind) {
        QRegularExpressionMatchIterator matches = re.globalMatch(text);
        while (matches.hasNext()) {
            const QRegularExpressionMatch match = matches.next();
            tryAdd(kind, span(match, 2), span(match, 1), span(match, 3));
        }
    };

    static const QRegularExpression footnoteRe(QStringLiteral("(\\[\\^)([^\\]\\s]+)(\\])(?!:)"));
    addDelimited(footnoteRe, InlineKind::Footnote);

    static const QRegularExpression linkRe(
        QStringLiteral("\\[([^\\]]+)\\]\\(((?:\\\\.|[^)])+)\\)"));
    QRegularExpressionMatchIterator linkMatches = linkRe.globalMatch(text);
    while (linkMatches.hasNext()) {
        const QRegularExpressionMatch match = linkMatches.next();
        const Span whole = span(match, 0);
        const Span content = span(match, 1);
        const int contentEnd = content.start + content.length;
        tryAdd(InlineKind::Link, content, {whole.start, 1},
               {contentEnd, whole.start + whole.length - contentEnd});
    }

    static const QRegularExpression boldItalicRe(
        QStringLiteral("(\\*\\*\\*|___)(?=\\S)(.+?)(?<=\\S)(\\1)"));
    addDelimited(boldItalicRe, InlineKind::BoldItalic);

    static const QRegularExpression boldRe(
        QStringLiteral("(\\*\\*|__)(?=\\S)(.+?)(?<=\\S)(\\1)"));
    addDelimited(boldRe, InlineKind::Bold);

    static const QRegularExpression starItalicRe(
        QStringLiteral("(?<!\\*)(\\*)(?=[^*\\s])([^*\\n]*?[^*\\s])(\\*)(?!\\*)"));
    addDelimited(starItalicRe, InlineKind::Italic);

    static const QRegularExpression underscoreItalicRe(
        QStringLiteral("(?<![_\\w])(_)(?=[^_\\s])([^_\\n]*?[^_\\s])(_)(?![_\\w])"));
    addDelimited(underscoreItalicRe, InlineKind::Italic);

    static const QRegularExpression strikeRe(QStringLiteral("(~~)(?=\\S)(.+?)(?<=\\S)(~~)"));
    addDelimited(strikeRe, InlineKind::Strike);

    std::sort(markup.begin(), markup.end(), [](const InlineMarkup &a, const InlineMarkup &b) {
        return a.markers[0].start < b.markers[0].start;
    });
    return markup;
}
