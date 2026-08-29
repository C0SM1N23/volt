#!/usr/bin/env python3
"""Checks that an exposition VOLT produced is one Prometheus would accept.

The point is independence: the exporter and this script share nothing but the
file, so a mistake in how VOLT writes the format cannot hide behind the same
mistake in how it reads it back. What is checked is the grammar of the text
exposition format, version 0.0.4, plus the rules that format puts on top of the
grammar: one HELP and one TYPE per family, no sample before its family is
declared, and a summary that carries the parts a summary must carry.
"""

import math
import re
import sys

METRIC_NAME = re.compile(r"^[a-zA-Z_:][a-zA-Z0-9_:]*$")
LABEL_NAME = re.compile(r"^[a-zA-Z_][a-zA-Z0-9_]*$")

KNOWN_TYPES = {"counter", "gauge", "histogram", "summary", "untyped"}

# Suffixes a series may carry beyond its family name, per metric type.
TYPE_SUFFIXES = {
    "summary": {"", "_sum", "_count"},
    "histogram": {"", "_bucket", "_sum", "_count"},
    "counter": {""},
    "gauge": {""},
    "untyped": {""},
}


class Failure(Exception):
    """A way the file is not a valid exposition."""


def parse_value(text):
    """Returns the float a sample value spells, including the three specials."""
    if text == "+Inf":
        return math.inf
    if text == "-Inf":
        return -math.inf
    if text == "NaN":
        return math.nan
    try:
        return float(text)
    except ValueError as error:
        raise Failure(f"value {text!r} is not a number") from error


def split_labels(text):
    """Returns the label pairs inside a brace group, honouring escapes."""
    labels = []
    index = 0
    while index < len(text):
        equals = text.find("=", index)
        if equals < 0:
            raise Failure(f"label without a value in {text!r}")
        name = text[index:equals].strip()
        if not LABEL_NAME.match(name):
            raise Failure(f"{name!r} is not a label name")
        if equals + 1 >= len(text) or text[equals + 1] != '"':
            raise Failure(f"label {name} is not quoted")

        value = []
        index = equals + 2
        closed = False
        while index < len(text):
            character = text[index]
            if character == "\\":
                if index + 1 >= len(text):
                    raise Failure("a label value ends in a backslash")
                escape = text[index + 1]
                if escape not in ('"', "\\", "n"):
                    raise Failure(f"\\{escape} is not an escape a label value allows")
                value.append("\n" if escape == "n" else escape)
                index += 2
                continue
            if character == '"':
                closed = True
                index += 1
                break
            value.append(character)
            index += 1
        if not closed:
            raise Failure(f"label {name} has no closing quote")

        labels.append((name, "".join(value)))
        if index < len(text) and text[index] == ",":
            index += 1
        elif index < len(text):
            raise Failure(f"labels are not comma separated in {text!r}")
    return labels


def parse_sample(line):
    """Returns the series name, its labels and its value."""
    brace = line.find("{")
    if brace < 0:
        parts = line.split(" ")
        if len(parts) < 2:
            raise Failure(f"sample {line!r} has no value")
        return parts[0], [], parse_value(parts[1])

    name = line[:brace]
    close = line.rfind("}")
    if close < brace:
        raise Failure(f"sample {line!r} has no closing brace")
    labels = split_labels(line[brace + 1 : close])
    remainder = line[close + 1 :].strip().split(" ")
    if not remainder or not remainder[0]:
        raise Failure(f"sample {line!r} has no value")
    return name, labels, parse_value(remainder[0])


def family_of(series, families):
    """Returns the declared family a series belongs to, or None."""
    for name, declared_type in families.items():
        if series == name:
            return name, declared_type, ""
        for suffix in TYPE_SUFFIXES[declared_type]:
            if suffix and series == name + suffix:
                return name, declared_type, suffix
    return None


def check(path):
    """Raises Failure unless the file at `path` is a valid exposition."""
    with open(path, "r", encoding="utf-8") as handle:
        text = handle.read()

    if text and not text.endswith("\n"):
        raise Failure("the exposition does not end with a newline")

    families = {}
    helped = set()
    typed = set()
    sampled = set()
    seen_series = set()
    samples = 0

    for number, raw in enumerate(text.split("\n"), start=1):
        line = raw
        if not line:
            continue
        try:
            if line.startswith("# HELP "):
                name = line[len("# HELP ") :].split(" ")[0]
                if not METRIC_NAME.match(name):
                    raise Failure(f"{name!r} is not a metric name")
                if name in helped:
                    raise Failure(f"{name} has more than one HELP line")
                helped.add(name)
            elif line.startswith("# TYPE "):
                parts = line[len("# TYPE ") :].split(" ")
                if len(parts) != 2:
                    raise Failure("a TYPE line names one metric and one type")
                name, declared = parts
                if not METRIC_NAME.match(name):
                    raise Failure(f"{name!r} is not a metric name")
                if declared not in KNOWN_TYPES:
                    raise Failure(f"{declared!r} is not a metric type")
                if name in typed:
                    raise Failure(f"{name} has more than one TYPE line")
                if name in sampled:
                    raise Failure(f"{name} is typed after it was already sampled")
                typed.add(name)
                families[name] = declared
            elif line.startswith("#"):
                continue
            else:
                series, labels, _ = parse_sample(line)
                if not METRIC_NAME.match(series):
                    raise Failure(f"{series!r} is not a metric name")
                found = family_of(series, families)
                if found is None:
                    raise Failure(f"{series} was sampled without a TYPE line")
                family, declared_type, suffix = found
                sampled.add(family)
                if declared_type == "summary" and suffix == "":
                    quantiles = [v for (n, v) in labels if n == "quantile"]
                    if len(quantiles) != 1:
                        raise Failure(f"{series} is a summary line without a quantile")
                    phi = parse_value(quantiles[0])
                    if not 0.0 <= phi <= 1.0:
                        raise Failure(f"quantile {phi} is outside zero to one")
                key = (series, tuple(sorted(labels)))
                if key in seen_series:
                    raise Failure(f"{series} repeats one label set")
                seen_series.add(key)
                samples += 1
        except Failure as error:
            raise Failure(f"line {number}: {error}\n  {line}") from error

    missing_help = typed - helped
    if missing_help:
        raise Failure(f"typed without help: {sorted(missing_help)}")

    for name, declared in families.items():
        if declared != "summary":
            continue
        if not any(series == name + "_sum" for (series, _) in seen_series):
            raise Failure(f"summary {name} has no _sum series")
        if not any(series == name + "_count" for (series, _) in seen_series):
            raise Failure(f"summary {name} has no _count series")

    if samples == 0:
        raise Failure("the exposition carries no samples at all")
    return samples, len(families)


def main(argv):
    """Validates each file named on the command line."""
    if len(argv) < 2:
        print("usage: validate_prometheus.py <exposition.txt>...", file=sys.stderr)
        return 2
    for path in argv[1:]:
        try:
            samples, families = check(path)
        except Failure as error:
            print(f"{path}: {error}", file=sys.stderr)
            return 1
        print(f"{path}: {samples} samples in {families} families")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
