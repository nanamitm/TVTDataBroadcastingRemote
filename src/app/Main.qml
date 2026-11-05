import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Shapes

ApplicationWindow {
    id: window
    width: 430
    height: 850
    visible: true
    title: "TVTest Remote"
    Material.theme: Material.Dark
    Material.accent: "#5ec9c1"
    color: "#111b25"
    readonly property var selectedTarget: {
        for (const target of remote.targets) {
            if (target.id === remote.selected) return target
        }
        return null
    }
    readonly property var controls: remote.controls
    readonly property bool canOperate: remote.connected && remote.selected.length > 0
    // 録画の経過時間 (サーバーの時計で録画開始時刻からの差を1秒ごとに更新する)
    property string recordElapsed: "録画中"
    function updateRecordElapsed() {
        const start = window.controls.recordStart
        const now = remote.serverNow()
        if (start === undefined || now <= 0) { recordElapsed = "録画中"; return }
        const total = Math.max(0, Math.floor((now - start) / 1000))
        const pad = (n) => (n < 10 ? "0" : "") + n
        recordElapsed = Math.floor(total / 3600) + ":" + pad(Math.floor(total / 60) % 60) + ":" + pad(total % 60)
    }
    Timer {
        interval: 1000
        repeat: true
        triggeredOnStart: true
        running: window.controls.record === true
        onTriggered: window.updateRecordElapsed()
    }

    component RemoteButton: Button {
        property string remoteKey
        Layout.fillWidth: true
        Layout.minimumHeight: 56
        enabled: remote.connected && remote.selected.length > 0
        font.pixelSize: 19
        onClicked: remote.press(remoteKey)
    }

    component DirectionButton: RemoteButton {
        id: directionButton
        property int arrowRotation: 0
        Accessible.name: text
        autoRepeat: true
        autoRepeatDelay: 450
        autoRepeatInterval: 160
        contentItem: Item {
            implicitWidth: 36
            implicitHeight: 36
            Shape {
                anchors.centerIn: parent
                width: 24
                height: 24
                rotation: directionButton.arrowRotation
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    strokeWidth: -1
                    fillColor: directionButton.enabled ? "#f4f7fa" : "#5a6977"
                    startX: 12
                    startY: 2
                    PathLine { x: 23; y: 22 }
                    PathLine { x: 1; y: 22 }
                    PathLine { x: 12; y: 2 }
                }
            }
        }
    }

    // 2行表示のマス（上：機能名、下：現在の状態）
    component StatusCell: Button {
        id: statusCell
        property string title
        property string detail
        property bool lit: false
        Layout.fillWidth: true
        Layout.minimumHeight: 56
        enabled: window.canOperate
        highlighted: lit
        contentItem: ColumnLayout {
            spacing: 0
            Label {
                text: statusCell.title
                font.pixelSize: 16
                horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
                color: statusCell.enabled ? "#f4f7fa" : "#5a6977"
            }
            Label {
                text: statusCell.detail
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                Layout.fillWidth: true
                color: statusCell.enabled ? (statusCell.lit ? "#f4f7fa" : "#a9c3cf") : "#5a6977"
            }
        }
    }

    header: ToolBar {
        Material.background: "#1c2c3b"
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 12
            Label { text: "TVTest Remote"; font.pixelSize: 21; Layout.fillWidth: true }
            // 全画面の切替。押し間違えると困る録画ボタンから離して置く
            ToolButton {
                id: fullscreenButton
                readonly property bool on: window.controls.fullscreen === true
                visible: window.canOperate && window.controls.fullscreen !== undefined
                implicitWidth: 48
                implicitHeight: 48
                Accessible.name: on ? "全画面表示を解除" : "全画面表示"
                ToolTip.text: Accessible.name
                ToolTip.visible: hovered
                onClicked: remote.control("fullscreen", !on)
                background: Rectangle {
                    anchors.centerIn: parent
                    width: 40; height: 40; radius: 20
                    color: fullscreenButton.on ? "#5ec9c1" : "transparent"
                }
                // 四隅のかぎ形で全画面を表す (端末のフォントや絵文字に依存しない)
                contentItem: Item {
                    implicitWidth: 24
                    implicitHeight: 24
                    Repeater {
                        model: 4
                        delegate: Item {
                            required property int index
                            readonly property color ink: fullscreenButton.on ? "#11202b" : "#edf4f8"
                            width: 8; height: 8
                            x: (index % 2 === 0) ? 4 : parent.width - width - 4
                            y: (index < 2) ? 4 : parent.height - height - 4
                            Rectangle { width: parent.width; height: 2; color: parent.ink; y: index < 2 ? 0 : parent.height - 2 }
                            Rectangle { width: 2; height: parent.height; color: parent.ink; x: index % 2 === 0 ? 0 : parent.width - 2 }
                        }
                    }
                }
            }
            ToolButton {
                id: settingsButton
                implicitWidth: 48
                implicitHeight: 48
                Accessible.name: "接続設定"
                ToolTip.text: "接続設定"
                ToolTip.visible: hovered
                onClicked: settings.open()
                contentItem: Item {
                    implicitWidth: 24
                    implicitHeight: 24
                    Item {
                        width: 24
                        height: 24
                        anchors.centerIn: parent
                        opacity: settingsButton.down ? 0.7 : 1
                        Repeater {
                            model: 8
                            delegate: Item {
                                required property int index
                                anchors.fill: parent
                                rotation: index * 45
                                Rectangle {
                                    x: 10; y: 1
                                    width: 4; height: 6
                                    radius: 0.7
                                    color: "#edf4f8"
                                    antialiasing: true
                                }
                            }
                        }
                        Rectangle {
                            anchors.centerIn: parent
                            width: 16; height: 16
                            radius: 8
                            color: "transparent"
                            border.width: 3
                            border.color: "#edf4f8"
                            antialiasing: true
                        }
                    }
                }
            }
        }
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        ColumnLayout {
            width: parent.width
            spacing: 10
            // 操作中のTVTestと、そのTVTestへの操作 (全画面・録画)
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 12
                Layout.topMargin: 4
                spacing: 6
                Label {
                    text: window.selectedTarget && !remote.status.startsWith("操作結果:")
                          && !remote.status.startsWith("接続エラー:")
                          ? window.selectedTarget.name + " / " + window.selectedTarget.channel
                          : remote.status
                    color: remote.connected ? "#88d5cc" : "#dfbc9c"
                    font.pixelSize: 14
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Button {
                    id: recordButton
                    readonly property bool recording: window.controls.record === true
                    visible: window.canOperate && window.controls.record !== undefined
                    text: recording ? "● " + window.recordElapsed : "● 録画"
                    Accessible.name: recording ? "録画を停止" : "録画を開始"
                    font.pixelSize: 13
                    font.bold: recording
                    flat: !recording
                    Material.background: recording ? "#b3343f" : "transparent"
                    Material.foreground: recording ? "#ffffff" : "#ff8a8a"
                    Layout.preferredHeight: 40
                    // 開始はすぐ行い、停止は取り消せないので確認してから行う
                    onClicked: recording ? stopRecordDialog.open() : remote.control("record", true)
                }
            }
            GridLayout {
                columns: 3
                uniformCellWidths: true
                Layout.fillWidth: true
                Layout.margins: 12
                columnSpacing: 10
                rowSpacing: 10
                RemoteButton { text: "d"; remoteKey: "data"; font.bold: true }
                DirectionButton { text: "上"; remoteKey: "up"; arrowRotation: 0 }
                RemoteButton { text: "戻る"; remoteKey: "back" }
                DirectionButton { text: "左"; remoteKey: "left"; arrowRotation: 270 }
                RemoteButton { text: "決定"; remoteKey: "enter"; Layout.minimumHeight: 72; highlighted: true }
                DirectionButton { text: "右"; remoteKey: "right"; arrowRotation: 90 }
                // 方向キー下段の左右：放送（字幕・チャンネル）と音量
                StatusCell {
                    title: "放送"
                    detail: window.controls.caption === undefined ? "字幕 --"
                            : (window.controls.caption ? "字幕 入" : "字幕 切")
                    lit: window.controls.caption === true
                    Accessible.name: "放送メニュー（字幕・チャンネル）"
                    onClicked: broadcastDrawer.open()
                }
                DirectionButton { text: "下"; remoteKey: "down"; arrowRotation: 180 }
                StatusCell {
                    title: "音量"
                    detail: window.controls.volume === undefined ? "--"
                            : (window.controls.mute ? "消音中" : window.controls.volume.toString())
                    lit: window.controls.mute === true
                    Accessible.name: "音量メニュー"
                    enabled: window.canOperate && window.controls.volume !== undefined
                    onClicked: audioPopup.show()
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                RemoteButton { text: "青"; remoteKey: "blue"; Material.background: "#255fa8" }
                RemoteButton { text: "赤"; remoteKey: "red"; Material.background: "#a53b46" }
                RemoteButton { text: "緑"; remoteKey: "green"; Material.background: "#327c55" }
                RemoteButton { text: "黄"; remoteKey: "yellow"; Material.background: "#8a6c23" }
            }
            GridLayout {
                columns: 3
                uniformCellWidths: true
                Layout.fillWidth: true
                Layout.margins: 12
                Repeater {
                    model: ["1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12"]
                    delegate: RemoteButton {
                        required property string modelData
                        text: modelData === "10" ? "10 / 0" : modelData
                        remoteKey: modelData
                    }
                }
            }
        }
    }

    // 現在のチャンネルか (一覧の位置はTVTestから分からないため、空間とIDで照合する)
    function isCurrentChannel(channel) {
        const c = window.controls.channel
        return !!c && window.listsCurrentDriver() && c.space === channel.space && c.serviceId === channel.serviceId
            && c.networkId === channel.networkId && c.transportStreamId === channel.transportStreamId
    }
    // 一覧が使用中のBonDriverのものか (古いプラグインはBonDriverを知らせないので常に真)
    function listsCurrentDriver() {
        const current = window.controls.channel ? window.controls.channel.driver : undefined
        return !current || !remote.channelDriver || current.toLowerCase() === remote.channelDriver.toLowerCase()
    }
    // "BonDriver_PX-MLT.dll" → "PX-MLT"
    function driverLabel(name) {
        return name.replace(/^BonDriver_/i, "").replace(/\.dll$/i, "")
    }
    function programTime(program) {
        const start = new Date(program.start)
        const end = new Date(program.start + program.duration * 1000)
        return Qt.formatTime(start, "hh:mm") + "–" + Qt.formatTime(end, "hh:mm")
    }

    // 放送メニュー：下から開くシート。字幕を最上部、その下に放送波ごとのチャンネル一覧
    // Drawerはシートのスワイプで閉じる操作が一覧の縦スクロールと奪い合い、
    // それを止めると外側のタップや戻るボタンでも閉じなくなるため、下から出すPopupにする
    Popup {
        id: broadcastDrawer
        readonly property bool channelsSupported: window.controls.channel !== undefined
        parent: Overlay.overlay
        modal: true
        width: window.width
        height: channelsSupported ? window.height * 0.8
                : Math.min(window.height * 0.7, broadcastContent.implicitHeight + 48 + bottomPadding)
        x: 0
        y: parent ? parent.height - height : 0
        padding: 0
        bottomPadding: Overlay.overlay ? Overlay.overlay.SafeArea.margins.bottom : 0
        // フォーカスが無いと戻るボタン (Key_Back) がアプリの終了として扱われる
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        enter: Transition { NumberAnimation { property: "y"; from: broadcastDrawer.parent.height; to: broadcastDrawer.parent.height - broadcastDrawer.height; duration: 200; easing.type: Easing.OutCubic } }
        exit: Transition { NumberAnimation { property: "y"; to: broadcastDrawer.parent.height; duration: 150; easing.type: Easing.InCubic } }
        background: Rectangle { color: "#1d2936"; radius: 16 }
        Overlay.modal: Rectangle { color: "#99000000" }
        onOpened: remote.requestChannels()
        function showCurrentSpace() {
            const c = window.controls.channel
            const spaces = remote.channelSpaces
            let index = channelTabs.currentIndex < spaces.length ? channelTabs.currentIndex : 0
            for (let i = 0; c && window.listsCurrentDriver() && i < spaces.length; i++) {
                if (spaces[i].space === c.space) { index = i; break }
            }
            channelTabs.currentIndex = index
            tabScroll.restart()
        }
        // タブが画面幅を超える場合も、選択中のタブ全体が見える位置までスクロールする。
        // 一覧 (BonDriver) が変わるとタブが作り直されるため、配置が終わってから動かす
        Timer {
            id: tabScroll
            interval: 50
            onTriggered: channelTabs.reveal(channelTabs.currentIndex)
        }
        ColumnLayout {
            id: broadcastContent
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12
            Rectangle { Layout.alignment: Qt.AlignHCenter; width: 40; height: 4; radius: 2; color: "#4a5d6e" }
            Label { text: "放送"; font.pixelSize: 20; color: "#edf4f8" }
            Switch {
                text: "字幕を表示"
                Layout.fillWidth: true
                // 状態はTVTestからの通知で更新するため、タップで直接切り替えない
                checkable: false
                checked: window.controls.caption === true
                enabled: window.canOperate && window.controls.caption !== undefined
                onClicked: remote.control("caption", !checked)
            }
            Label {
                visible: window.canOperate && window.controls.caption === undefined
                text: "このTVTestのプラグインは字幕操作に対応していません"
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                font.pixelSize: 13
                color: "#a9b5c2"
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: "#344657" }
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Label { text: "チャンネル"; font.bold: true }
                // 使っていないBonDriverを選ぶと、その一覧を表示する。局を選んだときに切り替える
                ComboBox {
                    id: driverBox
                    visible: remote.channelDrivers.length > 1
                    Layout.fillWidth: true
                    Accessible.name: "BonDriver"
                    model: remote.channelDrivers
                    displayText: remote.channelDriver ? window.driverLabel(remote.channelDriver) : ""
                    currentIndex: remote.channelDrivers.indexOf(remote.channelDriver)
                    enabled: !remote.channelsLoading
                    delegate: ItemDelegate {
                        required property string modelData
                        required property int index
                        width: driverBox.width
                        text: window.driverLabel(modelData)
                              + (window.controls.channel && window.controls.channel.driver
                                 && modelData.toLowerCase() === window.controls.channel.driver.toLowerCase() ? "（使用中）" : "")
                        highlighted: driverBox.highlightedIndex === index
                    }
                    onActivated: (index) => remote.requestChannels(remote.channelDrivers[index])
                }
            }
            Label {
                visible: !broadcastDrawer.channelsSupported || remote.channelSpaces.length === 0
                text: !window.canOperate ? "操作するTVTestを選択してください"
                      : !broadcastDrawer.channelsSupported ? "このTVTestのプラグインはチャンネル一覧に対応していません"
                      : remote.channelsLoading ? "チャンネル一覧を取得しています…"
                      : "チャンネルがありません"
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                font.pixelSize: 13
                color: "#a9b5c2"
            }
            // 放送波のタブ。TabBarは選択中のタブに合わせて内部の表示位置をずらし、先頭のタブが
            // 欠けることがあるため、横スクロールする行にタブを並べて位置を自分で決める
            Flickable {
                id: channelTabs
                property int currentIndex: 0
                visible: remote.channelSpaces.length > 0
                Layout.fillWidth: true
                // タブの合計幅でレイアウトが広がると左右にはみ出すため、幅はシートに合わせてスクロールさせる
                Layout.preferredWidth: 0
                Layout.maximumWidth: broadcastContent.width
                Layout.preferredHeight: tabRow.height
                contentWidth: tabRow.width
                contentHeight: tabRow.height
                clip: true
                flickableDirection: Flickable.HorizontalFlick
                boundsBehavior: Flickable.StopAtBounds
                // 指定したタブ全体が見える位置までスクロールする
                function reveal(index) {
                    const item = tabRepeater.itemAt(index)
                    const maxX = Math.max(0, contentWidth - width)
                    if (!item || index <= 0) { contentX = 0; return }
                    if (item.x < contentX) contentX = Math.min(maxX, item.x)
                    else if (item.x + item.width > contentX + width) contentX = Math.min(maxX, item.x + item.width - width)
                }
                Row {
                    id: tabRow
                    Repeater {
                        id: tabRepeater
                        model: remote.channelSpaces
                        delegate: TabButton {
                            id: spaceTab
                            required property var modelData
                            required property int index
                            text: modelData.name
                            width: implicitWidth
                            checkable: false
                            checked: channelTabs.currentIndex === index
                            onClicked: { channelTabs.currentIndex = index; channelTabs.reveal(index) }
                            Rectangle {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: 2
                                color: "#5ec9c1"
                                visible: spaceTab.checked
                            }
                        }
                    }
                }
            }
            StackLayout {
                visible: remote.channelSpaces.length > 0
                currentIndex: channelTabs.currentIndex
                Layout.fillWidth: true
                Layout.fillHeight: true
                Repeater {
                    model: remote.channelSpaces
                    delegate: ListView {
                        id: channelList
                        required property var modelData
                        clip: true
                        model: modelData.channels
                        boundsBehavior: Flickable.StopAtBounds
                        // スクロールの指の動きで項目が押された表示にならないよう、押下を少し遅らせる
                        pressDelay: 120
                        ScrollIndicator.vertical: ScrollIndicator {}
                        Component.onCompleted: {
                            for (let i = 0; i < modelData.channels.length; i++) {
                                if (window.isCurrentChannel(modelData.channels[i])) { positionViewAtIndex(i, ListView.Center); break }
                            }
                        }
                        delegate: ItemDelegate {
                            id: channelRow
                            required property var modelData
                            readonly property bool current: window.isCurrentChannel(modelData)
                            width: channelList.width
                            highlighted: current
                            enabled: window.canOperate
                            Accessible.name: modelData.name + (modelData.program ? " " + modelData.program.name : "")
                            onClicked: { remote.selectChannel(modelData); broadcastDrawer.close() }
                            contentItem: RowLayout {
                                spacing: 12
                                Label {
                                    text: channelRow.modelData.remoteControlKey > 0 ? channelRow.modelData.remoteControlKey : ""
                                    horizontalAlignment: Text.AlignRight
                                    Layout.preferredWidth: 28
                                    font.pixelSize: 18
                                    color: channelRow.current ? "#5ec9c1" : "#a9c3cf"
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Label {
                                        text: channelRow.modelData.name
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                        font.pixelSize: 16
                                        font.bold: channelRow.current
                                        color: "#edf4f8"
                                    }
                                    Label {
                                        visible: !!channelRow.modelData.program
                                        text: channelRow.modelData.program
                                              ? window.programTime(channelRow.modelData.program) + "  " + channelRow.modelData.program.name : ""
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                        font.pixelSize: 12
                                        color: "#a9b5c2"
                                    }
                                }
                            }
                        }
                    }
                }
            }
            Item { visible: !broadcastDrawer.channelsSupported || remote.channelSpaces.length === 0; Layout.fillHeight: true }
        }
    }

    // 音量ポップアップ：操作中は開いたまま、操作が止まって数秒で閉じる
    Popup {
        id: audioPopup
        function show() { open(); keepOpen() }
        function keepOpen() { autoClose.restart() }
        readonly property bool available: window.canOperate && window.controls.volume !== undefined
        parent: Overlay.overlay
        width: Math.min(window.width - 24, 420)
        x: (parent.width - width) / 2
        // ナビゲーションバーに重ならないよう、画面下端の安全領域分を上げる
        // contentItemは既に安全領域内なので、全画面を覆うOverlay側の余白を使う
        y: parent.height - height - 16 - parent.SafeArea.margins.bottom
        padding: 16
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: "#1d2936"; radius: 16; border.color: "#344657" }
        onClosed: autoClose.stop()
        Timer {
            id: autoClose
            interval: 4000
            onTriggered: {
                if (volumeSlider.pressed || volumeDown.pressed || volumeUp.pressed) restart()
                else audioPopup.close()
            }
        }
        contentItem: ColumnLayout {
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                Label { text: "音量"; font.pixelSize: 18; color: "#edf4f8"; Layout.fillWidth: true }
                Label {
                    text: audioPopup.available ? Math.round(volumeSlider.value).toString() : "--"
                    font.pixelSize: 18
                    color: window.controls.mute === true ? "#dfbc9c" : "#edf4f8"
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Button {
                    id: volumeDown
                    text: "−"
                    Accessible.name: "音量を下げる"
                    font.pixelSize: 22
                    Layout.preferredWidth: 56
                    Layout.minimumHeight: 52
                    enabled: audioPopup.available
                    autoRepeat: true
                    autoRepeatDelay: 450
                    autoRepeatInterval: 160
                    onClicked: { remote.stepVolume(-5); audioPopup.keepOpen() }
                }
                Slider {
                    id: volumeSlider
                    from: 0
                    to: 100
                    stepSize: 1
                    Layout.fillWidth: true
                    Accessible.name: "音量"
                    enabled: audioPopup.available
                    onMoved: { remote.control("volume", Math.round(value)); audioPopup.keepOpen() }
                    // ドラッグ中はTVTestからの状態通知で位置を戻さない
                    Binding on value {
                        when: !volumeSlider.pressed
                        value: window.controls.volume !== undefined ? window.controls.volume : 0
                    }
                }
                Button {
                    id: volumeUp
                    text: "＋"
                    Accessible.name: "音量を上げる"
                    font.pixelSize: 22
                    Layout.preferredWidth: 56
                    Layout.minimumHeight: 52
                    enabled: audioPopup.available
                    autoRepeat: true
                    autoRepeatDelay: 450
                    autoRepeatInterval: 160
                    onClicked: { remote.stepVolume(5); audioPopup.keepOpen() }
                }
            }
            Button {
                readonly property bool on: window.controls.mute === true
                text: on ? "消音を解除" : "消音"
                Accessible.name: "消音の切替"
                Layout.fillWidth: true
                Layout.minimumHeight: 48
                highlighted: on
                enabled: window.canOperate && window.controls.mute !== undefined
                onClicked: { remote.control("mute", !on); audioPopup.keepOpen() }
            }
            Label {
                text: "本体の音量ボタンでも操作できます"
                font.pixelSize: 12
                color: "#a9b5c2"
                Layout.fillWidth: true
            }
        }
    }

    // 録画の停止は取り消せないため確認する
    Dialog {
        id: stopRecordDialog
        anchors.centerIn: parent
        width: Math.min(window.width - 48, 360)
        modal: true
        focus: true
        title: "録画を停止しますか？"
        background: Rectangle { color: "#1d2936"; radius: 16 }
        Overlay.modal: Rectangle { color: "#b3000000" }
        contentItem: Label {
            text: (window.selectedTarget ? window.selectedTarget.name + " の" : "") + "録画を停止します（" + window.recordElapsed + "）。"
            wrapMode: Text.Wrap
            color: "#edf4f8"
        }
        footer: DialogButtonBox {
            background: Rectangle { color: "#1d2936"; radius: 16 }
            Button { text: "キャンセル"; flat: true; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
            Button { text: "停止"; highlighted: true; Material.accent: "#e05661"; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
        }
        onAccepted: remote.control("record", false)
    }

    Dialog {
        id: settings
        property string pendingTarget: ""
        anchors.centerIn: parent
        width: Math.min(window.width - 24, 430)
        height: Math.min(window.height - 32, implicitHeight)
        title: "接続設定"
        modal: true
        onClosed: pendingTarget = ""
        header: Label { text: settings.title; color: "#edf4f8"; font.pixelSize: 20; padding: 18 }
        footer: DialogButtonBox {
            background: Rectangle { color: "#1d2936"; radius: 16 }
            Button { text: "閉じる"; flat: true; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
            onRejected: settings.close()
        }
        background: Rectangle { color: "#1d2936"; radius: 16 }
        Overlay.modal: Rectangle { color: "#b3000000" }
        contentItem: ScrollView {
            implicitHeight: connectionForm.implicitHeight
            contentWidth: availableWidth
            ColumnLayout {
                id: connectionForm
                width: parent.width
                spacing: 12
                Label { text: "サーバーURL" }
                TextField { id: url; text: remote.savedUrl; placeholderText: "ws://192.168.1.10:18765"; Layout.fillWidth: true; inputMethodHints: Qt.ImhUrlCharactersOnly }
                Switch {
                    text: "起動・復帰時に自動接続"
                    checked: remote.autoConnect
                    Layout.fillWidth: true
                    onToggled: remote.autoConnect = checked
                }
                RowLayout {
                    Layout.fillWidth: true
                    Button {
                        text: remote.connected ? "再接続" : "接続"
                        Layout.fillWidth: true
                        onClicked: { settings.pendingTarget = ""; remote.connectTo(url.text) }
                    }
                    Button {
                        text: "切断"
                        Layout.fillWidth: true
                        onClicked: { settings.pendingTarget = ""; remote.disconnectFromServer() }
                    }
                }
                Label {
                    text: remote.status
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                    color: remote.connected ? "#88d5cc" : "#dfbc9c"
                    font.pixelSize: 13
                }
                Rectangle { visible: remote.connected; Layout.fillWidth: true; height: 1; color: "#344657" }
                Label { visible: remote.connected; text: "操作するTVTest"; font.bold: true }
                Repeater {
                    model: remote.targets
                    delegate: ItemDelegate {
                        id: targetChoice
                        required property var modelData
                        Layout.fillWidth: true
                        highlighted: modelData.id === remote.selected
                        enabled: !modelData.busy || modelData.id === remote.selected
                        Accessible.name: modelData.name + " / " + modelData.channel
                        onClicked: {
                            if (modelData.id === remote.selected) settings.close()
                            else {
                                settings.pendingTarget = modelData.id
                                remote.claim(modelData.id)
                            }
                        }
                        contentItem: RowLayout {
                            spacing: 10
                            Rectangle {
                                Layout.alignment: Qt.AlignVCenter
                                width: 18; height: 18; radius: 9
                                color: "transparent"
                                border.width: 2
                                border.color: targetChoice.modelData.id === remote.selected ? "#5ec9c1" : "#a9b5c2"
                                Rectangle {
                                    anchors.centerIn: parent
                                    width: 8; height: 8; radius: 4
                                    color: "#5ec9c1"
                                    visible: targetChoice.modelData.id === remote.selected
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Label { text: targetChoice.modelData.name; Layout.fillWidth: true; elide: Text.ElideRight }
                                Label {
                                    text: targetChoice.modelData.channel
                                          + (targetChoice.modelData.identityConflict ? "（識別トークン重複）" : "")
                                          + (targetChoice.modelData.busy && targetChoice.modelData.id !== remote.selected ? "（使用中）" : "")
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    font.pixelSize: 13
                                    color: "#a9b5c2"
                                }
                            }
                        }
                    }
                }
                Label {
                    visible: remote.connected && remote.targets.length === 0
                    text: "TVTestの接続を待っています"
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                    color: "#a9b5c2"
                }
                Button {
                    visible: remote.selected.length > 0
                    text: "操作権を解放"
                    Layout.fillWidth: true
                    onClicked: { settings.pendingTarget = ""; remote.release() }
                }
            }
        }
    }
    Connections {
        target: remote
        function onChanged() {
            if (settings.pendingTarget.length > 0 && remote.selected === settings.pendingTarget) {
                settings.pendingTarget = ""
                settings.close()
            }
            if (!window.canOperate) { broadcastDrawer.close(); audioPopup.close() }
        }
        function onChannelsChanged() {
            if (broadcastDrawer.visible) broadcastDrawer.showCurrentSpace()
        }
        function onVolumeKeyPressed() {
            if (!settings.opened && !broadcastDrawer.opened) audioPopup.show()
        }
    }
    Component.onCompleted: {
        if (japaneseFontFamily.length > 0) window.font.family = japaneseFontFamily
        remote.start()
        if (!remote.hasSavedConnection) settings.open()
    }
}
