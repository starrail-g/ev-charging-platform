#include "client_service.h"

#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QDoubleSpinBox>
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
#include <QStackedWidget>
#include <QVBoxLayout>

using namespace ev;

class UserWindow final : public QMainWindow {
  Q_OBJECT
public:
  UserWindow() {
    setWindowTitle(QStringLiteral("充电用户端"));
    setFixedSize(420, 760);
    setStyleSheet(QStringLiteral(
        "QMainWindow{background:#081225;color:#e8f1ff;}"
        "QWidget{color:#e8f1ff;font-size:13px;}"
        "QLineEdit,QComboBox,QDoubleSpinBox{background:#101f3b;border:1px solid #12d7f5;border-radius:8px;padding:8px;color:#f5f8ff;}"
        "QPushButton{background:#172b4d;border:1px solid #2c4772;border-radius:8px;padding:8px;color:#e8f1ff;}"
        "QPushButton:hover{background:#1e4470;} QPushButton:disabled{color:#70809b;background:#10192d;}"
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
  MockUserService service_;
  SessionManager session_;
  QStackedWidget *stack_{};
  QWidget *login_{}, *registerPage_{}, *home_{}, *detail_{}, *map_{}, *orderPage_{}, *profile_{};
  QStackedWidget *registerSteps_{};
  QLineEdit *phone_{}, *password_{}, *regPhone_{}, *regPassword_{}, *regConfirmPassword_{}, *regName_{}, *query_{}, *address_{}, *fromLocation_{}, *nickname_{};
  QComboBox *region_{};
  QDoubleSpinBox *rechargeAmount_{};
  QLabel *loginStatus_{}, *registerStatus_{}, *homeStatus_{}, *locationStatus_{}, *orderSummary_{}, *detailTitle_{}, *pileStatus_{}, *mapStatus_{}, *orderStatus_{}, *historySummary_{}, *profileLabel_{}, *avatarLabel_{};
  QWidget *confirmationControls_{};
  QListWidget *stationList_{}, *pileList_{}, *mapStationList_{}, *historyList_{};
  QPushButton *loginButton_{}, *registerNextButton_{}, *registerFinishButton_{}, *confirmOrderButton_{}, *reserveButton_{}, *returnPileButton_{}, *cancelReservationButton_{}, *startButton_{}, *stopButton_{}, *settleButton_{};
  Station selectedStation_{};
  Pile selectedPile_{};
  Order order_{};
  bool orderConfirmationMode_{false};

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
    form->addRow(QStringLiteral("密码（可选）"), passwordRow(password_));
    layout->addLayout(form);
    loginButton_ = new QPushButton(QStringLiteral("登录"), login_);
    loginButton_->setMinimumHeight(42);
    layout->addWidget(loginButton_);
    loginStatus_ = new QLabel(login_);
    loginStatus_->setWordWrap(true);
    layout->addWidget(loginStatus_);
    layout->addStretch();
    layout->addWidget(new QLabel(QStringLiteral("演示：13800000000；新手机号会自动创建用户"), login_));
    connect(loginButton_, &QPushButton::clicked, this, &UserWindow::login);
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
    auto *mode = new QComboBox(map_);
    mode->addItems({QStringLiteral("驾车（Mock）"), QStringLiteral("步行（Mock）")});
    layout->addWidget(mode);
    auto *route = new QPushButton(QStringLiteral("查询路线"), map_);
    layout->addWidget(route);
    layout->addWidget(new QLabel(QStringLiteral("真实腾讯地图 Web API / QWebEngineView：暂未接入"), map_));
    layout->addWidget(nav(QStringLiteral("返回首页"), map_, &UserWindow::showHome));
    addBottomNav(layout, map_);
    connect(mapStationList_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
      for (const auto &station : service_.stations(QString()).value) {
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
    stopButton_ = new QPushButton(QStringLiteral("停止充电"), orderPage_);
    settleButton_ = new QPushButton(QStringLiteral("结算"), orderPage_);
    returnPileButton_ = new QPushButton(QStringLiteral("返回充电桩"), orderPage_);
    cancelReservationButton_ = new QPushButton(QStringLiteral("取消预约"), orderPage_);
    layout->addWidget(startButton_);
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
    addBottomNav(layout, orderPage_);
    connect(confirmOrderButton_, &QPushButton::clicked, this, &UserWindow::confirmOrder);
    connect(reserveButton_, &QPushButton::clicked, this, &UserWindow::reservePile);
    connect(returnPileButton_, &QPushButton::clicked, this, [this] { showStationDetail(); });
    connect(cancelReservationButton_, &QPushButton::clicked, this, &UserWindow::cancelReservation);
    connect(startButton_, &QPushButton::clicked, this, &UserWindow::startCharging);
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
    auto *recharge = new QPushButton(QStringLiteral("充值（Mock）"), profile_);
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
    openStationForSelected();
  }

  void openStationForSelected() {
    detailTitle_->setText(QStringLiteral("%1\n%2\n坐标：%3, %4\n计费：¥ %5/度")
                              .arg(selectedStation_.name)
                              .arg(selectedStation_.address)
                              .arg(selectedStation_.latitude)
                              .arg(selectedStation_.longitude)
                              .arg(selectedStation_.pricePerKwh, 0, 'f', 2));
    pileList_->clear();
    const auto result = service_.piles(selectedStation_.id);
    if (!result.ok) {
      pileStatus_->setText(result.error);
      stack_->setCurrentWidget(detail_);
      return;
    }
    if (result.value.isEmpty()) {
      pileStatus_->setText(QStringLiteral("该站点暂无充电桩"));
      stack_->setCurrentWidget(detail_);
      return;
    }
    int index = 1;
    for (const auto &pile : result.value) {
      auto *pileItem = new QListWidgetItem(QStringLiteral("电桩 %1 · %2 · %3 · %4 · %5 kW\n计费 ¥ %6/度")
                                               .arg(index++)
                                               .arg(pile.number)
                                               .arg(pile.type)
                                               .arg(pileStatusText(pile.status))
                                               .arg(pile.powerKw, 0, 'f', 0)
                                               .arg(selectedStation_.pricePerKwh, 0, 'f', 2),
                                           pileList_);
      pileItem->setBackground(QColor("#10243d"));
      pileItem->setForeground(QColor("#ffffff"));
      pileItem->setData(Qt::UserRole, pile.id);
    }
    pileStatus_->setText(QStringLiteral("点击充电桩查看状态；闲置桩可进入订单确认"));
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
    refreshCurrentOrder();
    refreshOrderHistory();
    stack_->setCurrentWidget(orderPage_);
  }

  void showMap() {
    if (!session_.isLoggedIn()) {
      showLogin();
      return;
    }
    stack_->setCurrentWidget(map_);
    mapStationList_->clear();
    for (const auto &station : service_.stations(QString()).value) {
      auto *item = new QListWidgetItem(QStringLiteral("站点：%1 · (%2,%3) · %4")
                                           .arg(station.name)
                                           .arg(station.latitude)
                                           .arg(station.longitude)
                                           .arg(station.open ? QStringLiteral("营业中") : QStringLiteral("暂停营业")),
                                       mapStationList_);
      item->setData(Qt::UserRole, station.id);
    }
    if (selectedStation_.id.isEmpty()) {
      mapStatus_->setText(QStringLiteral("请选择目标站点；真实腾讯地图 API 暂未接入，当前为 Mock 路线。"));
    } else {
      mapStatus_->setText(QStringLiteral("当前目标：%1").arg(selectedStation_.name));
    }
  }

  void showProfile() {
    if (!session_.isLoggedIn()) {
      showLogin();
      return;
    }
    stack_->setCurrentWidget(profile_);
    profileLabel_->setText(QStringLiteral("手机号：%1\n余额：¥ %2")
                               .arg(session_.user().phone)
                               .arg(session_.user().walletBalance, 0, 'f', 2));
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
    auto result = service_.login(phone, password_->text());
    loginButton_->setEnabled(true);
    if (!result.ok) {
      loginStatus_->setText(result.error);
      return;
    }
    loginStatus_->clear();
    session_.setUser(result.value);
    showHome();
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
    auto result = service_.registerUser(regPhone_->text().trimmed(), regPassword_->text(), regName_->text());
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
    if (!session_.isLoggedIn()) {
      return;
    }
    stationList_->clear();
    homeStatus_->setText(QStringLiteral("正在查询站点…"));
    auto result = service_.stations(query_->text().trimmed());
    if (!result.ok) {
      homeStatus_->setText(result.error);
      return;
    }
    if (result.value.isEmpty()) {
      homeStatus_->setText(QStringLiteral("没有匹配站点"));
      return;
    }
    int index = 1;
    for (const auto &station : result.value) {
      auto *item = new QListWidgetItem(
          QStringLiteral("站点 %1  ·  %2\n%3\n空闲 %4/%5   ·   %6 km   ·   %7")
              .arg(index++)
              .arg(station.name)
              .arg(station.address)
              .arg(station.availablePiles)
              .arg(station.totalPiles)
              .arg(station.distanceKm, 0, 'f', 1)
              .arg(station.open ? QStringLiteral("营业中") : QStringLiteral("暂停营业")),
          stationList_);
      QFont font = item->font();
      font.setBold(true);
      item->setFont(font);
      item->setBackground(QColor("#10243d"));
      item->setForeground(QColor("#ffffff"));
      item->setSizeHint(QSize(0, 86));
      item->setData(Qt::UserRole, station.id);
    }
    homeStatus_->setText(QStringLiteral("已加载 %1 个站点 · 空闲桩数/总桩数 · 按距离由近及远").arg(result.value.size()));
  }

  void openStation(QListWidgetItem *item) {
    for (const auto &station : service_.stations(QString()).value) {
      if (station.id == item->data(Qt::UserRole).toString()) {
        selectedStation_ = station;
        break;
      }
    }
    openStationForSelected();
  }

  void selectPile(QListWidgetItem *item) {
    for (const auto &pile : service_.piles(selectedStation_.id).value) {
      if (pile.id == item->data(Qt::UserRole).toString()) {
        selectedPile_ = pile;
        break;
      }
    }
    if (selectedPile_.status != PileStatus::Idle) {
      pileStatus_->setText(QStringLiteral("该充电桩不可用"));
      return;
    }
    const auto current = service_.currentOrder(session_.user().id);
    if (current.ok && !current.value.id.isEmpty() &&
        (current.value.status == OrderStatus::Reserved || current.value.status == OrderStatus::Charging || current.value.status == OrderStatus::PendingSettlement)) {
      const bool isReservation = current.value.status == OrderStatus::Reserved;
      QMessageBox::information(this,
                               isReservation ? QStringLiteral("已有预约") : QStringLiteral("未完成订单"),
                               isReservation ? QStringLiteral("已有预约") : QStringLiteral("未完成订单"));
      order_ = current.value;
      orderConfirmationMode_ = false;
      if (confirmationControls_) confirmationControls_->setVisible(false);
      if (returnPileButton_) returnPileButton_->setVisible(false);
      updateOrderButtons();
      refreshOrderHistory();
      stack_->setCurrentWidget(orderPage_);
      return;
    }
    showOrderConfirmation();
  }

  void showOrderConfirmation() {
    if (!session_.isLoggedIn()) {
      showLogin();
      return;
    }
    orderConfirmationMode_ = true;
    if (confirmationControls_) confirmationControls_->setVisible(true);
    if (returnPileButton_) returnPileButton_->setVisible(true);
    if (cancelReservationButton_) cancelReservationButton_->setVisible(false);
    const auto current = service_.currentOrder(session_.user().id);
    if (!current.ok) {
      orderStatus_->setText(current.error);
      confirmOrderButton_->setEnabled(false);
    } else if (!current.value.id.isEmpty() && current.value.status != OrderStatus::Completed && current.value.status != OrderStatus::Cancelled) {
      orderStatus_->setText(QStringLiteral("您有未完成的充电订单，请先结算"));
      confirmOrderButton_->setEnabled(false);
    } else {
      order_ = Order{};
      orderStatus_->setText(QStringLiteral("待确认订单\n站点：%1\n充电桩：%2\n状态：闲置\n价格：¥ %3/度")
                                .arg(selectedStation_.name, selectedPile_.number)
                                .arg(selectedStation_.pricePerKwh, 0, 'f', 2));
      confirmOrderButton_->setEnabled(true);
    }
    stack_->setCurrentWidget(orderPage_);
    updateOrderButtons();
  }

  void createSelectedOrder(QPushButton *source) {
    source->setEnabled(false);
    auto result = service_.createOrder(session_.user().id, selectedStation_, selectedPile_);
    if (!result.ok) {
      orderStatus_->setText(result.error);
      source->setEnabled(true);
      return;
    }
    order_ = result.value;
    orderConfirmationMode_ = false;
    if (confirmationControls_) confirmationControls_->setVisible(false);
    if (returnPileButton_) returnPileButton_->setVisible(false);
    updateOrderButtons();
    refreshCurrentOrder();
  }

  void confirmOrder() { createSelectedOrder(confirmOrderButton_); }

  void reservePile() { createSelectedOrder(reserveButton_); }

  void refreshCurrentOrder() {
    if (!session_.isLoggedIn()) {
      orderSummary_->setText(QStringLiteral("当前订单：未登录"));
      return;
    }
    auto result = service_.currentOrder(session_.user().id);
    if (!result.ok) {
      orderSummary_->setText(result.error);
      orderStatus_->setText(result.error);
      return;
    }
    if (result.value.id.isEmpty()) {
      orderSummary_->setText(QStringLiteral("当前订单：暂无活动订单"));
      orderStatus_->setText(QStringLiteral("暂无活动订单，请从站点详情选择闲置充电桩"));
      updateOrderButtons();
      refreshOrderHistory();
      return;
    }
    order_ = result.value;
    orderSummary_->setText(QStringLiteral("当前订单：%1 · %2").arg(order_.id, orderStatusText(order_.status)));
    updateOrderButtons();
    refreshOrderHistory();
  }

  void refreshOrderHistory() {
    if (!session_.isLoggedIn() || !historyList_ || !historySummary_) return;
    historyList_->clear();
    const auto result = service_.orderHistory(session_.user().id);
    if (!result.ok) {
      historySummary_->setText(result.error);
      return;
    }
    double total = 0.0;
    for (const auto &historyOrder : result.value) {
      total += historyOrder.amount;
      historyList_->addItem(QStringLiteral("完成时间：%1\n充电站地址：%2\n花费：¥ %3")
                                .arg(historyOrder.completedAt.isEmpty() ? QStringLiteral("Mock 时间未记录") : historyOrder.completedAt)
                                .arg(historyOrder.stationAddress.isEmpty() ? QStringLiteral("地址未记录") : historyOrder.stationAddress)
                                .arg(historyOrder.amount, 0, 'f', 2));
    }
    historySummary_->setText(result.value.isEmpty()
                                 ? QStringLiteral("历史充电总结：暂无已完成记录")
                                 : QStringLiteral("历史充电总结：共 %1 次，累计消费 ¥ %2").arg(result.value.size()).arg(total, 0, 'f', 2));
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
    if (!latOk || !lngOk) {
      mapStatus_->setText(QStringLiteral("出发位置坐标无效"));
      return;
    }
    auto result = service_.route(lat, lng, selectedStation_);
    mapStatus_->setText(result.ok
                            ? QStringLiteral("Mock 路线：%1\n距离 %2 km · 预计 %3 分钟")
                                  .arg(result.value.summary)
                                  .arg(result.value.distanceKm)
                                  .arg(result.value.durationMin)
                            : result.error);
  }

  void updateOrderButtons() {
    const bool hasOrder = !order_.id.isEmpty();
    confirmOrderButton_->setEnabled(orderConfirmationMode_ && !hasOrder && !selectedPile_.id.isEmpty() && selectedPile_.status == PileStatus::Idle);
    reserveButton_->setEnabled(orderConfirmationMode_ && !hasOrder && !selectedPile_.id.isEmpty() && selectedPile_.status == PileStatus::Idle);
    returnPileButton_->setVisible(orderConfirmationMode_ && !hasOrder && !selectedPile_.id.isEmpty());
    cancelReservationButton_->setVisible(!orderConfirmationMode_ && hasOrder && order_.status == OrderStatus::Reserved);
    cancelReservationButton_->setEnabled(!orderConfirmationMode_ && hasOrder && order_.status == OrderStatus::Reserved);
    startButton_->setEnabled(order_.status == OrderStatus::Reserved);
    stopButton_->setEnabled(order_.status == OrderStatus::Charging);
    settleButton_->setEnabled(order_.status == OrderStatus::PendingSettlement);
    if (hasOrder) {
      orderStatus_->setText(QStringLiteral("订单 %1 · %2\n站点：%3\n充电桩：%4\n金额：¥ %5")
                                .arg(order_.id, orderStatusText(order_.status), order_.stationName, order_.pileNumber)
                                .arg(order_.amount, 0, 'f', 2));
    }
  }

  void startCharging() {
    auto result = service_.startCharging(session_.user().id, order_.id);
    if (!result.ok) {
      orderStatus_->setText(result.error);
      return;
    }
    order_ = result.value;
    updateOrderButtons();
    refreshCurrentOrder();
  }

  void cancelReservation() {
    cancelReservationButton_->setEnabled(false);
    const auto result = service_.cancelReservation(session_.user().id, order_.id);
    if (!result.ok) {
      orderStatus_->setText(result.error);
      updateOrderButtons();
      return;
    }
    order_ = result.value;
    updateOrderButtons();
    refreshCurrentOrder();
  }

  void stopCharging() {
    auto result = service_.stopCharging(session_.user().id, order_.id);
    if (!result.ok) {
      orderStatus_->setText(result.error);
      return;
    }
    order_ = result.value;
    updateOrderButtons();
    refreshCurrentOrder();
  }

  void settle() {
    auto result = service_.settle(session_.user().id, order_.id);
    if (!result.ok) {
      orderStatus_->setText(result.error);
      return;
    }
    order_ = result.value;
    updateOrderButtons();
    refreshCurrentOrder();
    QMessageBox::information(this, QStringLiteral("结算完成"), QStringLiteral("订单已完成，Mock 结算成功。"));
  }

  void saveProfile() {
    auto result = service_.updateProfile(session_.user().id, nickname_->text(), session_.user().avatarPath);
    if (!result.ok) {
      profileLabel_->setText(result.error);
      return;
    }
    session_.setUser(result.value);
    showProfile();
  }

  void chooseAvatar() {
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择头像"), QString(), QStringLiteral("图片 (*.png *.jpg *.jpeg)"));
    if (path.isEmpty()) {
      return;
    }
    session_.setAvatarPath(path);
    avatarLabel_->setPixmap(QPixmap(path).scaled(84, 84, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    avatarLabel_->setText({});
  }

  void recharge() {
    auto result = service_.recharge(session_.user().id, rechargeAmount_->value());
    if (!result.ok) {
      profileLabel_->setText(result.error);
      return;
    }
    auto user = session_.user();
    user.walletBalance = result.value;
    session_.setUser(user);
    showProfile();
  }
};

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  UserWindow window;
  window.show();
  return app.exec();
}

#include "main.moc"
