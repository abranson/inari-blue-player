import QtQuick 2.6
import Nemo.DBus 2.0
import Sailfish.Silica 1.0

Item {
    id: root

    property string label: qsTr("Disable Inari Blue")
    property string description
    property bool present
    property bool usePrivacySwitch: true
    property bool busy: true
    property string errorText
    width: parent.width
    height: present ? content.height : 0
    visible: present

    function applyState(json) {
        try {
            var state = JSON.parse(json)
            present = !!state.present
            usePrivacySwitch = state.privacySwitch !== false
            busy = false
        } catch (e) {
            busy = false
        }
    }

    Column {
        id: content
        width: parent.width
        IconTextSwitch {
            // Shared by the Inari page and the optional central privacy entry.
            //% "Disable Inari Blue"
            text: root.label
            description: root.description
            icon.source: "image://theme/icon-m-notifications"
            automaticCheck: false
            checked: root.usePrivacySwitch
            enabled: !root.busy
            onClicked: {
                root.busy = true
                root.errorText = ""
                blueDbus.typedCall("SetPrivacySwitch", [{ "type": "b", "value": !root.usePrivacySwitch }], function(accepted) {
                    root.busy = false
                    if (!accepted)
                        root.errorText = qsTr("Could not save the privacy switch setting")
                }, function() {
                    root.busy = false
                    root.errorText = qsTr("The Inari Blue player is unavailable")
                })
            }
        }
        Label {
            visible: root.errorText !== ""
            height: visible ? implicitHeight : 0
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * x
            wrapMode: Text.Wrap
            color: Theme.errorColor
            text: root.errorText
        }
    }
    DBusInterface {
        id: blueDbus
        bus: DBus.SessionBus
        service: "org.sailfishos.InariBlue1"
        path: "/org/sailfishos/InariBlue1"
        iface: "org.sailfishos.InariBlue1"
        signalsEnabled: true
        function stateChanged(json) { root.applyState(json) }
    }
    Component.onCompleted: blueDbus.typedCall("GetState", [], applyState, function() { root.busy = false })
}
