#!/usr/bin/env python3
"""Checks that a time-triggered table really satisfies its constraints.

The point is independence. This script shares nothing with the generator or
with the C++ checker beyond the two files it reads, so a misunderstanding
about what a rule means cannot hide behind the same misunderstanding on the
other side. Every rule is re-derived here from the constraints, in the most
literal way: expand each task into its releases, then compare every pair.

Usage:
    validate_schedule.py <constraints.yaml> <table.yaml>

Exits 0 when the table is sound, 1 when it is not, 2 when the input cannot
be read. Every violation is printed, not just the first, because whoever is
fixing a table wants the whole list.
"""

import sys
from math import gcd

import yaml


def load(path):
    with open(path, encoding="utf-8") as handle:
        return yaml.safe_load(handle)


def hyperperiod_of(tasks):
    """Least common multiple of the periods, the length the table repeats on."""
    multiple = 1
    for task in tasks:
        period = task["period_us"]
        if period <= 0:
            raise ValueError(f"task {task['id']} has a period of {period} us")
        multiple = multiple * period // gcd(multiple, period)
    return multiple


def overlaps(first, second):
    """Whether two half-open windows share an instant."""
    return first[0] < second[1] and second[0] < first[1]


def inside_cyclic(instant, start, length, hyperperiod):
    """Whether `instant` lies in [start, start + length) around the cycle."""
    if length <= 0:
        return False
    if length >= hyperperiod:
        return True
    return (instant - start) % hyperperiod < length


def check_windows(tasks, slots, hyperperiod, problems):
    """Every task runs once per period, inside the period it belongs to."""
    for task in tasks:
        mine = sorted(
            (slot for slot in slots if slot["task"] == task["id"]),
            key=lambda slot: slot["offset_us"],
        )
        expected = hyperperiod // task["period_us"]
        if len(mine) != expected:
            problems.append(
                f"task {task['id']} has {len(mine)} slots, expected {expected}"
            )
            continue
        for release, slot in enumerate(mine):
            window_start = release * task["period_us"]
            window_end = window_start + task["period_us"]
            if slot["budget_us"] != task["wcet_us"]:
                problems.append(
                    f"task {task['id']} slot at {slot['offset_us']} us reserves "
                    f"{slot['budget_us']} us, declared {task['wcet_us']} us"
                )
            if slot["offset_us"] < window_start:
                problems.append(
                    f"task {task['id']} release {release} starts at "
                    f"{slot['offset_us']} us, before its window opens at {window_start} us"
                )
            if slot["offset_us"] + slot["budget_us"] > window_end:
                problems.append(
                    f"task {task['id']} release {release} runs past {window_end} us"
                )


def check_pairs(table, exclusions, problems):
    """One lane runs one job, and excluded tasks never share an instant."""
    slots = table["slots"]
    excluded = {
        frozenset((rule["first"], rule["second"])) for rule in exclusions
    }
    for left in range(len(slots)):
        for right in range(left + 1, len(slots)):
            one, other = slots[left], slots[right]
            if not overlaps(
                (one["offset_us"], one["offset_us"] + one["budget_us"]),
                (other["offset_us"], other["offset_us"] + other["budget_us"]),
            ):
                continue
            if one["lane"] == other["lane"]:
                problems.append(
                    f"lane {one['lane']} runs tasks {one['task']} and "
                    f"{other['task']} at once, around {one['offset_us']} us"
                )
            if frozenset((one["task"], other["task"])) in excluded:
                problems.append(
                    f"tasks {one['task']} and {other['task']} are excluded but "
                    f"overlap around {one['offset_us']} us"
                )


def check_precedences(table, precedences, hyperperiod, problems):
    """No consumer starts inside a producer's window or its gap."""
    for rule in precedences:
        producers = [s for s in table["slots"] if s["task"] == rule["before"]]
        consumers = [s for s in table["slots"] if s["task"] == rule["after"]]
        for producer in producers:
            blackout = producer["budget_us"] + rule["min_gap_us"]
            for consumer in consumers:
                if inside_cyclic(
                    consumer["offset_us"], producer["offset_us"], blackout, hyperperiod
                ):
                    problems.append(
                        f"task {rule['after']} starts at {consumer['offset_us']} us, "
                        f"inside the blackout of task {rule['before']} at "
                        f"{producer['offset_us']} us ({blackout} us wide)"
                    )


def check_lanes(table, problems):
    """Slot lanes exist, and the table is in the order it promises."""
    for slot in table["slots"]:
        if not 0 <= slot["lane"] < table["lane_count"]:
            problems.append(
                f"slot at {slot['offset_us']} us names lane {slot['lane']}, "
                f"but the table has {table['lane_count']}"
            )
    ordered = sorted(table["slots"], key=lambda s: (s["offset_us"], s["lane"]))
    if ordered != table["slots"]:
        problems.append("slots are not sorted by offset then lane")


def main(argv):
    if len(argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    try:
        constraints = load(argv[1])
        table = load(argv[2])
        tasks = constraints["tasks"]
        hyperperiod = hyperperiod_of(tasks)
    except (OSError, yaml.YAMLError, KeyError, TypeError, ValueError) as failure:
        print(f"could not read the input: {failure}", file=sys.stderr)
        return 2

    problems = []
    if table.get("hyperperiod_us") != hyperperiod:
        problems.append(
            f"hyperperiod is {table.get('hyperperiod_us')} us, "
            f"the periods give {hyperperiod} us"
        )
    known = {task["id"] for task in tasks}
    for slot in table.get("slots", []):
        if slot["task"] not in known:
            problems.append(f"slot names unknown task {slot['task']}")

    if not problems:
        check_lanes(table, problems)
        check_windows(tasks, table["slots"], hyperperiod, problems)
        check_pairs(table, constraints.get("exclusions") or [], problems)
        check_precedences(
            table, constraints.get("precedences") or [], hyperperiod, problems
        )

    for problem in problems:
        print(f"invalid: {problem}", file=sys.stderr)
    if problems:
        return 1
    print(f"table is sound: {len(table['slots'])} slots over {hyperperiod} us")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
