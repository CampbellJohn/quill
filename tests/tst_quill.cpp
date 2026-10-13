#include <QtTest>
#include <QFont>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>

#include <QQuickTextDocument>
#include <QQuickWindow>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>

#include "backend.h"
#include "markdownhighlighter.h"

class QuillTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(m_settingsDirectory.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("quill-tests"));
        QQuickStyle::setStyle(QStringLiteral("Material"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           m_settingsDirectory.path());
    }

    void countsWords() {
        QCOMPARE(Backend::countWords(QStringLiteral("one two-three don't 42")), 4);
        QCOMPARE(Backend::countWords(QStringLiteral("你好 世界")), 2);
        QCOMPARE(Backend::countWords(QString()), 0);
    }

    void normalizesLinks() {
        QCOMPARE(Backend::normalizedLinkUrl(QStringLiteral("www.example.com/path")),
                 QStringLiteral("https://www.example.com/path"));
        QCOMPARE(Backend::normalizedLinkUrl(QStringLiteral("mailto:writer@example.com")),
                 QStringLiteral("mailto:writer@example.com"));
        QVERIFY(Backend::normalizedLinkUrl(QStringLiteral("example.com")).isEmpty());
        QVERIFY(Backend::normalizedLinkUrl(QStringLiteral("file:///tmp/private")).isEmpty());
    }

    void suggestsSafeNames() {
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("My first draft\nBody")),
                 QStringLiteral("My first draft.md"));
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("A/B")), QStringLiteral("A-B.md"));
        QCOMPARE(Backend::suggestedFileName(QString()), QStringLiteral("Untitled.md"));
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("Already.md")),
                 QStringLiteral("Already.md"));
    }

    void keepsNotoFromCrowdingTheFontList() {
        const QStringList fonts = Backend::selectableFontFamilies({
            QStringLiteral("Noto Sans Devanagari"), QStringLiteral("Liberation Serif"),
            QStringLiteral("Noto Serif"), QStringLiteral("Noto Sans Tamil UI"),
            QStringLiteral("adwaita Sans"), QStringLiteral("IBM Plex Mono"),
            QStringLiteral("Noto Sans"), QStringLiteral("Noto Sans Mono"),
            QStringLiteral("Liberation Serif"), QStringLiteral("Monospace"),
            QStringLiteral("Standard Symbols PS"), QStringLiteral("D050000L"),
            QStringLiteral("Noto Color Emoji"), QStringLiteral("Nimbus Sans [UKWN]"),
            QStringLiteral("Nimbus Sans [URW ]")});
        QCOMPARE(fonts, QStringList({QStringLiteral("IBM Plex Mono"),
                                     QStringLiteral("adwaita Sans"),
                                     QStringLiteral("Liberation Serif"),
                                     QStringLiteral("Nimbus Sans"),
                                     QStringLiteral("Noto Sans"),
                                     QStringLiteral("Noto Sans Mono"),
                                     QStringLiteral("Noto Serif")}));
    }

    void remembersEditorFont() {
        const QString installed = Backend().availableFonts().value(1);
        QVERIFY(!installed.isEmpty());

        {
            Backend backend;
            QCOMPARE(backend.editorFont(), Backend::defaultEditorFont());
            QSignalSpy fontSpy(&backend, &Backend::editorFontChanged);
            backend.setEditorFont(installed);
            QCOMPARE(fontSpy.count(), 1);
        }

        QCOMPARE(Backend().editorFont(), installed);

        // A remembered font that has since been uninstalled falls back.
        QSettings().setValue(QStringLiteral("editor/font"), QStringLiteral("No Such Font"));
        QCOMPARE(Backend().editorFont(), Backend::defaultEditorFont());
        QSettings().remove(QStringLiteral("editor/font"));
    }

    void findsInlineMarkdownRanges() {
        const auto markup = MarkdownHighlighter::inlineMarkup(
            QStringLiteral("**bold** and *italic* and [site](https://example.com)"));
        QCOMPARE(markup.size(), 3);
        QCOMPARE(markup.at(0).content.start, 2);
        QCOMPARE(markup.at(0).content.length, 4);
        QCOMPARE(markup.at(2).content.length, 4);
        QCOMPARE(markup.at(2).markers[0].length, 1);
    }

    void loadsCurrentOmarchyTheme() {
        QTemporaryDir homeDirectory;
        QVERIFY(homeDirectory.isValid());

        const QByteArray originalHome = qgetenv("HOME");
        struct HomeRestorer {
            QByteArray value;
            ~HomeRestorer() { qputenv("HOME", value); }
        } restoreHome{originalHome};
        QVERIFY(qputenv("HOME", homeDirectory.path().toUtf8()));

        const QString themeDirectory = homeDirectory.path()
            + QStringLiteral("/.local/state/omarchy/current/theme");
        QVERIFY(QDir().mkpath(themeDirectory));

        QFile colorsFile(themeDirectory + QStringLiteral("/colors.toml"));
        QVERIFY(colorsFile.open(QIODevice::WriteOnly | QIODevice::Text));
        const QByteArray palette(
            "mode = \"light\"\n"
            "accent = \"#112233\"\n"
            "selection = \"#445566\"\n"
            "background = \"#fefefe\"\n"
            "foreground = \"#101010\"\n");
        QCOMPARE(colorsFile.write(palette), qint64(palette.size()));
        colorsFile.close();

        Backend backend;
        QCOMPARE(backend.themeBackground(), QStringLiteral("#fefefe"));
        QCOMPARE(backend.themeForeground(), QStringLiteral("#101010"));
        QCOMPARE(backend.themeAccent(), QStringLiteral("#112233"));
        QCOMPARE(backend.themeSelection(), QStringLiteral("#445566"));
        QVERIFY(!backend.darkMode());
    }

    void ignoresFileWatcherEventsForSavedContents() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString path = directory.filePath(QStringLiteral("first-save.md"));
        Backend backend;
        QSignalSpy externalChangeSpy(&backend, &Backend::externalChangeDetected);

        backend.saveAs(QUrl::fromLocalFile(path));
        QVERIFY(QFileInfo::exists(path));

        QFile sameContents(path);
        QVERIFY(sameContents.open(QIODevice::WriteOnly | QIODevice::Truncate));
        sameContents.close();
        QTest::qWait(100);
        QCOMPARE(externalChangeSpy.count(), 0);

        QFile changedContents(path);
        QVERIFY(changedContents.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(changedContents.write("changed elsewhere"), qint64(17));
        changedContents.close();
        QTRY_COMPARE(externalChangeSpy.count(), 1);
    }

    void keepsCursorAndSelectionStableAcrossInsertions() {
        const QString mutationsPath = QFINDTESTDATA("../src/EditorMutations.js");
        QVERIFY(!mutationsPath.isEmpty());

        QQmlEngine engine;
        QQmlComponent component(&engine);
        const QByteArray harness = R"QML(
            import QtQuick
            import "EditorMutations.js" as EditorMutations

            TextEdit {
                property string insertionText
                property int insertionCursor
                property string wrappedText
                property int wrappedSelectionStart
                property int wrappedSelectionEnd

                Component.onCompleted: {
                    text = "alpha omega";
                    cursorPosition = 5;
                    EditorMutations.replaceRange(this, 5, 5, "one\r\ntwo");
                    insertionText = text;
                    insertionCursor = cursorPosition;

                    text = "alpha beta omega";
                    select(6, 10);
                    EditorMutations.replaceRange(this, selectionStart, selectionEnd,
                                                 "**beta**", 2, 6);
                    wrappedText = text;
                    wrappedSelectionStart = selectionStart;
                    wrappedSelectionEnd = selectionEnd;
                }
            }
        )QML";
        const QUrl harnessUrl = QUrl::fromLocalFile(
            QFileInfo(mutationsPath).absolutePath() + QStringLiteral("/MutationHarness.qml"));
        component.setData(harness, harnessUrl);
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> editor(component.create());
        QVERIFY2(editor, qPrintable(component.errorString()));

        QCOMPARE(editor->property("insertionText").toString(),
                 QStringLiteral("alphaone\ntwo omega"));
        QCOMPARE(editor->property("insertionCursor").toInt(), 12);
        QCOMPARE(editor->property("wrappedText").toString(),
                 QStringLiteral("alpha **beta** omega"));
        QCOMPARE(editor->property("wrappedSelectionStart").toInt(), 8);
        QCOMPARE(editor->property("wrappedSelectionEnd").toInt(), 12);
    }

    void savesAndOpensFromFooterButtons() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QVERIFY(window->findChild<QObject *>(QStringLiteral("sourceEditor")));
        QVERIFY(!window->findChild<QObject *>(QStringLiteral("renderedPreview")));
        QVERIFY(!window->findChild<QObject *>(QStringLiteral("modeToggle")));

        QObject *saveButton = window->findChild<QObject *>(QStringLiteral("saveButton"));
        QObject *openButton = window->findChild<QObject *>(QStringLiteral("openButton"));
        QVERIFY(saveButton);
        QVERIFY(openButton);

        QSignalSpy saveDialogSpy(&backend, &Backend::saveDialogRequested);
        QVERIFY(QMetaObject::invokeMethod(saveButton, "clicked"));
        QCOMPARE(saveDialogSpy.count(), 1);

        QSignalSpy openDialogSpy(&backend, &Backend::openDialogRequested);
        QVERIFY(QMetaObject::invokeMethod(openButton, "clicked"));
        QCOMPARE(openDialogSpy.count(), 1);
    }

    void scalesTextWithDesktopTextSize() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 20);

        // `omarchy display text size 16` sets the GNOME factor to 16/12.
        backend.setTextScale(16.0 / 12.0);
        QCOMPARE(window->property("editorFontPixelSize").toInt(), 27);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 27);

        backend.setTextScale(9.0 / 12.0);
        QCOMPARE(window->property("editorFontPixelSize").toInt(), 15);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 15);
    }

    void remembersLastSaveDirectory() {
        QTemporaryDir saveDirectory;
        QVERIFY(saveDirectory.isValid());

        const QString savedPath = saveDirectory.filePath(QStringLiteral("first.md"));
        Backend savedDocument;
        savedDocument.saveAs(QUrl::fromLocalFile(savedPath));

        Backend nextDocument;
        QSignalSpy saveDialogSpy(&nextDocument, &Backend::saveDialogRequested);
        nextDocument.saveAsDialog();
        QCOMPARE(saveDialogSpy.count(), 1);

        const QUrl suggestedUrl = saveDialogSpy.takeFirst().constFirst().toUrl();
        QCOMPARE(QFileInfo(suggestedUrl.toLocalFile()).absolutePath(),
                 saveDirectory.path());
        QCOMPARE(QFileInfo(suggestedUrl.toLocalFile()).fileName(),
                 QStringLiteral("Untitled.md"));

        QSettings().setValue(QStringLiteral("file/lastSaveDirectory"),
                             saveDirectory.filePath(QStringLiteral("missing")));
        Backend fallbackDocument;
        QSignalSpy fallbackDialogSpy(&fallbackDocument, &Backend::saveDialogRequested);
        fallbackDocument.saveAsDialog();
        const QUrl fallbackUrl = fallbackDialogSpy.takeFirst().constFirst().toUrl();
        QCOMPARE(QFileInfo(fallbackUrl.toLocalFile()).absolutePath(),
                 fallbackDocument.libraryPath());
        QSettings().remove(QStringLiteral("file/lastSaveDirectory"));
    }

    void parsesProseInlineMarkdown() {
        using Kind = MarkdownHighlighter::InlineKind;
        const auto kinds = [](const QString &text) {
            QList<Kind> result;
            for (const auto &item : MarkdownHighlighter::inlineMarkup(text))
                result.append(item.kind);
            return result;
        };

        const auto boldItalic = MarkdownHighlighter::inlineMarkup(QStringLiteral("***loud***"));
        QCOMPARE(boldItalic.size(), 1);
        QCOMPARE(boldItalic.at(0).kind, Kind::BoldItalic);
        QCOMPARE(boldItalic.at(0).content.start, 3);
        QCOMPARE(boldItalic.at(0).content.length, 4);

        QCOMPARE(kinds(QStringLiteral("**bold *inner* bold**")),
                 (QList<Kind>{Kind::Bold, Kind::Italic}));
        QCOMPARE(kinds(QStringLiteral("~~gone~~")), QList<Kind>{Kind::Strike});
        QCOMPARE(kinds(QStringLiteral("A claim.[^1]")), QList<Kind>{Kind::Footnote});
        QCOMPARE(kinds(QStringLiteral("snake_case_name and 2 * 3 * 4")), QList<Kind>{});
        QCOMPARE(kinds(QStringLiteral("`*not italic*`")), QList<Kind>{});
        QCOMPARE(kinds(QStringLiteral("[link](http://a_b_c.com)")), QList<Kind>{Kind::Link});
    }

    void classifiesProseBlocks() {
        using Kind = MarkdownHighlighter::BlockKind;
        int level = 0;
        QCOMPARE(MarkdownHighlighter::blockKind(QStringLiteral("## Chapter Two"), &level),
                 Kind::Heading);
        QCOMPARE(level, 2);
        QCOMPARE(MarkdownHighlighter::blockKind(QStringLiteral("* * *")), Kind::SceneBreak);
        QCOMPARE(MarkdownHighlighter::blockKind(QStringLiteral("---")), Kind::SceneBreak);
        QCOMPARE(MarkdownHighlighter::blockKind(QStringLiteral("* item")), Kind::ListItem);
        QCOMPARE(MarkdownHighlighter::blockKind(QStringLiteral("- item")), Kind::ListItem);
        QCOMPARE(MarkdownHighlighter::blockKind(QStringLiteral("->THE END<-")), Kind::Centered);
        QCOMPARE(MarkdownHighlighter::blockKind(QStringLiteral("\\pagebreak")), Kind::PageBreak);
        QCOMPARE(MarkdownHighlighter::blockKind(QStringLiteral("> letter")), Kind::Quote);
        QCOMPARE(MarkdownHighlighter::blockKind(QStringLiteral("[^1]: A note.")),
                 Kind::FootnoteDefinition);
        QCOMPARE(MarkdownHighlighter::blockKind(QStringLiteral("She left.")), Kind::Paragraph);
    }

    void findsDocumentTitles() {
        QCOMPARE(Backend::documentTitle(QStringLiteral("# The *Last* Light\n\nBody"),
                                        QStringLiteral("file")),
                 QStringLiteral("The Last Light"));
        QCOMPARE(Backend::documentTitle(QStringLiteral("No heading here"), QStringLiteral("file")),
                 QStringLiteral("file"));
    }

    void listsTheLibrary() {
        QTemporaryDir library;
        QVERIFY(library.isValid());
        QSettings().setValue(QStringLiteral("library/path"), library.path());
        const auto write = [&library](const QString &name, const QByteArray &contents) {
            QFile file(library.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(contents);
        };
        write(QStringLiteral("Story.md"), "# Story\n\nThree more words.");
        write(QStringLiteral("Notes.txt"), "two words");
        write(QStringLiteral("image.png"), "not a story");

        Backend backend;
        QCOMPARE(backend.libraryPath(), library.path());
        const QVariantList files = backend.libraryFiles();
        QCOMPARE(files.size(), 2);
        QStringList names;
        for (const QVariant &file : files)
            names.append(file.toMap().value(QStringLiteral("name")).toString());
        names.sort();
        QCOMPARE(names, (QStringList{QStringLiteral("Notes"), QStringLiteral("Story")}));
        for (const QVariant &file : files) {
            const QVariantMap map = file.toMap();
            if (map.value(QStringLiteral("name")) == QStringLiteral("Story"))
                QCOMPARE(map.value(QStringLiteral("words")).toInt(), 4);
        }

        QSignalSpy saveDialogSpy(&backend, &Backend::saveDialogRequested);
        backend.saveAsDialog();
        QCOMPARE(QFileInfo(saveDialogSpy.takeFirst().constFirst().toUrl().toLocalFile())
                     .absolutePath(),
                 library.path());
        QSettings().remove(QStringLiteral("library/path"));
    }

    void rendersProseInTheEditor() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));
        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);

        editor->setProperty("text", QStringLiteral("# Title\n\n->THE END<-\n\n> A letter\n\nPlain *text*"));
        auto *quickDocument = editor->property("textDocument").value<QQuickTextDocument *>();
        QVERIFY(quickDocument);
        QTextDocument *document = quickDocument->textDocument();
        QCOMPARE(document->findBlockByNumber(2).blockFormat().alignment(), Qt::AlignHCenter);
        QVERIFY(document->findBlockByNumber(4).blockFormat().leftMargin() > 0);
        QCOMPARE(document->findBlockByNumber(6).blockFormat().alignment(), Qt::AlignLeft);

        // Live preview: markers hide away from the caret and return under it.
        const auto markerSize = [document](int blockNumber, int column) {
            const QTextBlock block = document->findBlockByNumber(blockNumber);
            for (const QTextLayout::FormatRange &range : block.layout()->formats()) {
                if (column >= range.start && column < range.start + range.length)
                    return range.format.fontPointSize();
            }
            return 0.0;
        };
        editor->setProperty("cursorPosition", 0);
        QCOMPARE(markerSize(6, 6), 1.0);
        editor->setProperty("cursorPosition", document->findBlockByNumber(6).position());
        QVERIFY(markerSize(6, 6) != 1.0);
        QCOMPARE(markerSize(0, 0), 1.0);

        // Smart typography.
        const auto typeCharacter = [editor](const QString &before, const QString &character) {
            editor->setProperty("text", before);
            editor->setProperty("cursorPosition", before.length());
            QVariant handled;
            QMetaObject::invokeMethod(editor, "applySmartTypography", Q_RETURN_ARG(QVariant, handled),
                                      Q_ARG(QVariant, character));
            return editor->property("text").toString();
        };
        QCOMPARE(typeCharacter(QStringLiteral("He said "), QStringLiteral("\"")),
                 QStringLiteral("He said \u201c"));
        QCOMPARE(typeCharacter(QStringLiteral("\u201cHi"), QStringLiteral("\"")),
                 QStringLiteral("\u201cHi\u201d"));
        QCOMPARE(typeCharacter(QStringLiteral("don"), QStringLiteral("'")),
                 QStringLiteral("don\u2019"));
        QCOMPARE(typeCharacter(QStringLiteral("wait-"), QStringLiteral("-")),
                 QStringLiteral("wait\u2014"));
        QCOMPARE(typeCharacter(QStringLiteral("-"), QStringLiteral("-")), QStringLiteral("-"));
        QCOMPARE(typeCharacter(QStringLiteral("and so.."), QStringLiteral(".")),
                 QStringLiteral("and so\u2026"));
        QCOMPARE(typeCharacter(QStringLiteral("`code "), QStringLiteral("\"")),
                 QStringLiteral("`code "));
    }

    void opensLibraryStoriesFromTheKeyboard() {
        QTemporaryDir library;
        QVERIFY(library.isValid());
        QSettings().setValue(QStringLiteral("library/path"), library.path());
        for (const QString &name : {QStringLiteral("First.md"), QStringLiteral("Second.md")}) {
            QFile file(library.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(name.toUtf8());
        }

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(QFINDTESTDATA("../src/Main.qml")));
        QScopedPointer<QObject> root(component.create());
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        window->show();
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));

        QTest::keyClick(window, Qt::Key_L, Qt::ControlModifier);
        QTRY_VERIFY(window->property("libraryOpen").toBool());
        QTest::qWait(250);
        QTest::keyClick(window, Qt::Key_Down);
        QCOMPARE(window->findChild<QObject *>(QStringLiteral("libraryList"))
                     ->property("currentIndex").toInt(), 1);
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY(backend.fileUrl().isLocalFile());
        QVERIFY(!window->property("libraryOpen").toBool());

        // Arrow keys move through the list; Escape closes it again.
        QTest::keyClick(window, Qt::Key_L, Qt::ControlModifier);
        QTRY_VERIFY(window->property("libraryOpen").toBool());
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!window->property("libraryOpen").toBool());
        QSettings().remove(QStringLiteral("library/path"));
    }

    void importsAndExportsThroughPandoc() {
        if (QStandardPaths::findExecutable(QStringLiteral("pandoc")).isEmpty()
                || QStandardPaths::findExecutable(QStringLiteral("typst")).isEmpty())
            QSKIP("pandoc and typst are needed for import/export.");

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(QFINDTESTDATA("../src/Main.qml")));
        QScopedPointer<QObject> window(component.create());
        QVERIFY(window);
        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        editor->setProperty("text", QStringLiteral(
            "# Night Ferry\n\nThe *water* was black.[^1]\n\n* * *\n\n->END<-\n\n[^1]: Mostly."));

        const auto exportTo = [&](const QString &format, const QString &style) {
            const QString path = directory.filePath(QStringLiteral("out-%1-%2.%1").arg(format, style));
            backend.exportDocument(QUrl::fromLocalFile(path), format, style,
                                   QStringLiteral("Night Ferry"));
            QElapsedTimer timer;
            timer.start();
            while (backend.exporting() && timer.elapsed() < 30000)
                QTest::qWait(50);
            return path;
        };

        for (const QString &style : {QStringLiteral("manuscript"), QStringLiteral("book")}) {
            const QString pdf = exportTo(QStringLiteral("pdf"), style);
            QVERIFY2(QFileInfo(pdf).size() > 1000, qPrintable(backend.status()));
            const QString docx = exportTo(QStringLiteral("docx"), style);
            QVERIFY2(QFileInfo(docx).size() > 1000, qPrintable(backend.status()));
        }
        QFile text(exportTo(QStringLiteral("txt"), QStringLiteral("manuscript")));
        QVERIFY(text.open(QIODevice::ReadOnly));
        const QString plain = QString::fromUtf8(text.readAll());
        QVERIFY(plain.contains(QStringLiteral("The water was black.[1]")));
        QVERIFY(!plain.contains(QLatin1Char('*') + QStringLiteral("water")));

        // Opening the Word file brings the prose back as Markdown, unsaved,
        // with a .md beside the original suggested for saving.
        const QString docx = directory.filePath(QStringLiteral("out-docx-book.docx"));
        backend.open(QUrl::fromLocalFile(docx));
        QVERIFY(backend.modified());
        QVERIFY(!backend.fileUrl().isValid() || backend.fileUrl().isEmpty());
        QVERIFY2(editor->property("text").toString().contains(QStringLiteral("The *water* was black.")),
                 qPrintable(editor->property("text").toString()));
        QSignalSpy saveDialogSpy(&backend, &Backend::saveDialogRequested);
        backend.save();
        QCOMPARE(saveDialogSpy.takeFirst().constFirst().toUrl().toLocalFile(),
                 directory.filePath(QStringLiteral("out-docx-book.md")));
    }

    // Renders the window offscreen for eyeballing the live preview:
    // QUILL_SCREENSHOT_DIR=/tmp/shots QUILL_SAMPLE=story.md ./bin/test
    void capturesScreenshots() {
        const QString directory = qEnvironmentVariable("QUILL_SCREENSHOT_DIR");
        if (directory.isEmpty())
            QSKIP("Set QUILL_SCREENSHOT_DIR to capture screenshots.");

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(QFINDTESTDATA("../src/Main.qml")));
        QScopedPointer<QObject> root(component.create());
        auto *window = qobject_cast<QQuickWindow *>(root.data());
        QVERIFY(window);
        window->setWidth(1100);
        window->setHeight(1200);
        backend.setDarkMode(qEnvironmentVariable("QUILL_LIGHT").isEmpty());
        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        backend.open(QUrl::fromLocalFile(qEnvironmentVariable("QUILL_SAMPLE")));
        editor->setProperty("cursorPosition", qEnvironmentVariableIntValue("QUILL_CURSOR"));
        window->show();
        QTest::qWait(400);
        QVERIFY(window->grabWindow().save(directory + QStringLiteral("/editor.png")));

        for (const QString &name : {QStringLiteral("libraryButton"), QStringLiteral("aidsButton")}) {
            QObject *button = window->findChild<QObject *>(name);
            QMetaObject::invokeMethod(button, "clicked");
            QTest::qWait(400);
            QVERIFY(window->grabWindow().save(directory + QLatin1Char('/') + name + QStringLiteral(".png")));
            QMetaObject::invokeMethod(button, "clicked");
            QTest::qWait(300);
        }
        QMetaObject::invokeMethod(window->findChild<QObject *>(QStringLiteral("exportButton")), "clicked");
        QTest::qWait(400);
        QVERIFY(window->grabWindow().save(directory + QStringLiteral("/export.png")));
    }

private:
    QTemporaryDir m_settingsDirectory;
};

QTEST_MAIN(QuillTest)
#include "tst_quill.moc"
