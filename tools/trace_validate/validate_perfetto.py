#!/usr/bin/env python3
"""Checks that a trace exported by volt-trace is one a viewer can open.

The point is independence: the exporter and this script share nothing but the
file, so a mistake in how VOLT writes the format cannot hide behind the same
mistake in how it reads it back. What is verified here is the structure the
Chrome trace format requires and the invariants VOLT's own events add on top:
every interval closes, and every flow arrow has both ends.
"""

import json
import sys
from collections import defaultdict

# Phases the exporter emits, from the Chrome trace format.
PHASE_METADATA = "M"
PHASE_BEGIN = "B"
PHASE_END = "E"
PHASE_INSTANT = "i"
PHASE_FLOW_OUT = "s"
PHASE_FLOW_IN = "f"

KNOWN_PHASES = {
    PHASE_METADATA,
    PHASE_BEGIN,
    PHASE_END,
    PHASE_INSTANT,
    PHASE_FLOW_OUT,
    PHASE_FLOW_IN,
}


def failures_in(trace):
    """Yields one message per structural problem found."""
    if not isinstance(trace, dict):
        yield "the document is not an object"
        return

    events = trace.get("traceEvents")
    if not isinstance(events, list):
        yield "traceEvents is missing or is not a list"
        return

    open_intervals = defaultdict(list)
    flows_started = set()
    flows_ended = set()
    saw_process_name = False
    saw_thread_name = False

    for position, event in enumerate(events):
        if not isinstance(event, dict):
            yield f"event {position} is not an object"
            continue

        for required in ("name", "ph", "pid"):
            if required not in event:
                yield f"event {position} has no {required!r}"

        phase = event.get("ph")
        if phase not in KNOWN_PHASES:
            yield f"event {position} has unknown phase {phase!r}"

        if phase == PHASE_METADATA:
            saw_process_name = saw_process_name or event.get("name") == "process_name"
            saw_thread_name = saw_thread_name or event.get("name") == "thread_name"
            continue

        if not isinstance(event.get("ts"), (int, float)):
            yield f"event {position} has no numeric timestamp"

        track = (event.get("pid"), event.get("tid"))
        if phase == PHASE_BEGIN:
            open_intervals[track].append(event.get("name"))
        elif phase == PHASE_END:
            if not open_intervals[track]:
                yield f"event {position} closes an interval that was never opened"
            else:
                open_intervals[track].pop()
        elif phase == PHASE_FLOW_OUT:
            flows_started.add(event.get("id"))
        elif phase == PHASE_FLOW_IN:
            flows_ended.add(event.get("id"))

    for track, still_open in open_intervals.items():
        if still_open:
            yield f"track {track} left {len(still_open)} interval(s) open: {still_open}"

    for orphan in sorted(flows_ended - flows_started):
        yield f"flow {orphan} arrives without having been sent"

    if not saw_process_name:
        yield "no process_name metadata, so the viewer has no process to show"
    if not saw_thread_name:
        yield "no thread_name metadata, so every track would be unlabelled"

    if "voltDroppedRecords" not in trace:
        yield "the dropped-record count is missing, so a gap could pass for a quiet period"


def main(argv):
    if len(argv) != 2:
        print("usage: validate_perfetto.py <trace.json>")
        return 2

    try:
        with open(argv[1], "r", encoding="utf-8") as handle:
            trace = json.load(handle)
    except (OSError, json.JSONDecodeError) as error:
        print(f"cannot read {argv[1]}: {error}")
        return 1

    problems = list(failures_in(trace))
    for problem in problems:
        print(f"invalid: {problem}")
    if problems:
        return 1

    events = [event for event in trace["traceEvents"] if event.get("ph") != PHASE_METADATA]
    print(
        f"valid: {len(events)} events, "
        f"{len(trace['traceEvents']) - len(events)} metadata entries, "
        f"{trace['voltDroppedRecords']} dropped"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
