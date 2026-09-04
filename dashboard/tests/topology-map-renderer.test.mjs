import test from 'node:test';
import assert from 'node:assert/strict';
import { TopologyMapRenderer } from '../js/topology-map-renderer.js';

// 最小容器桩：渲染器只需 innerHTML + 事件监听 + 节点查询；
// 注入回归断言 SVG 源串层面（评审 P1-02 验收口径）。
function fakeContainer() {
  return {
    innerHTML: '',
    addEventListener() {},
    removeEventListener() {},
    querySelectorAll() {
      return [];
    },
  };
}

const STATION_BASE = {
  name: '东软园区',
  address: '沈阳市浑南区',
  longitude: 123.4395,
  latitude: 41.7331,
};

test('renders numeric station ids into an intact data-station-id attribute', () => {
  const container = fakeContainer();
  const renderer = new TopologyMapRenderer();
  renderer.mount(container, {
    stations: [{ ...STATION_BASE, id: 7 }],
    piles: [],
  });
  assert.ok(container.innerHTML.includes('data-station-id="7"'));
  renderer.destroy();
});

test('renders string-code station ids (S1) untouched', () => {
  const container = fakeContainer();
  const renderer = new TopologyMapRenderer();
  renderer.mount(container, {
    stations: [{ ...STATION_BASE, id: 'S1' }],
    piles: [],
  });
  assert.ok(container.innerHTML.includes('data-station-id="S1"'));
  renderer.destroy();
});

test('malicious station id cannot break the attribute boundary (P1-02)', () => {
  const container = fakeContainer();
  const renderer = new TopologyMapRenderer();
  const evil = '7" onmouseover="alert(1)';
  renderer.mount(container, {
    stations: [{ ...STATION_BASE, id: evil }],
    piles: [],
  });
  // 注入成功的标志是裸引号闭合属性后再接新属性（`" onmouseover=`）；
  // 转义后引号变 &quot;，该裸模式不应出现在 SVG 源串中。
  assert.ok(
    !container.innerHTML.includes('" onmouseover='),
    'attribute-breaking quote + injected handler must not survive'
  );
  assert.ok(
    container.innerHTML.includes('&quot;'),
    'quotes must be entity-escaped inside the attribute'
  );
  assert.ok(
    !container.innerHTML.includes(`data-station-id="${evil}`),
    'raw attack string must not appear as the attribute value'
  );
  renderer.destroy();
});

test('malicious station id with angle brackets cannot forge extra nodes (P1-02)', () => {
  const container = fakeContainer();
  const renderer = new TopologyMapRenderer();
  const evil = '1"><script>alert(1)</script><g class="topo-node"';
  renderer.mount(container, {
    stations: [{ ...STATION_BASE, id: evil }],
    piles: [],
  });
  assert.ok(!container.innerHTML.includes('<script>'), 'script tag must not survive');
  // 只有一个站点 → 只能有一个真实节点；伪造的 `<g class="topo-node"` 若存活会变成第二个
  const nodeCount = (container.innerHTML.match(/<g class="topo-node"/g) ?? []).length;
  assert.equal(nodeCount, 1, 'forged extra node must not appear');
  assert.ok(
    container.innerHTML.includes('&lt;') && container.innerHTML.includes('&gt;'),
    'angle brackets must be entity-escaped'
  );
  renderer.destroy();
});

test('station name and address stay escaped alongside the id', () => {
  const container = fakeContainer();
  const renderer = new TopologyMapRenderer();
  renderer.mount(container, {
    stations: [
      {
        ...STATION_BASE,
        id: 'S1',
        name: '站" onmouseover="alert(1)<测试>',
        address: '&<img src=x onerror=alert(1)>',
      },
    ],
    piles: [],
  });
  // name 出现在 aria-label 属性中：裸引号闭合注入不得存活
  assert.ok(!container.innerHTML.includes('" onmouseover='));
  // address 出现在 <title> 文本中：裸元素标签不得存活
  assert.ok(!container.innerHTML.includes('<img'));
  assert.ok(container.innerHTML.includes('&quot;') && container.innerHTML.includes('&lt;'));
  renderer.destroy();
});
