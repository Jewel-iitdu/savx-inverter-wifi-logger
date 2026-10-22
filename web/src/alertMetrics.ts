// GENERATED FILE - do not edit by hand.
// Source: shared/alert-metrics.json, checked against firebase/database.rules.json
// (metric ids, energy scopes, value and clear ranges). Regenerate with: python3 tools/gen_codes.py
/* eslint-disable */

export type MetricId = 'w' | 'pvw' | 'ld' | 'ov' | 'pvv' | 'tmp' | 'soc' | 'bv' | 'bw' | 'bdw' | 'bdi' | 'onbat' | 'gw' | 'gv' | 'gf' | 'nogrid' | 'eld' | 'egr' | 'epv' | 'edis' | 'ech' | 'gout';
export type MetricGroup = 'power' | 'battery' | 'grid' | 'energy';
export type RuleScope = 'inst' | 'day' | 'month';
export interface MetricDef {
  id: MetricId;
  label: string;
  group: MetricGroup;
  /** Canonical stored unit (Wh for energy, min for gout, '' for booleans). */
  unit: string;
  kind: 'num' | 'bool';
  scopes: readonly RuleScope[];
  /** Value range allowed by the database rules. */
  min: number;
  max: number;
  /** Default hysteresis when a rule has no clr: max(abs, pct % of |value|). */
  hyst?: { abs: number; pct?: number };
  /** Estimate, labelled "est." everywhere (not the utility meter). */
  est?: boolean;
  /** First catalogue version (status.acat) that knows this metric. */
  since: number;
}

/** The logger reports the catalogue version it understands as status.acat. */
export const ALERT_CATALOG_VERSION = 1;

export const METRIC_GROUPS: readonly { id: MetricGroup; label: string }[] = [
  {
    "id": "power",
    "label": "Power"
  },
  {
    "id": "battery",
    "label": "Battery"
  },
  {
    "id": "grid",
    "label": "Grid"
  },
  {
    "id": "energy",
    "label": "Energy"
  }
];

export const METRICS: readonly MetricDef[] = [
  {
    "id": "w",
    "label": "Load power",
    "group": "power",
    "unit": "W",
    "kind": "num",
    "scopes": [
      "inst"
    ],
    "min": -10000,
    "max": 20000,
    "hyst": {
      "abs": 50,
      "pct": 5
    },
    "since": 1
  },
  {
    "id": "pvw",
    "label": "Solar power",
    "group": "power",
    "unit": "W",
    "kind": "num",
    "scopes": [
      "inst"
    ],
    "min": -10000,
    "max": 20000,
    "hyst": {
      "abs": 50,
      "pct": 5
    },
    "since": 1
  },
  {
    "id": "ld",
    "label": "Load % of rating",
    "group": "power",
    "unit": "%",
    "kind": "num",
    "scopes": [
      "inst"
    ],
    "min": 0,
    "max": 250,
    "hyst": {
      "abs": 2
    },
    "since": 1
  },
  {
    "id": "ov",
    "label": "Output voltage",
    "group": "power",
    "unit": "V",
    "kind": "num",
    "scopes": [
      "inst"
    ],
    "min": 0,
    "max": 600,
    "hyst": {
      "abs": 5
    },
    "since": 1
  },
  {
    "id": "pvv",
    "label": "Solar voltage",
    "group": "power",
    "unit": "V",
    "kind": "num",
    "scopes": [
      "inst"
    ],
    "min": 0,
    "max": 600,
    "hyst": {
      "abs": 10
    },
    "since": 1
  },
  {
    "id": "tmp",
    "label": "Inverter temperature",
    "group": "power",
    "unit": "°C",
    "kind": "num",
    "scopes": [
      "inst"
    ],
    "min": -20,
    "max": 150,
    "hyst": {
      "abs": 3
    },
    "since": 1
  },
  {
    "id": "soc",
    "label": "Battery SOC",
    "group": "battery",
    "unit": "%",
    "kind": "num",
    "scopes": [
      "inst"
    ],
    "min": 0,
    "max": 250,
    "hyst": {
      "abs": 2
    },
    "since": 1
  },
  {
    "id": "bv",
    "label": "Battery voltage",
    "group": "battery",
    "unit": "V",
    "kind": "num",
    "scopes": [
      "inst"
    ],
    "min": 0,
    "max": 600,
    "hyst": {
      "abs": 0.3
    },
    "since": 1
  },
  {
    "id": "bw",
    "label": "Battery net power (+ = charging)",
    "group": "battery",
    "unit": "W",
    "kind": "num",
    "scopes": [
      "inst"
    ],
    "min": -10000,
    "max": 20000,
    "hyst": {
      "abs": 50,
      "pct": 5
    },
    "since": 1
  },
  {
    "id": "bdw",
    "label": "Battery discharge power",
    "group": "battery",
    "unit": "W",
    "kind": "num",
    "scopes": [
      "inst"
    ],
    "min": -10000,
    "max": 20000,
    "hyst": {
      "abs": 50,
      "pct": 5
    },
    "since": 1
  },
  {
    "id": "bdi",
    "label": "Battery discharge current",
    "group": "battery",
    "unit": "A",
    "kind": "num",
    "scopes": [
      "inst"
    ],
    "min": 0,
    "max": 300,
    "hyst": {
      "abs": 2
    },
    "since": 1
  },
  {
    "id": "onbat",
    "label": "On battery",
    "group": "battery",
    "unit": "",
    "kind": "bool",
    "scopes": [
      "inst"
    ],
    "min": 0,
    "max": 1,
    "since": 1
  },
  {
    "id": "gw",
    "label": "Grid import power",
    "group": "grid",
    "unit": "W",
    "kind": "num",
    "scopes": [
      "inst"
    ],
    "min": -10000,
    "max": 20000,
    "hyst": {
      "abs": 50,
      "pct": 5
    },
    "est": true,
    "since": 1
  },
  {
    "id": "gv",
    "label": "Grid voltage",
    "group": "grid",
    "unit": "V",
    "kind": "num",
    "scopes": [
      "inst"
    ],
    "min": 0,
    "max": 600,
    "hyst": {
      "abs": 5
    },
    "since": 1
  },
  {
    "id": "gf",
    "label": "Grid frequency",
    "group": "grid",
    "unit": "Hz",
    "kind": "num",
    "scopes": [
      "inst"
    ],
    "min": 40,
    "max": 70,
    "hyst": {
      "abs": 0.3
    },
    "since": 1
  },
  {
    "id": "nogrid",
    "label": "Grid absent",
    "group": "grid",
    "unit": "",
    "kind": "bool",
    "scopes": [
      "inst"
    ],
    "min": 0,
    "max": 1,
    "since": 1
  },
  {
    "id": "eld",
    "label": "Load energy",
    "group": "energy",
    "unit": "Wh",
    "kind": "num",
    "scopes": [
      "day",
      "month"
    ],
    "min": 0,
    "max": 5000000,
    "since": 1
  },
  {
    "id": "egr",
    "label": "Grid energy",
    "group": "energy",
    "unit": "Wh",
    "kind": "num",
    "scopes": [
      "day",
      "month"
    ],
    "min": 0,
    "max": 5000000,
    "est": true,
    "since": 1
  },
  {
    "id": "epv",
    "label": "Solar energy",
    "group": "energy",
    "unit": "Wh",
    "kind": "num",
    "scopes": [
      "day",
      "month"
    ],
    "min": 0,
    "max": 5000000,
    "since": 1
  },
  {
    "id": "edis",
    "label": "Battery discharged",
    "group": "energy",
    "unit": "Wh",
    "kind": "num",
    "scopes": [
      "day",
      "month"
    ],
    "min": 0,
    "max": 5000000,
    "since": 1
  },
  {
    "id": "ech",
    "label": "Battery charged",
    "group": "energy",
    "unit": "Wh",
    "kind": "num",
    "scopes": [
      "day",
      "month"
    ],
    "min": 0,
    "max": 5000000,
    "since": 1
  },
  {
    "id": "gout",
    "label": "Grid outage time",
    "group": "energy",
    "unit": "min",
    "kind": "num",
    "scopes": [
      "day",
      "month"
    ],
    "min": 0,
    "max": 44640,
    "since": 1
  }
];

export const METRIC_BY_ID = Object.fromEntries(METRICS.map((m) => [m.id, m])) as Record<MetricId, MetricDef>;

/** Range of a rule's custom clear level (alerts/rules/$rid/clr). */
export const CLR_RANGE = { min: -10000, max: 5000000 } as const;
