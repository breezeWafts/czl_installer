import QtQuick 2.12
import QtQml 2.12                                // ★ 新增，显式导入 QtQml
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.12
import QtQuick.Controls 1.4 as OldControls

Rectangle {
    id: root
    width: 1200
    height: 750
    color: "#fafafa"

    // =========================================================
    // 三段式纵向布局：顶部工具栏 / 中间内容 / 底部状态栏
    // =========================================================
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---------- 顶部工具栏 ----------
        Rectangle {
            Layout.fillWidth: true
            height: 46
            color: "#f5f5f5"

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: "#ddd"
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 12

                Label {
                    text: "服务器方式:"
                    font.bold: true
                }

                ComboBox {
                    id: serverTypeCombo
                    model: ["FTP (多地址备选)", "HTTP (多地址备选)"]
                    currentIndex: 0
                    onCurrentIndexChanged: {
                        var t = currentIndex === 0 ? "ftp" : "http";
                        installManager.setServerConfigFromQml(t);
                        treeModel.reload();
                        itemModel.loadAll();
                    }
                }

                Item { Layout.fillWidth: true }

                Label {
                    text: installManager.activeServerName
                          ? ("当前服务器: " + installManager.activeServerName)
                          : "当前服务器: 未连接"
                    color: installManager.activeServerName ? "#2b7a2b" : "#999"
                    font.bold: true
                }

                Label {
                    text: "本地缓存: " + configManager.localBaseDir
                    color: "#666"
                }
            }
        }

        // ---------- 中间主体：左树 + 右列表 ----------
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ===== 左侧：树形菜单 =====
            Rectangle {
                Layout.preferredWidth: 280
                Layout.minimumWidth: 200
                Layout.fillHeight: true
                color: "#ffffff"

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    Rectangle {
                        Layout.fillWidth: true
                        height: 36
                        color: "#f0f0f0"
                        Label {
                            anchors.centerIn: parent
                            text: "软件类别"
                            font.bold: true
                            font.pointSize: 11
                        }
                    }

                    // ★ 用 OldControls.TreeView（Qt 5.12 里只有 Controls 1 有 TreeView）
                    OldControls.TreeView {
                        id: treeView
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        model: treeModel
                        headerVisible: false
                        frameVisible: false
                        backgroundVisible: false

                        OldControls.TableViewColumn {
                            role: "display"
                            title: "类别"
                            width: treeView.width
                            resizable: false
                            movable: false
                        }

                        onCurrentIndexChanged: {
                            if (!currentIndex || !currentIndex.valid)
                                return;
                            // 257 = Qt::UserRole + 1 = TreeModel::IdRole
                            var catId = treeModel.data(currentIndex, 257);
                            if (catId)
                                itemModel.filterByCategory(catId);
                        }

                        itemDelegate: Rectangle {
                            color: styleData.selected
                                   ? "#d0e8ff"
                                   : (hoverTree.containsMouse ? "#eaf3ff" : "transparent")
                            implicitHeight: 28

                            MouseArea {
                                id: hoverTree
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.NoButton   // 只做悬停
                            }

                            Row {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left
                                anchors.leftMargin: 8 + styleData.depth * 16
                                spacing: 4

                                Label {
                                    text: styleData.hasChildren
                                          ? (styleData.isExpanded ? "▼" : "▶")
                                          : " "
                                    color: "#666"
                                    font.pointSize: 9
                                    width: 12
                                }

                                Label {
                                    text: styleData.value
                                    font.pointSize: 10
                                    font.bold: styleData.hasChildren
                                    color: styleData.selected ? "#1a4a80" : "#222"
                                }
                            }
                        }
                    }
                }
            }

            // 分隔线
            Rectangle {
                Layout.fillHeight: true
                width: 1
                color: "#ddd"
            }

            // ===== 右侧：可安装程序列表 =====
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#ffffff"

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    Rectangle {
                        Layout.fillWidth: true
                        height: 36
                        color: "#f0f0f0"
                        Label {
                            anchors.centerIn: parent
                            text: "可安装程序列表"
                            font.bold: true
                            font.pointSize: 11
                        }
                    }

                    ListView {
                        id: itemListView
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: itemModel
                        spacing: 4
                        topMargin: 8
                        bottomMargin: 8

                        delegate: Rectangle {
                            width: itemListView.width - 16
                            height: 80
                            x: 8
                            radius: 6
                            border.color: hoverItem.containsMouse ? "#4a90d9" : "#ddd"
                            border.width: hoverItem.containsMouse ? 2 : 1
                            color: hoverItem.containsMouse ? "#f5faff" : "#ffffff"

                            MouseArea {
                                id: hoverItem
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: detailPopup.showItem(model)
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 12

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 4

                                    Label {
                                        text: model.name
                                        font.bold: true
                                        font.pointSize: 12
                                    }
                                    Label {
                                        text: "版本: " + (model.version || "-")
                                              + "  |  发布者: " + (model.publisher || "-")
                                        font.pointSize: 9
                                        color: "#666"
                                    }
                                    Label {
                                        text: model.description || ""
                                        font.pointSize: 9
                                        color: "#888"
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                }

                                // 安装按钮（不用 highlighted，避免属性识别问题）
                                Button {
                                    text: {
                                        if (model.installStatus === "installing")
                                            return "安装中...";
                                        if (model.installStatus === "done")
                                            return "已安装";
                                        if (model.installStatus === "failed")
                                            return "重试";
                                        return "安装";
                                    }
                                    enabled: model.installStatus !== "installing"

                                    onClicked: {
                                        itemModel.setStatus(model.itemId, "installing");
                                        installManager.installItem(model.itemId);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // ---------- 底部状态栏 ----------
        Rectangle {
            Layout.fillWidth: true
            height: 30
            color: "#f8f8f8"

            Rectangle {
                anchors.top: parent.top
                width: parent.width
                height: 1
                color: "#e0e0e0"
            }

            Label {
                id: statusLabel
                anchors.centerIn: parent
                text: "就绪"
                font.pointSize: 9
            }
        }
    }

    // =========================================================
    // 详细信息弹窗
    // =========================================================
    Popup {
        id: detailPopup
        width: 420
        height: contentHeight + 60
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        property var currentItem: null

        function showItem(item) {
            currentItem = item;
            detailText.text =
                "名称: "   + (item.name || "-") + "\n" +
                "版本: "   + (item.version || "-") + "\n" +
                "发布者: " + (item.publisher || "-") + "\n" +
                "类型: "   + (item.type || "-") + "\n\n" +
                "描述:\n"  + (item.description || "-");
            open();
        }

        contentItem: ColumnLayout {
            spacing: 8
            Label { text: "详细信息"; font.bold: true; font.pointSize: 12 }
            Label {
                id: detailText
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                font.pointSize: 10
            }
        }
    }

    // =========================================================
    // 状态复位定时器
    // =========================================================
    Timer {
        id: statusTimer
        interval: 5000
        onTriggered: {
            statusLabel.text = "就绪";
            statusLabel.color = "black";
        }
    }

    // =========================================================
    // 安装流程信号绑定（★ Qt 5.12 语法：不要写 function 关键字）
    // =========================================================
    Connections {
        target: installManager

        onInstallProgress: {
            statusLabel.text = "[" + percent + "%] " + stage;
            statusLabel.color = "#333";
        }

        onInstallFinished: {
            statusLabel.text = message;
            statusLabel.color = success ? "#2b7a2b" : "#c00";
            itemModel.setStatus(itemId, success ? "done" : "failed");
            statusTimer.start();
        }
    }
}
