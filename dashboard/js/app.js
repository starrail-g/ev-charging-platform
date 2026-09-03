// 主装配：runtime config → demo 数据 → 指标/关注队列/利用率 → 地图（自动降级）→ 图表。
// 任何局部组件失败只显示局部 error 态，不让整个主屏白屏。
import { DemoDataProvider } from './data-provider.js';
import { mapPileStatus } from './status-map.js';
import { statusIconSvg } from './status-icons.js';
import { createDashboardState, deriveOverviewCards } from './dashboard-state.js';
import { MapSurface } from './map-surface.js';
import { TopologyMapRenderer } from './topology-map-renderer.js';
import { TencentMapRenderer } from './tencent-map-renderer.js';
import { renderLoadChart, renderStateDonut, renderRevenueTrend } from './charts.js';
import { renderPageState, statePresentation } from './state-view.js';

const state = createDashboardState({ liveMode: false });
const params = new URLSearchParams(location.search);

function announce(message) {
  const region = document.getElementById('globalState');
  if (region) region.textContent = message;
}

function fmtUtilization(ratio) {
  return `${Math.round((ratio ?? 0) * 100)}%`;
}

function statusChip(protocol) {
  const meta = mapPileStatus(protocol);
  const chip = document.createElement('span');
  chip.className = `status-chip state-${meta.protocol}`;
  chip.innerHTML = `${statusIconSvg(meta.protocol)}<span>${meta.label}</span>`;
  return chip;
}

/* ---------- 左栏指标 ---------- */

function renderOverviewCards(metrics) {
  const strip = document.getElementById('overview-metrics');
  strip.innerHTML = '';
  for (const card of deriveOverviewCards(metrics)) {
    const article = document.createElement('article');
    article.className = 'metric-card';
    const label = document.createElement('span');
    label.className = 'metric-label';
    label.textContent = card.label;
    const value = document.createElement('span');
    value.className = 'metric-value';
    value.textContent = card.value;
    article.append(label, value);
    if (card.sub) {
      const sub = document.createElement('span');
      sub.className = 'metric-value metric-sub';
      sub.textContent = card.sub;
      article.append(sub);
    }
    strip.appendChild(article);
  }
}

/* ---------- 右栏：利用率排行 ---------- */

function renderUtilization(model) {
  document.getElementById('avgUtilization').textContent = fmtUtilization(
    model.metrics.avgStationUtilization
  );
  const list = document.getElementById('utilizationList');
  list.innerHTML = '';
  for (const row of model.stationUtilization ?? []) {
    const item = document.createElement('li');
    item.className = 'util-item';
    const name = document.createElement('span');
    name.className = 'util-name';
    name.textContent = row.name;
    const value = document.createElement('span');
    value.className = 'util-value';
    value.textContent = fmtUtilization(row.utilization);
    const track = document.createElement('span');
    track.className = 'util-track';
    const bar = document.createElement('span');
    bar.className = 'util-bar';
    bar.style.width = `${Math.round(row.utilization * 100)}%`;
    track.append(bar);
    item.append(name, value, track);
    list.appendChild(item);
  }
}

/* ---------- 右栏：实时关注队列 + 异常聚焦模式 ---------- */

function attentionPiles(model) {
  const stationById = new Map(model.stations.map((station) => [station.id, station]));
  const items = [];
  for (const pile of model.piles) {
    if (pile.status === 'fault' || pile.status === 'offline') {
      items.push({ pile, meta: mapPileStatus(pile.status), station: stationById.get(pile.stationId) });
    }
  }
  return items;
}

function clearAlertSelection() {
  document.querySelectorAll('.alert-item').forEach((item) => {
    item.classList.remove('alert-active');
    item.setAttribute('aria-pressed', 'false');
  });
}

function renderFocusDetail(model, item, onClose) {
  const detail = document.getElementById('focus-detail');
  const station = item.station;
  detail.innerHTML = '';
  detail.hidden = false;

  const head = document.createElement('div');
  head.className = 'fd-head';
  const icon = document.createElement('span');
  icon.className = `status-icon state-${item.meta.protocol}`;
  icon.innerHTML = statusIconSvg(item.meta.protocol);
  const title = document.createElement('span');
  title.textContent = station ? station.name : '未知站点';
  const close = document.createElement('button');
  close.type = 'button';
  close.className = 'fd-close';
  close.setAttribute('aria-label', '退出异常聚焦');
  close.textContent = '✕';
  head.append(icon, title, close);
  detail.append(head);

  const dl = document.createElement('dl');
  const rows = [
    ['桩编号', item.pile.code],
    ['状态', item.meta.label],
    ['类型/功率', `${item.pile.type === 'fast' ? '快充' : '慢充'} · ${item.pile.powerKw} kW`],
    ['更新时间', String(model.updatedAt ?? '') + ' (UTC)'],
    ['异常说明', `${item.meta.label}桩需要现场检查或远程处置，请先在桩/站页面核实状态。`],
  ];
  for (const [dt, dd] of rows) {
    const term = document.createElement('dt');
    term.textContent = dt;
    const desc = document.createElement('dd');
    desc.textContent = dd;
    dl.append(term, desc);
  }
  detail.append(dl);
  // 关闭按钮走与 Esc / 再次点击 / 点击无告警站点相同的退出出口，
  // 不得绕过 focused 清空，否则“关闭后单击同一故障”会被误判为再次点击。
  close.addEventListener('click', () => onClose());
}

function renderAlertRail(model, { onActivate }) {
  const list = document.getElementById('alertList');
  list.innerHTML = '';
  const items = attentionPiles(model);
  document.getElementById('attentionCount').textContent = `· ${items.length} 项`;

  for (const item of items) {
    const button = document.createElement('button');
    button.type = 'button';
    button.className = `alert-item state-${item.pile.status}`;
    button.dataset.pileCode = item.pile.code;
    button.dataset.stationId = String(item.station?.id ?? '');
    button.setAttribute(
      'aria-label',
      `${item.pile.code} ${item.meta.label}，位于${item.station?.name ?? '未知站点'}，回车聚焦站点详情`
    );
    button.innerHTML = statusIconSvg(item.pile.status);

    const body = document.createElement('span');
    body.className = 'alert-body';
    const head = document.createElement('span');
    head.className = 'alert-head';
    const code = document.createElement('strong');
    code.textContent = item.pile.code;
    const tag = document.createElement('span');
    tag.className = 'alert-tag';
    tag.textContent = item.meta.label;
    head.append(code, tag);
    const meta = document.createElement('span');
    meta.className = 'alert-meta';
    meta.textContent =
      `${item.station?.name ?? '未知站点'} · ${item.pile.type === 'fast' ? '快充' : '慢充'} · ${item.pile.powerKw} kW`;
    body.append(head, meta);
    button.append(body);
    button.addEventListener('click', () => onActivate(item, button));
    list.appendChild(button);
  }
  if (items.length === 0) {
    const empty = document.createElement('p');
    empty.className = 'rail-empty';
    empty.textContent = '当前无故障或离线桩';
    list.appendChild(empty);
  }
  return items;
}

/* ---------- 局部错误 ---------- */

function renderLocalError(container, message) {
  const panel = document.createElement('div');
  panel.className = 'local-error';
  panel.setAttribute('role', 'alert');
  const title = document.createElement('strong');
  title.textContent = '该区域加载失败';
  const detail = document.createElement('span');
  detail.textContent = message;
  panel.append(title, detail);
  container.replaceChildren(panel);
}

/* ---------- 经营板块页签（紧凑态） ---------- */

// 页签与面板是 index.html 静态结构，只在 boot 绑定一次事件；
// 每次数据渲染后以 showChartTab(targetId) 切换（重试不重复绑监听）。
let chartsById = {};
let currentChartTab = 'chart-load';

function showChartTab(targetId) {
  currentChartTab = targetId;
  const panels = [...document.querySelectorAll('.chart-panel')];
  for (const panel of panels) {
    panel.dataset.tabHidden = panel.dataset.chartPanel === targetId ? 'false' : 'true';
  }
  for (const tab of document.querySelectorAll('.ops-tab')) {
    const isActive = tab.dataset.chartTab === targetId;
    tab.classList.toggle('is-active', isActive);
    tab.setAttribute('aria-pressed', String(isActive));
  }
  requestAnimationFrame(() => {
    chartsById[targetId]?.resize();
  });
}

function setupChartTabsOnce() {
  const tabs = [...document.querySelectorAll('.ops-tab')];
  for (const tab of tabs) {
    tab.addEventListener('click', () => showChartTab(tab.dataset.chartTab));
  }
  showChartTab('chart-load'); // 默认 24h（spec §5.5）；全景态 CSS 忽略 hidden 标记，三图同显
}

/* ---------- 启动 ---------- */

async function boot() {
  const config = window.__EV_CONFIG__ ?? { tencentMapKey: '', demo: true };
  const provider = new DemoDataProvider();

  const shell = document.querySelector('.dashboard-shell');
  const mapContainer = document.getElementById('map-surface');
  const modeBadge = document.getElementById('mapModeBadge');
  const sourceBadge = document.getElementById('sourceBadge');
  const freshnessEl = document.getElementById('freshness');
  const pageStateEl = document.getElementById('pageState');

  // 本地演示注入入口：?state=empty|error|offline|stale
  // 只能改变呈现分支；界面永远保留“演示状态注入”标识，不伪装成真实接口结果。
  const DEMO_STATES = ['empty', 'error', 'offline', 'stale'];
  const demoState = DEMO_STATES.includes(params.get('state')) ? params.get('state') : null;

  const mapSurface = new MapSurface({
    onlineRenderer: new TencentMapRenderer(),
    topologyRenderer: new TopologyMapRenderer(),
    timeoutMs: 5000,
  });

  let lastValidModel = null; // 最后有效快照：error 保留 / offline / stale 展示底座
  let lastValidAt = null;
  let chartInstances = [];
  let currentDeactivateFocus = null; // Esc 只注册一次，读取每轮渲染的最新退出出口
  let revenueRangeDays = 7; // 营收趋势档位（A-02）：7 或 30；静态按钮只绑一次

  /* ---- 页面级状态区 ---- */

  function showPageState(pageStateObject, { blocking = false, freshness = null } = {}) {
    renderPageState(pageStateEl, pageStateObject, {
      demo: demoState !== null,
      blocking,
      freshness,
    });
    document.body.classList.toggle('has-page-banner', !blocking);
    document.body.classList.toggle('page-blocking', blocking);
  }

  function hidePageState() {
    pageStateEl.hidden = true;
    document.body.classList.remove('has-page-banner', 'page-blocking');
  }

  function snapshotLabel() {
    return lastValidAt ? `最后有效快照：${lastValidAt}（UTC）` : null;
  }

  // 初次加载骨架：占位尺寸贴近真实主区，内容出现时不跳动
  function renderSkeleton() {
    const metricStrip = document.getElementById('overview-metrics');
    metricStrip.innerHTML = '';
    for (let index = 0; index < 4; index += 1) {
      const card = document.createElement('article');
      card.className = 'metric-card skel';
      card.setAttribute('aria-hidden', 'true');
      metricStrip.appendChild(card);
    }
    const mapSurfaceEl = document.getElementById('map-surface');
    mapSurfaceEl.classList.add('skel');
    const alertList = document.getElementById('alertList');
    alertList.innerHTML = '';
    for (let index = 0; index < 3; index += 1) {
      const line = document.createElement('span');
      line.className = 'skel skel-line';
      alertList.appendChild(line);
    }
  }

  function hideSkeleton() {
    const mapSurfaceEl = document.getElementById('map-surface');
    if (mapSurfaceEl) mapSurfaceEl.classList.remove('skel');
  }

  /* ---- 主体渲染（数据有效时；可被 loadDashboard 重试再次调用） ---- */

  async function renderContent(model) {
    hideSkeleton();
    renderOverviewCards(model.metrics);
    renderUtilization(model);

    // 异常聚焦状态机：每轮数据渲染重建闭包；退出出口统一经 deactivateFocus
    let focused = null;
    const deactivateFocus = async () => {
      focused = null;
      mapContainer.classList.remove('has-focus');
      document.getElementById('focus-detail').hidden = true;
      clearAlertSelection();
      await mapSurface.clearFocus();
    };
    currentDeactivateFocus = deactivateFocus;

    const activate = async (item, button) => {
      const stationId = item?.station?.id ?? null;
      if (focused && item && focused.pile.code === item.pile.code) {
        // 再次点击当前项 → 退出聚焦
        await deactivateFocus();
        return;
      }
      await deactivateFocus(); // 清残留（含渲染器高亮与 focused）
      focused = item;
      if (button) {
        button.classList.add('alert-active');
        button.setAttribute('aria-pressed', 'true');
      }
      mapContainer.classList.add('has-focus'); // 非相关节点降权
      renderFocusDetail(model, item, deactivateFocus);
      if (stationId) {
        mapSurface.focusStation(stationId).catch(() => {});
      }
    };

    const alertItems = renderAlertRail(model, { onActivate: activate });

    // 地图激活统一入口：拓扑节点点击/回车与腾讯 marker 点击都收敛到这里，
    // 收到的是站点 id（不做业务类型转换）；命中告警项则聚焦，否则退出聚焦。
    const activateStationNode = (stationId) => {
      const target = alertItems.find((item) => item.station?.id === Number(stationId));
      if (target) {
        const button = document.querySelector(`.alert-item[data-station-id="${stationId}"]`);
        activate(target, button).catch(() => {});
      } else {
        deactivateFocus().catch(() => {}); // 点击无告警站点 → 退出聚焦
      }
    };

    // 地图：离线（含 offline 演示注入）强制拓扑，重试时 surface 内部先清理旧渲染器
    const onlineAvailable = typeof navigator !== 'undefined' ? navigator.onLine : true;
    const useOnline = onlineAvailable && demoState !== 'offline';
    const forcedTopology = params.get('map') === 'topology' || demoState === 'offline';
    try {
      const mountResult = await mapSurface.mount(mapContainer, {
        key: config.tencentMapKey,
        online: useOnline,
        forceTopology: forcedTopology,
        stations: model.stations,
        piles: model.piles,
        onStationActivate: activateStationNode,
      });
      // 本轮挂载被更新的重试/销毁取代：容器与状态归新挂载所有，
      // 旧流程放弃后续副作用（不覆盖 modeBadge、不渲染图表）——P1-01。
      if (mountResult.mode === 'superseded') return;
      modeBadge.textContent = mountResult.mode === 'tencent' ? '腾讯地图' : '离线拓扑图';
      if (mountResult.degraded) {
        modeBadge.textContent += '（降级）';
        modeBadge.title = `在线地图不可用，已自动切换拓扑图：${mountResult.reason}`;
      } else {
        modeBadge.removeAttribute('title');
      }
    } catch (error) {
      modeBadge.textContent = '拓扑图';
      renderLocalError(mapContainer, `地图初始化失败：${error?.message ?? '未知原因'}`);
    }

    // 图表（各自独立；失败只影响本面板）
    chartsById = {};
    chartInstances = [];
    const revenueCentsSeries = revenueRangeDays === 30
      ? (model.revenue30dCents ?? [])
      : model.revenue7dCents;
    for (const [id, factory] of [
      ['chart-load', () => renderLoadChart(document.getElementById('chart-load'), model)],
      ['chart-states', () => renderStateDonut(document.getElementById('chart-states'), model.metrics.counts)],
      ['chart-revenue', () => renderRevenueTrend(
        document.getElementById('chart-revenue'), revenueCentsSeries, model.updatedAt)],
    ]) {
      try {
        const chart = factory();
        chartsById[id] = chart;
        chartInstances.push(chart);
      } catch (error) {
        renderLocalError(document.getElementById(id), error?.message ?? '图表初始化失败');
      }
    }
    showChartTab(currentChartTab); // 保持当前页签并完成首个 resize
  }

  /* ---- 数据加载与六态呈现（可重试；保留最后有效快照） ---- */

  async function loadDashboard() {
    announce(state.loading().title);
    const hasSnapshot = lastValidModel !== null;
    if (hasSnapshot) {
      showPageState(state.loading(), { blocking: false, freshness: snapshotLabel() });
    } else {
      renderSkeleton(); // 稳定高度骨架，避免地图区跳动
      showPageState(state.loading(), { blocking: false });
    }

    const result = await provider.load();

    // 演示注入 error：有数据底座但呈现阻断错误分支
    if (demoState === 'error' && result.ok) {
      const failed = state.reject('演示注入：模拟数据服务不可用');
      lastValidModel = null;
      lastValidAt = null;
      hideSkeleton();
      sourceBadge.textContent = '演示状态';
      announce(failed.title);
      showPageState(failed, { blocking: true });
      return;
    }

    if (!result.ok) {
      const failed = state.reject(result.error?.message ?? 'demo.json 加载失败');
      sourceBadge.textContent = '加载失败';
      announce(failed.title);
      if (hasSnapshot) {
        // 快照仍在：非阻断错误横幅，主体继续展示最后有效数据
        showPageState(failed, { blocking: false, freshness: snapshotLabel() });
      } else {
        hideSkeleton();
        showPageState(failed, { blocking: true });
      }
      return;
    }

    const model = result.data;
    const isEmpty = !model || !Array.isArray(model.stations) || model.stations.length === 0;
    if (isEmpty || demoState === 'empty') {
      lastValidModel = null;
      lastValidAt = null;
      hideSkeleton();
      sourceBadge.textContent = demoState === 'empty' ? '演示状态' : '暂无数据';
      announce(state.empty().title);
      showPageState(state.empty(), { blocking: true });
      return;
    }

    // 数据有效：记录快照并渲染主体，随后选择非阻断呈现分支
    lastValidModel = model;
    lastValidAt = model.updatedAt ?? null;
    const isOffline =
      demoState === 'offline' || (typeof navigator !== 'undefined' && !navigator.onLine);
    const resolved = state.resolve({ stations: model.stations, fetchedAt: model.updatedAt });
    const isStale = demoState === 'stale' || resolved.isStale;
    sourceBadge.textContent =
      demoState !== null
        ? '演示状态'
        : isOffline
          ? '本地演示数据'
          : config.demo
            ? '演示数据'
            : '实时数据';

    await renderContent(model);

    if (isOffline) {
      // 数据仍可用：非阻断横幅，明确本地演示数据 + 离线拓扑（无重试按钮，网络恢复自动消失）
      const offlineState = { ...state.offline(), action: null };
      showPageState(offlineState, { blocking: false, freshness: snapshotLabel() });
      freshnessEl.textContent = '离线：本地演示数据 + 离线拓扑（自动降级）';
      announce(offlineState.title);
    } else if (isStale) {
      showPageState(state.stale(lastValidAt), { blocking: false, freshness: snapshotLabel() });
      freshnessEl.textContent = `最后更新 ${lastValidAt ?? '—'}（UTC）· 数据可能已过期`;
      announce('数据可能已过期，请刷新');
    } else {
      hidePageState();
      freshnessEl.textContent = `更新于 ${model.updatedAt}（UTC）· ${result.source} 数据源`;
      announce(`数据已就绪：${model.metrics.totalPiles} 台桩`);
    }
  }

  /* ---- 一次性装配 ---- */

  // 状态区重试按钮：直接重跑 loadDashboard，保留最后有效快照，不做整页刷新
  pageStateEl.querySelector('[data-page-state-action]').addEventListener('click', () => {
    loadDashboard().catch(() => {});
  });

  document.addEventListener('keydown', (event) => {
    if (event.key === 'Escape' && currentDeactivateFocus) {
      currentDeactivateFocus().catch(() => {});
    }
  });

  setupChartTabsOnce();

  // 营收趋势 7 日/30 日档位：静态按钮只绑一次；切换后按最近有效快照重渲该图
  const revenueRangeGroup = document.querySelector('.chart-range');
  if (revenueRangeGroup) {
    for (const btn of revenueRangeGroup.querySelectorAll('.range-btn')) {
      btn.addEventListener('click', () => {
        const days = Number(btn.dataset.rangeDays);
        if (!Number.isInteger(days) || days === revenueRangeDays) return;
        revenueRangeDays = days;
        for (const other of revenueRangeGroup.querySelectorAll('.range-btn')) {
          const isActive = other === btn;
          other.classList.toggle('is-active', isActive);
          other.setAttribute('aria-pressed', String(isActive));
        }
        if (!lastValidModel) return; // 尚无有效快照（加载失败态），按钮保持但不重渲
        const centsSeries = days === 30
          ? (lastValidModel.revenue30dCents ?? [])
          : lastValidModel.revenue7dCents;
        try {
          const chart = renderRevenueTrend(
            document.getElementById('chart-revenue'), centsSeries, lastValidModel.updatedAt);
          chartsById['chart-revenue'] = chart;
          if (!chartInstances.includes(chart)) chartInstances.push(chart);
        } catch (error) {
          renderLocalError(document.getElementById('chart-revenue'),
                           error?.message ?? '图表切换失败');
        }
      });
    }
  }

  // 自适应：容器与窗口变化时重排所有活跃图表
  if (typeof ResizeObserver !== 'undefined') {
    const observer = new ResizeObserver(() => {
      for (const chart of chartInstances) chart.resize();
    });
    observer.observe(document.getElementById('operations-strip'));
  }
  window.addEventListener('resize', () => {
    for (const chart of chartInstances) chart.resize();
  });

  await loadDashboard();
}

boot().catch((error) => {
  announce('主屏初始化失败');
  const failed = state.reject(error?.message ?? '未知错误');
  const pageStateEl = document.getElementById('pageState');
  const sourceBadge = document.getElementById('sourceBadge');
  if (sourceBadge) sourceBadge.textContent = '启动失败';
  if (pageStateEl) {
    renderPageState(pageStateEl, failed, { blocking: true });
    document.body.classList.add('page-blocking');
  }
  const shell = document.querySelector('.dashboard-shell');
  if (shell) shell.style.display = 'none';
});
