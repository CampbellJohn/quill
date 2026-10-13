// Book style: a 6" x 9" trade page, serif, justified, indented paragraphs
// (but not after headings or breaks), chapters opening on a new page, a
// centered page number in the footer, ornamental scene breaks.

#let book-title = "$quill-header$"

#set document(title: book-title)
#set page(
  width: 6in,
  height: 9in,
  margin: (inside: 0.875in, outside: 0.75in, top: 0.8in, bottom: 0.9in),
  footer: context {
    let number = counter(page).get().first()
    // No folio on the title page or a chapter's opening page.
    let opening = query(heading.where(level: 1)).any(h => h.location().page() == here().page())
    if number > 1 and not opening {
      align(center, text(size: 9pt, counter(page).display()))
    }
  },
)
#set text(
  font: ("EB Garamond", "Garamond", "Libertinus Serif"),
  size: 11pt,
  lang: "en",
  hyphenate: true,
)
#set par(justify: true, leading: 0.62em, spacing: 0.62em, first-line-indent: 1.4em)
#set heading(numbering: none)
// The level-one rule comes second so it takes precedence for chapters.
#show heading: it => {
  set par(first-line-indent: 0pt, justify: false)
  block(width: 100%, above: 1.6em, below: 1.2em,
    align(center, text(size: 11pt, weight: "regular", style: "italic", it.body)))
}
#show heading.where(level: 1): it => {
  pagebreak(weak: true)
  v(1.6in)
  set par(first-line-indent: 0pt, justify: false)
  align(center, text(size: 15pt, weight: "regular", tracking: 0.08em, upper(it.body)))
  v(0.5in)
}
#show quote.where(block: true): it => pad(left: 1.5em, right: 1.5em, it.body)
#set footnote.entry(separator: line(length: 1.2in, stroke: 0.4pt))
#show footnote.entry: set text(size: 8.5pt)

#let scenebreak() = {
  block(width: 100%, above: 1.1em, below: 1.1em, align(center, text(size: 13pt, "⁂")))
}

#let titleblock(title: "", author: "", contact: "", words: "") = {
  set par(first-line-indent: 0pt, justify: false)
  v(2.2in)
  align(center, text(size: 24pt, title))
  if author != "" {
    v(0.4in)
    align(center, text(size: 13pt, tracking: 0.06em, upper(author)))
  }
  pagebreak()
}

#let horizontalrule = scenebreak()

$body$
