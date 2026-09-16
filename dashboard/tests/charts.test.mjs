import test from 'node:test';
import assert from 'node:assert/strict';
import { buildChartPalette, renderOrderFunnel, renderStationClusters, renderServiceControl, renderEquipmentOverview, renderEquipmentRisk } from '../js/charts.js';

test('funnel translates states and station series share the ranked station axis', () => {
  const oldDocument = globalThis.document, oldStyle = globalThis.getComputedStyle, oldEcharts = globalThis.echarts;
  let option;
  globalThis.document = { documentElement: {} };
  globalThis.getComputedStyle = () => ({ getPropertyValue: () => '#aaaaaa' });
  globalThis.echarts = { getInstanceByDom: () => ({ setOption: value => { option = value; } }) };
  try {
    renderOrderFunnel({}, { status_counts: [{label:'completed',count:3},{label:'exception',count:1}] });
    assert.deepEqual(option.series[0].data, [{name:'已完成',value:3},{name:'异常',value:1}]);
    renderStationClusters({}, {pareto:[{station_id:2,name:'乙站',cumulative_share:.8},{station_id:1,name:'甲站',cumulative_share:1}]},
      [{station_id:1,utilization:.1,cluster:'低负荷'},{station_id:2,utilization:.7,cluster:'高负荷'}]);
    assert.deepEqual(option.xAxis.data, ['乙站\n高负荷','甲站\n低负荷']);
    assert.deepEqual(option.series[0].data.map(r=>r.value), [.7,.1]);
    assert.deepEqual(option.series[1].data, [.8,1]);
    renderServiceControl({}, {daily:[{date:'2026-09-01',total:10,completed:8}]},
      {service_control:{baseline_completion_rate:.8,limits:[{date:'2026-09-01',lower:.4,upper:1}]}});
    assert.deepEqual(option.series.map(s=>s.data), [[80],[80],[100],[40]]);
    renderEquipmentOverview({}, {status_counts:{idle:4,fault:1,charging:2}});
    assert.deepEqual(option.xAxis.data, ['空闲','故障','充电中']);
    assert.deepEqual(option.series[0].data.map(r=>r.value), [4,1,2]);
    renderEquipmentRisk({}, {}, {top_anomalies:[{pile_id:1,z_score:2.5,anomaly:true}],threshold:2});
    assert.equal(option.series[0].data[0].value,2.5);
    assert.deepEqual(option.series[0].markLine.data, [{xAxis:2},{xAxis:-2}]);
    renderEquipmentRisk({}, {}, {top_anomalies:[{pile_id:1,z_score:null,total_charge_count:7}]});
    assert.equal(option.series[0].data[0].value,7);
    assert.equal(option.xAxis.name,'次数');
  } finally {
    globalThis.document=oldDocument; globalThis.getComputedStyle=oldStyle; globalThis.echarts=oldEcharts;
  }
});

test('chart palette contains resolved colors rather than CSS var expressions', () => {
  const values = {
    '--night-text': '#E9F5ED',
    '--night-muted-text': '#A6B0B4',
    '--night-decorative': '#2A4554',
    '--night-surface': '#0F1E26',
  };
  const palette = buildChartPalette((name) => values[name]);
  assert.deepEqual(palette, {
    text: '#E9F5ED', muted: '#A6B0B4', divider: '#2A4554', surface: '#0F1E26',
  });
  assert.ok(Object.values(palette).every((value) => !value.startsWith('var(')));
});
