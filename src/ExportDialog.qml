import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Dialog {
    id: root

    property bool darkMode: true
    property color textColor: darkMode ? "#d0d0d0" : "#42464c"
    property color strongTextColor: darkMode ? "#eeeeee" : "#222324"
    property color mutedColor: "#909191"
    property color activeButtonColor: "#428bca"
    property int containerWidth: 420
    property int containerHeight: 320
    property real textScale: 1

    // Remembered between exports.
    property string format: "pdf"
    property string style: "manuscript"

    readonly property var formats: [
        {key: "md", label: "Markdown", extension: "md"},
        {key: "txt", label: "Plain text", extension: "txt"},
        {key: "pdf", label: "PDF", extension: "pdf"},
        {key: "docx", label: "Word", extension: "docx"}
    ]
    readonly property bool styled: format === "pdf" || format === "docx"

    signal exportRequested(string format, string style, string title, string extension)

    function scaledSize(pixels) {
        return Math.max(1, Math.round(pixels * textScale));
    }

    function extensionFor(key) {
        for (var i = 0; i < formats.length; ++i) {
            if (formats[i].key === key)
                return formats[i].extension;
        }
        return "md";
    }

    Settings {
        category: "export"
        property alias format: root.format
        property alias style: root.style
    }

    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    width: Math.min(scaledSize(480), containerWidth - 48)
    x: Math.round((containerWidth - width) / 2)
    y: Math.round(Math.max(24, (containerHeight - height) / 2))
    padding: 20

    onAboutToShow: titleField.text = backend.currentTitle()

    background: Rectangle {
        color: root.darkMode ? "#1a1a1a" : "#ffffff"
        border.color: root.darkMode ? "#343434" : "#d8d8d8"
        radius: 0
    }

    component SectionLabel: Label {
        color: root.mutedColor
        font.family: "IBM Plex Mono"
        font.pixelSize: root.scaledSize(11)
        topPadding: 4
    }

    component Choice: Button {
        id: choice
        property bool selected: false
        checkable: false
        Layout.fillWidth: true
        Layout.preferredWidth: 1
        leftPadding: 8
        rightPadding: 8
        topPadding: 7
        bottomPadding: 7

        contentItem: Label {
            text: choice.text
            color: choice.selected ? "#ffffff" : root.textColor
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.family: "IBM Plex Mono"
            font.pixelSize: root.scaledSize(12)
        }

        background: Rectangle {
            implicitHeight: 34
            color: choice.selected ? root.activeButtonColor
                 : choice.hovered ? (root.darkMode ? "#242424" : "#eeeeee")
                 : (root.darkMode ? "#202020" : "#f6f6f6")
            border.color: choice.activeFocus ? root.strongTextColor
                        : (root.darkMode ? "#424242" : "#c8c8c8")
        }
    }

    contentItem: ColumnLayout {
        spacing: 8

        Label {
            text: "Export"
            color: root.strongTextColor
            font.family: "IBM Plex Mono"
            font.pixelSize: root.scaledSize(16)
            font.bold: true
        }

        SectionLabel { text: "Format" }

        RowLayout {
            spacing: 6
            Repeater {
                model: root.formats
                Choice {
                    required property var modelData
                    objectName: "format_" + modelData.key
                    text: modelData.label
                    selected: root.format === modelData.key
                    onClicked: root.format = modelData.key
                }
            }
        }

        SectionLabel { text: "Style"; visible: root.styled }

        RowLayout {
            visible: root.styled
            spacing: 6
            Choice {
                objectName: "style_manuscript"
                text: "Manuscript"
                selected: root.style === "manuscript"
                onClicked: root.style = "manuscript"
            }
            Choice {
                objectName: "style_book"
                text: "Book"
                selected: root.style === "book"
                onClicked: root.style = "book"
            }
        }

        Label {
            Layout.fillWidth: true
            visible: root.styled
            wrapMode: Text.Wrap
            color: root.mutedColor
            font.family: "IBM Plex Mono"
            font.pixelSize: root.scaledSize(11)
            text: root.style === "manuscript"
                  ? "Standard submission format: Times 12pt, double-spaced, 1\" margins, name and word count on page one, Name / Title / page header."
                  : "Printed-book look: serif, justified, indented paragraphs, ornamental scene breaks, chapters on new pages."
        }

        SectionLabel { text: "Title"; visible: root.styled }

        TextField {
            id: titleField
            objectName: "exportTitleField"
            Layout.fillWidth: true
            visible: root.styled
            font.family: "IBM Plex Mono"
            font.pixelSize: root.scaledSize(13)
        }

        SectionLabel { text: "Author"; visible: root.styled }

        TextField {
            objectName: "exportAuthorField"
            Layout.fillWidth: true
            visible: root.styled
            placeholderText: "Your name (or pen name)"
            text: backend.authorName
            font.family: "IBM Plex Mono"
            font.pixelSize: root.scaledSize(13)
            onEditingFinished: backend.authorName = text
            onTextChanged: backend.authorName = text
        }

        SectionLabel {
            text: "Contact block (top-left of page one)"
            visible: root.styled && root.style === "manuscript"
        }

        TextArea {
            objectName: "exportContactField"
            Layout.fillWidth: true
            visible: root.styled && root.style === "manuscript"
            placeholderText: "Street address\nCity, State ZIP\nemail@example.com"
            text: backend.authorContact
            wrapMode: TextEdit.Wrap
            font.family: "IBM Plex Mono"
            font.pixelSize: root.scaledSize(12)
            onTextChanged: backend.authorContact = text
        }
    }

    footer: Item {
        implicitHeight: dialogButtons.implicitHeight + 20

        Row {
            id: dialogButtons
            anchors.right: parent.right
            anchors.rightMargin: 20
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8

            SquareDialogButton {
                text: "Cancel"
                darkMode: root.darkMode
                textScale: root.textScale
                labelColor: root.textColor
                onClicked: root.reject()
            }

            SquareDialogButton {
                objectName: "exportConfirmButton"
                text: "Export…"
                primary: true
                darkMode: root.darkMode
                textScale: root.textScale
                activeColor: root.activeButtonColor
                onClicked: {
                    root.close();
                    root.exportRequested(root.format, root.style, titleField.text.trim(),
                                         root.extensionFor(root.format));
                }
            }
        }
    }
}
