#!/usr/bin/env python3
import unittest

from pile_simulator import (
    DeterministicPlanner,
    PileSnapshot,
    StationSnapshot,
    generate_piles,
    generator_digest,
    transition,
)


class ContractVectorTest(unittest.TestCase):
    def test_generation_vectors(self):
        expected = [
            ("demo-2026-09", "poi-001", "4387d8ad9230ac66c58ece8cad123e5bcf9cad2a225a3436c35d471325b029c2", [
                ("fast", 60, 130, "idle"), ("fast", 120, 130, "fault"),
                ("slow", 11, 130, "idle"), ("slow", 11, 130, "idle")]),
            ("demo-2026-09", "poi-002", "470d337567a7fd7e862871d0473d4f50f163e870fe29ac0a68636e4f365cf23e", [
                ("fast", 180, 150, "idle"), ("fast", 60, 130, "idle"),
                ("slow", 22, 130, "idle"), ("slow", 11, 90, "idle")]),
            ("test-seed", "shenyang-42", "f91b6b81a4b211b546bdfeadc6f4e97ad08d992bc7ad44a0a3e26c1c3dde9675", [
                ("fast", 60, 110, "idle"), ("fast", 180, 150, "idle"),
                ("fast", 120, 110, "idle"), ("fast", 60, 130, "idle"),
                ("slow", 11, 130, "offline"), ("slow", 7, 130, "offline"),
                ("slow", 22, 110, "idle")]),
        ]
        for seed, poi, digest, rows in expected:
            self.assertEqual(generator_digest(seed, "tencent", poi), digest)
            self.assertEqual([
                (item.pile_type, item.power_kw, item.price_fen_per_kwh, item.status)
                for item in generate_piles(seed, "tencent", poi)
            ], rows)

    def test_transition_vectors(self):
        self.assertEqual(transition("demo-2026-09", 1842, 4201, "idle"), (58, "idle"))
        self.assertEqual(transition("demo-2026-09", 1842, 4202, "fault"), (34, "fault"))
        self.assertEqual(transition("test-seed", 7, 101, "offline"), (39, "offline"))

    def test_planner_is_sqlite_free_and_stable(self):
        station = StationSnapshot(
            42, 7, "demo-2026-09", "active",
            (PileSnapshot(4201, 42, "idle", True, False),
             PileSnapshot(4202, 42, "charging", True, True)),
        )
        planner = DeterministicPlanner("sim-1", "demo-2026-09")
        self.assertEqual(planner.proposal(1842, [station]), planner.proposal(1842, [station]))
        self.assertEqual(planner.proposal(1842, [station])["expected_versions"], {"42": 7})
        self.assertEqual(planner.proposal(1842, [station])["changes"], [])


if __name__ == "__main__":
    unittest.main()
