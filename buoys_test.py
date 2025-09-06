import unittest
from datetime import datetime

from buoys import Observation, Observations


class BuoysTest(unittest.TestCase):
    def test_one_per_hour_one_happy_path(self):
        obss = Observations([
            Observation(datetime(year=2025, month=9, day=1, hour=12), 7, 15),
            Observation(datetime(year=2025, month=9, day=1, hour=11), 6, 15),
            Observation(datetime(year=2025, month=9, day=1, hour=10), 5, 15)
        ])
        oph = list(obss.one_per_hour(3))
        self.assertEqual(3, len(oph))
        self.assertEqual([5, 6, 7], [obs.wave_height for obs in oph])

    def test_one_per_hour_one_input(self):
        obss = Observations([
            Observation(datetime(year=2025, month=9, day=1, hour=12), 5, 15)
        ])
        oph = list(obss.one_per_hour(3))
        self.assertEqual(3, len(oph))
        self.assertEqual([5, 5, 5], [obs.wave_height for obs in oph])

    def test_one_per_hour_gap(self):
        obss = Observations([
            Observation(datetime(year=2025, month=9, day=1, hour=12), 5, 15),
            Observation(datetime(year=2025, month=9, day=1, hour=10), 7, 15)
        ])
        oph = list(obss.one_per_hour(3))
        self.assertEqual(3, len(oph))
        self.assertEqual([7, 5, 5], [obs.wave_height for obs in oph])

    def test_one_per_hour_dups(self):
        obss = Observations([
            Observation(datetime(year=2025, month=9, day=1, hour=12, minute=30), 5, 15),
            Observation(datetime(year=2025, month=9, day=1, hour=12, minute=20), 5, 15),
            Observation(datetime(year=2025, month=9, day=1, hour=12, minute=10), 5, 15),
            Observation(datetime(year=2025, month=9, day=1, hour=11), 6, 15),
            Observation(datetime(year=2025, month=9, day=1, hour=10), 7, 15)
        ])
        oph = list(obss.one_per_hour(3))
        self.assertEqual(3, len(oph))
        self.assertEqual([7, 6, 5], [obs.wave_height for obs in oph])


if __name__ == '__main__':
    unittest.main()
