#include "client_service.h"
#include "socket_user_service.h"

#include <QApplication>
#include <QColor>
#include <cmath>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <QFileDialog>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QPixmap>
#include <QRegularExpression>
#include <QtMath>
#include <QStackedWidget>
#include <QVBoxLayout>

using namespace ev;

class UserWindow final : public QMainWindow {
  Q_OBJECT
public:
  ~UserWindow() { for (auto *watcher : activeWatchers_) watcher->waitForFinished(); }

  UserWindow() {
    setWindowTitle(QStringLiteral("充电用户端"));
    setFixedSize(420, 760);
    if (qEnvironmentVariable("EV_USER_CLIENT_TRANSPORT").compare(QStringLiteral("socket"), Qt::CaseInsensitive) == 0) service_ = &socketService_;
    setStyleSheet(QStringLiteral(
        "QMainWindow{background:#081225;color:#e8f1ff;}"
        "QWidget{color:#e8f1ff;font-size:13px;}"
        "QLineEdit,QComboBox,QDoubleSpinBox{background:#101f3b;border:1px solid #12d7f5;border-radius:8px;padding:8px;color:#f5f8ff;}"
        "QPushButton{background:#172b4d;border:1px solid #2c4772;border-radius:8px;padding:8px;color:#e8f1ff;}"
        "QPushButton:hover{background:#1e4470;} QPushButton:disabled{color:#70809b;background:#10192d;}"
        "QComboBox{background:#101f3b;color:#ffffff;border:1px solid #12d7f5;border-radius:8px;padding:8px;} QComboBox::drop-down{border:0;width:26px;} QComboBox QAbstractItemView{background:#101f3b;color:#ffffff;border:1px solid #12d7f5;border-radius:8px;padding:4px;outline:0;} QComboBox QAbstractItemView::item{background:#101f3b;color:#ffffff;padding:6px 8px;border-radius:5px;} QComboBox QAbstractItemView::item:hover,QComboBox QAbstractItemView::item:selected{background:#ffffff;color:#101010;}"
        "QListWidget{background:#0c1830;border:0;padding:4px;} QListWidget::item{border-radius:12px;padding:8px;}"
        "QListWidget#stationCards,QListWidget#pileCards{background:#0b172b;border:0;padding:4px;}"
        "QListWidget#stationCards::item,QListWidget#pileCards::item{background:#10243d;border:1px solid #8bdcff;border-radius:12px;padding:10px;color:#ffffff;}"
        "QListWidget#stationCards::item:selected,QListWidget#pileCards::item:selected{background:#173a5a;border:1px solid #c2f0ff;color:#ffffff;}"
        "QLabel#title{font-size:20px;font-weight:700;} QLabel#muted{color:#8ea6c9;}"));
    stack_ = new QStackedWidget(this);
    setCentralWidget(stack_);
    buildLogin();
    buildRegister();
    buildHome();
    buildStationDetail();
    buildMap();
    buildOrder();
    buildProfile();
    showLogin();
  }

private:
  MockUserService mockService_;
  SocketUserService socketService_;
  IUserService *service_{&mockService_};
  SessionManager session_;
  QStackedWidget *stack_{};
  QWidget *login_{}, *registerPage_{}, *home_{}, *detail_{}, *map_{}, *orderPage_{}, *profile_{};
  QStackedWidget *registerSteps_{};
  QLineEdit *phone_{}, *password_{}, *regPhone_{}, *regPassword_{}, *regConfirmPassword_{}, *regName_{}, *query_{}, *address_{}, *fromLocation_{}, *nickname_{};
  QComboBox *region_{}, *routeMode_{};
  QDoubleSpinBox *rechargeAmount_{};
  QLabel *loginStatus_{}, *registerStatus_{}, *homeStatus_{}, *locationStatus_{}, *orderSummary_{}, *detailTitle_{}, *pileStatus_{}, *mapStatus_{}, *orderStatus_{}, *historySummary_{}, *profileLabel_{}, *avatarLabel_{};
  QWidget *confirmationControls_{};
  QListWidget *stationList_{}, *pileList_{}, *mapStationList_{}, *historyList_{};
  QPushButton *loginButton_{}, *registerButton_{}, *registerNextButton_{}, *registerFinishButton_{}, *confirmOrderButton_{}, *reserveButton_{}, *returnPileButton_{}, *cancelReservationButton_{}, *directStartButton_{}, *startButton_{}, *stopButton_{}, *settleButton_{}, *rechargeButton_{};
  Station selectedStation_{};
  Pile selectedPile_{};
  Order order_{};
  bool orderConfirmationMode_{false};
  QVector<Station> stationCache_;
  QVector<Pile> pileCache_;
  QList<QFutureWatcherBase *> activeWatchers_;

  QWidget *passwordRow(QLineEdit *&edit) {
    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    edit = new QLineEdit(row);
    edit->setEchoMode(QLineEdit::Password);
    auto *eye = new QPushButton(QStringLiteral("👁"), row);
    eye->setCheckable(true);
    eye->setToolTip(QStringLiteral("显示/隐藏密码"));
    eye->setFixedWidth(42);
    connect(eye, &QPushButton::toggled, row, [edit](bool visible) {
      edit->setEchoMode(visible ? QLineEdit::Normal : QLineEdit::Password);
    });
    layout->addWidget(edit);
    layout->addWidget(eye);
    return row;
  }

  void addBottomNav(QVBoxLayout *layout, QWidget *parent) {
    auto *bar = new QHBoxLayout;
    auto *homeButton = new QPushButton(QStringLiteral("⌂\n首页"), parent);
    auto *chargeButton = new QPushButton(QStringLiteral("⚡\n充电"), parent);
    auto *mineButton = new QPushButton(QStringLiteral("♙\n我的"), parent);
    bar->addWidget(homeButton);
    bar->addWidget(chargeButton);
    bar->addWidget(mineButton);
    layout->addLayout(bar);
    connect(homeButton, &QPushButton::clicked, this, &UserWindow::showHome);
    connect(chargeButton, &QPushButton::clicked, this, &UserWindow::showOrderPage);
    connect(mineButton, &QPushButton::clicked, this, &UserWindow::showProfile);
  }

  QPushButton *nav(const QString &text, QWidget *parent, void (UserWindow::*slot)()) {
    auto *button = new QPushButton(text, parent);
    connect(button, &QPushButton::clicked, this, slot);
    return button;
  }

  template <typename T, typename Fn, typename Done>
  void runService(Fn fn, Done done) {
    if (service_ == &mockService_) { done(fn()); return; }
    auto *watcher = new QFutureWatcher<Result<T>>(this);
    activeWatchers_.push_back(watcher);
    connect(watcher, &QFutureWatcher<Result<T>>::finished, this, [this, watcher, done]() mutable {
      const auto result = watcher->result();
      activeWatchers_.removeOne(watcher);
      watcher->deleteLater();
      done(result);
    });
    watcher->setFuture(QtConcurrent::run(fn));
  }

  void buildLogin() {
    login_ = new QWidget;
    auto *layout = new QVBoxLayout(login_);
    layout->setContentsMargins(24, 28, 24, 18);
    auto *title = new QLabel(QStringLiteral("⚡\n充电用户端"), login_);
    title->setObjectName(QStringLiteral("title"));
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);
    auto *subtitle = new QLabel(QStringLiteral("手机号免密登录 · 首次登录自动注册"), login_);
    subtitle->setObjectName(QStringLiteral("muted"));
    subtitle->setAlignment(Qt::AlignCenter);
    layout->addWidget(subtitle);
    auto *form = new QFormLayout;
    phone_ = new QLineEdit;
    phone_->setPlaceholderText(QStringLiteral("请输入 11 位手机号"));
    form->addRow(QStringLiteral("手机号"), phone_);
    layout->addLayout(form);
    loginButton_ = new QPushButton(QStringLiteral("登录"), login_);
    loginButton_->setMinimumHeight(42);
    layout->addWidget(loginButton_);
    registerButton_ = new QPushButton(QStringLiteral("注册新账号"), login_);
    layout->addWidget(registerButton_);
    loginStatus_ = new QLabel(login_);
    loginStatus_->setWordWrap(true);
    layout->addWidget(loginStatus_);
    layout->addStretch();
    layout->addWidget(new QLabel(QStringLiteral("演示：13800000000；新手机号会自动创建用户"), login_));
    connect(loginButton_, &QPushButton::clicked, this, &UserWindow::login);
    connect(registerButton_, &QPushButton::clicked, this, &UserWindow::showRegister);
    stack_->addWidget(login_);
  }

  void buildRegister() {
    registerPage_ = new QWidget;
    auto *layout = new QVBoxLayout(registerPage_);
    layout->setContentsMargins(24, 28, 24, 18);
    layout->addWidget(new QLabel(QStringLiteral("注册 / 完善账号"), registerPage_));
    registerSteps_ = new QStackedWidget(registerPage_);

    auto *credentials = new QWidget;
    auto *credentialForm = new QFormLayout(credentials);
    regPhone_ = new QLineEdit;
    regPhone_->setPlaceholderText(QStringLiteral("请输入 11 位手机号"));
    credentialForm->addRow(QStringLiteral("手机号"), regPhone_);
    credentialForm->addRow(QStringLiteral("密码"), passwordRow(regPassword_));
    credentialForm->addRow(QStringLiteral("确认密码"), passwordRow(regConfirmPassword_));
    registerNextButton_ = new QPushButton(QStringLiteral("下一步"), credentials);
    credentialForm->addRow(registerNextButton_);

    auto *profile = new QWidget;
    auto *profileLayout = new QFormLayout(profile);
    regName_ = new QLineEdit;
    regName_->setPlaceholderText(QStringLiteral("请输入昵称"));
    profileLayout->addRow(QStringLiteral("昵称"), regName_);
    registerFinishButton_ = new QPushButton(QStringLiteral("注册并登录"), profile);
    auto *back = new QPushButton(QStringLiteral("返回修改"), profile);
    profileLayout->addRow(registerFinishButton_);
    profileLayout->addRow(back);

    registerSteps_->addWidget(credentials);
    registerSteps_->addWidget(profile);
    layout->addWidget(registerSteps_);
    registerStatus_ = new QLabel(registerPage_);
    registerStatus_->setWordWrap(true);
    layout->addWidget(registerStatus_);
    layout->addStretch();
    layout->addWidget(nav(QStringLiteral("返回登录"), registerPage_, &UserWindow::showLogin));
    connect(registerNextButton_, &QPushButton::clicked, this, &UserWindow::registerNextStep);
    connect(registerFinishButton_, &QPushButton::clicked, this, &UserWindow::registerUser);
    connect(back, &QPushButton::clicked, this, [this] {
      registerSteps_->setCurrentIndex(0);
      registerStatus_->clear();
    });
    stack_->addWidget(registerPage_);
  }

  void buildHome() {
    home_ = new QWidget;
    auto *layout = new QVBoxLayout(home_);
    layout->setContentsMargins(14, 14, 14, 8);
    auto *top = new QHBoxLayout;
    auto *title = new QLabel(QStringLiteral("首页"), home_);
    title->setObjectName(QStringLiteral("title"));
    top->addWidget(title);
    top->addStretch();
    layout->addLayout(top);

    orderSummary_ = new QLabel(QStringLiteral("当前订单：暂无活动订单"), home_);
    orderSummary_->setStyleSheet(QStringLiteral("color:#12d7f5;font-weight:600;"));
    layout->addWidget(orderSummary_);

    auto *locationCard = new QVBoxLayout;
    locationStatus_ = new QLabel(QStringLiteral("⌖ 当前位置：深圳市"), home_);
    locationCard->addWidget(locationStatus_);
    auto *locationRow = new QHBoxLayout;
    region_ = new QComboBox(home_);
    region_->addItems({QStringLiteral("深圳市"), QStringLiteral("南山区"), QStringLiteral("福田区"), QStringLiteral("宝安区")});
    address_ = new QLineEdit(home_);
    address_->setPlaceholderText(QStringLiteral("手动输入地址重新定位"));
    auto *locate = new QPushButton(QStringLiteral("定位"), home_);
    locationRow->addWidget(region_);
    locationRow->addWidget(address_);
    locationRow->addWidget(locate);
    locationCard->addLayout(locationRow);
    layout->addLayout(locationCard);

    query_ = new QLineEdit(home_);
    query_->setPlaceholderText(QStringLiteral("搜索站点名称或地址"));
    auto *search = new QPushButton(QStringLiteral("查询"), home_);
    auto *searchRow = new QHBoxLayout;
    searchRow->addWidget(query_);
    searchRow->addWidget(search);
    layout->addLayout(searchRow);

    stationList_ = new QListWidget(home_);
    stationList_->setObjectName(QStringLiteral("stationCards"));
    stationList_->setSpacing(7);
    stationList_->setAlternatingRowColors(false);
    layout->addWidget(stationList_);

    homeStatus_ = new QLabel(home_);
    homeStatus_->setObjectName(QStringLiteral("muted"));
    homeStatus_->setWordWrap(true);
    layout->addWidget(homeStatus_);
    addBottomNav(layout, home_);
    connect(locate, &QPushButton::clicked, this, [this] {
      const QString manual = address_->text().trimmed();
      locationStatus_->setText(manual.isEmpty() ? QStringLiteral("⌖ 当前位置：%1").arg(region_->currentText())
                                                 : QStringLiteral("⌖ 当前位置：%1 · %2").arg(region_->currentText(), manual));
      if (!manual.isEmpty()) {
        query_->setText(manual);
      }
      searchStations();
    });
    connect(search, &QPushButton::clicked, this, &UserWindow::searchStations);
    connect(stationList_, &QListWidget::itemClicked, this, &UserWindow::openStation);
    stack_->addWidget(home_);
  }

  void buildStationDetail() {
    detail_ = new QWidget;
    auto *layout = new QVBoxLayout(detail_);
    layout->setContentsMargins(14, 14, 14, 8);
    detailTitle_ = new QLabel(detail_);
    detailTitle_->setObjectName(QStringLiteral("title"));
    detailTitle_->setWordWrap(true);
    layout->addWidget(detailTitle_);
    pileList_ = new QListWidget(detail_);
    pileList_->setObjectName(QStringLiteral("pileCards"));
    pileList_->setSpacing(5);
    layout->addWidget(pileList_);
    pileStatus_ = new QLabel(detail_);
    pileStatus_->setWordWrap(true);
    layout->addWidget(pileStatus_);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(nav(QStringLiteral("Mock 导航"), detail_, &UserWindow::showMap));
    buttons->addWidget(nav(QStringLiteral("返回首页"), detail_, &UserWindow::showHome));
    layout->addLayout(buttons);
    addBottomNav(layout, detail_);
    connect(pileList_, &QListWidget::itemClicked, this, &UserWindow::selectPile);
    stack_->addWidget(detail_);
  }

  void buildMap() {
    map_ = new QWidget;
    auto *layout = new QVBoxLayout(map_);
    layout->setContentsMargins(14, 14, 14, 8);
    layout->addWidget(new QLabel(QStringLiteral("一键导航 · Mock 接口"), map_));
    fromLocation_ = new QLineEdit(map_);
    fromLocation_->setPlaceholderText(QStringLiteral("起点：纬度,经度"));
    fromLocation_->setText(QStringLiteral("22.530,113.930"));
    layout->addWidget(fromLocation_);
    mapStationList_ = new QListWidget(map_);
    mapStationList_->setToolTip(QStringLiteral("点击站点标记选择终点"));
    layout->addWidget(mapStationList_);
    mapStatus_ = new QLabel(map_);
    mapStatus_->setWordWrap(true);
    layout->addWidget(mapStatus_);
    routeMode_ = new QComboBox(map_);
    routeMode_->addItems({QStringLiteral("驾车（Mock）"), QStringLiteral("步行（Mock）")});
    layout->addWidget(routeMode_);
    auto *route = new QPushButton(QStringLiteral("查询路线"), map_);
    layout->addWidget(route);
    layout->addWidget(new QLabel(QStringLiteral("真实腾讯地图 Web API / QWebEngineView：暂未接入"), map_));
    layout->addWidget(nav(QStringLiteral("返回首页"), map_, &UserWindow::showHome));
    addBottomNav(layout, map_);
    connect(mapStationList_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
      for (const auto &station : stationCache_) {
        if (station.id == item->data(Qt::UserRole).toString()) {
          selectedStation_ = station;
          mapStatus_->setText(QStringLiteral("目标站点：%1（%2, %3）").arg(station.name).arg(station.latitude).arg(station.longitude));
          break;
        }
      }
    });
    connect(route, &QPushButton::clicked, this, &UserWindow::queryRoute);
    stack_->addWidget(map_);
  }

  void buildOrder() {
    orderPage_ = new QWidget;
    auto *layout = new QVBoxLayout(orderPage_);
    layout->setContentsMargins(14, 14, 14, 8);
    auto *title = new QLabel(QStringLiteral("当前充电状态"), orderPage_);
    title->setObjectName(QStringLiteral("title"));
    layout->addWidget(title);
    orderStatus_ = new QLabel(orderPage_);
    orderStatus_->setWordWrap(true);
    layout->addWidget(orderStatus_);

    confirmationControls_ = new QWidget(orderPage_);
    auto *confirmRow = new QHBoxLayout(confirmationControls_);
    confirmRow->setContentsMargins(0, 0, 0, 0);
    confirmOrderButton_ = new QPushButton(QStringLiteral("确认创建订单"), orderPage_);
    reserveButton_ = new QPushButton(QStringLiteral("预约该充电桩"), orderPage_);
    confirmRow->addWidget(confirmOrderButton_);
    confirmRow->addWidget(reserveButton_);
    layout->addWidget(confirmationControls_);
    startButton_ = new QPushButton(QStringLiteral("开始充电"), orderPage_);
    directStartButton_ = new QPushButton(QStringLiteral("直接开始充电"), orderPage_);
    stopButton_ = new QPushButton(QStringLiteral("停止充电"), orderPage_);
    settleButton_ = new QPushButton(QStringLiteral("结算"), orderPage_);
    returnPileButton_ = new QPushButton(QStringLiteral("返回充电桩"), orderPage_);
    cancelReservationButton_ = new QPushButton(QStringLiteral("取消预约"), orderPage_);
    layout->addWidget(startButton_);
    layout->addWidget(directStartButton_);
    layout->addWidget(stopButton_);
    layout->addWidget(settleButton_);
    layout->addWidget(returnPileButton_);
    layout->addWidget(cancelReservationButton_);
    layout->addWidget(new QLabel(QStringLiteral("历史充电记录"), orderPage_));
    historySummary_ = new QLabel(orderPage_);
    historySummary_->setWordWrap(true);
    historySummary_->setStyleSheet(QStringLiteral("background:#10203a;border-radius:10px;padding:8px;color:#d9e8ff;"));
    layout->addWidget(historySummary_);
    historyList_ = new QListWidget(orderPage_);
    historyList_->setMaximumHeight(150);
    layout->addWidget(historyList_);
    layout->addStretch();
    confirmationControls_->setVisible(false);
    returnPileButton_->setVisible(false);
    cancelReservationButton_->setVisible(false);
    directStartButton_->setVisible(false);
    addBottomNav(layout, orderPage_);
    connect(confirmOrderButton_, &QPushButton::clicked, this, &UserWindow::confirmOrder);
    connect(reserveButton_, &QPushButton::clicked, this, &UserWindow::reservePile);
    connect(returnPileButton_, &QPushButton::clicked, this, [this] { showStationDetail(); });
    connect(cancelReservationButton_, &QPushButton::clicked, this, &UserWindow::cancelReservation);
    connect(startButton_, &QPushButton::clicked, this, &UserWindow::startCharging);
    connect(directStartButton_, &QPushButton::clicked, this, &UserWindow::startChargingDirect);
    connect(stopButton_, &QPushButton::clicked, this, &UserWindow::stopCharging);
    connect(settleButton_, &QPushButton::clicked, this, &UserWindow::settle);
    stack_->addWidget(orderPage_);
  }

  void buildProfile() {
    profile_ = new QWidget;
    auto *layout = new QVBoxLayout(profile_);
    layout->setContentsMargins(14, 14, 14, 8);
    auto *title = new QLabel(QStringLiteral("我的"), profile_);
    title->setObjectName(QStringLiteral("title"));
    layout->addWidget(title);
    auto *accountCard = new QHBoxLayout;
    avatarLabel_ = new QLabel(QStringLiteral("用"), profile_);
    avatarLabel_->setFixedSize(84, 84);
    avatarLabel_->setAlignment(Qt::AlignCenter);
    avatarLabel_->setStyleSheet(QStringLiteral("background:#12d7f5;color:#081225;border-radius:10px;font-size:22px;font-weight:700;"));
    accountCard->addWidget(avatarLabel_);
    profileLabel_ = new QLabel(profile_);
    profileLabel_->setWordWrap(true);
    accountCard->addWidget(profileLabel_);
    layout->addLayout(accountCard);
    nickname_ = new QLineEdit(profile_);
    nickname_->setPlaceholderText(QStringLiteral("修改昵称"));
    auto *save = new QPushButton(QStringLiteral("保存昵称"), profile_);
    auto *avatar = new QPushButton(QStringLiteral("选择头像"), profile_);
    auto *profileButtons = new QHBoxLayout;
    profileButtons->addWidget(nickname_);
    profileButtons->addWidget(save);
    profileButtons->addWidget(avatar);
    layout->addLayout(profileButtons);
    auto *wallet = new QHBoxLayout;
    rechargeAmount_ = new QDoubleSpinBox(profile_);
    rechargeAmount_->setRange(1.0, 10000.0);
    rechargeAmount_->setDecimals(2);
    rechargeAmount_->setPrefix(QStringLiteral("¥ "));
    rechargeButton_ = new QPushButton(QStringLiteral("充值（Mock）"), profile_);
    auto *recharge = rechargeButton_;
    wallet->addWidget(rechargeAmount_);
    wallet->addWidget(recharge);
    layout->addLayout(wallet);
    layout->addWidget(new QLabel(QStringLiteral("余额和账号信息均为 Mock 数据，后续由服务端适配层替换"), profile_));
    layout->addStretch();
    layout->addWidget(nav(QStringLiteral("退出登录"), profile_, &UserWindow::logout));
    addBottomNav(layout, profile_);
    connect(save, &QPushButton::clicked, this, &UserWindow::saveProfile);
    connect(avatar, &QPushButton::clicked, this, &UserWindow::chooseAvatar);
    connect(recharge, &QPushButton::clicked, this, &UserWindow::recharge);
    stack_->addWidget(profile_);
  }

  void showLogin() {
    stack_->setCurrentWidget(login_);
  }

  void showRegister() {
    regPhone_->clear();
    regPassword_->clear();
    regConfirmPassword_->clear();
    regName_->clear();
    registerStatus_->clear();
    registerSteps_->setCurrentIndex(0);
    stack_->setCurrentWidget(registerPage_);
  }

  void showHome() {
    if (!session_.isLoggedIn()) {
      showLogin();
      return;
    }
    orderConfirmationMode_ = false;
    if (confirmationControls_) confirmationControls_->setVisible(false);
    if (returnPileButton_) returnPileButton_->setVisible(false);
    if (cancelReservationButton_) cancelReservationButton_->setVisible(false);
    if (directStartButton_) directStartButton_->setVisible(false);
    stack_->setCurrentWidget(home_);
    searchStations();
    refreshCurrentOrder();
  }

  void showStationDetail() {
    if (!session_.isLoggedIn()) {
      showLogin();
      return;
    }
    if (selectedStation_.id.isEmpty()) {
      showHome();
      return;
    }
    orderConfirmationMode_ = false;
    if (confirmationControls_) confirmationControls_->setVisible(false);
    if (returnPileButton_) returnPileButton_->setVisible(false);
    if (cancelReservationButton_) cancelReservationButton_->setVisible(false);
    if (directStartButton_) directStartButton_->setVisible(false);
    openStationForSelected();
  }

  void openStationForSelected() {
    if (selectedStation_.id.isEmpty()) { showHome(); return; }
    detailTitle_->setText(QStringLiteral("%1\n%2\n坐标：%3, %4").arg(selectedStation_.name).arg(selectedStation_.address).arg(selectedStation_.latitude).arg(selectedStation_.longitude));
    pileList_->clear();
    pileStatus_->setText(QStringLiteral("正在加载充电桩…"));
    const QString stationId = selectedStation_.id;
    runService<QVector<Pile>>([this, stationId] { return service_->piles(stationId); }, [this](const Result<QVector<Pile>> &result) {
      if (!result.ok) { pileStatus_->setText(result.error); stack_->setCurrentWidget(detail_); return; }
      pileCache_ = result.value;
      if (result.value.isEmpty()) { pileStatus_->setText(QStringLiteral("该站点暂无充电桩")); stack_->setCurrentWidget(detail_); return; }
      int index = 1;
      for (const auto &pile : result.value) {
        auto *pileItem = new QListWidgetItem(QStringLiteral("电桩 %1 · %2 · %3 · %4 · %5 kW\n计费 ¥ %6/度").arg(index++).arg(pile.number).arg(pile.type).arg(pileStatusText(pile.status)).arg(pile.powerKw).arg(pile.priceCentsPerKwh / 100.0), pileList_);
        pileItem->setBackground(QColor("#10243d")); pileItem->setForeground(QColor("#ffffff"));
        pileItem->setData(Qt::UserRole, pile.id);
      }
      pileStatus_->setText(QStringLiteral("点击充电桩查看状态；闲置桩可进入订单确认"));
      stack_->setCurrentWidget(detail_);
    });
    stack_->setCurrentWidget(detail_);
  }
  void showOrderPage() {
    if (!session_.isLoggedIn()) {
      showLogin();
      return;
    }
    orderConfirmationMode_ = false;
    if (confirmationControls_) confirmationControls_->setVisible(false);
    if (returnPileButton_) returnPileButton_->setVisible(false);
    if (directStartButton_) directStartButton_->setVisible(false);
    refreshCurrentOrder();
    refreshOrderHistory();
    stack_->setCurrentWidget(orderPage_);
  }

  void showMap() {
    if (!session_.isLoggedIn()) { showLogin(); return; }
    stack_->setCurrentWidget(map_);
    mapStationList_->clear();
    mapStatus_->setText(QStringLiteral("正在加载站点…"));
    runService<QVector<Station>>([this] { return service_->stations(QString()); }, [this](const Result<QVector<Station>> &result) {
      if (!result.ok) { mapStatus_->setText(result.error); return; }
      stationCache_ = result.value;
      for (const auto &station : result.value) {
        auto *item = new QListWidgetItem(QStringLiteral("站点：%1 · (%2,%3) · %4").arg(station.name).arg(station.latitude).arg(station.longitude).arg(station.open ? QStringLiteral("营业中") : QStringLiteral("暂停营业")), mapStationList_);
        item->setData(Qt::UserRole, station.id);
      }
      mapStatus_->setText(selectedStation_.id.isEmpty() ? QStringLiteral("请选择目标站点；当前为 Mock/离线路线。") : QStringLiteral("当前目标：%1").arg(selectedStation_.name));
    });
  }
  void showProfile() {
    if (!session_.isLoggedIn()) {
      showLogin();
      return;
    }
    stack_->setCurrentWidget(profile_);
    profileLabel_->setText(QStringLiteral("手机号：%1\n余额：¥ %2")
                               .arg(session_.user().phone)
                               .arg(session_.user().walletBalanceCents / 100.0, 0, 'f', 2));
    nickname_->setText(session_.user().displayName);
    if (!session_.user().avatarPath.isEmpty()) {
      avatarLabel_->setPixmap(QPixmap(session_.user().avatarPath).scaled(84, 84, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
      avatarLabel_->setText({});
    } else {
      avatarLabel_->setPixmap({});
      avatarLabel_->setText(QStringLiteral("用"));
    }
  }

  void logout() {
    session_.clear();
    order_ = Order{};
    selectedStation_ = Station{};
    selectedPile_ = Pile{};
    orderConfirmationMode_ = false;
    orderSummary_->setText(QStringLiteral("当前订单：未登录"));
    showLogin();
  }

  void login() {
    const QString phone = phone_->text().trimmed();
    loginButton_->setEnabled(false);
    loginStatus_->setText(QStringLiteral("正在登录…"));
    runService<User>([this, phone] { return service_->login(phone); }, [this](const Result<User> &result) {
      loginButton_->setEnabled(true);
      if (!result.ok) { loginStatus_->setText(result.error); return; }
      session_.setUser(result.value);
      showHome();
      if (result.value.status == UserStatus::Frozen) homeStatus_->setText(QStringLiteral("账号已冻结：可查看资料和订单并完成收尾，预约、开始充电和充值不可用。"));
      else loginStatus_->clear();
    });
  }

  void registerNextStep() {
    const QString phone = regPhone_->text().trimmed();
    const QString password = regPassword_->text();
    const QString confirmation = regConfirmPassword_->text();
    if (phone.isEmpty()) {
      registerStatus_->setText(QStringLiteral("请输入手机号"));
    } else if (!isValidPhone(phone)) {
      registerStatus_->setText(QStringLiteral("手机号格式错误"));
    } else if (password.isEmpty()) {
      registerStatus_->setText(QStringLiteral("请输入密码"));
    } else if (password.size() < 6) {
      registerStatus_->setText(QStringLiteral("密码长度不能少于 6 位"));
    } else if (confirmation.isEmpty()) {
      registerStatus_->setText(QStringLiteral("请输入确认密码"));
    } else if (password != confirmation) {
      registerStatus_->setText(QStringLiteral("密码和确认密码必须相同"));
    } else {
      registerStatus_->clear();
      registerSteps_->setCurrentIndex(1);
      regName_->setFocus();
    }
  }

  void registerUser() {
    auto result = service_->registerUser(regPhone_->text().trimmed(), regPassword_->text(), regName_->text());
    if (!result.ok) {
      registerStatus_->setText(result.error);
      return;
    }
    session_.setUser(result.value);
    order_ = Order{};
    selectedStation_ = Station{};
    selectedPile_ = Pile{};
    showHome();
  }

  void searchStations() {
    if (!session_.isLoggedIn()) return;
    stationList_->clear();
    homeStatus_->setText(QStringLiteral("正在查询站点…"));
    const QString query = query_->text().trimmed();
    runService<QVector<Station>>([this, query] { return service_->stations(query); }, [this](const Result<QVector<Station>> &result) {
      if (!result.ok) { homeStatus_->setText(result.error); return; }
      stationCache_ = result.value;
      if (result.value.isEmpty()) { homeStatus_->setText(QStringLiteral("没有匹配站点")); return; }
      int index = 1;
      for (const auto &station : result.value) {
        const QString distance = station.distanceKm >= 0.0 ? QString::number(station.distanceKm) + QStringLiteral(" km") : QStringLiteral("距离待定位");
        auto *item = new QListWidgetItem(QStringLiteral("站点 %1  ·  %2\n%3\n空闲 %4/%5   ·   %6   ·   %7").arg(index++).arg(station.name).arg(station.address).arg(station.availablePiles).arg(station.totalPiles).arg(distance).arg(station.open ? QStringLiteral("营业中") : QStringLiteral("暂停营业")), stationList_);
        QFont font = item->font(); font.setBold(true); item->setFont(font);
        item->setBackground(QColor("#10243d")); item->setForeground(QColor("#ffffff"));
        item->setSizeHint(QSize(0, 86)); item->setData(Qt::UserRole, station.id);
      }
      homeStatus_->setText(QStringLiteral("已加载 %1 个站点 · 空闲桩数/总桩数 · 按距离由近及远").arg(result.value.size()));
    });
  }

  void openStation(QListWidgetItem *item) {
    const QString stationId = item->data(Qt::UserRole).toString();
    for (const auto &station : stationCache_) if (station.id == stationId) { selectedStation_ = station; break; }
    openStationForSelected();
  }

  void selectPile(QListWidgetItem *item) {
    selectedPile_ = Pile{};
    const QString pileId = item->data(Qt::UserRole).toString();
    for (const auto &pile : pileCache_) if (pile.id == pileId) { selectedPile_ = pile; break; }
    if (selectedPile_.id.isEmpty()) { pileStatus_->setText(QStringLiteral("充电桩信息已失效，请刷新站点")); return; }
    if (selectedPile_.status != PileStatus::Idle) { pileStatus_->setText(QStringLiteral("该充电桩不可用")); return; }
    pileStatus_->setText(QStringLiteral("正在检查当前订单…"));
    const QString userId = session_.user().id;
    runService<Order>([this, userId] { return service_->currentOrder(userId); }, [this](const Result<Order> &current) {
      if (!current.ok) { pileStatus_->setText(current.error); return; }
      if (!current.value.id.isEmpty() && (current.value.status == OrderStatus::Reserved || current.value.status == OrderStatus::Charging || current.value.status == OrderStatus::PendingSettlement || current.value.status == OrderStatus::PendingReservation)) {
        const bool isReservation = current.value.status == OrderStatus::Reserved || current.value.status == OrderStatus::PendingReservation;
        QMessageBox::information(this, isReservation ? QStringLiteral("已有预约") : QStringLiteral("未完成订单"), isReservation ? QStringLiteral("已有预约") : QStringLiteral("未完成订单"));
        order_ = current.value; orderConfirmationMode_ = false;
        if (confirmationControls_) confirmationControls_->setVisible(false);
        if (returnPileButton_) returnPileButton_->setVisible(false);
        updateOrderButtons(); refreshOrderHistory(); stack_->setCurrentWidget(orderPage_);
        return;
      }
      showOrderConfirmation();
    });
  }
  void showOrderConfirmation() {
    if (!session_.isLoggedIn()) { showLogin(); return; }
    orderConfirmationMode_ = true;
    order_ = Order{};
    confirmationControls_->setVisible(true);
    returnPileButton_->setVisible(true);
    cancelReservationButton_->setVisible(false);
    orderStatus_->setText(QStringLiteral("正在检查当前订单…"));
    stack_->setCurrentWidget(orderPage_);
    const QString userId = session_.user().id;
    runService<Order>([this, userId] { return service_->currentOrder(userId); }, [this](const Result<Order> &current) {
      if (!current.ok) { orderStatus_->setText(current.error); confirmOrderButton_->setEnabled(false); reserveButton_->setEnabled(false); return; }
      if (!current.value.id.isEmpty() && current.value.status != OrderStatus::Completed && current.value.status != OrderStatus::Cancelled) {
        order_ = current.value; orderConfirmationMode_ = false; confirmationControls_->setVisible(true); returnPileButton_->setVisible(false);
        orderStatus_->setText(QStringLiteral("您有未完成的充电订单，请先完成收尾")); updateOrderButtons(); return;
      }
      order_ = Order{};
      orderStatus_->setText(QStringLiteral("待确认订单\n站点：%1\n充电桩：%2\n状态：闲置\n价格：¥ %3/度").arg(selectedStation_.name, selectedPile_.number).arg((selectedPile_.priceCentsPerKwh > 0 ? selectedPile_.priceCentsPerKwh : selectedStation_.priceCentsPerKwh) / 100.0));
      updateOrderButtons();
    });
  }

  void createSelectedOrder(QPushButton *source, bool autoConfirm) {
    source->setEnabled(false);
    const QString userId = session_.user().id;
    const Station station = selectedStation_;
    const Pile pile = selectedPile_;
    runService<Order>([this, userId, station, pile] { return service_->createOrder(userId, station, pile); }, [this, source, autoConfirm](const Result<Order> &result) {
      if (!result.ok) { orderStatus_->setText(result.error); source->setEnabled(true); return; }
      order_ = result.value; orderConfirmationMode_ = false; confirmationControls_->setVisible(true); returnPileButton_->setVisible(false);
      if (!autoConfirm) { updateOrderButtons(); return; }
      orderStatus_->setText(QStringLiteral("预约已创建，正在确认…"));
      const QString userId = session_.user().id; const QString orderId = order_.id;
      runService<Order>([this, userId, orderId] { return service_->confirmReservation(userId, orderId); }, [this, source](const Result<Order> &confirmed) {
        if (!confirmed.ok) { orderStatus_->setText(QStringLiteral("预约已创建但确认失败：%1\n可点击确认预约重试。").arg(confirmed.error)); source->setEnabled(true); updateOrderButtons(); return; }
        order_ = confirmed.value; confirmationControls_->setVisible(false); updateOrderButtons(); refreshCurrentOrder();
      });
    });
  }

  void confirmOrder() {
    if (order_.status != OrderStatus::PendingReservation) { createSelectedOrder(confirmOrderButton_, true); return; }
    confirmOrderButton_->setEnabled(false);
    const QString userId = session_.user().id; const QString orderId = order_.id;
    runService<Order>([this, userId, orderId] { return service_->confirmReservation(userId, orderId); }, [this](const Result<Order> &confirmed) {
      if (!confirmed.ok) { orderStatus_->setText(confirmed.error); updateOrderButtons(); return; }
      order_ = confirmed.value; confirmationControls_->setVisible(false); updateOrderButtons(); refreshCurrentOrder();
    });
  }

  void reservePile() { createSelectedOrder(reserveButton_, false); }
  void refreshCurrentOrder() {
    if (!session_.isLoggedIn()) { orderSummary_->setText(QStringLiteral("当前订单：未登录")); return; }
    const QString userId = session_.user().id;
    orderSummary_->setText(QStringLiteral("正在加载当前订单…"));
    runService<Order>([this, userId] { return service_->currentOrder(userId); }, [this](const Result<Order> &result) {
      if (!result.ok) { orderSummary_->setText(result.error); orderStatus_->setText(result.error); return; }
      if (result.value.id.isEmpty()) { order_ = Order{}; orderSummary_->setText(QStringLiteral("当前订单：暂无活动订单")); orderStatus_->setText(QStringLiteral("暂无活动订单，请从站点详情选择闲置充电桩")); updateOrderButtons(); refreshOrderHistory(); return; }
      order_ = result.value; orderSummary_->setText(QStringLiteral("当前订单：%1 · %2").arg(order_.id, orderStatusText(order_.status))); updateOrderButtons(); refreshOrderHistory();
    });
  }
  void refreshOrderHistory() {
    if (!session_.isLoggedIn() || !historyList_ || !historySummary_) return;
    historyList_->clear(); historySummary_->setText(QStringLiteral("正在加载历史记录…"));
    const QString userId = session_.user().id;
    runService<QVector<Order>>([this, userId] { return service_->orderHistory(userId); }, [this](const Result<QVector<Order>> &result) {
      if (!result.ok) { historySummary_->setText(result.error); return; }
      qint64 totalCents = 0;
      for (const auto &historyOrder : result.value) { totalCents += historyOrder.amountCents; historyList_->addItem(QStringLiteral("完成时间：%1\n充电站地址：%2\n花费：¥ %3").arg(historyOrder.completedAt.isEmpty() ? QStringLiteral("时间未记录") : historyOrder.completedAt).arg(historyOrder.stationAddress.isEmpty() ? QStringLiteral("地址未记录") : historyOrder.stationAddress).arg(historyOrder.amountCents / 100.0)); }
      historySummary_->setText(result.value.isEmpty() ? QStringLiteral("历史充电总结：暂无已完成记录") : QStringLiteral("历史充电总结：共 %1 次，累计消费 ¥ %2").arg(result.value.size()).arg(totalCents / 100.0));
    });
  }
  void queryRoute() {
    if (selectedStation_.id.isEmpty()) {
      mapStatus_->setText(QStringLiteral("请先选择目标站点"));
      return;
    }
    const auto parts = fromLocation_->text().split(',');
    if (parts.size() != 2) {
      mapStatus_->setText(QStringLiteral("出发位置格式应为 纬度,经度"));
      return;
    }
    bool latOk = false;
    bool lngOk = false;
    const double lat = parts[0].trimmed().toDouble(&latOk);
    const double lng = parts[1].trimmed().toDouble(&lngOk);
    if (!latOk || !lngOk || !std::isfinite(lat) || !std::isfinite(lng) || lat < -90.0 || lat > 90.0 || lng < -180.0 || lng > 180.0) {
      mapStatus_->setText(QStringLiteral("出发位置坐标无效"));
      return;
    }
    const RouteMode mode = routeMode_ && routeMode_->currentIndex() == 1 ? RouteMode::Walking : RouteMode::Driving;
    auto result = service_->route(lat, lng, selectedStation_, mode);
    mapStatus_->setText(result.ok
                            ? QStringLiteral("Mock 路线：%1\n距离 %2 km · 预计 %3 分钟")
                                  .arg(result.value.summary)
                                  .arg(result.value.distanceKm)
                                  .arg(result.value.durationMin)
                            : result.error);
  }

  void updateOrderButtons() {
    const bool hasOrder = !order_.id.isEmpty();
    const bool pending = hasOrder && order_.status == OrderStatus::PendingReservation;
    confirmationControls_->setVisible(orderConfirmationMode_ || pending);
    confirmOrderButton_->setVisible(orderConfirmationMode_ || pending);
    reserveButton_->setVisible(orderConfirmationMode_ && !hasOrder);
    confirmOrderButton_->setText(pending ? QStringLiteral("确认预约") : QStringLiteral("确认创建订单"));
    confirmOrderButton_->setEnabled((orderConfirmationMode_ && !hasOrder && !selectedPile_.id.isEmpty() && selectedPile_.status == PileStatus::Idle) || pending);
    reserveButton_->setEnabled(orderConfirmationMode_ && !hasOrder && !selectedPile_.id.isEmpty() && selectedPile_.status == PileStatus::Idle);
    returnPileButton_->setVisible(orderConfirmationMode_ && !hasOrder && !selectedPile_.id.isEmpty());
    cancelReservationButton_->setVisible(!orderConfirmationMode_ && hasOrder && (order_.status == OrderStatus::Reserved || pending));
    cancelReservationButton_->setEnabled(!orderConfirmationMode_ && hasOrder && (order_.status == OrderStatus::Reserved || pending));
    startButton_->setEnabled(order_.status == OrderStatus::Reserved);
    directStartButton_->setVisible(orderConfirmationMode_ && !hasOrder);
    directStartButton_->setEnabled(orderConfirmationMode_ && !hasOrder && !selectedPile_.id.isEmpty() && selectedPile_.status == PileStatus::Idle);
    stopButton_->setEnabled(order_.status == OrderStatus::Charging);
    settleButton_->setEnabled(order_.status == OrderStatus::PendingSettlement);
    if (hasOrder) orderStatus_->setText(QStringLiteral("订单 %1 · %2\n站点：%3\n充电桩：%4\n金额：¥ %5").arg(order_.id, orderStatusText(order_.status), order_.stationName, order_.pileNumber).arg(order_.amountCents / 100.0));
  }
  void startCharging() {
    startButton_->setEnabled(false);
    const QString userId = session_.user().id; const QString orderId = order_.id;
    runService<Order>([this, userId, orderId] { return service_->startCharging(userId, orderId); }, [this](const Result<Order> &result) {
      if (!result.ok) { orderStatus_->setText(result.error); updateOrderButtons(); return; }
      order_ = result.value; updateOrderButtons(); refreshCurrentOrder();
    });
  }

  void startChargingDirect() {
    directStartButton_->setEnabled(false);
    const QString userId = session_.user().id;
    const QString pileId = selectedPile_.id;
    runService<Order>([this, userId, pileId] { return service_->startChargingDirect(userId, pileId); }, [this](const Result<Order> &result) {
      if (!result.ok) { orderStatus_->setText(result.error); updateOrderButtons(); return; }
      order_ = result.value;
      orderConfirmationMode_ = false;
      confirmationControls_->setVisible(false);
      updateOrderButtons();
      refreshCurrentOrder();
    });
  }

  void cancelReservation() {
    cancelReservationButton_->setEnabled(false);
    const QString userId = session_.user().id; const QString orderId = order_.id;
    runService<Order>([this, userId, orderId] { return service_->cancelReservation(userId, orderId); }, [this](const Result<Order> &result) {
      if (!result.ok) { orderStatus_->setText(result.error); updateOrderButtons(); return; }
      order_ = result.value; updateOrderButtons(); refreshCurrentOrder();
    });
  }

  void stopCharging() {
    stopButton_->setEnabled(false);
    const QString userId = session_.user().id; const QString orderId = order_.id;
    runService<Order>([this, userId, orderId] { return service_->stopCharging(userId, orderId); }, [this](const Result<Order> &result) {
      if (!result.ok) { orderStatus_->setText(result.error); updateOrderButtons(); return; }
      order_ = result.value; updateOrderButtons(); refreshCurrentOrder();
      orderStatus_->setText(QStringLiteral("充电已停止，电桩已释放，请在订单页完成结算。"));
    });
  }

  void settle() {
    settleButton_->setEnabled(false);
    const QString userId = session_.user().id; const QString orderId = order_.id;
    runService<Order>([this, userId, orderId] { return service_->settle(userId, orderId); }, [this, userId](const Result<Order> &result) {
      if (!result.ok) { orderStatus_->setText(result.error); updateOrderButtons(); return; }
      order_ = result.value; updateOrderButtons(); refreshCurrentOrder();
      runService<User>([this, userId] { return service_->profile(userId); }, [this](const Result<User> &profileResult) { if (profileResult.ok) session_.setUser(profileResult.value); });
      QMessageBox::information(this, QStringLiteral("结算完成"), QStringLiteral("订单已完成，结算成功。"));
    });
  }

  void saveProfile() {
    const QString userId = session_.user().id; const QString name = nickname_->text(); const QString avatar = session_.user().avatarPath;
    runService<User>([this, userId, name, avatar] { return service_->updateProfile(userId, name, avatar); }, [this](const Result<User> &result) {
      if (!result.ok) { profileLabel_->setText(result.error); return; }
      session_.setUser(result.value); showProfile();
    });
  }

  void chooseAvatar() {
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择头像"), QString(), QStringLiteral("图片 (*.png *.jpg *.jpeg)"));
    if (path.isEmpty()) return;
    session_.setAvatarPath(path);
    avatarLabel_->setPixmap(QPixmap(path).scaled(84, 84, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    avatarLabel_->setText({});
  }

  void recharge() {
    const qint64 amountCents = qRound64(rechargeAmount_->value() * 100.0);
    rechargeButton_->setEnabled(false);
    const QString userId = session_.user().id;
    runService<qint64>([this, userId, amountCents] { return service_->recharge(userId, amountCents); }, [this](const Result<qint64> &result) {
      rechargeButton_->setEnabled(true);
      if (!result.ok) { profileLabel_->setText(result.error); return; }
      auto user = session_.user(); user.walletBalanceCents = result.value; session_.setUser(user); showProfile();
    });
  }
};

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  UserWindow window;
  window.show();
  return app.exec();
}

#include "main.moc"
