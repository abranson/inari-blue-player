import QtQuick 2.6
import Sailfish.Silica 1.0

Page {
    SilicaFlickable {
        anchors.fill: parent
        contentHeight: content.height

        Column {
            id: content

            width: parent.width

            PageHeader {
                title: qsTr("Inari Blue")
            }

            InariBlue {
                id: controls

                width: parent.width
            }
        }

        ViewPlaceholder {
            enabled: !controls.present && !controls.loadingState
            text: controls.serviceError || qsTr("Attach your Inari Blue cover to configure its lights")
        }

        VerticalScrollDecorator {}
    }
}
