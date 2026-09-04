#ifndef STATESTACK_H
#define STATESTACK_H

#include <QStackedWidget>

#include <functional>

class QLabel;
class QPushButton;

// 四态展示组件（C-S1-008：加载中/空数据/接口失败/正常内容）。
// 页面将真实内容放入 setContentWidget 后，由数据层决定展示哪一态：
//   Loading —— 加载中转圈提示
//   Empty   —— 空态文案
//   Error   —— 错误文案 + 重试按钮（可注入 handler）
//   Content —— 正常内容
class StateStack : public QStackedWidget
{
    Q_OBJECT

public:
    enum class State { Loading, Empty, Error, Content };

    explicit StateStack(QWidget *parent = nullptr);

    void showState(State state, const QString &text = QString());
    void setContentWidget(QWidget *content);
    void setRetryHandler(std::function<void()> handler);

    State currentState() const { return m_state; }

private:
    State m_state = State::Loading;
    QWidget *m_loadingPage = nullptr;
    QWidget *m_emptyPage = nullptr;
    QWidget *m_errorPage = nullptr;
    QWidget *m_contentPage = nullptr;
    QLabel *m_loadingLabel = nullptr;
    QLabel *m_emptyLabel = nullptr;
    QLabel *m_errorLabel = nullptr;
    QPushButton *m_retryButton = nullptr;
    std::function<void()> m_retryHandler;
};

#endif // STATESTACK_H
