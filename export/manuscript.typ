// Standard manuscript format (after William Shunn): US Letter, 1" margins,
// Times 12pt, double-spaced, every paragraph indented half an inch, contact
// details and word count on page one, "Surname / Title / page" on the rest.

#let running-header = "$quill-header$"

#set document(title: running-header)
#set page(
  paper: "us-letter",
  margin: 1in,
  header: context {
    if counter(page).get().first() > 1 {
      align(right, running-header + " / " + counter(page).display())
    }
  },
)
#set text(
  font: ("Times New Roman", "Liberation Serif", "TeX Gyre Termes", "Libertinus Serif"),
  size: 12pt,
  lang: "en",
  // A line box exactly one em tall, so a 1em leading is true double spacing.
  top-edge: 0.8em,
  bottom-edge: -0.2em,
)
#set par(leading: 1em, spacing: 1em, first-line-indent: (amount: 0.5in, all: true), justify: false)
#set heading(numbering: none)
#show heading: it => {
  set text(size: 12pt, weight: "regular")
  set par(first-line-indent: 0pt)
  block(width: 100%, above: 1em, below: 1em, align(center, it.body))
}
#show quote.where(block: true): it => pad(left: 0.5in, right: 0.5in, it.body)
#show footnote.entry: set par(leading: 0.5em, first-line-indent: 0pt)
#set footnote.entry(separator: line(length: 2in, stroke: 0.5pt))

#let scenebreak() = {
  set par(first-line-indent: 0pt)
  block(width: 100%, above: 1em, below: 1em, align(center, "#"))
}

#let titleblock(title: "", author: "", contact: "", words: "") = {
  {
    set par(leading: 0.15em, spacing: 0.15em, first-line-indent: 0pt)
    grid(
      columns: (1fr, auto),
      {
        if author != "" { author; linebreak() }
        if contact != "" { for line in contact.split("\n") { line; linebreak() } }
      },
      if words != "" { "about " + words + " words" },
    )
  }
  v(2.5in)
  align(center, block(title))
  if author != "" { align(center, block("by " + author)) }
  v(1em)
}

#let horizontalrule = scenebreak()

$body$
