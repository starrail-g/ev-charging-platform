#include "client_service.h"
#include "map_service.h"
#include "map_web_view.h"
#include "server_map_service.h"
#include "socket_user_service.h"

#include <QApplication>
#include <cmath>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <QFile>
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
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
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
    socketMode_ = qEnvironmentVariable("EV_USER_CLIENT_TRANSPORT").compare(QStringLiteral("socket"), Qt::CaseInsensitive) == 0;
    setWindowFlag(Qt::WindowMaximizeButtonHint, false);
    setMinimumSize(kMinUnits * kAspectWidth, kMinUnits * kAspectHeight);
    setMaximumSize(kMaxUnits * kAspectWidth, kMaxUnits * kAspectHeight);
    const QRect available = QGuiApplication::primaryScreen()->availableGeometry();
    const int initialUnits = qBound(kMinUnits,
        qMin(20, qMin(available.width() * 9 / (10 * kAspectWidth),
                      available.height() * 9 / (10 * kAspectHeight))), kMaxUnits);
    resize(initialUnits * kAspectWidth, initialUnits * kAspectHeight);
    if (socketMode_) {
      service_ = &socketService_;
      mapService_ = &serverMapService_;
    } else {
      mapService_ = &mockMapService_;
    }
    QFile theme(QStringLiteral(":/user-client/theme.qss"));
    if (theme.open(QIODevice::ReadOnly | QIODevice::Text))
      setStyleSheet(QString::fromUtf8(theme.readAll()));
    stack_ = new QStackedWidget(this);
    stack_->setObjectName(QStringLiteral("userStack"));
    setCentralWidget(stack_);
    buildLogin();
    buildHome();
    buildStationDetail();
    buildMap();
    buildOrder();
    buildProfile();
    showLogin();
  }

protected:
  void resizeEvent(QResizeEvent *event) override {
    QMainWindow::resizeEvent(event);
    if (aspectResizeInProgress_) return;
    const QSize previous = event->oldSize();
    const QSize requested = event->size();
    const int widthDelta = previous.isValid() ? qAbs(requested.width() - previous.width()) : requested.width();
    const int heightDeltaAsWidth = previous.isValid()
        ? qRound(qAbs(requested.height() - previous.height()) * qreal(kAspectWidth) / kAspectHeight) : 0;
    const int requestedUnits = widthDelta >= heightDeltaAsWidth
        ? qRound(qreal(requested.width()) / kAspectWidth)
        : qRound(qreal(requested.height()) / kAspectHeight);
    const int units = qBound(kMinUnits, requestedUnits, kMaxUnits);
    const QSize constrained(units * kAspectWidth, units * kAspectHeight);
    if (constrained == requested) return;
    aspectResizeInProgress_ = true;
    resize(constrained);
    aspectResizeInProgress_ = false;
  }

private:
  static constexpr int kAspectWidth = 21;
  static constexpr int kAspectHeight = 38;
  static constexpr int kMinUnits = 15;
  static constexpr int kMaxUnits = 40;

  MockUserService mockService_;
  SocketUserService socketService_;
  MockMapService mockMapService_;
  ServerMapService serverMapService_;
  IUserService *service_{&mockService_};
  IMapService *mapService_{&mockMapService_};
  bool socketMode_{false};
  bool aspectResizeInProgress_{false};
  SessionManager session_;
  QStackedWidget *stack_{};
  QWidget *login_{}, *home_{}, *detail_{}, *map_{}, *orderPage_{}, *profile_{};

  QLineEdit *phone_{}, *query_{}, *address_{}, *fromLocation_{}, *nickname_{};
  QComboBox *region_{}, *routeMode_{};
  QDoubleSpinBox *rechargeAmount_{};
  QLabel *loginStatus_{}, *homeStatus_{}, *locationStatus_{}, *orderSummary_{}, *detailTitle_{}, *pileStatus_{}, *mapStatus_{}, *orderStatus_{}, *historySummary_{}, *profileLabel_{}, *avatarLabel_{};
  QWidget *confirmationControls_{};
  QListWidget *stationList_{}, *pileList_{}, *mapStationList_{}, *mapPoiList_{}, *historyList_{};
  MapWebView *mapView_{};
  QPushButton *loginButton_{}, *confirmOrderButton_{}, *reserveButton_{}, *returnPileButton_{}, *cancelReservationButton_{}, *directStartButton_{}, *startButton_{}, *stopButton_{}, *settleButton_{}, *rechargeButton_{};
  Station selectedStation_{};
  Pile selectedPile_{};
  Order order_{};
  bool orderConfirmationMode_{false};
  QVector<Station> stationCache_;
  QVector<Pile> pileCache_;
  quint64 stationRequestGeneration_{0};
  quint64 pileRequestGeneration_{0};
  quint64 mapRequestGeneration_{0};
  GeoCoordinate mapOrigin_{22.5300, 113.9300};
  QVector<MapPoi> mapPois_;
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

  template <typename T, typename Fn, typename Done, typename Discard>
  void runService(Fn fn, Done done, Discard discard) {
    const quint64 requestGeneration = session_.generation();
    const QString requestUserId = session_.isLoggedIn() ? session_.user().id : QString();
    const auto requestStillValid = [this, requestGeneration, requestUserId] {
      if (session_.generation() != requestGeneration) return false;
      if (!requestUserId.isEmpty()) return session_.isLoggedIn() && session_.user().id == requestUserId;
      return true;
    };
    if (service_ == &mockService_) {
      const auto result = fn();
      if (requestStillValid()) done(result);
      else discard();
      return;
    }
    auto *watcher = new QFutureWatcher<Result<T>>(this);
    activeWatchers_.push_back(watcher);
    connect(watcher, &QFutureWatcher<Result<T>>::finished, this, [this, watcher, done, discard, requestStillValid]() mutable {
      const auto result = watcher->result();
      activeWatchers_.removeOne(watcher);
      watcher->deleteLater();
      if (!requestStillValid()) { discard(); return; }
      done(result);
    });
    watcher->setFuture(QtConcurrent::run(fn));
  }

  template <typename T, typename Fn, typename Done>
  void runService(Fn fn, Done done) {
    runService<T>(fn, done, [] {});
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
    loginStatus_ = new QLabel(login_);
    loginStatus_->setWordWrap(true);
    layout->addWidget(loginStatus_);
    layout->addStretch();
    if (!socketMode_)
      layout->addWidget(new QLabel(QStringLiteral("演示：13800000000；新手机号会自动创建用户"), login_));
    connect(loginButton_, &QPushButton::clicked, this, &UserWindow::login);
    stack_->addWidget(login_);
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
    orderSummary_->setObjectName(QStringLiteral("summaryAccent"));
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
    buttons->addWidget(nav(QStringLiteral("地图导航"), detail_, &UserWindow::showMap));
    buttons->addWidget(nav(QStringLiteral("返回首页"), detail_, &UserWindow::showHome));
    layout->addLayout(buttons);
    addBottomNav(layout, detail_);
    connect(pileList_, &QListWidget::itemClicked, this, &UserWindow::selectPile);
    stack_->addWidget(detail_);
  }

  void buildMap() {
    map_ = new QWidget;
    auto *rootLayout = new QVBoxLayout(map_);
    rootLayout->setContentsMargins(10, 10, 10, 8);
    auto *scroll = new QScrollArea(map_);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *content = new QWidget(scroll);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(4, 4, 4, 8);
    layout->setSpacing(8);
    auto *title = new QLabel(QStringLiteral("一键导航 · 服务端地图"), content);
    title->setObjectName(QStringLiteral("title"));
    layout->addWidget(title);
    fromLocation_ = new QLineEdit(content);
    fromLocation_->setPlaceholderText(QStringLiteral("起点：地址或 纬度,经度"));
    fromLocation_->setText(QStringLiteral("22.530,113.930"));
    layout->addWidget(fromLocation_);
    auto *locate = new QPushButton(QStringLiteral("定位并查询附近地图 POI"), content);
    layout->addWidget(locate);
    mapView_ = new MapWebView(content);
    mapView_->setServiceBacked(socketMode_);
    layout->addWidget(mapView_);
    layout->addWidget(new QLabel(QStringLiteral("地图 POI（仅位置数据，不能直接下单）"), content));
    mapPoiList_ = new QListWidget(content);
    mapPoiList_->setMinimumHeight(100);
    mapPoiList_->setMaximumHeight(130);
    layout->addWidget(mapPoiList_);
    layout->addWidget(new QLabel(QStringLiteral("业务站点（可作为导航终点）"), content));
    mapStationList_ = new QListWidget(content);
    mapStationList_->setToolTip(QStringLiteral("点击站点标记选择终点"));
    mapStationList_->setMinimumHeight(110);
    mapStationList_->setMaximumHeight(150);
    layout->addWidget(mapStationList_);
    mapStatus_ = new QLabel(content);
    mapStatus_->setWordWrap(true);
    mapStatus_->setObjectName(QStringLiteral("statusPanel"));
    layout->addWidget(mapStatus_);
    auto *routeRow = new QHBoxLayout;
    routeMode_ = new QComboBox(content);
    routeMode_->addItems({QStringLiteral("驾车"), QStringLiteral("步行")});
    auto *route = new QPushButton(QStringLiteral("查询路线"), content);
    routeRow->addWidget(routeMode_, 1);
    routeRow->addWidget(route, 1);
    layout->addLayout(routeRow);
    scroll->setWidget(content);
    rootLayout->addWidget(scroll, 1);
    rootLayout->addWidget(nav(QStringLiteral("返回首页"), map_, &UserWindow::showHome));
    addBottomNav(rootLayout, map_);
    connect(mapStationList_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
      for (const auto &station : stationCache_) {
        if (station.id == item->data(Qt::UserRole).toString()) {
          selectedStation_ = station;
          mapService_->setTargetStationId(station.id);
          mapStatus_->setText(QStringLiteral("目标站点：%1（%2, %3）").arg(station.name).arg(station.latitude).arg(station.longitude));
          break;
        }
      }
    });
    connect(mapPoiList_, &QListWidget::itemClicked, this,
            [this](QListWidgetItem *item) { openMapMarker(item->data(Qt::UserRole).toString()); });
    connect(mapView_, &MapWebView::markerActivated, this, &UserWindow::openMapMarker);
    connect(locate, &QPushButton::clicked, this, &UserWindow::locateMapOrigin);
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
    historySummary_->setObjectName(QStringLiteral("statusPanel"));
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
    avatarLabel_->setObjectName(QStringLiteral("avatarBadge"));
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
    rechargeButton_ = new QPushButton(socketMode_ ? QStringLiteral("充值") : QStringLiteral("充值（Mock）"), profile_);
    auto *recharge = rechargeButton_;
    wallet->addWidget(rechargeAmount_);
    wallet->addWidget(recharge);
    layout->addLayout(wallet);
    if (!socketMode_)
      layout->addWidget(new QLabel(QStringLiteral("余额和账号信息为本地演示数据"), profile_));
    layout->addStretch();
    layout->addWidget(nav(QStringLiteral("退出登录"), profile_, &UserWindow::logout));
    addBottomNav(layout, profile_);
    connect(save, &QPushButton::clicked, this, &UserWindow::saveProfile);
    connect(avatar, &QPushButton::clicked, this, &UserWindow::chooseAvatar);
    connect(recharge, &QPushButton::clicked, this, &UserWindow::recharge);
    stack_->addWidget(profile_);
  }

  quint64 beginMapRequest() {
    const quint64 generation = ++mapRequestGeneration_;
    mapService_->cancelPending();
    return generation;
  }

  void deactivateMapIfVisible() {
    if (!map_ || !stack_ || stack_->currentWidget() != map_) return;
    ++mapRequestGeneration_;
    mapService_->cancelPending();
    if (mapView_) mapView_->deactivate();
  }

  void openMapMarker(const QString &id) {
    if (id.isEmpty()) return;
    for (const auto &station : stationCache_) {
      if (station.id == id) {
        selectedStation_ = station;
        showStationDetail();
        return;
      }
    }
    for (const auto &poi : mapPois_) {
      if (poi.id != id) continue;
      const QString businessStationId = matchingBusinessStationId(poi, stationCache_);
      if (!businessStationId.isEmpty()) {
        for (const auto &station : stationCache_) {
          if (station.id != businessStationId) continue;
          selectedStation_ = station;
          showStationDetail();
          return;
        }
      }
      const quint64 requestGeneration = beginMapRequest();
      const quint64 sessionGeneration = session_.generation();
      mapStatus_->setText(QStringLiteral("正在读取 POI 详情：%1…").arg(poi.title));
      mapService_->getPoiDetail(id, [this, requestGeneration, sessionGeneration, poi](const MapResult<MapPoi> &result) {
        if (requestGeneration != mapRequestGeneration_ || sessionGeneration != session_.generation()) return;
        const MapPoi detail = result.ok ? result.value : poi;
        mapStatus_->setText(QStringLiteral("地图 POI：%1\n%2\n坐标：%3,%4\n该 POI 未关联业务桩数据，请从业务站点列表选择目标。%5%6")
            .arg(detail.title, detail.address).arg(detail.coordinate.latitude).arg(detail.coordinate.longitude)
            .arg(result.notice.isEmpty() ? QString() : QStringLiteral("\n%1").arg(mapResultNotice(result)))
            .arg(result.ok ? QString() : QStringLiteral("\n详情服务不可用，显示搜索结果。")));
      });
      return;
    }
  }

  void showLogin() {
    stack_->setCurrentWidget(login_);
  }

  void showHome() {
    if (!session_.isLoggedIn()) {
      showLogin();
      return;
    }
    deactivateMapIfVisible();
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
    deactivateMapIfVisible();
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
    const quint64 requestGeneration = ++pileRequestGeneration_;
    runService<QVector<Pile>>([this, stationId] { return service_->piles(stationId); }, [this, stationId, requestGeneration](const Result<QVector<Pile>> &result) {
      if (requestGeneration != pileRequestGeneration_ || selectedStation_.id != stationId) return;
      if (!result.ok) { pileStatus_->setText(result.error); stack_->setCurrentWidget(detail_); return; }
      pileCache_ = result.value;
      if (result.value.isEmpty()) { pileStatus_->setText(QStringLiteral("该站点暂无充电桩")); stack_->setCurrentWidget(detail_); return; }
      int index = 1;
      for (const auto &pile : result.value) {
        auto *pileItem = new QListWidgetItem(QStringLiteral("电桩 %1 · %2 · %3 · %4 · %5 kW\n计费 ¥ %6/度").arg(index++).arg(pile.number).arg(pile.type).arg(pileStatusText(pile.status)).arg(pile.powerKw).arg(pile.priceCentsPerKwh / 100.0), pileList_);
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
    deactivateMapIfVisible();
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
    beginMapRequest();
    stack_->setCurrentWidget(map_);
    mapStationList_->clear();
    mapPoiList_->clear();
    mapService_->setUserId(session_.user().id);
    mapService_->setTargetStationId(selectedStation_.id);
    mapView_->showOffline(socketMode_ ? QStringLiteral("等待服务端返回站点与路线数据")
                                      : QStringLiteral("当前使用本地演示地图"));
    const quint64 requestGeneration = ++stationRequestGeneration_;
    runService<QVector<Station>>([this] { return service_->stations(QString()); }, [this, requestGeneration](const Result<QVector<Station>> &result) {
      if (requestGeneration != stationRequestGeneration_) return;
      if (!result.ok) { mapStatus_->setText(result.error); return; }
      stationCache_ = result.value;
      QVector<MapPoi> markers;
      for (const auto &station : result.value) {
        auto *item = new QListWidgetItem(QStringLiteral("站点：%1 · (%2,%3) · %4").arg(station.name).arg(station.latitude).arg(station.longitude).arg(station.open ? QStringLiteral("营业中") : QStringLiteral("暂停营业")), mapStationList_);
        item->setData(Qt::UserRole, station.id);
        if (isValidCoordinate({station.latitude, station.longitude}))
          markers.push_back({station.id, station.name, station.address, {station.latitude, station.longitude}, station.distanceKm >= 0 ? qRound64(station.distanceKm * 1000.0) : -1, socketMode_ ? MapSource::Server : MapSource::Mock});
      }
      mapView_->setMarkers(markers);
      mapStatus_->setText(selectedStation_.id.isEmpty()
          ? (socketMode_ ? QStringLiteral("请选择目标站点；地图和路线由服务端提供。")
                         : QStringLiteral("请选择目标站点；当前使用本地演示地图。"))
          : QStringLiteral("当前目标：%1").arg(selectedStation_.name));
    });
  }
  void showProfile() {
    if (!session_.isLoggedIn()) {
      showLogin();
      return;
    }
    deactivateMapIfVisible();
    stack_->setCurrentWidget(profile_);
    rechargeButton_->setEnabled(session_.user().status == UserStatus::Active);
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
    deactivateMapIfVisible();
    ++stationRequestGeneration_;
    ++pileRequestGeneration_;
    ++mapRequestGeneration_;
    mapService_->cancelPending();
    session_.clear();
    mapService_->setUserId({});
    mapService_->setTargetStationId({});
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
      session_.beginSession(result.value);
      mapService_->setUserId(result.value.id);
      updateOrderButtons();
      showHome();
      if (result.value.status == UserStatus::Frozen) homeStatus_->setText(QStringLiteral("账号已冻结：可查看资料和订单并完成收尾，预约、开始充电和充值不可用。"));
      else loginStatus_->clear();
    });
  }

  void searchStations() {
    if (!session_.isLoggedIn()) return;
    stationList_->clear();
    homeStatus_->setText(QStringLiteral("正在查询站点…"));
    const QString query = query_->text().trimmed();
    const quint64 requestGeneration = ++stationRequestGeneration_;
    runService<QVector<Station>>([this, query] { return service_->stations(query); }, [this, requestGeneration](const Result<QVector<Station>> &result) {
      if (requestGeneration != stationRequestGeneration_) return;
      if (!result.ok) { homeStatus_->setText(result.error); return; }
      stationCache_ = result.value;
      if (result.value.isEmpty()) { homeStatus_->setText(QStringLiteral("没有匹配站点")); return; }
      int index = 1;
      for (const auto &station : result.value) {
        const QString distance = station.distanceKm >= 0.0 ? QString::number(station.distanceKm) + QStringLiteral(" km") : QStringLiteral("距离待定位");
        auto *item = new QListWidgetItem(QStringLiteral("站点 %1  ·  %2\n%3\n空闲 %4/%5   ·   %6   ·   %7").arg(index++).arg(station.name).arg(station.address).arg(station.availablePiles).arg(station.totalPiles).arg(distance).arg(station.open ? QStringLiteral("营业中") : QStringLiteral("暂停营业")), stationList_);
        QFont font = item->font(); font.setBold(true); item->setFont(font);
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
  void locateMapOrigin() {
    const quint64 requestGeneration = beginMapRequest();
    const quint64 sessionGeneration = session_.generation();
    const QString input = fromLocation_->text().trimmed();
    if (input.contains(',')) {
      const auto parts = input.split(',');
      bool latOk = false, lngOk = false;
      const GeoCoordinate coordinate{parts.size() == 2 ? parts[0].trimmed().toDouble(&latOk) : 0.0, parts.size() == 2 ? parts[1].trimmed().toDouble(&lngOk) : 0.0};
      if (!latOk || !lngOk || !isValidCoordinate(coordinate)) { mapStatus_->setText(QStringLiteral("出发位置坐标无效")); return; }
      mapOrigin_ = coordinate;
      mapStatus_->setText(QStringLiteral("定位成功：%1,%2\n正在查询附近充电站 POI…").arg(coordinate.latitude).arg(coordinate.longitude));
      queryNearbyPois(coordinate, requestGeneration, sessionGeneration, {});
      return;
    }
    mapStatus_->setText(QStringLiteral("正在定位地址并查询附近充电站 POI…"));
    const int radiusMeters = socketMode_ ? 1000 : 5000;
    mapService_->searchNearbyChargingStations(input, radiusMeters,
        [this, requestGeneration, sessionGeneration](const MapResult<QVector<MapPoi>> &result) {
      if (requestGeneration != mapRequestGeneration_ || sessionGeneration != session_.generation()) return;
      if (!result.ok) { mapStatus_->setText(QStringLiteral("定位或附近 POI 查询失败：%1").arg(result.error.userMessage)); return; }
      if (!result.hasResolvedOrigin || !isValidCoordinate(result.resolvedOrigin)) {
        mapStatus_->setText(QStringLiteral("服务端未返回有效的起点坐标")); return;
      }
      mapOrigin_ = result.resolvedOrigin;
      const QString visibleNotice = mapResultNotice(result);
      mapStatus_->setText(visibleNotice.isEmpty()
          ? QStringLiteral("定位成功：%1,%2\n正在查询附近充电站 POI…").arg(mapOrigin_.latitude).arg(mapOrigin_.longitude)
          : QStringLiteral("%1；正在展示附近站点…").arg(visibleNotice));
      renderMapPois(result.value, result.value.isEmpty() ? QStringLiteral("附近暂无充电站 POI")
          : QStringLiteral("已加载 %1 个地图 POI").arg(result.value.size()));
    });
  }

  void queryNearbyPois(const GeoCoordinate &center, quint64 requestGeneration, quint64 sessionGeneration,
                       const QString &priorNotice) {
    const int radiusMeters = socketMode_ ? 1000 : 5000;
    mapService_->searchNearbyChargingStations(center, radiusMeters,
        [this, requestGeneration, sessionGeneration, priorNotice](const MapResult<QVector<MapPoi>> &result) {
      if (requestGeneration != mapRequestGeneration_ || sessionGeneration != session_.generation()) return;
      if (!result.ok) { mapStatus_->setText(QStringLiteral("附近 POI 查询失败：%1").arg(result.error.userMessage)); return; }
      const QString resultNotice = mapResultNotice(result);
      const QString notice = !resultNotice.isEmpty() ? resultNotice : priorNotice;
      const QString status = result.value.isEmpty() ? QStringLiteral("附近暂无充电站 POI")
          : QStringLiteral("已加载 %1 个地图 POI").arg(result.value.size());
      renderMapPois(result.value, notice.isEmpty() ? status : QStringLiteral("%1；%2").arg(notice, status));
    });
  }

  void renderMapPois(const QVector<MapPoi> &pois, const QString &status) {
    mapPois_ = pois; mapPoiList_->clear();
    for (const auto &poi : pois) {
      const QString distance = poi.distanceMeters >= 0
          ? QStringLiteral("%1 km").arg(poi.distanceMeters / 1000.0, 0, 'f', 2)
          : QStringLiteral("距离未知");
      const bool linked = !matchingBusinessStationId(poi, stationCache_).isEmpty();
      auto *item = new QListWidgetItem(QStringLiteral("%1 · %2 · %3%4")
          .arg(poi.title, poi.address, distance, linked ? QStringLiteral(" · 已关联业务站点") : QStringLiteral(" · 仅地图位置")), mapPoiList_);
      item->setData(Qt::UserRole, poi.id);
    }
    QVector<MapPoi> markers = pois;
    for (const auto &station : stationCache_) {
      if (isValidCoordinate({station.latitude, station.longitude}))
          markers.push_back({station.id, station.name, station.address, {station.latitude, station.longitude},
                           station.distanceKm >= 0 ? qRound64(station.distanceKm * 1000.0) : -1,
                           socketMode_ ? MapSource::Server : MapSource::Mock});
    }
    mapView_->setMarkers(markers);
    if (!pois.isEmpty() && pois.first().source != MapSource::Tencent)
      mapView_->showOffline(status);
    mapStatus_->setText(status);
  }

  void queryRoute() {
    if (selectedStation_.id.isEmpty() || !isValidCoordinate({selectedStation_.latitude, selectedStation_.longitude})) { mapStatus_->setText(QStringLiteral("请先选择坐标有效的业务站点")); return; }
    const quint64 requestGeneration = beginMapRequest();
    const quint64 sessionGeneration = session_.generation();
    const auto parts = fromLocation_->text().split(',');
    if (parts.size() == 2) {
      bool latOk = false, lngOk = false;
      const GeoCoordinate coordinate{parts[0].trimmed().toDouble(&latOk), parts[1].trimmed().toDouble(&lngOk)};
      if (!latOk || !lngOk || !isValidCoordinate(coordinate)) { mapStatus_->setText(QStringLiteral("出发位置坐标无效")); return; }
      mapOrigin_ = coordinate; queryRouteFromCoordinates(coordinate, requestGeneration, sessionGeneration, {}); return;
    }
    mapStatus_->setText(QStringLiteral("正在规划地址路线…"));
    mapService_->queryRouteFromAddress(fromLocation_->text().trimmed(),
        {selectedStation_.latitude, selectedStation_.longitude}, routeMode_ && routeMode_->currentIndex() == 1 ? RouteMode::Walking : RouteMode::Driving,
        [this, requestGeneration, sessionGeneration](const MapResult<MapRoute> &result) {
      if (requestGeneration != mapRequestGeneration_ || sessionGeneration != session_.generation()) return;
      if (!result.ok) { mapStatus_->setText(QStringLiteral("路线查询失败：%1").arg(result.error.userMessage)); return; }
      const QString notice = mapResultNotice(result);
      if (result.hasResolvedOrigin && isValidCoordinate(result.resolvedOrigin)) mapOrigin_ = result.resolvedOrigin;
      if (result.value.source != MapSource::Tencent) mapView_->showOffline(notice);
      mapView_->setRoute(result.value);
      const QString source = socketMode_ && result.value.source == MapSource::Mock
          ? QStringLiteral("服务端备用路线") : mapSourceText(result.value.source);
      mapStatus_->setText(QStringLiteral("%1：%2\n距离 %3 km · 预计 %4 分钟%5%6")
          .arg(source, result.value.summary).arg(result.value.distanceMeters / 1000.0, 0, 'f', 2)
          .arg(qRound(result.value.durationSeconds / 60.0))
          .arg(result.value.polyline.isEmpty() ? QStringLiteral("\n未返回折线，仅显示路线摘要") : QString())
          .arg(notice.isEmpty() ? QString() : QStringLiteral("\n%1").arg(notice)));
    });
  }

  void queryRouteFromCoordinates(const GeoCoordinate &origin, quint64 requestGeneration, quint64 sessionGeneration,
                                 const QString &priorNotice) {
    const RouteMode mode = routeMode_ && routeMode_->currentIndex() == 1 ? RouteMode::Walking : RouteMode::Driving;
    const GeoCoordinate destination{selectedStation_.latitude, selectedStation_.longitude};
    mapStatus_->setText(QStringLiteral("正在查询%1路线…").arg(mode == RouteMode::Driving ? QStringLiteral("驾车") : QStringLiteral("步行")));
    mapService_->queryRoute(origin, destination, mode, [this, requestGeneration, sessionGeneration, priorNotice](const MapResult<MapRoute> &result) {
      if (requestGeneration != mapRequestGeneration_ || sessionGeneration != session_.generation()) return;
      if (!result.ok) { mapStatus_->setText(QStringLiteral("路线查询失败：%1").arg(result.error.userMessage)); return; }
      const QString resultNotice = mapResultNotice(result);
      const QString notice = !resultNotice.isEmpty() ? resultNotice : priorNotice;
      if (result.value.source != MapSource::Tencent) mapView_->showOffline(notice);
      mapView_->setRoute(result.value);
      const QString source = socketMode_ && result.value.source == MapSource::Mock
          ? QStringLiteral("服务端备用路线") : mapSourceText(result.value.source);
      mapStatus_->setText(QStringLiteral("%1：%2\n距离 %3 km · 预计 %4 分钟%5%6")
          .arg(source, result.value.summary).arg(result.value.distanceMeters / 1000.0, 0, 'f', 2)
          .arg(qRound(result.value.durationSeconds / 60.0))
          .arg(result.value.polyline.isEmpty() ? QStringLiteral("\n未返回折线，仅显示路线摘要") : QString())
          .arg(notice.isEmpty() ? QString() : QStringLiteral("\n%1").arg(notice)));
    });
  }

  QString mapDisplayText(QString text) const {
    if (!socketMode_) return text;
    text.replace(QRegularExpression(QStringLiteral("server[_ -]?mock"), QRegularExpression::CaseInsensitiveOption),
                 QStringLiteral("服务端备用数据"));
    text.replace(QRegularExpression(QStringLiteral("mock"), QRegularExpression::CaseInsensitiveOption),
                 QStringLiteral("备用数据"));
    return text;
  }

  template <typename T>
  QString mapResultNotice(const MapResult<T> &result) const {
    if (!socketMode_ || !result.warning.isPresent()) return mapDisplayText(result.notice);
    if (result.warning.code == 1410) return QStringLiteral("服务端备用数据");
    if (result.warning.code == 1403 && result.warning.degraded)
      return QStringLiteral("地图上游暂不可用，当前展示服务端缓存结果");
    return mapDisplayText(result.warning.message);
  }

  void updateOrderButtons() {
    const bool hasOrder = !order_.id.isEmpty();
    const bool pending = hasOrder && order_.status == OrderStatus::PendingReservation;
    const bool activeUser = session_.isLoggedIn() && session_.user().status == UserStatus::Active;
    confirmationControls_->setVisible(orderConfirmationMode_ || pending);
    confirmOrderButton_->setVisible(orderConfirmationMode_ || pending);
    reserveButton_->setVisible(orderConfirmationMode_ && !hasOrder);
    confirmOrderButton_->setText(pending ? QStringLiteral("确认预约") : QStringLiteral("确认创建订单"));
    confirmOrderButton_->setEnabled(activeUser && ((orderConfirmationMode_ && !hasOrder && !selectedPile_.id.isEmpty() && selectedPile_.status == PileStatus::Idle) || pending));
    reserveButton_->setEnabled(activeUser && orderConfirmationMode_ && !hasOrder && !selectedPile_.id.isEmpty() && selectedPile_.status == PileStatus::Idle);
    returnPileButton_->setVisible(orderConfirmationMode_ && !hasOrder && !selectedPile_.id.isEmpty());
    cancelReservationButton_->setVisible(!orderConfirmationMode_ && hasOrder && (order_.status == OrderStatus::Reserved || pending));
    cancelReservationButton_->setEnabled(!orderConfirmationMode_ && hasOrder && (order_.status == OrderStatus::Reserved || pending));
    startButton_->setEnabled(activeUser && order_.status == OrderStatus::Reserved);
    directStartButton_->setVisible(orderConfirmationMode_ && !hasOrder);
    directStartButton_->setEnabled(activeUser && orderConfirmationMode_ && !hasOrder && !selectedPile_.id.isEmpty() && selectedPile_.status == PileStatus::Idle);
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
      runService<User>([this, userId] { return service_->profile(userId); }, [this](const Result<User> &profileResult) { if (profileResult.ok) session_.updateUser(profileResult.value); });
      QMessageBox::information(this, QStringLiteral("结算完成"), QStringLiteral("订单已完成，结算成功。"));
    });
  }

  void saveProfile() {
    const QString userId = session_.user().id; const QString name = nickname_->text(); const QString avatar = session_.user().avatarPath;
    runService<User>([this, userId, name, avatar] { return service_->updateProfile(userId, name, avatar); }, [this](const Result<User> &result) {
      if (!result.ok) { profileLabel_->setText(result.error); return; }
      session_.updateUser(result.value); showProfile();
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
    if (!session_.isLoggedIn() || session_.user().status != UserStatus::Active) {
      profileLabel_->setText(QStringLiteral("账号已冻结，当前不可充值。"));
      rechargeButton_->setEnabled(false);
      return;
    }
    const qint64 amountCents = qRound64(rechargeAmount_->value() * 100.0);
    rechargeButton_->setEnabled(false);
    const QString userId = session_.user().id;
    runService<qint64>([this, userId, amountCents] { return service_->recharge(userId, amountCents); }, [this](const Result<qint64> &result) {
      rechargeButton_->setEnabled(session_.isLoggedIn() && session_.user().status == UserStatus::Active);
      if (!result.ok) { profileLabel_->setText(result.error); return; }
      auto user = session_.user(); user.walletBalanceCents = result.value; session_.updateUser(user); showProfile();
    }, [this] { rechargeButton_->setEnabled(session_.isLoggedIn() && session_.user().status == UserStatus::Active); });
  }
};

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  UserWindow window;
  window.show();
  return app.exec();
}

#include "main.moc"
