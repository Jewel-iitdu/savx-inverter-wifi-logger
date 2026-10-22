#!/usr/bin/env python3
"""Generate web/src/codes.ts and web/src/alertMetrics.ts from shared/.

shared/codes.json is the single source of truth for QMOD mode letters, QPIGS
status bits, QPIWS warning/fault bits, LCD / REVO code tables and event types
(d1 section 3.9). shared/alert-metrics.json is the metric catalogue of the
owner's alert rules (research/r8-email-alerts.md section 4.3); it is checked
against firebase/database.rules.json (metric ids, energy set, value ranges) so
the PWA's client-side validation can never drift from the database rules.

This script only writes the web files; the firmware owns its own generator for
firmware/include/codes.h (and, later, include/alert_metrics.h).

Usage (from anywhere):
    python3 tools/gen_codes.py            # write web/src/codes.ts and web/src/alertMetrics.ts
    python3 tools/gen_codes.py --check    # exit 1 if either file is stale or the rules disagree
    python3 tools/gen_codes.py --stdout   # print codes.ts instead of writing

Only the Python standard library is used.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "shared" / "codes.json"
DST = ROOT / "web" / "src" / "codes.ts"
METRICS_SRC = ROOT / "shared" / "alert-metrics.json"
METRICS_DST = ROOT / "web" / "src" / "alertMetrics.ts"
RULES = ROOT / "firebase" / "database.rules.json"

BIT_KINDS = ("W", "F", "FA1", "FC", "WF", "R")


class CodesError(ValueError):
    pass


def _check_codes(name: str, table: dict) -> None:
    seen = set()
    for e in table.get("codes", []):
        code = e.get("code")
        if not isinstance(code, str) or not code:
            raise CodesError(f"{name}: entry without a string 'code': {e!r}")
        if not isinstance(e.get("text"), str) or not e["text"]:
            raise CodesError(f"{name}: code {code} has no text")
        if code in seen:
            raise CodesError(f"{name}: duplicate code {code}")
        seen.add(code)


def validate(data: dict) -> None:
    """Fail loudly on structural mistakes so a typo never reaches the app."""
    for key in ("qmod", "statusBits", "qpiws", "lcdFault", "voltronicFaultExtra",
                "lcdWarning", "revoWarning", "revoFault", "severity", "eventTypes"):
        if key not in data:
            raise CodesError(f"missing top-level key '{key}'")

    letters = [m["code"] for m in data["qmod"]]
    if len(letters) != len(set(letters)) or any(len(c) != 1 for c in letters):
        raise CodesError("qmod: codes must be unique single letters")

    for v in data["qpiws"]["variants"]:
        bits = v["bits"]
        if len(bits) != v["length"]:
            raise CodesError(f"qpiws {v['id']}: {len(bits)} bits listed, length says {v['length']}")
        for i, b in enumerate(bits):
            if len(b) != 3 or b[0] != i:
                raise CodesError(f"qpiws {v['id']}: bit row {i} is {b!r}; rows must be [index, kind, text] in order")
            if b[1] not in BIT_KINDS:
                raise CodesError(f"qpiws {v['id']}: bit {i} has unknown kind {b[1]!r}")
        fc = v.get("faultCodeAt")
        if fc is not None:
            if len(fc) != 2 or any(bits[i][1] != "FC" for i in fc):
                raise CodesError(f"qpiws {v['id']}: faultCodeAt {fc} must point at two 'FC' bits")

    for name in ("lcdFault", "voltronicFaultExtra", "lcdWarning", "revoWarning", "revoFault"):
        _check_codes(name, data[name])
    overlap = {e["code"] for e in data["lcdFault"]["codes"]} & {e["code"] for e in data["voltronicFaultExtra"]["codes"]}
    if overlap:
        raise CodesError(f"voltronicFaultExtra repeats LCD codes {sorted(overlap)}")

    sevs = [s["sev"] for s in data["severity"]]
    if sevs != list(range(6)):
        raise CodesError("severity must list sev 0..5 in order")
    known = {t["type"] for t in data["eventTypes"]["types"]}
    for t in data["eventTypes"]["types"]:
        if not (0 <= t["sev"] <= 5) or len(t["type"]) > 24:
            raise CodesError(f"eventTypes: bad entry {t!r} (sev 0-5, type <= 24 chars per the RTDB rules)")
        if t.get("clears") and t["clears"] not in known:
            raise CodesError(f"eventTypes: {t['type']} clears unknown type {t['clears']}")


def js(value) -> str:
    """JSON is valid TypeScript for literals; keep non-ASCII readable."""
    return json.dumps(value, ensure_ascii=False, indent=2)


def code_map(table: dict, with_secondary: bool = False) -> dict:
    out = {}
    for e in table["codes"]:
        if with_secondary:
            out[e["code"]] = {"text": e["text"], **({"secondary": True} if e.get("secondary") else {})}
        else:
            out[e["code"]] = e["text"]
    return out


def render(data: dict) -> str:
    qmod = {m["code"]: {"name": m["name"], "short": m["short"]} for m in data["qmod"]}
    variants = [
        {
            "id": v["id"],
            "length": v["length"],
            "label": v["label"],
            "faultCodeAt": v.get("faultCodeAt"),
            "bits": [[b[0], b[1], b[2]] for b in v["bits"]],
        }
        for v in data["qpiws"]["variants"]
    ]
    event_types = {
        t["type"]: {"sev": t["sev"], "label": t["label"], **({"clears": t["clears"]} if t.get("clears") else {})}
        for t in data["eventTypes"]["types"]
    }
    severity = [s["name"] for s in data["severity"]]

    parts = [
        "// GENERATED FILE - do not edit by hand.",
        "// Source: shared/codes.json. Regenerate with: python3 tools/gen_codes.py",
        "// LCD codes, QPIWS bits and REVO QALL codes use different numbering:",
        "// always decode a code with the table named by its source (d1 K13).",
        "/* eslint-disable */",
        "",
        "export type BitKind = 'W' | 'F' | 'FA1' | 'FC' | 'WF' | 'R';",
        "export type QpiwsBit = readonly [index: number, kind: BitKind, text: string];",
        "export interface QpiwsVariant {",
        "  id: string;",
        "  length: number;",
        "  label: string;",
        "  faultCodeAt: readonly [number, number] | null;",
        "  bits: readonly QpiwsBit[];",
        "}",
        "export interface RevoCode { text: string; secondary?: boolean }",
        "",
        f"export const CODES_VERSION = {int(data['version'])};",
        "",
        f"export const CODE_SOURCES: Record<string, string> = {js(data['sources'])};",
        "",
        f"export const QMOD: Record<string, {{ name: string; short: string }}> = {js(qmod)};",
        "",
        f"export const ST_BITS: readonly {{ pos: number; bit: string; name: string }}[] = {js(data['statusBits']['st'])};",
        "",
        f"export const ST2_BITS: readonly {{ pos: number; bit: string; name: string }}[] = {js(data['statusBits']['st2'])};",
        "",
        f"export const QPIWS_VARIANTS: readonly QpiwsVariant[] = {js(variants)} as unknown as readonly QpiwsVariant[];",
        "",
        f"export const LCD_FAULT: Record<string, string> = {js(code_map(data['lcdFault']))};",
        "",
        f"export const VOLTRONIC_FAULT_EXTRA: Record<string, string> = {js(code_map(data['voltronicFaultExtra']))};",
        "",
        f"export const LCD_WARNING: Record<string, string> = {js(code_map(data['lcdWarning']))};",
        "",
        f"export const REVO_WARNING: Record<string, RevoCode> = {js(code_map(data['revoWarning'], True))};",
        "",
        f"export const REVO_FAULT: Record<string, RevoCode> = {js(code_map(data['revoFault'], True))};",
        "",
        f"export const SEVERITY_NAMES: readonly string[] = {js(severity)};",
        "",
        f"export const EVENT_TYPES: Record<string, {{ sev: number; label: string; clears?: string }}> = {js(event_types)};",
        "",
    ]
    return "\n".join(parts)


# ------------------------------------------------------------------ alert metric catalogue

SCOPES = ("inst", "day", "month")


def validate_metrics(cat: dict) -> None:
    if not isinstance(cat.get("version"), int) or cat["version"] < 1:
        raise CodesError("alert-metrics: 'version' must be an integer >= 1")
    groups = [g["id"] for g in cat["groups"]]
    if len(groups) != len(set(groups)):
        raise CodesError("alert-metrics: duplicate group id")
    seen = set()
    for m in cat["metrics"]:
        mid = m.get("id")
        if not isinstance(mid, str) or not re.fullmatch(r"[a-z]{1,8}", mid):
            raise CodesError(f"alert-metrics: bad id {mid!r}")
        if mid in seen:
            raise CodesError(f"alert-metrics: duplicate id {mid}")
        seen.add(mid)
        if m.get("group") not in groups:
            raise CodesError(f"alert-metrics: {mid} has unknown group {m.get('group')!r}")
        if m.get("kind") not in ("num", "bool"):
            raise CodesError(f"alert-metrics: {mid} kind must be num or bool")
        sc = m.get("scopes")
        if sc not in (["inst"], ["day", "month"]):
            raise CodesError(f"alert-metrics: {mid} scopes must be [inst] or [day, month] (database rules)")
        if not (isinstance(m.get("min"), (int, float)) and isinstance(m.get("max"), (int, float)) and m["min"] < m["max"]):
            raise CodesError(f"alert-metrics: {mid} needs numeric min < max")
        if m["kind"] == "bool" and (m["min"], m["max"], sc) != (0, 1, ["inst"]):
            raise CodesError(f"alert-metrics: boolean {mid} must be inst with min 0, max 1")
        h = m.get("hyst")
        if m["kind"] == "num" and sc == ["inst"]:
            if not isinstance(h, dict) or not isinstance(h.get("abs"), (int, float)) or h["abs"] <= 0:
                raise CodesError(f"alert-metrics: {mid} needs a default hysteresis 'hyst.abs' > 0")
        elif h is not None:
            raise CodesError(f"alert-metrics: {mid} must not have 'hyst' (booleans and latched energy have none)")
        if not isinstance(m.get("since"), int) or not 1 <= m["since"] <= cat["version"]:
            raise CodesError(f"alert-metrics: {mid} 'since' must be 1..version")
        if not isinstance(m.get("label"), str) or not m["label"] or not isinstance(m.get("unit"), str):
            raise CodesError(f"alert-metrics: {mid} needs a label and a unit string")


_ALT = r"\(\^\(([a-z|]+)\)\$/\)"


def check_against_rules(cat: dict, rules_path: Path) -> dict:
    """Assert the database rules allow exactly this catalogue; return the clr range."""
    rules = json.loads(rules_path.read_text(encoding="utf-8"))
    try:
        rid = rules["rules"]["devices"]["inv1"]["alerts"]["rules"]["$rid"]
    except KeyError as exc:
        raise CodesError(f"{rules_path.name}: no alerts/rules/$rid node ({exc})") from exc
    ids = [m["id"] for m in cat["metrics"]]
    energy = {m["id"] for m in cat["metrics"] if m["scopes"] != ["inst"]}

    mm = re.search(r"matches\(/\^\(([a-z|]+)\)\$/\)", rid["metric"][".validate"])
    rule_ids = mm.group(1).split("|") if mm else []
    if sorted(rule_ids) != sorted(ids):
        raise CodesError(f"rules metric regex {sorted(rule_ids)} != catalogue {sorted(ids)}")
    me = re.search(r"child\('metric'\)\.val\(\)\.matches\(/\^\(([a-z|]+)\)\$/\) \? newData\.child\('scope'\)", rid[".validate"])
    if not me or set(me.group(1).split("|")) != energy:
        raise CodesError(f"rules energy-scope regex {me.group(1) if me else None!r} != catalogue {sorted(energy)}")

    ranges: dict[str, tuple[float, float]] = {}
    branch = re.compile(
        r"(?:\.matches\(/\^\(([a-z|]+)\)\$/\)|\.val\(\) === '([a-z]+)') \? "
        r"\((?:newData\.val\(\) >= (-?[\d.]+) && newData\.val\(\) <= (-?[\d.]+)|(newData\.val\(\) === 0 \|\| newData\.val\(\) === 1))\)"
    )
    for g in branch.finditer(rid["value"][".validate"]):
        names = (g.group(1) or g.group(2)).split("|")
        lo, hi = (0.0, 1.0) if g.group(5) else (float(g.group(3)), float(g.group(4)))
        for n in names:
            if n in ranges:
                raise CodesError(f"rules value range lists {n} twice")
            ranges[n] = (lo, hi)
    for m in cat["metrics"]:
        if ranges.get(m["id"]) != (float(m["min"]), float(m["max"])):
            raise CodesError(f"{m['id']}: catalogue range {m['min']}..{m['max']} != rules {ranges.get(m['id'])}")
    if set(ranges) != set(ids):
        raise CodesError(f"rules value ranges cover {sorted(ranges)}, catalogue has {sorted(ids)}")

    mc = re.fullmatch(r"newData\.isNumber\(\) && newData\.val\(\) >= (-?\d+) && newData\.val\(\) <= (\d+)", rid["clr"][".validate"])
    if not mc:
        raise CodesError("rules clr range not recognised")
    return {"min": int(mc.group(1)), "max": int(mc.group(2))}


def render_metrics(cat: dict, clr: dict) -> str:
    ids = [m["id"] for m in cat["metrics"]]
    metrics = []
    for m in cat["metrics"]:
        d = {k: m[k] for k in ("id", "label", "group", "unit", "kind", "scopes", "min", "max")}
        if "hyst" in m:
            d["hyst"] = m["hyst"]
        if m.get("est"):
            d["est"] = True
        d["since"] = m["since"]
        metrics.append(d)
    groups = [{"id": g["id"], "label": g["label"]} for g in cat["groups"]]
    union = lambda xs: " | ".join(f"'{x}'" for x in xs)  # noqa: E731
    parts = [
        "// GENERATED FILE - do not edit by hand.",
        "// Source: shared/alert-metrics.json, checked against firebase/database.rules.json",
        "// (metric ids, energy scopes, value and clear ranges). Regenerate with: python3 tools/gen_codes.py",
        "/* eslint-disable */",
        "",
        f"export type MetricId = {union(ids)};",
        f"export type MetricGroup = {union([g['id'] for g in groups])};",
        f"export type RuleScope = {union(SCOPES)};",
        "export interface MetricDef {",
        "  id: MetricId;",
        "  label: string;",
        "  group: MetricGroup;",
        "  /** Canonical stored unit (Wh for energy, min for gout, '' for booleans). */",
        "  unit: string;",
        "  kind: 'num' | 'bool';",
        "  scopes: readonly RuleScope[];",
        "  /** Value range allowed by the database rules. */",
        "  min: number;",
        "  max: number;",
        "  /** Default hysteresis when a rule has no clr: max(abs, pct % of |value|). */",
        "  hyst?: { abs: number; pct?: number };",
        "  /** Estimate, labelled \"est.\" everywhere (not the utility meter). */",
        "  est?: boolean;",
        "  /** First catalogue version (status.acat) that knows this metric. */",
        "  since: number;",
        "}",
        "",
        "/** The logger reports the catalogue version it understands as status.acat. */",
        f"export const ALERT_CATALOG_VERSION = {int(cat['version'])};",
        "",
        f"export const METRIC_GROUPS: readonly {{ id: MetricGroup; label: string }}[] = {js(groups)};",
        "",
        f"export const METRICS: readonly MetricDef[] = {js(metrics)};",
        "",
        "export const METRIC_BY_ID = Object.fromEntries(METRICS.map((m) => [m.id, m])) as Record<MetricId, MetricDef>;",
        "",
        "/** Range of a rule's custom clear level (alerts/rules/$rid/clr). */",
        f"export const CLR_RANGE = {{ min: {clr['min']}, max: {clr['max']} }} as const;",
        "",
    ]
    return "\n".join(parts)


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--check", action="store_true", help="exit 1 if a generated web file is out of date")
    ap.add_argument("--stdout", action="store_true", help="print the generated file instead of writing it")
    args = ap.parse_args(argv)

    try:
        data = json.loads(SRC.read_text(encoding="utf-8"))
        validate(data)
    except (OSError, json.JSONDecodeError, CodesError, KeyError, TypeError) as exc:
        print(f"gen_codes: {SRC}: {exc}", file=sys.stderr)
        return 2

    try:
        cat = json.loads(METRICS_SRC.read_text(encoding="utf-8"))
        validate_metrics(cat)
        clr = check_against_rules(cat, RULES)
    except (OSError, json.JSONDecodeError, CodesError, KeyError, TypeError) as exc:
        print(f"gen_codes: {METRICS_SRC}: {exc}", file=sys.stderr)
        return 2

    out = render(data)
    if args.stdout:
        sys.stdout.write(out)
        return 0
    outputs = [(DST, out), (METRICS_DST, render_metrics(cat, clr))]
    if args.check:
        stale = 0
        for dst, text in outputs:
            current = dst.read_text(encoding="utf-8") if dst.exists() else ""
            if current != text:
                print(f"gen_codes: {dst.relative_to(ROOT)} is stale; run python3 tools/gen_codes.py", file=sys.stderr)
                stale = 1
            else:
                print(f"gen_codes: {dst.relative_to(ROOT)} is up to date")
        return stale
    for dst, text in outputs:
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_text(text, encoding="utf-8")
        print(f"gen_codes: wrote {dst.relative_to(ROOT)} ({len(text)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
