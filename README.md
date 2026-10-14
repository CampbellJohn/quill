# Quill

A minimalist writing app for short stories and other prose, for Arch Linux.

You write in Markdown, but you don't have to look at it: the symbols show only on the line you're
editing, and everything else is displayed as finished text. When the story is done, Quill exports it
as a standard manuscript for submission or as a book-style PDF or Word file.

Quill is a fork of [Omawrite](https://github.com/omacom-io/omawrite). It keeps Omawrite's look,
shortcuts and crash recovery, and adds the live preview, a story library, writing aids and export.

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

## Install

Quill runs on any Arch Linux (or Arch-based) system. Install the dependencies:

```sh
sudo pacman -S --needed base-devel qt6-base qt6-declarative xdg-desktop-portal python pandoc-cli typst
```

Then clone and install, either as a pacman package or just for your user:

```sh
git clone https://github.com/CampbellJohn/quill.git
cd quill
bin/install         # build and install a system package with makepkg (asks for sudo)
# or
bin/install-local   # install to ~/.local only, no sudo
```

Launch it from your app launcher or run `quill`. To update, `git pull` and run the same install
command again.

`bin/install-local` puts the binary in `~/.local/bin`, so that directory needs to be on your `PATH`.
The export helper (`quill-export`, its `quill.lua` filter and the `manuscript.typ`/`book.typ`
templates) goes to `~/.local/share/quill/`.

## Desktop support

Quill works on any desktop: Omarchy, GNOME, KDE, Hyprland, Sway and others.

- **On [Omarchy](https://omarchy.org)**, it uses the current Omarchy theme's colours and updates
  live when you switch themes. It also follows `omarchy display text size`.
- **Elsewhere**, it uses its own neutral dark or light colours, chosen from your desktop's
  dark-mode setting through `xdg-desktop-portal`. It also follows the GNOME text-scaling setting
  if your desktop provides it.

PDF export prefers Times New Roman (manuscript) and EB Garamond (book) when they're installed and
falls back to fonts that come with Typst, so it works without any extra fonts.

## Development

```sh
bin/build   # build to build/quill
bin/test    # run the test suite
```

To capture screenshots during the tests, set `QUILL_SCREENSHOT_DIR` to an output folder and
`QUILL_SAMPLE` to a Markdown story to load:

```sh
QUILL_SCREENSHOT_DIR=shots QUILL_SAMPLE=story.md bin/test
```

## License

MIT; see `LICENSE`. Based on Omawrite by David Heinemeier Hansson.

IBM Plex Mono is bundled under the SIL Open Font License 1.1; see `fonts/OFL.txt`.
