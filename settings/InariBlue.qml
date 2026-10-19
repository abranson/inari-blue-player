import QtQuick 2.6
import Nemo.DBus 2.0
import Sailfish.Silica 1.0
import com.jolla.settings.system 1.0

Item {
    id: root

    visible: present

    property bool present
    property bool lightsEnabled
    property bool blocked
    property string serviceError
    property var assignments: ({})
    property bool loadingState
    property var patterns: []

    height: present ? content.height : 0

    readonly property var categories: [
        //% "Idle"
        { key: "idle", label: qsTrId("settings_sounds-inari_blue-idle") },
        //% "Ringtone"
        { key: "ringtone", label: qsTrId("settings_sounds-inari_blue-ringtone") },
        //% "Internet call"
        { key: "internet-call", label: qsTrId("settings_sounds-inari_blue-internet_call") },
        //% "Message"
        { key: "message", label: qsTrId("settings_sounds-inari_blue-message") },
        //% "Chat"
        { key: "chat", label: qsTrId("settings_sounds-inari_blue-chat") },
        //% "Email"
        { key: "email", label: qsTrId("settings_sounds-inari_blue-email") },
        //% "Clock alarm"
        { key: "clock", label: qsTrId("settings_sounds-inari_blue-clock") },
        //% "Calendar"
        { key: "calendar", label: qsTrId("settings_sounds-inari_blue-calendar") },
        //% "Other notifications"
        { key: "other", label: qsTrId("settings_sounds-inari_blue-other") }
    ]

    function applyState(json) {
        loadingState = true
        try {
            var state = JSON.parse(json)
            present = !!state.present
            lightsEnabled = !!state.enabled
            blocked = !!state.blocked
            serviceError = state.error || ""
            assignments = state.assignments || ({})
            patterns = state.patterns || []
            loadingState = false
        } catch (e) {
            loadingState = false
            present = false
            serviceError = "Invalid player state"
        }
    }

    function refreshState() {
        loadingState = true
        blueDbus.typedCall("GetState", [], function (json) {
            applyState(json)
        }, function (error) {
            loadingState = false
            present = false
            serviceError = error || "Inari Blue player unavailable"
        })
    }

    function callFailed(error) {
        loadingState = false
        serviceError = error || "Inari Blue player request failed"
    }

    function setEnabled(value) {
        loadingState = true
        blueDbus.typedCall("SetEnabled", [{ "type": "b", "value": value }], function (accepted) {
            if (accepted)
                loadingState = false
            else
                refreshState()
        }, function (error) {
            callFailed(error)
        })
    }

    function patternName(id) {
        for (var i = 0; i < patterns.length; ++i) {
            if (id !== "off" && patterns[i].id === id)
                return patterns[i].name
        }
        //% "None"
        return qsTrId("settings_sounds-inari_blue-none")
    }

    function choosePattern(category, label) {
        var pending = pageStack.animatorPush(Qt.resolvedUrl("InariPatternDialog.qml"), {
            categoryTitle: label,
            activePattern: assignments[category] || "off",
            activePatternName: patternName(assignments[category]),
            patterns: patterns
        })
        pending.pageCompleted.connect(function(dialog) {
            dialog.accepted.connect(function() {
                loadingState = true
                var method = dialog.selectedFile ? "SetPatternFile" : "SetPattern"
                blueDbus.typedCall(method, [
                    { "type": "s", "value": category },
                    { "type": "s", "value": dialog.selectedFile || dialog.selectedPattern }
                ], function(accepted) {
                    loadingState = false
                    if (!accepted) {
                        //% "Could not save the pattern"
                        serviceError = qsTrId("settings_sounds-inari_blue-save_failed")
                    }
                }, callFailed)
            })
        })
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

    Column {
        id: content

        width: parent.width
        spacing: Theme.paddingSmall

        SectionHeader {
            //% "Inari Blue"
            text: qsTrId("settings_sounds-inari_blue-section")
        }

        DisabledByMdmBanner {
            active: root.blocked
            compressed: true
            text: qsTrId("settings_system-la-disabled_by_privacy_mode")
        }

        TextSwitch {
            automaticCheck: false
            checked: root.lightsEnabled
            enabled: !root.loadingState && !root.blocked
            //% "Inari Blue lights"
            text: qsTrId("settings_sounds-inari_blue-enable")
            //% "Use the notification light patterns"
            description: qsTrId("settings_sounds-inari_blue-enable_description")
            onClicked: root.setEnabled(!root.lightsEnabled)
        }

        Label {
            visible: root.serviceError.length > 0
            width: parent.width - 2 * Theme.horizontalPageMargin
            x: Theme.horizontalPageMargin
            color: Theme.errorColor
            wrapMode: Text.Wrap
            text: root.serviceError
        }

        Repeater {
            model: root.categories
            delegate: ValueButton {
                width: content.width
                label: modelData.label
                value: root.patternName(root.assignments[modelData.key])
                enabled: !root.loadingState && !root.blocked
                onClicked: root.choosePattern(modelData.key, modelData.label)
            }
        }
    }

    Component.onCompleted: refreshState()
}
