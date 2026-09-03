// 离线 SVG 拓扑渲染器：站点间关联线 + 按“最需关注状态”着色的站点节点。
// 状态语义与 Qt StationTopologyWidget 一致：Fault > Offline > Charging > Reserved > Idle。
import { mapPileStatus } from './status-map.js';
import { projectStations, resolveStationState } from './map-surface.js';

function svgEscape(value) {
  return String(value)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}

export class TopologyMapRenderer {
  constructor() {
    this._container = null;
    this._nodeElements = new Map(); // stationId -> <g>
    this._idByData = new Map(); // data-station-id(字符串) -> 原始 station.id
    this._focused = null;
    this._onActivate = null;
    this._boundClick = this._handleClick.bind(this);
    this._boundKeydown = this._handleKeydown.bind(this);
  }

  /** 构建整幅 SVG：抽象网格底纹、站点弱关联线、五态站点节点（含 <title>）。 */
  mount(container, { stations, piles, onStationActivate }) {
    // 幂等：重复 mount（数据重试）先移除旧监听，避免同容器多份委托
    container.removeEventListener('click', this._boundClick);
    container.removeEventListener('keydown', this._boundKeydown);
    this._container = container;
    this._onActivate = onStationActivate ?? null;
    this._nodeElements.clear();
    this._idByData.clear();
    this._focused = null;

    const list = stations ?? [];
    for (const station of list) {
      this._idByData.set(String(station.id), station.id);
    }
    container.addEventListener('click', this._boundClick);
    container.addEventListener('keydown', this._boundKeydown);
    const width = 1000;
    const height = 620;
    const points = projectStations(list, { width, height, padding: 110 });

    const pilesByStation = new Map();
    for (const pile of piles ?? []) {
      if (!pilesByStation.has(pile.stationId)) pilesByStation.set(pile.stationId, []);
      pilesByStation.get(pile.stationId).push(pile);
    }

    const links = points
      .slice(1)
      .map(({ x, y }, index) => {
        const previous = points[index];
        return `<line x1="${previous.x}" y1="${previous.y}" x2="${x}" y2="${y}" class="topo-link"/>`;
      })
      .join('');

    const nodes = points
      .map(({ x, y, station }) => {
        const state = resolveStationState(station, pilesByStation);
        const meta = mapPileStatus(state);
        const pileCount = station.pileCount ?? 0;
        const labelX = x + 20;
        return `<g class="topo-node" data-station-id="${station.id}" data-motion="${meta.motion}"
     style="color: ${meta.color}" role="button" tabindex="0"
   aria-label="站点 ${svgEscape(station.name)}（${meta.label}）">
  <circle class="topo-halo" cx="${x}" cy="${y}" r="24" data-halo="${meta.motion}"/>
  <circle class="topo-core" cx="${x}" cy="${y}" r="14" fill="${meta.color}"
          stroke="var(--night-bg)" stroke-width="3"/>
  <text class="topo-label" x="${labelX}" y="${y - 10}">${svgEscape(station.name)}</text>
  <text class="topo-meta" x="${labelX}" y="${y + 6}">${pileCount} 桩 · ${meta.label}</text>
  <title>${svgEscape(station.name)}（${meta.label}）：${svgEscape(station.address ?? '')}</title>
</g>`;
      })
      .join('');

    const gridId = 'topo-grid';
    container.innerHTML = `<svg class="topo-map" viewBox="0 0 ${width} ${height}" role="img"
     aria-label="充电站点离线拓扑示意图">
  <defs>
    <pattern id="${gridId}" width="48" height="48" patternUnits="userSpaceOnUse">
      <path d="M 48 0 L 0 0 0 48" fill="none" class="topo-grid-line"/>
    </pattern>
  </defs>
  <rect class="topo-bg" x="0" y="0" width="${width}" height="${height}" fill="var(--night-surface)"/>
  <rect class="topo-grid" x="0" y="0" width="${width}" height="${height}" fill="url(#${gridId})"/>
  <g class="topo-links">${links}</g>
  <g class="topo-nodes">${nodes}</g>
</svg>`;

    for (const element of container.querySelectorAll('.topo-node')) {
      const dataId = element.dataset.stationId;
      this._nodeElements.set(this._idByData.get(dataId) ?? dataId, element);
    }
    return { mode: 'topology' };
  }

  /** 节点激活统一出口：以原始 station.id 回调业务方。 */
  _activateNode(dataId) {
    if (!this._onActivate) return;
    const id = this._idByData.get(String(dataId));
    this._onActivate(id !== undefined ? id : dataId);
  }

  _handleClick(event) {
    const node = event.target.closest?.('.topo-node');
    if (node) this._activateNode(node.dataset.stationId);
  }

  _handleKeydown(event) {
    if (event.key !== 'Enter' && event.key !== ' ') return;
    const node = event.target.closest?.('.topo-node');
    if (!node) return;
    event.preventDefault();
    this._activateNode(node.dataset.stationId);
  }

  /** 高亮站点节点并滚动到可视区域（告警点击联动）。 */
  focusStation(stationId) {
    if (this._focused) this._focused.classList.remove('topo-focused');
    const element = this._nodeElements.get(stationId);
    if (!element) return;
    element.classList.add('topo-focused');
    this._focused = element;
    element.scrollIntoView({ behavior: 'smooth', block: 'center' });
  }

  /** 退出异常聚焦：清除节点高亮与焦点记录。 */
  clearFocus() {
    if (this._focused) {
      this._focused.classList.remove('topo-focused');
      this._focused = null;
    }
  }

  destroy() {
    if (this._container) {
      this._container.removeEventListener('click', this._boundClick);
      this._container.removeEventListener('keydown', this._boundKeydown);
      this._container.innerHTML = '';
    }
    this._container = null;
    this._nodeElements.clear();
    this._idByData.clear();
    this._focused = null;
    this._onActivate = null;
  }
}
