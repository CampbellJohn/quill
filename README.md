# Quill

A minimalist writing app for short stories and other prose. It's a fork of
[Omawrite](https://github.com/omacom-io/omawrite), so it has Omawrite's look and shortcuts, follows the
Omarchy theme, and recovers unsaved work after a crash. Quill adds a live Markdown preview, so you
write Markdown without having to look at the symbols.

## Writing

Markdown symbols stay visible only on the line your cursor is on. Every other line shows the
finished result: headings at larger sizes, *italic*, **bold**, ***both***, ~~strikethrough~~,
blockquotes, lists, links, `code` and footnotes. Quill also understands a few conventions for prose:

| Type                  | You get                                          |
|-----------------------|--------------------------------------------------|
| `***`, `---`, `* * *` | Scene break (`#` in manuscripts, `⁂` in books)   |
| `->THE END<-`         | A centered line                                  |
| `\pagebreak`          | Page break in PDF and Word (`Ctrl+Enter`)        |
| `Text.[^1]` / `[^1]: Note` | Footnote                                    |

Smart typography turns `"` and `'` into curly quotes, `--` into an em dash and `...` into an
ellipsis as you type. A dash at the start of a line is left alone, so lists and `---` still work.

Writing aids each have an on/off switch in the footer's sliders popup:
- smart typography
- typewriter scrolling
- word count
- session word goal (a hairline progress bar across the top of the window)
- autosave

The footer fades while you type.

## Files

- `Ctrl+L` opens the library: the stories in `~/Documents/Writing`, newest first, with a filter box and a **New** button.
- `Ctrl+O` opens `.md`, `.txt`, `.docx`, `.odt` or `.rtf` files from anywhere.
- A Word, ODT or RTF file is converted to Markdown when it's opened. Saving it writes a new `.md` beside the original, which is never changed.
- Stories that have a file autosave as you type.

## Export (`Ctrl+E`)

- **Markdown:** a copy of the source.
- **Plain text:** formatting removed, one line per paragraph, footnotes as `[1]`.
- **PDF / Word, Manuscript style:** standard submission format.
  - Times 12pt, double-spaced, 1" margins.
  - Page one has your name and contact details plus "about N words".
  - The title and byline sit partway down page one.
  - Later pages carry a `Surname / Title / page` header.
- **PDF / Word, Book style:**
  - A 6×9 serif page, justified, with a title page.
  - Paragraphs are indented, except after headings and breaks.
  - Chapters start on a new page.
  - Scene breaks get an ornament, and page numbers are centered.

If the first line is `# Title`, it becomes the title. Otherwise the file name is used. The top
heading level left in the story is treated as chapter level.

## Shortcuts

`Ctrl+S` save · `Ctrl+Shift+S` save as · `Ctrl+O` open · `Ctrl+L` library · `Ctrl+Shift+N` new story ·
`Ctrl+N` new window · `Ctrl+E` export · `Ctrl+F` / `Ctrl+H` find / replace · `Ctrl+B` bold ·
`Ctrl+I` italic · `Ctrl+D` strikethrough · `Ctrl+K` link · `Ctrl+Enter` page break · `Ctrl+P` print ·
`F11` / `Super+F` fullscreen · `Ctrl+?` all shortcuts

## Build and install

Requirements: Qt 6 (`qt6-base`, `qt6-declarative`), `xdg-desktop-portal`, plus `python`,
`pandoc-cli` and `typst` for import and export.

```sh
bin/build           # build/quill
bin/test            # test suite (QUILL_SCREENSHOT_DIR=… QUILL_SAMPLE=story.md to capture screenshots)
bin/install-local   # install to ~/.local: binary, export helper, launcher entry, icon
```

The export helper (`export/quill-export`, with the `quill.lua` filter and the
`manuscript.typ`/`book.typ` templates) is installed to `~/.local/share/quill/`.

IBM Plex Mono is bundled under the SIL Open Font License 1.1; see `fonts/OFL.txt`.
