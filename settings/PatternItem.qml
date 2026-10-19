import QtQuick 2.6
import Sailfish.Silica 1.0

BackgroundItem {
    id: root

    property bool selected
    property alias title: titleLabel.text
    property alias subtitle: subtitleLabel.text
    property alias icon: previewIcon.source
    property alias iconOpacity: previewIcon.opacity

    width: parent ? parent.width : 0
    implicitHeight: Math.max(Theme.itemSizeSmall, labels.height + 2 * Theme.paddingSmall)
    height: implicitHeight
    opacity: enabled ? 1 : Theme.opacityLow

    Rectangle {
        anchors.fill: parent
        visible: root.selected && !root.down
        color: Theme.highlightBackgroundColor
        opacity: Theme.highlightBackgroundOpacity
    }

    Column {
        id: labels

        x: Theme.horizontalPageMargin
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width - 2 * Theme.horizontalPageMargin - Theme.itemSizeSmall

        Label {
            id: titleLabel

            width: parent.width
            truncationMode: TruncationMode.Fade
            color: root.selected || root.down ? Theme.highlightColor : Theme.primaryColor
        }

        Label {
            id: subtitleLabel

            width: parent.width
            visible: text.length > 0
            font.pixelSize: Theme.fontSizeExtraSmall
            truncationMode: TruncationMode.Fade
            color: root.selected || root.down ? Theme.secondaryHighlightColor : Theme.secondaryColor
        }
    }

    Image {
        id: previewIcon

        anchors.right: parent.right
        anchors.rightMargin: Theme.horizontalPageMargin + (Theme.itemSizeSmall - width) / 2
        anchors.verticalCenter: parent.verticalCenter
        opacity: 0
        Behavior on opacity { FadeAnimation {} }
    }
}
