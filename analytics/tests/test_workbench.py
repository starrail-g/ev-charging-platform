"""Filter semantics and missing-vs-zero tests, no Spark required."""
import json
import unittest
from unittest.mock import patch
from analytics.api.app import create_app
from analytics.api.app import _filter_data
from analytics.api.snapshot_store import SnapshotStore, SnapshotCorrupt
from analytics.tests.test_api import dash_payload
from analytics.api.workbench import revenue_trend, station_groups, equipment_activity


class WorkbenchCase(unittest.TestCase):
    def setUp(self):
        self.dash = dash_payload('test')
        self.data = self.dash['data']
        self.data['workbenchFacts'] = {'version': 1, 'totalUsers': 3, 'users': [
            dict(user_id=1, station_id=1, stat_date='2026-09-01', frequency=1, monetary=100,
                 last_settled_at='2026-09-01T10:00:00Z'),
            dict(user_id=1, station_id=1, stat_date='2026-09-02', frequency=1, monetary=200,
                 last_settled_at='2026-09-02T23:00:00Z'),
            dict(user_id=2, station_id=2, stat_date='2026-09-02', frequency=2, monetary=800,
                 last_settled_at='2026-09-02T20:00:00Z')], 'orders': [
            dict(station_id=1, stat_date='2026-09-01', status='completed', start_hour=8,
                 order_count=1, duration_seconds=3600),
            dict(station_id=1, stat_date='2026-09-02', status='cancelled', start_hour=None,
                 order_count=1, duration_seconds=0)]}

    def get(self, start='2026-09-01', station=None):
        return _filter_data(self.data, start, '2026-09-03', station)[1]['analytics']

    def test_rfm_combines_days_and_excludes_other_station(self):
        a = self.get(station=1)
        self.assertEqual(a['users']['total'], 1)
        self.assertEqual(a['users']['repeat_rate'], 1)
        self.assertEqual(a['user_mining']['top_users'][0]['monetary'], 300)
        self.assertEqual(a['user_mining']['top_users'][0]['recency_days'], 0)
        self.assertEqual(a['user_mining']['sample_count'], 1)

    def test_date_filter_changes_frequency_and_global_denominator(self):
        a = self.get('2026-09-02')
        self.assertEqual(a['users']['total'], 3)
        self.assertEqual(a['users']['repeat_users'], 1)
        self.assertEqual(a['users']['segments'][0]['count'], 1)
        self.assertEqual(a['orders']['cancelled'], 1)
        self.assertIsNone(a['orders']['avg_duration_minutes'])

    def test_old_batch_and_empty_station_have_no_fake_statistics(self):
        a = self.get(station=999)
        self.assertEqual(a['users']['total'], 0)
        self.assertIsNone(a['users']['repeat_rate'])
        self.assertIsNone(a['energy']['peak_share'])
        json.dumps(a, allow_nan=False)
        del self.data['workbenchFacts']
        a = self.get()
        self.assertNotIn('users', a)
        self.assertEqual(sum(a['equipment']['status_counts'].values()), 2)
        self.assertIsNone(a['equipment']['restart_count'])

    def test_energy_and_revenue_use_existing_filtered_ads(self):
        a = self.get()
        self.assertAlmostEqual(a['energy']['total_kwh'], 15.001)
        self.assertAlmostEqual(sum(r['energy_kwh'] for r in a['energy']['time_bands']), 15.001)
        self.assertEqual(a['revenue']['total_30d_cents'], 1901)
        self.assertEqual(a['orders']['total'], 2)

    def test_bad_facts_are_rejected_before_serving(self):
        SnapshotStore._validate_dashboard(self.dash, 'test')
        self.data['workbenchFacts']['orders'][0]['start_hour'] = 24
        with self.assertRaises(SnapshotCorrupt):
            SnapshotStore._validate_dashboard(self.dash, 'test')

    def test_runtime_config_passes_explicit_ml_endpoint_only(self):
        with patch.dict('os.environ', {'EV_ANALYSIS_API_BASE_URL': 'http://127.0.0.1:61501',
                                       'TENCENT_MAP_JS_KEY': 'browser-map-test-key',
                                       'TENCENT_MAP_KEY': 'must-not-expose'}):
            response = create_app().test_client().get('/runtime-config.js').get_data(as_text=True)
        self.assertIn('"analysisApiBaseUrl": "http://127.0.0.1:61501"', response)
        self.assertNotIn('must-not-expose', response)
        self.assertIn('"tencentMapJsKey": "browser-map-test-key"', response)

    def test_equipment_scores_use_selected_snapshot_population(self):
        self.data['piles'] = [dict(id=i, stationId=1 if i<5 else 2, code=f'P-{i}',
                                  status='idle', totalChargeCount=0 if i<5 else 12) for i in range(6)]
        mining = self.get()['equipment_mining']
        self.assertEqual(mining['sample_count'], 6)
        self.assertEqual(mining['anomaly_count'], 1)
        self.assertEqual(mining['top_anomalies'][0]['pile_id'], 5)
        self.assertAlmostEqual(mining['top_anomalies'][0]['z_score'], 5**.5)
        self.assertEqual(self.get('2026-09-02')['equipment_mining'], mining)
        selected = self.get(station=1)['equipment_mining']
        self.assertEqual(selected['sample_count'], 5)
        self.assertIsNone(selected['anomaly_count'])
        self.assertTrue(all(r['z_score'] is None for r in selected['top_anomalies']))
        self.assertIsNone(equipment_activity([]))
        self.assertIsNone(equipment_activity([dict(id=1, code='P', stationId=1)]))
        self.assertIsNone(equipment_activity([dict(totalChargeCount=-1)]))
        json.dumps(selected, allow_nan=False)

    def test_regression_uses_calendar_days_and_handles_constant_series(self):
        rows = [{'date': '2026-09-01', 'revenue_cents': 100},
                {'date': '2026-09-03', 'revenue_cents': 300},
                {'date': '2026-09-04', 'revenue_cents': 400}]
        trend = revenue_trend(rows)
        self.assertEqual(trend['slope_cents_per_day'], 100)
        self.assertEqual(trend['r2'], 1)
        self.assertEqual(trend['fitted_cents'], [100, 300, 400])
        self.assertIsNone(revenue_trend(rows[:1]))
        for row in rows:
            row['revenue_cents'] = 0
        self.assertIsNone(revenue_trend(rows)['r2'])

    def test_station_thresholds_and_pareto_use_same_station_axis(self):
        rows = [dict(station_id=i, revenue_cents=value, utilization=util)
                for i, value, util in [(1, 60, .65), (2, 30, .35), (3, 10, .1)]]
        groups = station_groups(rows)
        self.assertEqual([r['cluster'] for r in groups['pareto']], ['高负荷', '均衡', '低负荷'])
        self.assertEqual([r['cumulative_share'] for r in groups['pareto']], [.6, .9, 1])
        self.assertEqual([r['count'] for r in groups['centroids']], [1, 1, 1])
        groups = station_groups([dict(station_id=1, revenue_cents=0, utilization=0)])
        self.assertIsNone(groups['pareto'][0]['cumulative_share'])
        self.assertIsNone(groups['centroids'][0]['avg_utilization'])

    def test_duration_conservation_filtering_and_control_limits(self):
        facts = self.data['workbenchFacts']
        facts['version'] = 2
        for row in facts['orders']:
            row.update(duration_le15=0, duration_15_30=0, duration_30_60=0, duration_gt60=0)
        facts['orders'][0]['duration_30_60'] = 1
        SnapshotStore._validate_dashboard(self.dash, 'test')
        a = self.get()
        self.assertEqual([r['count'] for r in a['service']['duration_buckets']], [0, 0, 1, 0])
        self.assertEqual(a['service']['service_control']['baseline_completion_rate'], .5)
        self.assertEqual(len(a['service']['service_control']['limits']), 2)
        self.assertTrue(all(0 <= r['lower'] <= r['upper'] <= 1 for r in a['service']['service_control']['limits']))
        self.assertEqual(sum(r['count'] for r in self.get('2026-09-02')['service']['duration_buckets']), 0)
        facts['orders'][0]['duration_30_60'] = 2
        with self.assertRaises(SnapshotCorrupt):
            SnapshotStore._validate_dashboard(self.dash, 'test')


if __name__ == '__main__':
    unittest.main()
