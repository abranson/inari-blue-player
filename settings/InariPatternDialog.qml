import QtQuick 2.6
import Nemo.DBus 2.0
import Sailfish.Silica 1.0
import Sailfish.Pickers 1.0
import com.jolla.settings.system 1.0

Dialog {
    id: patternDialog

    property string categoryTitle
    property string activePattern
    property string activePatternName
    property var patterns: []
    property string selectedPattern: activePattern
    property string selectedFile
    property string selectedName: activePatternName
    property bool validating
    property bool blocked
    property bool present: true
    property bool previewing
    property bool previewRequested
    property bool previewOnActive
    property string errorText
    property var levels: [0, 0, 0]
    property int selectionGeneration
    property int previewGeneration
    readonly property bool foreground: Qt.application.state === Qt.ApplicationActive

    canAccept: !validating && !blocked && present && (selectedPattern !== "" || selectedFile !== "")

    function stopPreview() {
        ++previewGeneration
        previewRequested = false
        previewing = false
        levels = [0, 0, 0]
        blueDbus.typedCall("StopPreview", [], function() {}, function() {})
    }

    function startPreview() {
        if (blocked || !present || !foreground || status !== PageStatus.Active)
            return
        var generation = ++previewGeneration
        previewRequested = true
        errorText = ""
        blueDbus.typedCall(selectedFile ? "PreviewFile" : "Preview", [
            { "type": "s", "value": selectedFile || selectedPattern },
            { "type": "u", "value": 5000 }
        ], function(accepted) {
            if (generation !== previewGeneration)
                return
            previewing = accepted && selectedPattern !== "off"
            if (!accepted) {
                previewRequested = false
                //% "Could not preview the pattern"
                errorText = qsTrId("settings_sounds-inari_blue-preview_failed")
            }
        }, function() {
            if (generation === previewGeneration) {
                previewRequested = false
                //% "The Inari Blue player is unavailable"
                errorText = qsTrId("settings_sounds-inari_blue-unavailable")
            }
        })
    }

    function selectPattern(id, name) {
        var stopOnly = !selectedFile && selectedPattern === id && previewing
        ++selectionGeneration
        validating = false
        stopPreview()
        selectedFile = ""
        selectedPattern = id
        selectedName = name
        errorText = ""
        if (!stopOnly)
            startPreview()
    }

    function selectFile(path) {
        stopPreview()
        validating = true
        errorText = ""
        var generation = ++selectionGeneration
        blueDbus.typedCall("InspectPatternFile", [{ "type": "s", "value": path }], function(json) {
            if (generation !== selectionGeneration)
                return
            validating = false
            try {
                var info = JSON.parse(json)
                if (!info.valid) {
                    errorText = info.error
                    return
                }
                selectedFile = path
                selectedPattern = ""
                selectedName = info.name
                if (status === PageStatus.Active)
                    startPreview()
                else
                    previewOnActive = true
            } catch (e) {
                //% "Invalid pattern file"
                errorText = qsTrId("settings_sounds-inari_blue-invalid_file")
            }
        }, function() {
            if (generation === selectionGeneration) {
                validating = false
                errorText = qsTrId("settings_sounds-inari_blue-unavailable")
            }
        })
    }

    function applyState(json) {
        try {
            var state = JSON.parse(json)
            blocked = !!state.blocked
            present = !!state.present
            previewing = previewRequested && !!state.preview && state.playing !== ""
            if (!previewing)
                levels = [0, 0, 0]
            if (blocked || !present) {
                previewOnActive = false
                if (previewRequested || previewing) stopPreview()
            }
        } catch (e) {
            errorText = qsTrId("settings_sounds-inari_blue-unavailable")
        }
    }

    DBusInterface {
        id: blueDbus
        bus: DBus.SessionBus
        service: "org.sailfishos.InariBlue1"
        path: "/org/sailfishos/InariBlue1"
        iface: "org.sailfishos.InariBlue1"
        signalsEnabled: true
        function stateChanged(json) { patternDialog.applyState(json) }
        function frameChanged(a, b, c) { if (patternDialog.previewRequested) patternDialog.levels = [a, b, c] }
    }

    Component {
        id: filePicker
        FilePickerPage {
            nameFilters: ["*.json"]
            //% "Select pattern file"
            title: qsTrId("settings_sounds-inari_blue-select_file")
            onSelectedContentPropertiesChanged: {
                if (selectedContentProperties && selectedContentProperties.filePath)
                    patternDialog.selectFile(selectedContentProperties.filePath)
            }
        }
    }

    SilicaListView {
        id: patternList
        anchors.fill: parent
        model: patternDialog.patterns
        header: Column {
            width: parent.width
            DialogHeader {
                dialog: patternDialog
                title: patternDialog.categoryTitle
            }
            PatternItem {
                title: patternDialog.activePatternName
                //% "Current pattern"
                subtitle: qsTrId("settings_sounds-inari_blue-current_pattern")
                icon: "image://theme/icon-m-pause"
                iconOpacity: patternDialog.previewing && !patternDialog.selectedFile && patternDialog.selectedPattern === patternDialog.activePattern ? 1 : 0
                enabled: !patternDialog.blocked && patternDialog.present
                onClicked: patternDialog.selectPattern(patternDialog.activePattern, patternDialog.activePatternName)
            }
            BackgroundItem {
                id: selectFromFile
                enabled: !patternDialog.blocked && patternDialog.present
                onClicked: {
                    patternDialog.stopPreview()
                    pageStack.animatorPush(filePicker)
                }
                Image {
                    id: fileIcon
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    source: "image://theme/icon-m-document" + (selectFromFile.down ? "?" + Theme.highlightColor : "")
                }
                Label {
                    anchors.left: fileIcon.right
                    anchors.leftMargin: Theme.paddingMedium
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    color: selectFromFile.down ? Theme.highlightColor : Theme.primaryColor
                    //% "Select from file"
                    text: qsTrId("settings_sounds-inari_blue-select_from_file")
                }
            }
            PatternItem {
                visible: patternDialog.selectedFile !== ""
                height: visible ? implicitHeight : 0
                title: patternDialog.selectedName
                //% "Selected file"
                subtitle: qsTrId("settings_sounds-inari_blue-selected_file")
                selected: true
                icon: "image://theme/icon-m-pause"
                iconOpacity: patternDialog.previewing ? 1 : 0
                enabled: !patternDialog.blocked && patternDialog.present
                onClicked: patternDialog.previewing ? patternDialog.stopPreview() : patternDialog.startPreview()
            }
            TextSwitch {
                //% "None"
                text: qsTrId("settings_sounds-inari_blue-none")
                automaticCheck: false
                checked: !patternDialog.selectedFile && patternDialog.selectedPattern === "off"
                enabled: !patternDialog.blocked && patternDialog.present
                onClicked: {
                    if (!checked)
                        patternDialog.selectPattern("off", text)
                    else if (patternDialog.activePattern !== "off")
                        patternDialog.selectPattern(patternDialog.activePattern, patternDialog.activePatternName)
                }
            }
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Theme.paddingLarge
                Repeater {
                    model: 3
                    delegate: Rectangle {
                        width: Theme.iconSizeSmall
                        height: width
                        radius: width / 2
                        color: Theme.highlightColor
                        opacity: 0.15 + 0.85 * patternDialog.levels[index] / 255
                        Label {
                            anchors.centerIn: parent
                            text: index
                            color: Theme.primaryColor
                            font.pixelSize: Theme.fontSizeTiny
                        }
                    }
                }
            }
            Label {
                visible: patternDialog.errorText !== "" || patternDialog.blocked || !patternDialog.present
                height: visible ? implicitHeight : 0
                width: parent.width - 2 * Theme.horizontalPageMargin
                x: Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: patternDialog.blocked ? Theme.secondaryColor : Theme.errorColor
                text: patternDialog.blocked
                      ? //% "Disabled by privacy mode"
                        qsTrId("settings_sounds-inari_blue-privacy_disabled")
                      : (!patternDialog.present
                         ? //% "Attach Inari Blue to preview patterns"
                           qsTrId("settings_sounds-inari_blue-cover_missing")
                         : patternDialog.errorText)
            }
            Item { width: 1; height: Theme.paddingLarge }
        }
        delegate: PatternItem {
            visible: modelData.id !== "off"
            height: visible ? implicitHeight : 0
            title: modelData.name
            selected: !patternDialog.selectedFile && patternDialog.selectedPattern === modelData.id
            icon: "image://theme/icon-m-pause"
            iconOpacity: selected && patternDialog.previewing ? 1 : 0
            enabled: !patternDialog.blocked && patternDialog.present
            onClicked: patternDialog.selectPattern(modelData.id, modelData.name)
        }
        VerticalScrollDecorator {}
    }

    Component.onCompleted: blueDbus.typedCall("GetState", [], applyState, function() {})
    Component.onDestruction: stopPreview()
    onStatusChanged: {
        if (status === PageStatus.Deactivating || status === PageStatus.Inactive)
            stopPreview()
        else if (status === PageStatus.Active && previewOnActive) {
            previewOnActive = false
            startPreview()
        }
    }
    onForegroundChanged: if (!foreground) stopPreview()
    onDone: stopPreview()
}
