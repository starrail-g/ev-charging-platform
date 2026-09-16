import test from 'node:test';
import assert from 'node:assert/strict';
import { TencentMapRenderer } from '../js/tencent-map-renderer.js';

test('Tencent map fits station bounds, resizes after visibility changes and releases resources', async () => {
  const calls = [], previous = globalThis.ResizeObserver;
  let resize;
  globalThis.ResizeObserver = class {
    constructor(callback) { resize = callback; }
    observe() {}
    disconnect() { calls.push('disconnect'); }
  };
  const renderer = new TencentMapRenderer();
  renderer._loadSdk = async () => ({
    LatLng: class { constructor(lat, lng) { this.lat = lat; this.lng = lng; } },
    LatLngBounds: class { constructor() { this.points = []; } extend(p) { this.points.push(p); } },
    Map: class { fitBounds(bounds) { calls.push(bounds.points.length); } resize() { calls.push('resize'); } destroy() { calls.push('destroy'); } },
    MarkerStyle: class {},
    MultiMarker: class { setMap(map) { calls.push(map); } },
  });
  try {
    await renderer.mount({}, {key:'test-key', stations:[{id:1,latitude:22.5,longitude:114}, {id:2,latitude:22.6,longitude:114.1}], piles:[]});
    resize([{contentRect:{width:800,height:500}}]);
    resize([{contentRect:{width:800,height:500}}]);
    resize([{contentRect:{width:0,height:0}}]);
    resize([{contentRect:{width:800,height:500}}]);
    renderer.destroy();
    renderer.destroy();
    assert.deepEqual(calls,[2,'resize',2,'resize',2,'disconnect',null,'destroy']);
  } finally { globalThis.ResizeObserver = previous; }
});
