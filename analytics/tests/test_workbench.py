"""Filter semantics and missing-vs-zero tests, no Spark required."""
import json
import unittest
from unittest.mock import patch
from analytics.api.app import create_app
from analytics.api.app import _filter_data
from analytics.api.snapshot_store import SnapshotStore, SnapshotCorrupt
from analytics.tests.test_api import dash_payload


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
                                       'TENCENT_MAP_KEY': 'must-not-expose'}):
            response = create_app().test_client().get('/runtime-config.js').get_data(as_text=True)
        self.assertIn('"analysisApiBaseUrl": "http://127.0.0.1:61501"', response)
        self.assertNotIn('must-not-expose', response)


if __name__ == '__main__':
    unittest.main()
