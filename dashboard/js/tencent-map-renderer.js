// 腾讯地图渲染器（可选）：仅在配置了开发密钥且联网时使用。
// 密钥只在运行时进入 SDK script URL；禁止把 URL / key / 配置打印到控制台。
// SDK 加载失败、超时或初始化异常统一以枚举 reason 抛给 MapSurface 做拓扑降级。
import { mapPileStatus } from './status-map.js';
import { resolveStationState } from './map-surface.js';

function dotDataUri(color) {
  const svg =
    `<svg xmlns='http://www.w3.org/2000/svg' width='30' height='30'>` +
    `<circle cx='15' cy='15' r='10' fill='${color}' stroke='#0A1110' stroke-width='3'/></svg>`;
  return `data:image/svg+xml;charset=utf-8,${encodeURIComponent(svg)}`;
}

export class TencentMapRenderer {
  constructor({ scriptTimeoutMs = 5000 } = {}) {
    this._timeoutMs = scriptTimeoutMs;
    this._map = null;
    this._markers = null;
    this._container = null;
    this._stations = [];
  }

  /** 加载 SDK：复用已存在的 TMap；否则动态注入 script 并等待（不打印任何内容）。 */
  _loadSdk(key) {
    return new Promise((resolve, reject) => {
      if (typeof globalThis.TMap !== 'undefined') {
        resolve(globalThis.TMap);
        return;
      }
      const script = document.createElement('script');
      script.async = true;
      script.src = `https://map.qq.com/api/gljs?v=1.exp&key=${encodeURIComponent(key)}`;
      let finished = false;
      const cleanup = () => {
        clearTimeout(timer);
        script.removeEventListener('load', onLoad);
        script.removeEventListener('error', onError);
        script.remove();
      };
      const onLoad = () => {
        if (finished) return;
        finished = true;
        if (typeof globalThis.TMap === 'undefined') {
          cleanup();
          reject(new Error('sdk-load-failed'));
          return;
        }
        cleanup();
        resolve(globalThis.TMap);
      };
      const onError = () => {
        if (finished) return;
        finished = true;
        cleanup();
        reject(new Error('sdk-load-failed'));
      };
      const timer = setTimeout(() => {
        if (finished) return;
        finished = true;
        cleanup();
        reject(new Error('sdk-load-timeout'));
      }, this._timeoutMs);
      script.addEventListener('load', onLoad);
      script.addEventListener('error', onError);
      document.head.appendChild(script);
    });
  }

  async mount(container, { key, stations, piles }) {
    if (!key) throw new Error('renderer-failed');
    let TMap;
    try {
      TMap = await this._loadSdk(key);
      this._container = container;
      this._stations = stations ?? [];
      const pilesByStation = new Map();
      for (const pile of piles ?? []) {
        if (!pilesByStation.has(pile.stationId)) pilesByStation.set(pile.stationId, []);
        pilesByStation.get(pile.stationId).push(pile);
      }
      const centerStation = this._stations[0];
      const center = centerStation
        ? new TMap.LatLng(centerStation.latitude, centerStation.longitude)
        : new TMap.LatLng(39.908, 116.397); // 默认北京中心（无站点时的兜底）
      this._map = new TMap.Map(container, {
        center,
        zoom: centerStation ? 11 : 10,
        viewMode: '2D',
      });
      this._markers = this._buildMarkers(TMap, pilesByStation);
      return { mode: 'tencent' };
    } catch (error) {
      this._reset(container);
      throw error;
    }
  }

  _buildMarkers(TMap, pilesByStation) {
    const styles = {};
    const styleIds = new Set();
    const geometries = [];
    for (const station of this._stations) {
      const state = resolveStationState(station, pilesByStation);
      const meta = mapPileStatus(state);
      if (!styleIds.has(state)) {
        styleIds.add(state);
        styles[`st_${state}`] = new TMap.MarkerStyle({
          width: 30,
          height: 30,
          anchor: { x: 15, y: 15 },
          src: dotDataUri(meta.color),
        });
      }
      geometries.push({
        id: String(station.id),
        styleId: `st_${state}`,
        position: new TMap.LatLng(station.latitude, station.longitude),
        properties: { stationId: station.id, name: station.name },
      });
    }
    return new TMap.MultiMarker({
      map: this._map,
      styles,
      geometries,
    });
  }

  focusStation(stationId) {
    if (!this._map) return;
    const station = this._stations.find((item) => item.id === stationId);
    if (!station) return;
    this._map.setCenter(new TMap.LatLng(station.latitude, station.longitude));
    this._map.setZoom(13);
  }

  _reset(container) {
    this._map = null;
    this._markers = null;
    if (container) container.innerHTML = '';
  }

  destroy() {
    this._reset(this._container);
    this._container = null;
    this._stations = [];
  }
}
