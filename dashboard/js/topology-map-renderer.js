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
    this._focused = null;
  }

  /** 构建整幅 SVG：抽象网格底纹、站点弱关联线、五态站点节点（含 <title>）。 */
  mount(container, { stations, piles }) {
    this._container = container;
    this._nodeElements.clear();
    this._focused = null;

    const list = stations ?? [];
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
      this._nodeElements.set(Number(element.dataset.stationId), element);
    }
    return { mode: 'topology' };
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

  destroy() {
    if (this._container) this._container.innerHTML = '';
    this._container = null;
    this._nodeElements.clear();
    this._focused = null;
  }
}
