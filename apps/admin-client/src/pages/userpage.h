#ifndef USERPAGE_H
#define USERPAGE_H

#include <QWidget>

#include <memory>

#include "data/adminrepository.h"
#include "data/mockdataset.h"

class QLabel;
class QTableWidget;

// 用户管理页（统一 UI Task 6）：用户列表
//（手机号 / 昵称 / 余额 / 状态）。演示数据可显示完整号码，
// 真实数据接入后按权限脱敏；冻结/解冻等服务端操作未接入前不提供假按钮，
// 只在页脚注明（不得把 Mock 描述为已接入真实服务端）。
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

    ev::AdminRepository *m_repository = nullptr;
    std::unique_ptr<ev::AdminRepository> m_ownedRepository;
    int m_loadGeneration = 0;

    QList<ev::UserInfo> m_users;
    bool m_ok = false;
    QString m_error;

    QTableWidget *m_table = nullptr;
    QLabel *m_hintLabel = nullptr;
};

#endif // USERPAGE_H
