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
    this._focusedStationId = null;
    this._markerClickHandler = null;
  }

  /** 加载 SDK：复用已存在的 TMap；否则动态注入 script 并等待（不打印任何内容）。
   *  signal 中止时移除 script、清理 timer 并 reject，绝不留下晚到的 onLoad。 */
  _loadSdk(key, signal) {
    return new Promise((resolve, reject) => {
      if (typeof globalThis.TMap !== 'undefined') {
        resolve(globalThis.TMap);
        return;
      }
      const script = document.createElement('script');
      script.async = true;
      script.src = `https://map.qq.com/api/gljs?v=1.exp&key=${encodeURIComponent(key)}`;
      let finished = false;
      const fail = (reason) => {
        if (finished) return;
        finished = true;
        cleanup();
        reject(new Error(reason));
      };
      const cleanup = () => {
        clearTimeout(timer);
        script.removeEventListener('load', onLoad);
        script.removeEventListener('error', onError);
        script.remove();
        signal?.removeEventListener('abort', onAbort);
      };
      const onLoad = () => {
        if (finished) return;
        finished = true;
        cleanup();
        if (typeof globalThis.TMap === 'undefined') {
          reject(new Error('sdk-load-failed'));
          return;
        }
        if (signal?.aborted) {
          reject(new Error(signal.reason ?? 'sdk-load-timeout'));
          return;
        }
        resolve(globalThis.TMap);
      };
      const onError = () => fail('sdk-load-failed');
      const onAbort = () => fail(signal?.reason ?? 'sdk-load-timeout');
      const timer = setTimeout(() => fail('sdk-load-timeout'), this._timeoutMs);
      script.addEventListener('load', onLoad);
      script.addEventListener('error', onError);
      signal?.addEventListener('abort', onAbort);
      document.head.appendChild(script);
    });
  }

  async mount(container, { key, stations, piles, signal, onStationActivate }) {
    if (!key) throw new Error('renderer-failed');
    let TMap;
    try {
      TMap = await this._loadSdk(key, signal);
      // 超时后 MapSurface 已切到拓扑；SDK 才返回时不得再初始化地图
      if (signal?.aborted) throw new Error(signal.reason ?? 'sdk-load-timeout');
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
      this._bindMarkerClicks(onStationActivate);
      return { mode: 'tencent' };
    } catch (error) {
      this._release();
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
    this._focusedStationId = stationId;
    this._map.setCenter(new TMap.LatLng(station.latitude, station.longitude));
    this._map.setZoom(13);
  }

  /** 退出异常聚焦：腾讯 marker 无持久高亮样式，仅清除记录（保持当前视野）。 */
  clearFocus() {
    this._focusedStationId = null;
  }

  /** marker 点击 → 统一激活回调（参数为原始 station.id，转换交给业务方）。 */
  _bindMarkerClicks(onStationActivate) {
    this._unbindMarkerClicks();
    if (!this._markers || typeof onStationActivate !== 'function') return;
    this._markerClickHandler = (event) => {
      const stationId = event?.geometry?.properties?.stationId;
      if (stationId != null) onStationActivate(stationId);
    };
    this._markers.on('click', this._markerClickHandler);
  }

  _unbindMarkerClicks() {
    if (this._markers && this._markerClickHandler) {
      this._markers.off('click', this._markerClickHandler);
    }
    this._markerClickHandler = null;
  }

  /** 释放本渲染器持有的地图与 marker 引用；绝不改动共享容器
   *  （容器生命周期由 MapSurface 单点管理）。 */
  _release() {
    this._unbindMarkerClicks();
    this._map = null;
    this._markers = null;
  }

  /** 释放自身全部引用（含容器记录）。容器内容由调用方决定是否清空。 */
  destroy() {
    this._release();
    this._container = null;
    this._stations = [];
    this._focusedStationId = null;
  }
}
