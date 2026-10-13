import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

Rectangle {
    id: panel

    property bool open: false
    property bool darkMode: true
    property real textScale: 1
    property color textColor: "#d0d0d0"
    property color mutedColor: "#909191"
    property color highlightColor: "#428bca"
    property var files: []

    signal fileChosen(url url)
    signal newRequested()
    signal dismissed()

    function scaledSize(pixels) {
        return Math.max(1, Math.round(pixels * textScale));
    }

    function refresh() {
        files = backend.libraryFiles();
    }

    readonly property var filteredFiles: {
        var query = filterField.text.trim().toLocaleLowerCase();
        if (query.length === 0)
            return files;
        return files.filter(function(file) {
            return file.name.toLocaleLowerCase().indexOf(query) !== -1;
        });
    }

    x: open ? 0 : -width
    visible: x > -width
    color: darkMode ? "#161616" : "#f7f7f5"
    focus: open

    Behavior on x {
        NumberAnimation { duration: 160; easing.type: Easing.OutCubic }
    }

    onOpenChanged: {
        if (open) {
            refresh();
            filterField.text = "";
            fileList.currentIndex = 0;
            filterField.forceActiveFocus();
        }
    }

    Connections {
        target: backend
        function onLibraryChanged() { if (panel.open) panel.refresh(); }
        function onFileUrlChanged() { if (panel.open) panel.refresh(); }
    }

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: panel.mutedColor
        opacity: 0.25
    }

    Column {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10

        Item {
            width: parent.width
            height: panel.scaledSize(28)

            Label {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: "Writing"
                color: panel.textColor
                font.family: "IBM Plex Mono"
                font.pixelSize: panel.scaledSize(15)
                font.bold: true
            }

            Button {
                objectName: "newStoryButton"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: "+ New"
                flat: true
                font.family: "IBM Plex Mono"
                font.pixelSize: panel.scaledSize(12)
                onClicked: panel.newRequested()
            }
        }

        Label {
            width: parent.width
            text: backend.libraryPath.replace(/^\/home\/[^\/]+/, "~")
            color: panel.mutedColor
            elide: Text.ElideMiddle
            font.family: "IBM Plex Mono"
            font.pixelSize: panel.scaledSize(11)
        }

        Item {
            width: parent.width
            height: panel.scaledSize(30)

            TextInput {
                id: filterField
                anchors.fill: parent
                verticalAlignment: TextInput.AlignVCenter
                color: panel.textColor
                selectByMouse: true
                clip: true
                font.family: "IBM Plex Mono"
                font.pixelSize: panel.scaledSize(13)
                onTextChanged: fileList.currentIndex = 0

                Keys.onUpPressed: fileList.decrementCurrentIndex()
                Keys.onDownPressed: fileList.incrementCurrentIndex()
                Keys.onReturnPressed: chooseCurrent()
                Keys.onEnterPressed: chooseCurrent()
                Keys.onEscapePressed: panel.dismissed()

                function chooseCurrent() {
                    var file = panel.filteredFiles[fileList.currentIndex];
                    if (file)
                        panel.fileChosen(file.url);
                }
            }

            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: "Filter stories"
                visible: filterField.text.length === 0
                color: panel.mutedColor
                font: filterField.font
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: panel.mutedColor
                opacity: 0.3
            }
        }

        ListView {
            id: fileList
            objectName: "libraryList"
            width: parent.width
            height: parent.height - y
            clip: true
            model: panel.filteredFiles
            boundsBehavior: Flickable.StopAtBounds
            highlightMoveDuration: 0
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: ItemDelegate {
                id: row
                required property var modelData
                required property int index

                width: fileList.width
                height: panel.scaledSize(52)
                leftPadding: 8
                rightPadding: 8
                highlighted: ListView.isCurrentItem
                onClicked: panel.fileChosen(modelData.url)

                background: Rectangle {
                    radius: 5
                    color: panel.highlightColor
                    opacity: row.highlighted ? 0.22 : (row.hovered ? 0.12 : 0)
                }

                contentItem: Column {
                    spacing: 2

                    Label {
                        width: parent.width
                        text: row.modelData.name
                        elide: Text.ElideRight
                        color: panel.textColor
                        font.family: "IBM Plex Mono"
                        font.pixelSize: panel.scaledSize(13)
                        font.bold: row.modelData.current
                    }

                    Label {
                        width: parent.width
                        text: row.modelData.words.toLocaleString(Qt.locale(), "f", 0)
                              + (row.modelData.words === 1 ? " word" : " words")
                              + " · " + row.modelData.modified
                        color: panel.mutedColor
                        font.family: "IBM Plex Mono"
                        font.pixelSize: panel.scaledSize(11)
                    }
                }
            }

            Label {
                anchors.top: parent.top
                anchors.topMargin: 8
                width: parent.width
                visible: fileList.count === 0
                text: panel.files.length === 0
                      ? "No stories yet. Save one here and it will show up."
                      : "No matches."
                color: panel.mutedColor
                wrapMode: Text.Wrap
                font.family: "IBM Plex Mono"
                font.pixelSize: panel.scaledSize(12)
            }
        }
    }
}
