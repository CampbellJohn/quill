#include "backend.h"

#include <QClipboard>
#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QMimeData>
#include <QProcess>
#include <QPrintDialog>
#include <QPrinter>
#include <QQuickTextDocument>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextStream>
#include <QUrl>
#include <QVariantMap>
#include <QWindow>

#include <algorithm>

#include "markdownhighlighter.h"

constexpr qreal typoraLineHeightPercent = 140;
const QString lastSaveDirectorySetting = QStringLiteral("file/lastSaveDirectory");
const QString editorFontSetting = QStringLiteral("editor/font");
const QString libraryPathSetting = QStringLiteral("library/path");

// Formats pandoc can turn into Markdown when they are opened.
const QStringList importableSuffixes = {QStringLiteral("docx"), QStringLiteral("odt"),
                                        QStringLiteral("rtf")};

// Noto ships a separate family per script, which buries every other font in
// the picker. Offer only the core Latin families; the rest still serve as
// fallbacks for whatever scripts a document contains.
const QStringList keptNotoFamilies = {QStringLiteral("Noto Sans"),
                                      QStringLiteral("Noto Sans Mono"),
                                      QStringLiteral("Noto Serif")};

// Fontconfig's generic aliases just point at another listed family.
const QStringList genericFontAliases = {QStringLiteral("Monospace"), QStringLiteral("Mono"),
                                        QStringLiteral("Sans Serif"), QStringLiteral("Sans"),
                                        QStringLiteral("Serif"), QStringLiteral("System-ui")};

// Symbol fonts often claim Latin coverage because they map their glyphs onto
// ASCII, so they can only be told apart by name. D050000L is URW's Dingbats.
const QRegularExpression symbolFontName(
    QStringLiteral("symbol|dingbat|emoji|icon|awesome|^D050000L$"),
    QRegularExpression::CaseInsensitiveOption);

QString Backend::normalizedLinkUrl(const QString &clipboardText) {
    QString candidate = clipboardText.trimmed();
    static const QRegularExpression lineBreakRe(QStringLiteral("[\\r\\n]"));
    const int lineBreak = candidate.indexOf(lineBreakRe);
    if (lineBreak >= 0)
        candidate = candidate.left(lineBreak).trimmed();

    if (candidate.isEmpty())
        return {};

    if (candidate.startsWith(QStringLiteral("www."), Qt::CaseInsensitive))
        candidate.prepend(QStringLiteral("https://"));

    static const QRegularExpression schemeRe(
        QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]*:"));
    if (!schemeRe.match(candidate).hasMatch())
        return {};

    const QUrl url(candidate);
    if (!url.isValid() || url.scheme().isEmpty())
        return {};

    const QString scheme = url.scheme().toLower();
    const bool webUrl = scheme == QStringLiteral("http")
        || scheme == QStringLiteral("https")
        || scheme == QStringLiteral("ftp");
    if (webUrl && url.host().isEmpty())
        return {};

    if (!webUrl && scheme != QStringLiteral("mailto"))
        return {};

    return url.toString();
}

Backend::Backend(QObject *parent) : QObject(parent) {
    QSettings settings;
    m_smartTypography = settings.value(QStringLiteral("aids/smartTypography"), true).toBool();
    m_typewriterScrolling = settings.value(QStringLiteral("aids/typewriterScrolling"), false).toBool();
    m_showWordCount = settings.value(QStringLiteral("aids/wordCount"), true).toBool();
    m_showSessionGoal = settings.value(QStringLiteral("aids/sessionGoal"), false).toBool();
    m_sessionGoal = qMax(1, settings.value(QStringLiteral("aids/sessionGoalWords"), 500).toInt());
    m_autosave = settings.value(QStringLiteral("file/autosave"), true).toBool();
    m_authorName = settings.value(QStringLiteral("export/authorName")).toString();
    m_authorContact = settings.value(QStringLiteral("export/authorContact")).toString();
    m_libraryPath = settings.value(libraryPathSetting, defaultLibraryPath()).toString();
    QDir().mkpath(m_libraryPath);
    watchLibrary();
    connect(&m_libraryWatcher, &QFileSystemWatcher::directoryChanged, this, [this]() {
        emit libraryChanged();
    });

    m_autosaveTimer.setSingleShot(true);
    m_autosaveTimer.setInterval(1500);
    connect(&m_autosaveTimer, &QTimer::timeout, this, [this]() {
        if (m_modified && m_fileUrl.isLocalFile())
            saveTo(m_fileUrl);
    });

    const QString savedFont = settings.value(editorFontSetting).toString();
    m_editorFont = !savedFont.isEmpty() && QFontDatabase::hasFamily(savedFont)
        ? savedFont
        : defaultEditorFont();

    const QString stateDirectory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(stateDirectory);
    // Claim an orphaned snapshot before taking an empty slot. This ensures a
    // crash in window 2 is still recovered even if window 1 exited normally.
    for (int pass = 0; pass < 2 && !m_recoveryLock; ++pass) {
        for (int slot = 0; slot < 100; ++slot) {
            const QString base = QDir(stateDirectory).filePath(
                QStringLiteral("recovery-%1").arg(slot));
            const bool snapshotExists = QFileInfo::exists(base + QStringLiteral(".json"));
            if ((pass == 0) != snapshotExists)
                continue;
            auto lock = std::make_unique<QLockFile>(base + QStringLiteral(".lock"));
            if (lock->tryLock()) {
                m_recoveryPath = base + QStringLiteral(".json");
                m_recoveryLock = std::move(lock);
                break;
            }
        }
    }
    m_wordCountTimer.setSingleShot(true);
    m_wordCountTimer.setInterval(120);
    connect(&m_wordCountTimer, &QTimer::timeout, this, &Backend::refreshWordCount);
    m_recoveryTimer.setSingleShot(true);
    m_recoveryTimer.setInterval(750);
    connect(&m_recoveryTimer, &QTimer::timeout, this, &Backend::writeRecovery);
    connect(&m_fileWatcher, &QFileSystemWatcher::fileChanged, this,
            [this](const QString &path) {
                if (path != m_fileUrl.toLocalFile())
                    return;

                const bool deleted = !QFileInfo::exists(path);
                if (!deleted && m_hasKnownFileContents) {
                    QFile file(path);
                    if (file.open(QIODevice::ReadOnly)
                            && file.readAll() == m_lastKnownFileContents) {
                        // Atomic saves can replace the watched inode. Re-arm the
                        // watcher, but do not report our own save as an outside edit.
                        watchCurrentFile();
                        return;
                    }
                }

                emit externalChangeDetected(deleted, m_modified);
            });

    loadOmarchyTheme();
    watchOmarchyTheme();
    connect(&m_themeWatcher, &QFileSystemWatcher::fileChanged, this, [this]() {
        loadOmarchyTheme();
        watchOmarchyTheme();
    });
    connect(&m_themeWatcher, &QFileSystemWatcher::directoryChanged, this, [this]() {
        loadOmarchyTheme();
        watchOmarchyTheme();
    });
}

Backend::~Backend() = default;

void Backend::setParentWindow(QWindow *window) {
    m_parentWindow = window;
}

QString Backend::fileName() const {
    if (!m_fileUrl.isValid() || m_fileUrl.isEmpty())
        return QStringLiteral("Untitled.md");

    if (m_fileUrl.isLocalFile()) {
        const QFileInfo info(m_fileUrl.toLocalFile());
        if (!info.fileName().isEmpty())
            return info.fileName();
    }

    const QString name = m_fileUrl.fileName();
    return name.isEmpty() ? QStringLiteral("Untitled.md") : name;
}

void Backend::setDarkMode(bool darkMode) {
    if (m_darkMode == darkMode)
        return;

    m_darkMode = darkMode;
    loadOmarchyTheme();
    emit darkModeChanged();
}

void Backend::setTextScale(qreal textScale) {
    if (qFuzzyCompare(m_textScale, textScale))
        return;

    m_textScale = textScale;
    emit textScaleChanged();
}

void Backend::attachDocument(QObject *textDocument) {
    auto *quickDocument = qobject_cast<QQuickTextDocument *>(textDocument);
    if (!quickDocument || !quickDocument->textDocument()) {
        setStatus(QStringLiteral("Could not attach the Markdown renderer."));
        return;
    }

    if (m_highlighter)
        delete m_highlighter.data();

    m_document = quickDocument->textDocument();
    m_lastDocumentText = m_document->toPlainText();
    m_highlighter = new MarkdownHighlighter(m_document);
    m_highlighter->setDarkMode(m_darkMode);
    m_highlighter->setColors(m_themeBackground, m_themeForeground, m_themeAccent);

    connect(m_document, &QTextDocument::contentsChange, this,
            [this](int position, int charsRemoved, int charsAdded) {
                if (m_formattingTypography || m_loading)
                    return;
                m_lastChangePos = position;
                m_lastChangeAdded = charsAdded;
                m_lastChangeRemoved = charsRemoved;
            });

    applyDocumentTypography();
    restoreRecovery();
}

void Backend::openDialog() {
    emit openDialogRequested();
}

void Backend::open(const QUrl &url) {
    if (!url.isLocalFile()) {
        setStatus(QStringLiteral("Only local files can be opened."));
        return;
    }

    const QString targetName = QFileInfo(url.toLocalFile()).fileName();
    if (importableSuffixes.contains(QFileInfo(targetName).suffix().toLower())) {
        importDocument(url);
        return;
    }

    QFile file(url.toLocalFile());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setStatus(QStringLiteral("Could not open %1.").arg(targetName));
        return;
    }

    const QByteArray contents = file.readAll();
    loadDocumentText(QString::fromUtf8(contents));
    clearRecovery();
    m_importedFrom.clear();
    m_lastKnownFileContents = contents;
    m_hasKnownFileContents = true;
    setFileUrl(url);
    watchCurrentFile();
    setModified(false);
    resetSessionGoal();
    setStatus(QStringLiteral("Opened %1").arg(fileName()));
}

void Backend::save() {
    if (!m_fileUrl.isValid() || m_fileUrl.isEmpty()) {
        saveAsDialog();
        return;
    }

    saveTo(m_fileUrl);
}

void Backend::saveForClose() {
    if (!m_modified) {
        emit closeAfterSave();
        return;
    }

    m_closeAfterSave = true;
    save();
}

void Backend::saveAsDialog() {
    emit saveDialogRequested(suggestedSaveUrl());
}

void Backend::saveAs(const QUrl &url) {
    saveTo(url);
}

void Backend::fileDialogCanceled() {
    m_closeAfterSave = false;
}

void Backend::discardRecovery() {
    clearRecovery();
}

void Backend::reloadFromDisk() {
    if (m_fileUrl.isLocalFile())
        open(m_fileUrl);
}

void Backend::keepExternalVersion() {
    QFile file(m_fileUrl.toLocalFile());
    if (file.open(QIODevice::ReadOnly)) {
        m_lastKnownFileContents = file.readAll();
        m_hasKnownFileContents = true;
    } else {
        m_lastKnownFileContents.clear();
        m_hasKnownFileContents = false;
    }
    setModified(true);
    scheduleRecovery();
    watchCurrentFile();
    setStatus(QStringLiteral("Kept your version"));
}

void Backend::printDocument() {
    if (!m_document) {
        setStatus(QStringLiteral("There is no document to print."));
        return;
    }

    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dialog(&printer);
    dialog.setWindowTitle(QStringLiteral("Print %1").arg(fileName()));
    dialog.winId();
    if (dialog.windowHandle() && m_parentWindow)
        dialog.windowHandle()->setTransientParent(m_parentWindow);

    if (dialog.exec() == QDialog::Accepted) {
        QTextDocument rendered;
        rendered.setDefaultFont(m_document->defaultFont());
        rendered.setMarkdown(currentDocumentText());
        rendered.print(&printer);
    }
}

void Backend::newWindow() {
    const bool started = QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                                 QStringList());
    if (!started)
        setStatus(QStringLiteral("Could not open a new window."));
}

QString Backend::clipboardUrl() const {
    const QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return {};

    const QMimeData *mimeData = clipboard->mimeData();
    if (!mimeData)
        return {};

    if (mimeData->hasUrls()) {
        const QList<QUrl> urls = mimeData->urls();
        for (const QUrl &url : urls) {
            const QString normalized = normalizedLinkUrl(url.toString());
            if (!normalized.isEmpty())
                return normalized;
        }
    }

    if (!mimeData->hasText())
        return {};

    return normalizedLinkUrl(mimeData->text());
}

QString Backend::clipboardText() const {
    const QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return {};

    const QMimeData *mimeData = clipboard->mimeData();
    return mimeData && mimeData->hasText() ? mimeData->text() : QString();
}

bool Backend::editorTextChanged() {
    if (m_loading || m_formattingTypography)
        return false;

    const QString text = currentDocumentText();
    if (text == m_lastDocumentText)
        return false;
    m_lastDocumentText = text;

    reapplyTypographyToChange();

    scheduleWordCount();
    setModified(true);
    setStatus(QStringLiteral("Unsaved"));
    scheduleRecovery();
    if (m_autosave && m_fileUrl.isLocalFile())
        m_autosaveTimer.start();
    return true;
}

bool Backend::saveIfAutosaving() {
    if (m_modified && m_autosave && m_fileUrl.isLocalFile()) {
        m_autosaveTimer.stop();
        saveTo(m_fileUrl);
    }
    return !m_modified;
}

void Backend::setCursorPosition(int position) {
    if (!m_document || !m_highlighter)
        return;
    const QTextBlock block =
        m_document->findBlock(qBound(0, position, m_document->characterCount() - 1));
    m_highlighter->setActiveBlock(block.isValid() ? block.blockNumber() : -1);
}

void Backend::newDocument() {
    loadDocumentText(QString());
    clearRecovery();
    m_importedFrom.clear();
    m_lastKnownFileContents.clear();
    m_hasKnownFileContents = false;
    setFileUrl(QUrl());
    setModified(false);
    resetSessionGoal();
    setStatus(QStringLiteral("New story"));
}

QVariantList Backend::libraryFiles() const {
    QVariantList files;
    const QDir library(m_libraryPath);
    const QFileInfoList entries = library.entryInfoList(
        {QStringLiteral("*.md"), QStringLiteral("*.markdown"), QStringLiteral("*.txt")},
        QDir::Files | QDir::Readable, QDir::Time);
    const QDate today = QDate::currentDate();
    for (const QFileInfo &info : entries) {
        QFile file(info.absoluteFilePath());
        int words = 0;
        if (file.open(QIODevice::ReadOnly | QIODevice::Text))
            words = countWords(QString::fromUtf8(file.readAll()));
        const QDateTime modified = info.lastModified();
        const QString when = modified.date() == today
            ? modified.toString(QStringLiteral("h:mm ap"))
            : modified.date().year() == today.year()
                ? modified.toString(QStringLiteral("MMM d"))
                : modified.toString(QStringLiteral("MMM d, yyyy"));
        files.append(QVariantMap{
            {QStringLiteral("name"), info.completeBaseName()},
            {QStringLiteral("url"), QUrl::fromLocalFile(info.absoluteFilePath())},
            {QStringLiteral("modified"), when},
            {QStringLiteral("words"), words},
            {QStringLiteral("current"),
             m_fileUrl.isLocalFile() && QFileInfo(m_fileUrl.toLocalFile()) == info},
        });
    }
    return files;
}

QString Backend::currentTitle() const {
    QString fallback = QStringLiteral("Untitled");
    if (m_fileUrl.isLocalFile())
        fallback = QFileInfo(m_fileUrl.toLocalFile()).completeBaseName();
    else if (m_importedFrom.isLocalFile())
        fallback = QFileInfo(m_importedFrom.toLocalFile()).completeBaseName();
    return documentTitle(currentDocumentText(), fallback);
}

QString Backend::documentTitle(const QString &text, const QString &fallback) {
    static const QRegularExpression titleRe(QStringLiteral("^\\s*#\\s+(.+?)\\s*#*\\s*$"));
    const QString firstLine = text.trimmed().section(QLatin1Char('\n'), 0, 0);
    const QRegularExpressionMatch match = titleRe.match(firstLine);
    if (!match.hasMatch())
        return fallback;
    QString title = match.captured(1);
    static const QRegularExpression emphasisRe(QStringLiteral("[*_~`]"));
    title.remove(emphasisRe);
    return title.trimmed().isEmpty() ? fallback : title.trimmed();
}

QUrl Backend::suggestedExportUrl(const QString &extension) const {
    QDir directory(m_libraryPath);
    QString base = QStringLiteral("Untitled");
    const QUrl source = m_fileUrl.isLocalFile() ? m_fileUrl : m_importedFrom;
    if (source.isLocalFile()) {
        const QFileInfo info(source.toLocalFile());
        directory = info.absoluteDir();
        base = info.completeBaseName();
    } else {
        base = QFileInfo(suggestedFileName(currentDocumentText())).completeBaseName();
    }
    return QUrl::fromLocalFile(directory.filePath(base + QLatin1Char('.') + extension));
}

void Backend::exportDocument(const QUrl &url, const QString &format, const QString &style,
                             const QString &title) {
    if (!url.isLocalFile() || m_exportProcess)
        return;

    const QString helper = helperPath();
    if (helper.isEmpty()) {
        setStatus(QStringLiteral("Export helper is missing; reinstall Quill."));
        return;
    }

    const QString inputPath = QDir(QDir::tempPath()).filePath(
        QStringLiteral("quill-export-%1.md").arg(QCoreApplication::applicationPid()));
    QSaveFile input(inputPath);
    if (!input.open(QIODevice::WriteOnly | QIODevice::Text)) {
        setStatus(QStringLiteral("Could not prepare the export."));
        return;
    }
    input.write(currentDocumentText().toUtf8());
    if (!input.commit()) {
        setStatus(QStringLiteral("Could not prepare the export."));
        return;
    }

    const QString outputPath = url.toLocalFile();
    const QString outputName = QFileInfo(outputPath).fileName();
    m_exportProcess = new QProcess(this);
    m_exportProcess->setProgram(helper);
    m_exportProcess->setArguments({QStringLiteral("export"),
                                   QStringLiteral("--format"), format,
                                   QStringLiteral("--style"), style,
                                   QStringLiteral("--title"), title,
                                   QStringLiteral("--author"), m_authorName,
                                   QStringLiteral("--contact"), m_authorContact,
                                   QStringLiteral("--input"), inputPath,
                                   QStringLiteral("--output"), outputPath});
    connect(m_exportProcess, &QProcess::finished, this,
            [this, inputPath, outputName](int exitCode, QProcess::ExitStatus exitStatus) {
                const QString error = QString::fromUtf8(m_exportProcess->readAllStandardError())
                                          .trimmed().section(QLatin1Char('\n'), -1);
                m_exportProcess->deleteLater();
                m_exportProcess = nullptr;
                QFile::remove(inputPath);
                emit exportingChanged();
                if (exitStatus == QProcess::NormalExit && exitCode == 0)
                    setStatus(QStringLiteral("Exported %1").arg(outputName));
                else
                    setStatus(error.isEmpty() ? QStringLiteral("Export failed.")
                                              : QStringLiteral("Export failed: %1").arg(error));
            });
    connect(m_exportProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && m_exportProcess) {
            m_exportProcess->deleteLater();
            m_exportProcess = nullptr;
            emit exportingChanged();
            setStatus(QStringLiteral("Could not run the export helper."));
        }
    });
    setStatus(QStringLiteral("Exporting %1…").arg(outputName));
    m_exportProcess->start();
    emit exportingChanged();
}

void Backend::resetSessionGoal() {
    m_sessionStartWords = m_wordCount;
    emit wordCountChanged();
}

void Backend::importDocument(const QUrl &url) {
    const QString sourceName = QFileInfo(url.toLocalFile()).fileName();
    const QString helper = helperPath();
    if (helper.isEmpty()) {
        setStatus(QStringLiteral("Import helper is missing; reinstall Quill."));
        return;
    }

    QProcess process;
    process.start(helper, {QStringLiteral("import"), url.toLocalFile()});
    if (!process.waitForFinished(60000) || process.exitStatus() != QProcess::NormalExit
            || process.exitCode() != 0) {
        const QString error = QString::fromUtf8(process.readAllStandardError())
                                  .trimmed().section(QLatin1Char('\n'), -1);
        setStatus(error.isEmpty() ? QStringLiteral("Could not import %1.").arg(sourceName)
                                  : error);
        return;
    }

    loadDocumentText(QString::fromUtf8(process.readAllStandardOutput()));
    clearRecovery();
    m_lastKnownFileContents.clear();
    m_hasKnownFileContents = false;
    m_importedFrom = url;
    setFileUrl(QUrl());
    setModified(true);
    resetSessionGoal();
    scheduleRecovery();
    setStatus(QStringLiteral("Imported %1 · save to keep it as Markdown").arg(sourceName));
}

QString Backend::helperPath() const {
    const QString bundled = QDir(QCoreApplication::applicationDirPath())
                                .filePath(QStringLiteral("../export/quill-export"));
    if (QFileInfo(bundled).isExecutable())
        return QFileInfo(bundled).canonicalFilePath();
    return QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                  QStringLiteral("quill/quill-export"));
}

QString Backend::defaultLibraryPath() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
        .filePath(QStringLiteral("Writing"));
}

void Backend::watchLibrary() {
    if (QDir(m_libraryPath).exists())
        m_libraryWatcher.addPath(m_libraryPath);
}

void Backend::setPreference(bool &member, bool value, const QString &key) {
    if (member == value)
        return;
    member = value;
    QSettings().setValue(key, value);
    emit preferencesChanged();
}

void Backend::setSmartTypography(bool enabled) {
    setPreference(m_smartTypography, enabled, QStringLiteral("aids/smartTypography"));
}

void Backend::setTypewriterScrolling(bool enabled) {
    setPreference(m_typewriterScrolling, enabled, QStringLiteral("aids/typewriterScrolling"));
}

void Backend::setShowWordCount(bool enabled) {
    setPreference(m_showWordCount, enabled, QStringLiteral("aids/wordCount"));
}

void Backend::setShowSessionGoal(bool enabled) {
    setPreference(m_showSessionGoal, enabled, QStringLiteral("aids/sessionGoal"));
}

void Backend::setAutosave(bool enabled) {
    setPreference(m_autosave, enabled, QStringLiteral("file/autosave"));
    if (!enabled)
        m_autosaveTimer.stop();
}

void Backend::setSessionGoal(int words) {
    words = qMax(1, words);
    if (m_sessionGoal == words)
        return;
    m_sessionGoal = words;
    QSettings().setValue(QStringLiteral("aids/sessionGoalWords"), words);
    emit preferencesChanged();
}

void Backend::setAuthorName(const QString &name) {
    if (m_authorName == name)
        return;
    m_authorName = name;
    QSettings().setValue(QStringLiteral("export/authorName"), name);
    emit preferencesChanged();
}

void Backend::setAuthorContact(const QString &contact) {
    if (m_authorContact == contact)
        return;
    m_authorContact = contact;
    QSettings().setValue(QStringLiteral("export/authorContact"), contact);
    emit preferencesChanged();
}

void Backend::setSearchHighlight(const QString &query, int currentMatchStart) {
    if (m_highlighter)
        m_highlighter->setSearch(query, currentMatchStart);
}

void Backend::openExternalUrl(const QUrl &url) {
    const QString scheme = url.scheme().toLower();
    if (scheme == QStringLiteral("http") || scheme == QStringLiteral("https")
            || scheme == QStringLiteral("mailto"))
        QDesktopServices::openUrl(url);
}

QVariantMap Backend::windowGeometry() const {
    QSettings settings;
    return {{QStringLiteral("x"), settings.value(QStringLiteral("window/x"), -1)},
            {QStringLiteral("y"), settings.value(QStringLiteral("window/y"), -1)},
            {QStringLiteral("width"), settings.value(QStringLiteral("window/width"), 1280)},
            {QStringLiteral("height"), settings.value(QStringLiteral("window/height"), 820)},
            {QStringLiteral("maximized"), settings.value(QStringLiteral("window/maximized"), false)}};
}

void Backend::saveWindowGeometry(int x, int y, int width, int height, bool maximized) {
    QSettings settings;
    if (!maximized) {
        settings.setValue(QStringLiteral("window/x"), x);
        settings.setValue(QStringLiteral("window/y"), y);
        settings.setValue(QStringLiteral("window/width"), width);
        settings.setValue(QStringLiteral("window/height"), height);
    }
    settings.setValue(QStringLiteral("window/maximized"), maximized);
}

QString Backend::defaultEditorFont() {
    return QStringLiteral("IBM Plex Mono");
}

void Backend::setEditorFont(const QString &family) {
    if (family.isEmpty() || family == m_editorFont)
        return;

    m_editorFont = family;
    QSettings().setValue(editorFontSetting, family);
    emit editorFontChanged();
}

QStringList Backend::availableFonts() const {
    // Leave out icon, emoji, and other symbol fonts, along with anything that
    // cannot set the Latin text the editor is written in.
    QStringList families;
    for (const QString &family : QFontDatabase::families(QFontDatabase::Latin)) {
        if (QFontDatabase::isPrivateFamily(family)
                || QFontDatabase::writingSystems(family).contains(QFontDatabase::Symbol))
            continue;
        families.append(family);
    }
    return selectableFontFamilies(families);
}

QStringList Backend::selectableFontFamilies(const QStringList &families) {
    QStringList selectable;
    static const QRegularExpression foundrySuffix(QStringLiteral("\\s*\\[[^\\]]*\\]$"));
    for (QString family : families) {
        // The same family from two foundries is listed as "Name [Foundry]".
        family.remove(foundrySuffix);
        if (family.startsWith(QStringLiteral("Noto ")) && !keptNotoFamilies.contains(family))
            continue;
        if (genericFontAliases.contains(family, Qt::CaseInsensitive)
                || family.contains(symbolFontName))
            continue;
        if (family == defaultEditorFont() || selectable.contains(family, Qt::CaseInsensitive))
            continue;
        selectable.append(family);
    }
    std::sort(selectable.begin(), selectable.end(), [](const QString &a, const QString &b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    selectable.prepend(defaultEditorFont());
    return selectable;
}

void Backend::documentFontChanged() {
    if (m_highlighter)
        m_highlighter->refreshFont();
}

void Backend::loadDocumentText(const QString &text) {
    if (!m_document) {
        setStatus(QStringLiteral("Could not attach the Markdown renderer."));
        return;
    }

    m_loading = true;
    m_document->setPlainText(text);
    m_lastDocumentText = text;
    m_loading = false;

    applyDocumentTypography();
    m_wordCountTimer.stop();
    setWordCount(countWords(text));
}

void Backend::setFileUrl(const QUrl &url) {
    if (m_fileUrl == url)
        return;

    m_fileUrl = url;
    emit fileUrlChanged();
    watchCurrentFile();
}

void Backend::setModified(bool modified) {
    if (m_modified == modified)
        return;

    m_modified = modified;
    emit modifiedChanged();
}

void Backend::setStatus(const QString &status) {
    if (m_status == status)
        return;

    m_status = status;
    emit statusChanged();
}

void Backend::saveTo(const QUrl &url) {
    if (!url.isLocalFile()) {
        m_closeAfterSave = false;
        setStatus(QStringLiteral("Only local files can be saved."));
        return;
    }

    const QString targetName = QFileInfo(url.toLocalFile()).fileName();
    QSaveFile file(url.toLocalFile());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_closeAfterSave = false;
        setStatus(QStringLiteral("Could not save %1.").arg(targetName));
        return;
    }

    const QByteArray contents = currentDocumentText().toUtf8();
    file.write(contents);

    // QSaveFile commits by replacing the target. Stop watching the old inode
    // before that replacement so our own write is not classified as external.
    const QStringList watched = m_fileWatcher.files();
    if (!watched.isEmpty())
        m_fileWatcher.removePaths(watched);

    // commit() flushes, fsyncs, and atomically renames the temp file into place,
    // returning false (and leaving the original untouched) on any write error.
    if (!file.commit()) {
        watchCurrentFile();
        m_closeAfterSave = false;
        setStatus(QStringLiteral("Could not write %1.").arg(targetName));
        return;
    }

    const bool shouldClose = m_closeAfterSave;
    m_closeAfterSave = false;
    m_lastKnownFileContents = contents;
    m_hasKnownFileContents = true;
    m_importedFrom.clear();
    setFileUrl(url);
    watchCurrentFile();
    QSettings().setValue(lastSaveDirectorySetting,
                         QFileInfo(url.toLocalFile()).absolutePath());
    setModified(false);
    setStatus(QStringLiteral("Saved %1").arg(fileName()));
    clearRecovery();
    emit saveSucceeded();

    if (shouldClose)
        emit closeAfterSave();
}

void Backend::scheduleRecovery() {
    m_recoveryTimer.start();
}

QString Backend::recoveryPath() const {
    return m_recoveryPath;
}

void Backend::writeRecovery() {
    if (!m_modified)
        return;
    const QString path = recoveryPath();
    if (path.isEmpty())
        return;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    const QJsonObject recovery{{QStringLiteral("fileUrl"), m_fileUrl.toString()},
                               {QStringLiteral("text"), currentDocumentText()}};
    file.write(QJsonDocument(recovery).toJson(QJsonDocument::Compact));
    file.commit();
}

void Backend::restoreRecovery() {
    QFile file(recoveryPath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll());
    if (!json.isObject() || !json.object().contains(QStringLiteral("text")))
        return;
    const QJsonObject recovery = json.object();
    loadDocumentText(recovery.value(QStringLiteral("text")).toString());
    const QUrl recoveredUrl(recovery.value(QStringLiteral("fileUrl")).toString());
    QFile diskFile(recoveredUrl.toLocalFile());
    if (recoveredUrl.isLocalFile() && diskFile.open(QIODevice::ReadOnly)) {
        m_lastKnownFileContents = diskFile.readAll();
        m_hasKnownFileContents = true;
    } else {
        m_lastKnownFileContents.clear();
        m_hasKnownFileContents = false;
    }
    setFileUrl(recoveredUrl);
    setModified(true);
    resetSessionGoal();
    setStatus(QStringLiteral("Recovered unsaved changes"));
}

void Backend::clearRecovery() {
    m_recoveryTimer.stop();
    QFile::remove(recoveryPath());
}

void Backend::watchCurrentFile() {
    const QStringList watched = m_fileWatcher.files();
    if (!watched.isEmpty())
        m_fileWatcher.removePaths(watched);
    if (m_fileUrl.isLocalFile() && QFileInfo::exists(m_fileUrl.toLocalFile()))
        m_fileWatcher.addPath(m_fileUrl.toLocalFile());
}

void Backend::loadOmarchyTheme() {
    m_themeBackground = m_darkMode ? QStringLiteral("#101010") : QStringLiteral("#ffffff");
    m_themeForeground = m_darkMode ? QStringLiteral("#eeeeee") : QStringLiteral("#222324");
    m_themeAccent = m_darkMode ? QStringLiteral("#5584aa") : QStringLiteral("#2077b2");
    m_themeSelection = m_darkMode ? QStringLiteral("#186a9a") : QStringLiteral("#2077b2");

    const QString colorsPath = QDir::homePath()
        + QStringLiteral("/.local/state/omarchy/current/theme/colors.toml");
    QString themeMode;
    QFile file(colorsPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        while (!in.atEnd()) {
            const QString line = in.readLine().trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
                continue;

            const int equals = line.indexOf(QLatin1Char('='));
            if (equals < 0)
                continue;

            const QString key = line.left(equals).trimmed();
            QString value = line.mid(equals + 1).trimmed();
            if (value.size() >= 2
                    && ((value.front() == QLatin1Char('"') && value.back() == QLatin1Char('"'))
                        || (value.front() == QLatin1Char('\'') && value.back() == QLatin1Char('\''))))
                value = value.mid(1, value.size() - 2);

            if (key == QStringLiteral("mode"))
                themeMode = value;
            else if (key == QStringLiteral("background"))
                m_themeBackground = value;
            else if (key == QStringLiteral("foreground"))
                m_themeForeground = value;
            else if (key == QStringLiteral("accent"))
                m_themeAccent = value;
            else if (key == QStringLiteral("selection"))
                m_themeSelection = value;
        }
    }

    bool themeModeKnown = false;
    bool themeIsDark = m_darkMode;
    if (themeMode == QStringLiteral("dark")) {
        themeIsDark = true;
        themeModeKnown = true;
    } else if (themeMode == QStringLiteral("light")) {
        themeIsDark = false;
        themeModeKnown = true;
    } else {
        const QColor background(m_themeBackground);
        if (background.isValid()) {
            const double luminance = 0.299 * background.redF()
                + 0.587 * background.greenF() + 0.114 * background.blueF();
            themeIsDark = luminance < 0.5;
            themeModeKnown = true;
        }
    }
    if (themeModeKnown && themeIsDark != m_darkMode) {
        m_darkMode = themeIsDark;
        emit darkModeChanged();
    }

    if (m_highlighter) {
        m_highlighter->setDarkMode(m_darkMode);
        m_highlighter->setColors(m_themeBackground, m_themeForeground, m_themeAccent);
    }

    emit themeColorsChanged();
}

void Backend::watchOmarchyTheme() {
    const QStringList watched = m_themeWatcher.files() + m_themeWatcher.directories();
    if (!watched.isEmpty())
        m_themeWatcher.removePaths(watched);

    const QString currentDir = QDir::homePath()
        + QStringLiteral("/.local/state/omarchy/current");
    const QString themeDir = currentDir + QStringLiteral("/theme");
    const QString colorsPath = themeDir + QStringLiteral("/colors.toml");

    if (QDir(currentDir).exists())
        m_themeWatcher.addPath(currentDir);
    if (QDir(themeDir).exists())
        m_themeWatcher.addPath(themeDir);
    if (QFile::exists(colorsPath))
        m_themeWatcher.addPath(colorsPath);
}

QUrl Backend::suggestedSaveUrl() const {
    if (m_fileUrl.isLocalFile())
        return m_fileUrl;

    // An imported Word file is saved as Markdown beside the original, which
    // is never overwritten.
    if (m_importedFrom.isLocalFile()) {
        const QFileInfo source(m_importedFrom.toLocalFile());
        return QUrl::fromLocalFile(
            source.absoluteDir().filePath(source.completeBaseName() + QStringLiteral(".md")));
    }

    const QString savedDirectory = QSettings().value(lastSaveDirectorySetting).toString();
    const QDir directory = savedDirectory.isEmpty() || !QDir(savedDirectory).exists()
        ? QDir(m_libraryPath)
        : QDir(savedDirectory);
    return QUrl::fromLocalFile(
        directory.filePath(suggestedFileName(currentDocumentText())));
}

QString Backend::currentDocumentText() const {
    return m_document ? m_document->toPlainText() : QString();
}

int Backend::countWords(const QString &text) {
    static const QRegularExpression wordRe(
        QStringLiteral("[\\p{L}\\p{N}]+(?:['-][\\p{L}\\p{N}]+)*"));
    int count = 0;
    QRegularExpressionMatchIterator it = wordRe.globalMatch(text);
    while (it.hasNext()) {
        it.next();
        ++count;
    }
    return count;
}

QString Backend::suggestedFileName(const QString &text) {
    QString name = text.section(QLatin1Char('\n'), 0, 0).trimmed();
    name.replace(QRegularExpression(QStringLiteral("[/\\x00-\\x1f\\x7f]")),
                 QStringLiteral("-"));
    name = name.left(120).trimmed();
    if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral(".."))
        name = QStringLiteral("Untitled");
    if (!name.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive))
        name += QStringLiteral(".md");
    return name;
}

void Backend::setWordCount(int words) {
    if (m_wordCount == words)
        return;

    m_wordCount = words;
    emit wordCountChanged();
}

void Backend::refreshWordCount() {
    setWordCount(countWords(currentDocumentText()));
}

void Backend::scheduleWordCount() {
    m_wordCountTimer.start();
}

void Backend::applyDocumentTypography() {
    if (!m_document)
        return;

    // A full pass is only used for freshly loaded/attached documents, so it is
    // safe to drop undo history here (re-enabling clears the stack anyway).
    const bool undoEnabled = m_document->isUndoRedoEnabled();
    m_document->setUndoRedoEnabled(false);
    layoutBlocks(0, m_document->characterCount() - 1);
    m_document->setUndoRedoEnabled(undoEnabled);
}

void Backend::reapplyTypographyToChange() {
    if (!m_document)
        return;

    // Lay out only the block(s) touched by the last edit instead of the whole
    // document. A heading, quote or centered line can change kind with any
    // keystroke, so this runs on every edit, but only writes a block format
    // when the block's layout actually changed.
    const int maxPos = m_document->characterCount() - 1;
    const int start = qBound(0, m_lastChangePos, maxPos);
    const int end = qBound(start, m_lastChangePos + m_lastChangeAdded, maxPos);
    layoutBlocks(start, end);
}

void Backend::layoutBlocks(int from, int to) {
    const qreal fontPixels = m_document->defaultFont().pixelSize() > 0
        ? m_document->defaultFont().pixelSize()
        : m_document->defaultFont().pointSizeF() * 4 / 3;

    QTextCursor cursor(m_document);
    bool editing = false;
    m_formattingTypography = true;
    const QTextBlock last = m_document->findBlock(to);
    for (QTextBlock block = m_document->findBlock(from); block.isValid(); block = block.next()) {
        const bool insideFence = block.previous().isValid() && block.previous().userState() == 1;
        int headingLevel = 0;
        const MarkdownHighlighter::BlockKind kind = insideFence
            ? MarkdownHighlighter::BlockKind::Paragraph
            : MarkdownHighlighter::blockKind(block.text(), &headingLevel);

        Qt::Alignment alignment = Qt::AlignLeft;
        qreal leftMargin = 0;
        qreal topMargin = 0;
        switch (kind) {
        case MarkdownHighlighter::BlockKind::Centered:
        case MarkdownHighlighter::BlockKind::SceneBreak:
        case MarkdownHighlighter::BlockKind::PageBreak:
            alignment = Qt::AlignHCenter;
            break;
        case MarkdownHighlighter::BlockKind::Quote:
            leftMargin = qRound(fontPixels * 1.5);
            break;
        case MarkdownHighlighter::BlockKind::Heading:
            topMargin = headingLevel <= 2 ? qRound(fontPixels * 0.5) : 0;
            break;
        default:
            break;
        }

        const QTextBlockFormat current = block.blockFormat();
        if (current.alignment() != alignment || !qFuzzyCompare(current.leftMargin() + 1, leftMargin + 1)
                || !qFuzzyCompare(current.topMargin() + 1, topMargin + 1)
                || current.lineHeightType() != QTextBlockFormat::ProportionalHeight
                || !qFuzzyCompare(current.lineHeight(), typoraLineHeightPercent)) {
            if (!editing) {
                // Fold the layout into the preceding edit command so a single
                // undo reverts both the text and its formatting.
                cursor.joinPreviousEditBlock();
                editing = true;
            }
            QTextBlockFormat format = current;
            format.setAlignment(alignment);
            format.setLeftMargin(leftMargin);
            format.setTopMargin(topMargin);
            format.setLineHeight(typoraLineHeightPercent, QTextBlockFormat::ProportionalHeight);
            cursor.setPosition(block.position());
            cursor.setBlockFormat(format);
        }
        if (block == last)
            break;
    }
    if (editing)
        cursor.endEditBlock();
    m_formattingTypography = false;
}
