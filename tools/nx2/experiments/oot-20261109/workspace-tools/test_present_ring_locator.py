import struct
import unittest
from present_ring_locator import IncompleteWindowError, locate_ring, select_input_window
from oot_benchmark_inputs import balanced_steps


class RingLocatorTests(unittest.TestCase):
    def test_balanced_route_keeps_yaw_separate_from_translation(self):
        steps = balanced_steps(1920)
        self.assertEqual(sum(step['frames'] for step in steps), 1920)
        for step in steps:
            self.assertFalse(any(step['left_stick']) and any(step['right_stick']))
        self.assertEqual(sum(step['buttons'] == ['X'] for step in steps), 8)
        self.assertEqual(sum(step['buttons'] == ['Y'] for step in steps), 8)

    @staticmethod
    def ring_at(counter):
        ring = [0] * 4096
        for frame in range(counter - 4095, counter + 1):
            ring[frame % 4096] = 100000000000 + frame * 17000000
        return ring

    def test_input_window_starts_at_eligibility_counter(self):
        ring = self.ring_at(5120)
        times = select_input_window(ring, 5000, 120, 5120, 5120,
                                    ring[4999 % 4096], ring[5120 % 4096])
        self.assertEqual(times[0], ring[5000 % 4096])
        self.assertEqual(times[-1], ring[5120 % 4096])
        self.assertEqual(len(times), 121)

    def test_input_window_crosses_ring_wrap(self):
        ring = self.ring_at(8202)
        times = select_input_window(ring, 8190, 12, 8202, 8203,
                                    ring[8189 % 4096], ring[8202 % 4096])
        self.assertEqual(times, [100000000000 + frame * 17000000
                                 for frame in range(8190, 8203)])

    def test_incremented_counter_with_unpublished_timestamp_requires_retry(self):
        ring = self.ring_at(5120)
        capture_end = ring[5120 % 4096]
        ring[5120 % 4096] -= 4096 * 17000000
        with self.assertRaises(IncompleteWindowError):
            select_input_window(ring, 5000, 120, 5120, 5120,
                                ring[4999 % 4096], capture_end)

    def test_overwritten_window_is_rejected_even_when_slots_are_monotonic(self):
        ring = self.ring_at(9300)
        with self.assertRaisesRegex(ValueError, 'overwritten'):
            select_input_window(ring, 5000, 120, 9300, 9300,
                                1, ring[9300 % 4096])

    def test_adjacent_timestamps_are_not_part_of_the_ring(self):
        count = 5000
        ordered = [10000000000 + i * 17000000 for i in range(4096)]
        slots = [0] * 4096
        for frame, timestamp in zip(range(count - 4095, count + 1), ordered):
            slots[frame % 4096] = timestamp
        values = [0] * 8 + slots + [ordered[-1] + i * 17000000 for i in range(1, 101)]
        self.assertEqual(locate_ring(values, count, ordered[-1] + 1000000), 8)

    def test_partial_ring_is_rejected(self):
        with self.assertRaises(ValueError):
            locate_ring([0] * 4096, 5000, 10000000000)


if __name__ == '__main__':
    unittest.main()
