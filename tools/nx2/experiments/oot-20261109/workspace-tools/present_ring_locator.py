"""Locate a full present ring using an independently sampled, stable frame counter."""


class IncompleteWindowError(ValueError):
    """The present counter may have advanced before its timestamp was published."""


def select_input_window(ring, first_input_counter, frames, before_counter, after_counter,
                        request_start_ns, capture_end_ns):
    capacity = len(ring)
    if not 0 < frames < capacity or not 0 <= before_counter <= after_counter:
        raise ValueError('Invalid counter window or capacity')
    last = first_input_counter + frames
    if first_input_counter < after_counter - capacity + 1:
        raise ValueError('Requested timestamp window was overwritten')
    if last > before_counter:
        raise IncompleteWindowError('The final present counter has not been observed')
    times = [ring[counter % capacity] for counter in range(first_input_counter, last + 1)]
    if any(a >= b for a, b in zip(times, times[1:])):
        raise IncompleteWindowError('Timestamp publication is incomplete or nonmonotonic')
    if times[0] < request_start_ns or times[-1] > capture_end_ns:
        raise IncompleteWindowError('Timestamp window is outside the capture clock bounds')
    return times


def locate_ring(values, counter, now, capacity=4096):
    valid = [now - 86400000000000 < value <= now for value in values]
    matches = []
    run = 0
    for end, yes in enumerate(valid):
        run = run + 1 if yes else 0
        if run < capacity:
            continue
        start = end - capacity + 1
        ring = values[start:end + 1]
        oldest = (counter + 1) % capacity
        ordered = ring[oldest:] + ring[:oldest]
        if now - ordered[-1] > 1000000000:
            continue
        if all(a < b for a, b in zip(ordered, ordered[1:])):
            matches.append(start)
    if len(matches) != 1:
        raise ValueError(f'Expected one counter-aligned complete ring, found {len(matches)}')
    return matches[0]
