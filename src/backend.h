#pragma once

#include <QObject>
#include <QPointer>
#include <QByteArray>
#include <QFileSystemWatcher>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <memory>

class MarkdownHighlighter;
class QTextDocument;
class QWindow;
class QLockFile;

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QUrl fileUrl READ fileUrl NOTIFY fileUrlChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY fileUrlChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY modifiedChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(int wordCount READ wordCount NOTIFY wordCountChanged)
    Q_PROPERTY(bool darkMode READ darkMode WRITE setDarkMode NOTIFY darkModeChanged)
    Q_PROPERTY(qreal textScale READ textScale WRITE setTextScale NOTIFY textScaleChanged)
    Q_PROPERTY(QString themeBackground READ themeBackground NOTIFY themeColorsChanged)
    Q_PROPERTY(QString themeForeground READ themeForeground NOTIFY themeColorsChanged)
    Q_PROPERTY(QString themeAccent READ themeAccent NOTIFY themeColorsChanged)
    Q_PROPERTY(QString themeSelection READ themeSelection NOTIFY themeColorsChanged)
    Q_PROPERTY(QString editorFont READ editorFont WRITE setEditorFont NOTIFY editorFontChanged)
    Q_PROPERTY(QStringList availableFonts READ availableFonts CONSTANT)
    Q_PROPERTY(bool smartTypography READ smartTypography WRITE setSmartTypography NOTIFY preferencesChanged)
    Q_PROPERTY(bool typewriterScrolling READ typewriterScrolling WRITE setTypewriterScrolling NOTIFY preferencesChanged)
    Q_PROPERTY(bool showWordCount READ showWordCount WRITE setShowWordCount NOTIFY preferencesChanged)
    Q_PROPERTY(bool showSessionGoal READ showSessionGoal WRITE setShowSessionGoal NOTIFY preferencesChanged)
    Q_PROPERTY(int sessionGoal READ sessionGoal WRITE setSessionGoal NOTIFY preferencesChanged)
    Q_PROPERTY(bool autosave READ autosave WRITE setAutosave NOTIFY preferencesChanged)
    Q_PROPERTY(int sessionWords READ sessionWords NOTIFY wordCountChanged)
    Q_PROPERTY(QString libraryPath READ libraryPath CONSTANT)
    Q_PROPERTY(bool exporting READ exporting NOTIFY exportingChanged)
    Q_PROPERTY(QString authorName READ authorName WRITE setAuthorName NOTIFY preferencesChanged)
    Q_PROPERTY(QString authorContact READ authorContact WRITE setAuthorContact NOTIFY preferencesChanged)

public:
    explicit Backend(QObject *parent = nullptr);
    ~Backend() override;

    void setParentWindow(QWindow *window);

    QUrl fileUrl() const { return m_fileUrl; }
    QString fileName() const;

    bool modified() const { return m_modified; }
    QString status() const { return m_status; }
    int wordCount() const { return m_wordCount; }
    bool darkMode() const { return m_darkMode; }
    void setDarkMode(bool darkMode);
    qreal textScale() const { return m_textScale; }
    void setTextScale(qreal textScale);
    QString themeBackground() const { return m_themeBackground; }
    QString themeForeground() const { return m_themeForeground; }
    QString themeAccent() const { return m_themeAccent; }
    QString themeSelection() const { return m_themeSelection; }
    QString editorFont() const { return m_editorFont; }
    void setEditorFont(const QString &family);
    QStringList availableFonts() const;
    static QString defaultEditorFont();
    static QStringList selectableFontFamilies(const QStringList &families);
    static int countWords(const QString &text);
    static QString normalizedLinkUrl(const QString &clipboardText);
    static QString suggestedFileName(const QString &text);
    static QString documentTitle(const QString &text, const QString &fallback);
    static QString defaultLibraryPath();

    bool smartTypography() const { return m_smartTypography; }
    void setSmartTypography(bool enabled);
    bool typewriterScrolling() const { return m_typewriterScrolling; }
    void setTypewriterScrolling(bool enabled);
    bool showWordCount() const { return m_showWordCount; }
    void setShowWordCount(bool enabled);
    bool showSessionGoal() const { return m_showSessionGoal; }
    void setShowSessionGoal(bool enabled);
    int sessionGoal() const { return m_sessionGoal; }
    void setSessionGoal(int words);
    bool autosave() const { return m_autosave; }
    void setAutosave(bool enabled);
    int sessionWords() const { return qMax(0, m_wordCount - m_sessionStartWords); }
    QString libraryPath() const { return m_libraryPath; }
    bool exporting() const { return m_exportProcess != nullptr; }
    QString authorName() const { return m_authorName; }
    void setAuthorName(const QString &name);
    QString authorContact() const { return m_authorContact; }
    void setAuthorContact(const QString &contact);

    Q_INVOKABLE void attachDocument(QObject *textDocument);
    Q_INVOKABLE void openDialog();
    Q_INVOKABLE void open(const QUrl &url);
    Q_INVOKABLE void save();
    Q_INVOKABLE void saveForClose();
    Q_INVOKABLE void saveAsDialog();
    Q_INVOKABLE void saveAs(const QUrl &url);
    Q_INVOKABLE void fileDialogCanceled();
    Q_INVOKABLE void discardRecovery();
    Q_INVOKABLE void reloadFromDisk();
    Q_INVOKABLE void keepExternalVersion();
    Q_INVOKABLE void printDocument();
    Q_INVOKABLE void newWindow();
    Q_INVOKABLE QString clipboardUrl() const;
    Q_INVOKABLE QString clipboardText() const;
    Q_INVOKABLE bool editorTextChanged();
    Q_INVOKABLE bool saveIfAutosaving();
    Q_INVOKABLE void setCursorPosition(int position);
    Q_INVOKABLE void newDocument();
    Q_INVOKABLE QVariantList libraryFiles() const;
    Q_INVOKABLE QString currentTitle() const;
    Q_INVOKABLE QUrl suggestedExportUrl(const QString &extension) const;
    Q_INVOKABLE void exportDocument(const QUrl &url, const QString &format, const QString &style,
                                    const QString &title);
    Q_INVOKABLE void resetSessionGoal();
    Q_INVOKABLE void setSearchHighlight(const QString &query, int currentMatchStart);
    Q_INVOKABLE void openExternalUrl(const QUrl &url);
    Q_INVOKABLE QVariantMap windowGeometry() const;
    Q_INVOKABLE void saveWindowGeometry(int x, int y, int width, int height, bool maximized);
    Q_INVOKABLE void documentFontChanged();

signals:
    void fileUrlChanged();
    void modifiedChanged();
    void statusChanged();
    void wordCountChanged();
    void darkModeChanged();
    void textScaleChanged();
    void themeColorsChanged();
    void editorFontChanged();
    void closeAfterSave();
    void openDialogRequested();
    void saveDialogRequested(const QUrl &suggestedUrl);
    void saveSucceeded();
    void externalChangeDetected(bool deleted, bool locallyModified);
    void preferencesChanged();
    void libraryChanged();
    void exportingChanged();

private:
    void loadDocumentText(const QString &text);
    void setFileUrl(const QUrl &url);
    void setModified(bool modified);
    void setStatus(const QString &status);
    void saveTo(const QUrl &url);
    QUrl suggestedSaveUrl() const;
    QString currentDocumentText() const;
    void setWordCount(int words);
    void refreshWordCount();
    void scheduleWordCount();
    void applyDocumentTypography();
    void reapplyTypographyToChange();
    void layoutBlocks(int from, int to);
    void importDocument(const QUrl &url);
    QString helperPath() const;
    void setPreference(bool &member, bool value, const QString &key);
    void watchLibrary();
    void scheduleRecovery();
    void writeRecovery();
    void restoreRecovery();
    void clearRecovery();
    QString recoveryPath() const;
    void watchCurrentFile();
    void loadOmarchyTheme();
    void watchOmarchyTheme();

    QUrl m_fileUrl;
    bool m_modified = false;
    QString m_status;
    int m_wordCount = 0;
    bool m_darkMode = true;
    qreal m_textScale = 1.0;
    bool m_loading = false;
    bool m_closeAfterSave = false;
    bool m_formattingTypography = false;
    int m_lastChangePos = 0;
    int m_lastChangeAdded = 0;
    int m_lastChangeRemoved = 0;
    int m_sessionStartWords = 0;
    bool m_smartTypography = true;
    bool m_typewriterScrolling = false;
    bool m_showWordCount = true;
    bool m_showSessionGoal = false;
    int m_sessionGoal = 500;
    bool m_autosave = true;
    QString m_libraryPath;
    QString m_authorName;
    QString m_authorContact;
    QUrl m_importedFrom;
    QTimer m_autosaveTimer;
    QFileSystemWatcher m_libraryWatcher;
    QProcess *m_exportProcess = nullptr;
    QTimer m_wordCountTimer;
    QTimer m_recoveryTimer;
    QFileSystemWatcher m_fileWatcher;
    QPointer<QTextDocument> m_document;
    QPointer<QWindow> m_parentWindow;
    QPointer<MarkdownHighlighter> m_highlighter;
    QString m_lastDocumentText;
    QByteArray m_lastKnownFileContents;
    bool m_hasKnownFileContents = false;
    QString m_recoveryPath;
    std::unique_ptr<QLockFile> m_recoveryLock;

    QString m_themeBackground;
    QString m_themeForeground;
    QString m_themeAccent;
    QString m_themeSelection;
    QString m_editorFont;
    QFileSystemWatcher m_themeWatcher;
};
