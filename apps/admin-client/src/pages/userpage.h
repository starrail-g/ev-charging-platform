#ifndef USERPAGE_H
#define USERPAGE_H

#include <QWidget>

#include <memory>

#include "data/adminrepository.h"
#include "data/mockdataset.h"

class QLabel;
class QPushButton;
class QTableWidget;

// 用户管理页（统一 UI Task 6）：用户列表
//（手机号 / 昵称 / 余额 / 状态 / 注册时间 UTC）。
// C-S1-007：冻结/解冻（第一阶段为服务端确认后的状态模拟）——
// "冻结/解冻选中用户"按钮经 AdminRepository::setUserStatus 异步动作，
// 结果在提示行可观察；页面与数据来源标识（Mock 演示）明确区分模拟与真实，
// 不把 Mock 描述为已接入真实服务端（9/7 接口闸门后由 Socket 适配层替换）。
// 演示数据可显示完整号码，真实数据接入后按权限脱敏。
class UserPage : public QWidget
{
    Q_OBJECT

public:
    explicit UserPage(ev::AdminRepository *repository = nullptr,
                      QWidget *parent = nullptr);

    // 经 Repository 异步加载用户列表；mode 仅第一阶段驱动 Mock 演示层
    void refresh(ev::mockdata::DataMode mode = ev::mockdata::DataMode::Normal);

    int visibleRowCount() const;

private:
    void rebuildRows();
    void showHint(const QString &text);
    void clearHint();
    // 选中行变化 → 状态按钮文案/可用态（active→冻结 / frozen→解冻）
    void onUserSelectionChanged(int currentRow);
    // "冻结/解冻选中用户"：经 Repository 异步动作，成功后提示 + 重新拉取
    void onStatusButtonClicked();

    ev::AdminRepository *m_repository = nullptr;
    std::unique_ptr<ev::AdminRepository> m_ownedRepository;
    int m_loadGeneration = 0;

    QList<ev::UserInfo> m_users;
    bool m_ok = false;
    QString m_error;

    QPushButton *m_statusButton = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_hintLabel = nullptr;
    QString m_actionPendingHint; // 动作成功提示：下一次 rebuildRows 完成后展示一次
};

#endif // USERPAGE_H
