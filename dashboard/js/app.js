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

function exitFocus(mapContainer) {
  mapContainer.classList.remove('has-focus');
  document.getElementById('focus-detail').hidden = true;
  clearAlertSelection();
}

function renderFocusDetail(model, item) {
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
  close.addEventListener('click', () => {
    exitFocus(document.getElementById('map-surface'));
  });
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

function setupChartTabs(chartsById) {
  const tabs = [...document.querySelectorAll('.ops-tab')];
  const panels = [...document.querySelectorAll('.chart-panel')];
  const show = (targetId) => {
    for (const panel of panels) {
      panel.dataset.tabHidden = panel.dataset.chartPanel === targetId ? 'false' : 'true';
    }
    for (const tab of tabs) {
      const isActive = tab.dataset.chartTab === targetId;
      tab.classList.toggle('is-active', isActive);
      tab.setAttribute('aria-pressed', String(isActive));
    }
    requestAnimationFrame(() => {
      chartsById[targetId]?.resize();
    });
  };
  for (const tab of tabs) {
    tab.addEventListener('click', () => show(tab.dataset.chartTab));
  }
  show('chart-load'); // 默认 24h（spec §5.5）；全景态 CSS 忽略 hidden 标记，三图同显
}

/* ---------- 启动 ---------- */

async function boot() {
  const config = window.__EV_CONFIG__ ?? { tencentMapKey: '', demo: true };
  const provider = new DemoDataProvider();

  announce(state.loading().title);
  const result = await provider.load();
  if (!result.ok) {
    const failed = state.reject(result.error?.message ?? 'demo.json 加载失败');
    renderLocalError(document.getElementById('overview-metrics'), failed.detail);
    renderLocalError(document.getElementById('map-surface'), failed.detail);
    document.getElementById('sourceBadge').textContent = '加载失败';
    announce(failed.title);
    return;
  }
  const model = result.data;
  const content = state.resolve({ stations: model.stations, fetchedAt: model.updatedAt });
  if (content.kind !== 'content') {
    renderLocalError(document.getElementById('overview-metrics'), '演示数据为空');
    announce('演示数据为空');
    return;
  }
  announce(`数据已就绪：${model.metrics.totalPiles} 台桩`);
  document.getElementById('freshness').textContent =
    `更新于 ${model.updatedAt}（UTC）· ${result.source} 数据源`;

  // 左栏指标 + 右栏利用率
  renderOverviewCards(model.metrics);
  renderUtilization(model);

  // 地图
  const mapContainer = document.getElementById('map-surface');
  const modeBadge = document.getElementById('mapModeBadge');
  const mapSurface = new MapSurface({
    onlineRenderer: new TencentMapRenderer(),
    topologyRenderer: new TopologyMapRenderer(),
    timeoutMs: 5000,
  });
  try {
    const mountResult = await mapSurface.mount(mapContainer, {
      key: config.tencentMapKey,
      online: typeof navigator !== 'undefined' ? navigator.onLine : true,
      forceTopology: params.get('map') === 'topology',
      stations: model.stations,
      piles: model.piles,
    });
    modeBadge.textContent = mountResult.mode === 'tencent' ? '腾讯地图' : '离线拓扑图';
    if (mountResult.degraded) {
      modeBadge.textContent += '（降级）';
      modeBadge.title = `在线地图不可用，已自动切换拓扑图：${mountResult.reason}`;
    }
  } catch {
    modeBadge.textContent = '拓扑图';
    renderLocalError(mapContainer, '地图初始化失败，请刷新重试');
  }

  // 异常聚焦状态（当前选中桩）
  let focused = null;

  const activate = (item, button) => {
    const stationId = item?.station?.id ?? null;
    if (focused && focused.pile.code === item?.pile?.code) {
      // 再次点击当前项 → 退出聚焦
      exitFocus(mapContainer);
      focused = null;
      return;
    }
    exitFocus(mapContainer); // 清残留
    clearAlertSelection();
    focused = item;
    if (button) {
      button.classList.add('alert-active');
      button.setAttribute('aria-pressed', 'true');
    }
    mapContainer.classList.add('has-focus'); // 非相关节点降权
    renderFocusDetail(model, item);
    if (stationId) {
      mapSurface.focusStation(stationId).catch(() => {});
    }
  };

  const alertItems = renderAlertRail(model, { onActivate: activate });

  // 地图节点激活 → 反向聚焦对应站点的关注项
  const activateStationNode = (stationId) => {
    const target = alertItems.find((item) => item.station?.id === Number(stationId));
    if (target) {
      activate(target, document.querySelector(`.alert-item[data-station-id="${stationId}"]`));
    } else {
      exitFocus(mapContainer);
      focused = null;
    }
  };
  mapContainer.addEventListener('click', (event) => {
    const node = event.target.closest?.('.topo-node');
    if (node) activateStationNode(node.dataset.stationId);
  });
  mapContainer.addEventListener('keydown', (event) => {
    if (event.key !== 'Enter' && event.key !== ' ') return;
    const node = event.target.closest?.('.topo-node');
    if (!node) return;
    event.preventDefault();
    activateStationNode(node.dataset.stationId);
  });
  document.addEventListener('keydown', (event) => {
    if (event.key === 'Escape' && focused) {
      exitFocus(mapContainer);
      focused = null;
    }
  });

  // 图表（各自独立；失败只影响本面板）
  const chartInstances = [];
  const chartsById = {};
  for (const [id, factory] of [
    ['chart-load', () => renderLoadChart(document.getElementById('chart-load'), model)],
    ['chart-states', () => renderStateDonut(document.getElementById('chart-states'), model.metrics.counts)],
    ['chart-revenue', () => renderRevenueTrend(document.getElementById('chart-revenue'), model.revenue7dCents)],
  ]) {
    try {
      const chart = factory();
      chartsById[id] = chart;
      chartInstances.push(chart);
    } catch (error) {
      renderLocalError(document.getElementById(id), error?.message ?? '图表初始化失败');
    }
  }
  setupChartTabs(chartsById);

  // 自适应
  if (typeof ResizeObserver !== 'undefined') {
    const observer = new ResizeObserver(() => {
      for (const chart of chartInstances) chart.resize();
    });
    observer.observe(document.getElementById('operations-strip'));
  }
  window.addEventListener('resize', () => {
    for (const chart of chartInstances) chart.resize();
  });
}

boot().catch((error) => {
  announce('主屏初始化失败');
  const shell = document.querySelector('.dashboard-shell');
  if (shell) renderLocalError(shell, error?.message ?? '未知错误');
});
