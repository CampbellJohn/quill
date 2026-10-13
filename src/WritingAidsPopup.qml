import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Popup {
    id: popup

    property bool darkMode: true
    property real textScale: 1
    property color textColor: "#d0d0d0"
    property color mutedColor: "#909191"

    function scaledSize(pixels) {
        return Math.max(1, Math.round(pixels * textScale));
    }

    modal: false
    focus: true
    padding: 14
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Material.elevation: 8

    background: Rectangle {
        radius: 9
        color: popup.darkMode ? "#22221f" : "#fffef2"
    }

    component AidSwitch: Item {
        id: aid
        property alias text: label.text
        property alias hint: hintLabel.text
        property alias checked: toggle.checked
        signal toggled(bool checked)

        width: parent ? parent.width : 0
        implicitHeight: Math.max(labels.implicitHeight, toggle.implicitHeight)

        Column {
            id: labels
            anchors.left: parent.left
            anchors.right: toggle.left
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter

            Label {
                id: label
                color: popup.textColor
                font.family: "IBM Plex Mono"
                font.pixelSize: popup.scaledSize(13)
            }

            Label {
                id: hintLabel
                width: parent.width
                visible: text.length > 0
                color: popup.mutedColor
                wrapMode: Text.Wrap
                font.family: "IBM Plex Mono"
                font.pixelSize: popup.scaledSize(11)
            }
        }

        Switch {
            id: toggle
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            onToggled: aid.toggled(checked)
        }
    }

    contentItem: Column {
        width: popup.availableWidth
        spacing: 6

        Label {
            text: "Writing aids"
            color: popup.textColor
            font.family: "IBM Plex Mono"
            font.pixelSize: popup.scaledSize(14)
            font.bold: true
            bottomPadding: 4
        }

        AidSwitch {
            objectName: "smartTypographySwitch"
            text: "Smart typography"
            hint: "Curly quotes, -- to em dash, ... to ellipsis"
            checked: backend.smartTypography
            onToggled: function(checked) { backend.smartTypography = checked; }
        }

        AidSwitch {
            objectName: "typewriterSwitch"
            text: "Typewriter scrolling"
            hint: "Keep the line you're writing centered"
            checked: backend.typewriterScrolling
            onToggled: function(checked) { backend.typewriterScrolling = checked; }
        }

        AidSwitch {
            objectName: "wordCountSwitch"
            text: "Word count"
            checked: backend.showWordCount
            onToggled: function(checked) { backend.showWordCount = checked; }
        }

        AidSwitch {
            objectName: "sessionGoalSwitch"
            text: "Session goal"
            hint: "Words written since this story was opened"
            checked: backend.showSessionGoal
            onToggled: function(checked) { backend.showSessionGoal = checked; }
        }

        RowLayout {
            width: parent.width
            visible: backend.showSessionGoal
            spacing: 8

            SpinBox {
                id: goalBox
                objectName: "sessionGoalBox"
                from: 50
                to: 20000
                stepSize: 50
                editable: true
                value: backend.sessionGoal
                onValueModified: backend.sessionGoal = value
            }

            Label {
                text: "words"
                color: popup.mutedColor
                font.family: "IBM Plex Mono"
                font.pixelSize: popup.scaledSize(12)
            }

            Item { Layout.fillWidth: true }

            Button {
                text: "Restart"
                flat: true
                font.family: "IBM Plex Mono"
                font.pixelSize: popup.scaledSize(11)
                onClicked: backend.resetSessionGoal()
                ToolTip.visible: hovered
                ToolTip.text: "Count the session from the current word count"
            }
        }

        Rectangle {
            width: parent.width
            height: 1
            color: popup.mutedColor
            opacity: 0.3
        }

        AidSwitch {
            objectName: "autosaveSwitch"
            text: "Autosave"
            hint: "Save stories that already have a file as you type"
            checked: backend.autosave
            onToggled: function(checked) { backend.autosave = checked; }
        }
    }
}
