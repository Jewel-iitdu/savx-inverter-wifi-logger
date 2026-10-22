// GENERATED FILE - do not edit by hand.
// Source: shared/codes.json. Regenerate with: python3 tools/gen_codes.py
// LCD codes, QPIWS bits and REVO QALL codes use different numbering:
// always decode a code with the table named by its source (d1 K13).
/* eslint-disable */

export type BitKind = 'W' | 'F' | 'FA1' | 'FC' | 'WF' | 'R';
export type QpiwsBit = readonly [index: number, kind: BitKind, text: string];
export interface QpiwsVariant {
  id: string;
  length: number;
  label: string;
  faultCodeAt: readonly [number, number] | null;
  bits: readonly QpiwsBit[];
}
export interface RevoCode { text: string; secondary?: boolean }

export const CODES_VERSION = 1;

export const CODE_SOURCES: Record<string, string> = {
  "manual": "Savx F4 paper manual v3.0, section 5 'Fault Reference Code' and section 6 'Warning Indicator' (printed pages 27-28; reference/Savx-F4-user-manual.pdf page 15). [CONFIRMED-primary]",
  "p2014": "Voltronic 'Infini/Axpert' PI30 protocol P2014 (HS/MS/MSX), QPIWS a0..a31 and fault-code table. Via research/r2-voltronic-pi30.md section 5.6. [CONFIRMED-primary]",
  "p2015": "Voltronic PI30 P2015 (Plus Duo), QPIWS b1..b38 (b1 = a0). Via r2 section 5.6. [CONFIRMED-primary]",
  "pmax": "Voltronic PI30MAX (2021-02-17), QPIWS a0..a35; 24 V model fault code in a31..a32. Via r2 section 5.6 and v1 V19. [CONFIRMED-primary]",
  "pgk": "Voltronic PIP-GK/MK, QPIWS a0..a35, fault code in a31..a32, extra QMOD letters. Via r2 section 5.6. [CONFIRMED-primary]",
  "revo": "Sorotec 'Revo RS232 Communication Protocol V02' (2020-11-12), QALL warning code table 00-07/21-26 and fault code table 01-12 (Google-translated original wording kept). Via v1 C12/C13. [CONFIRMED-primary]",
  "mppsolar": "jblance/mpp-solar pi30revo.py additions (warning 08, 39). NOT in the Sorotec document. [SECONDARY]"
};

export const QMOD: Record<string, { name: string; short: string }> = {
  "P": {
    "name": "Power on",
    "short": "Power-on"
  },
  "S": {
    "name": "Standby",
    "short": "Standby"
  },
  "L": {
    "name": "Line (grid) mode",
    "short": "Line"
  },
  "B": {
    "name": "Battery (inverter) mode",
    "short": "Battery"
  },
  "F": {
    "name": "Fault",
    "short": "Fault"
  },
  "H": {
    "name": "Power saving (REVO: sleep)",
    "short": "Saving"
  },
  "D": {
    "name": "Shutdown",
    "short": "Shutdown"
  },
  "C": {
    "name": "Charge mode",
    "short": "Charge"
  },
  "Y": {
    "name": "Bypass",
    "short": "Bypass"
  },
  "E": {
    "name": "ECO mode",
    "short": "ECO"
  }
};

export const ST_BITS: readonly { pos: number; bit: string; name: string }[] = [
  {
    "pos": 0,
    "bit": "b7",
    "name": "SBU priority version added (PGK: PV or AC feeds the load)"
  },
  {
    "pos": 1,
    "bit": "b6",
    "name": "Configuration changed"
  },
  {
    "pos": 2,
    "bit": "b5",
    "name": "SCC firmware updated"
  },
  {
    "pos": 3,
    "bit": "b4",
    "name": "Load on"
  },
  {
    "pos": 4,
    "bit": "b3",
    "name": "Battery voltage steady while charging"
  },
  {
    "pos": 5,
    "bit": "b2",
    "name": "Charging (any source)"
  },
  {
    "pos": 6,
    "bit": "b1",
    "name": "Solar (SCC) charging"
  },
  {
    "pos": 7,
    "bit": "b0",
    "name": "AC (utility) charging"
  }
];

export const ST2_BITS: readonly { pos: number; bit: string; name: string }[] = [
  {
    "pos": 0,
    "bit": "b10",
    "name": "Charging to float"
  },
  {
    "pos": 1,
    "bit": "b9",
    "name": "Switch on"
  },
  {
    "pos": 2,
    "bit": "b8",
    "name": "Reserved / dustproof installed"
  }
];

export const QPIWS_VARIANTS: readonly QpiwsVariant[] = [
  {
    "id": "p2014",
    "length": 32,
    "label": "PI30 P2014 (32 chars)",
    "faultCodeAt": null,
    "bits": [
      [
        0,
        "R",
        "Reserved"
      ],
      [
        1,
        "F",
        "Inverter fault"
      ],
      [
        2,
        "F",
        "Bus over voltage"
      ],
      [
        3,
        "F",
        "Bus under voltage"
      ],
      [
        4,
        "F",
        "Bus soft start failed"
      ],
      [
        5,
        "W",
        "Line (grid) fail"
      ],
      [
        6,
        "W",
        "Output short circuit (OPV short)"
      ],
      [
        7,
        "F",
        "Inverter voltage too low"
      ],
      [
        8,
        "F",
        "Inverter voltage too high"
      ],
      [
        9,
        "FA1",
        "Over temperature"
      ],
      [
        10,
        "FA1",
        "Fan locked"
      ],
      [
        11,
        "FA1",
        "Battery voltage high"
      ],
      [
        12,
        "W",
        "Battery low alarm"
      ],
      [
        13,
        "R",
        "Reserved"
      ],
      [
        14,
        "W",
        "Battery under shutdown"
      ],
      [
        15,
        "R",
        "Reserved"
      ],
      [
        16,
        "FA1",
        "Overload"
      ],
      [
        17,
        "W",
        "EEPROM fault"
      ],
      [
        18,
        "F",
        "Inverter over current"
      ],
      [
        19,
        "F",
        "Inverter soft start failed"
      ],
      [
        20,
        "F",
        "Self test failed"
      ],
      [
        21,
        "F",
        "Output DC voltage over"
      ],
      [
        22,
        "F",
        "Battery open"
      ],
      [
        23,
        "F",
        "Current sensor failed"
      ],
      [
        24,
        "F",
        "Battery short"
      ],
      [
        25,
        "W",
        "Power limit"
      ],
      [
        26,
        "W",
        "PV voltage high"
      ],
      [
        27,
        "W",
        "MPPT overload fault"
      ],
      [
        28,
        "W",
        "MPPT overload warning"
      ],
      [
        29,
        "W",
        "Battery too low to charge"
      ],
      [
        30,
        "R",
        "Reserved"
      ],
      [
        31,
        "R",
        "Reserved"
      ]
    ]
  },
  {
    "id": "pmax",
    "length": 36,
    "label": "PI30 MAX (36 chars, 24 V fault code in a31-a32)",
    "faultCodeAt": [
      31,
      32
    ],
    "bits": [
      [
        0,
        "W",
        "PV loss"
      ],
      [
        1,
        "F",
        "Inverter fault"
      ],
      [
        2,
        "F",
        "Bus over voltage"
      ],
      [
        3,
        "F",
        "Bus under voltage"
      ],
      [
        4,
        "F",
        "Bus soft start failed"
      ],
      [
        5,
        "W",
        "Line (grid) fail"
      ],
      [
        6,
        "F",
        "Output short circuit (OPV short)"
      ],
      [
        7,
        "F",
        "Inverter voltage too low"
      ],
      [
        8,
        "F",
        "Inverter voltage too high"
      ],
      [
        9,
        "FA1",
        "Over temperature"
      ],
      [
        10,
        "FA1",
        "Fan locked"
      ],
      [
        11,
        "FA1",
        "Battery voltage high"
      ],
      [
        12,
        "W",
        "Battery low alarm"
      ],
      [
        13,
        "R",
        "Reserved"
      ],
      [
        14,
        "W",
        "Battery under shutdown"
      ],
      [
        15,
        "W",
        "Battery derating"
      ],
      [
        16,
        "FA1",
        "Overload"
      ],
      [
        17,
        "W",
        "EEPROM fault"
      ],
      [
        18,
        "F",
        "Inverter over current"
      ],
      [
        19,
        "F",
        "Inverter soft start failed"
      ],
      [
        20,
        "F",
        "Self test failed"
      ],
      [
        21,
        "F",
        "Output DC voltage over"
      ],
      [
        22,
        "F",
        "Battery open"
      ],
      [
        23,
        "F",
        "Current sensor failed"
      ],
      [
        24,
        "R",
        "Reserved"
      ],
      [
        25,
        "R",
        "Reserved"
      ],
      [
        26,
        "R",
        "Reserved"
      ],
      [
        27,
        "R",
        "Reserved"
      ],
      [
        28,
        "R",
        "Reserved"
      ],
      [
        29,
        "R",
        "Reserved"
      ],
      [
        30,
        "R",
        "Reserved"
      ],
      [
        31,
        "FC",
        "Fault code digit 1 (24 V model)"
      ],
      [
        32,
        "FC",
        "Fault code digit 2 (24 V model)"
      ],
      [
        33,
        "R",
        "Reserved (48 V model: fault code digit)"
      ],
      [
        34,
        "R",
        "Reserved"
      ],
      [
        35,
        "W",
        "Battery equalization"
      ]
    ]
  },
  {
    "id": "pgk",
    "length": 36,
    "label": "PIP-GK/MK (36 chars, fault code in a31-a32)",
    "faultCodeAt": [
      31,
      32
    ],
    "bits": [
      [
        0,
        "W",
        "PV loss"
      ],
      [
        1,
        "F",
        "Inverter fault"
      ],
      [
        2,
        "F",
        "Bus over voltage"
      ],
      [
        3,
        "F",
        "Bus under voltage"
      ],
      [
        4,
        "F",
        "Bus soft start failed"
      ],
      [
        5,
        "W",
        "Line (grid) fail"
      ],
      [
        6,
        "F",
        "Output short circuit (OPV short)"
      ],
      [
        7,
        "F",
        "Inverter voltage too low"
      ],
      [
        8,
        "F",
        "Inverter voltage too high"
      ],
      [
        9,
        "FA1",
        "Over temperature"
      ],
      [
        10,
        "FA1",
        "Fan locked"
      ],
      [
        11,
        "FA1",
        "Battery voltage high"
      ],
      [
        12,
        "W",
        "Battery low alarm"
      ],
      [
        13,
        "R",
        "Reserved"
      ],
      [
        14,
        "W",
        "Battery under shutdown"
      ],
      [
        15,
        "W",
        "Battery derating"
      ],
      [
        16,
        "FA1",
        "Overload"
      ],
      [
        17,
        "W",
        "EEPROM fault"
      ],
      [
        18,
        "F",
        "Inverter over current"
      ],
      [
        19,
        "F",
        "Inverter soft start failed"
      ],
      [
        20,
        "F",
        "Self test failed"
      ],
      [
        21,
        "F",
        "Output DC voltage over"
      ],
      [
        22,
        "F",
        "Battery open"
      ],
      [
        23,
        "F",
        "Current sensor failed"
      ],
      [
        24,
        "F",
        "Battery short"
      ],
      [
        25,
        "W",
        "Power limit"
      ],
      [
        26,
        "WF",
        "PV voltage high"
      ],
      [
        27,
        "F",
        "MPPT overload fault"
      ],
      [
        28,
        "W",
        "MPPT overload warning"
      ],
      [
        29,
        "W",
        "Battery too low to charge"
      ],
      [
        30,
        "F",
        "DC/DC over current"
      ],
      [
        31,
        "FC",
        "Fault code digit 1"
      ],
      [
        32,
        "FC",
        "Fault code digit 2"
      ],
      [
        33,
        "W",
        "Low PV energy"
      ],
      [
        34,
        "W",
        "High AC input during BUS soft start"
      ],
      [
        35,
        "W",
        "Battery equalization"
      ]
    ]
  },
  {
    "id": "p2015",
    "length": 38,
    "label": "PI30 P2015 / Plus Duo (38 chars, b1 = a0)",
    "faultCodeAt": null,
    "bits": [
      [
        0,
        "R",
        "Reserved"
      ],
      [
        1,
        "F",
        "Inverter fault"
      ],
      [
        2,
        "F",
        "Bus over voltage"
      ],
      [
        3,
        "F",
        "Bus under voltage"
      ],
      [
        4,
        "F",
        "Bus soft start failed"
      ],
      [
        5,
        "W",
        "Line (grid) fail"
      ],
      [
        6,
        "W",
        "Output short circuit (OPV short)"
      ],
      [
        7,
        "F",
        "Inverter voltage too low"
      ],
      [
        8,
        "F",
        "Inverter voltage too high"
      ],
      [
        9,
        "FA1",
        "Over temperature"
      ],
      [
        10,
        "FA1",
        "Fan locked"
      ],
      [
        11,
        "FA1",
        "Battery voltage high"
      ],
      [
        12,
        "W",
        "Battery low alarm"
      ],
      [
        13,
        "R",
        "Reserved (overcharge)"
      ],
      [
        14,
        "W",
        "Battery under shutdown"
      ],
      [
        15,
        "W",
        "Battery derating"
      ],
      [
        16,
        "FA1",
        "Overload"
      ],
      [
        17,
        "W",
        "EEPROM fault"
      ],
      [
        18,
        "F",
        "Inverter over current"
      ],
      [
        19,
        "F",
        "Inverter soft start failed"
      ],
      [
        20,
        "F",
        "Self test failed"
      ],
      [
        21,
        "F",
        "Output DC voltage over"
      ],
      [
        22,
        "F",
        "Battery open"
      ],
      [
        23,
        "F",
        "Current sensor failed"
      ],
      [
        24,
        "F",
        "Battery short"
      ],
      [
        25,
        "W",
        "Power limit"
      ],
      [
        26,
        "W",
        "PV1 voltage high"
      ],
      [
        27,
        "W",
        "MPPT1 overload fault"
      ],
      [
        28,
        "W",
        "MPPT1 overload warning"
      ],
      [
        29,
        "W",
        "Battery too low to charge (1)"
      ],
      [
        30,
        "W",
        "PV2 voltage high"
      ],
      [
        31,
        "W",
        "MPPT2 overload fault"
      ],
      [
        32,
        "W",
        "MPPT2 overload warning"
      ],
      [
        33,
        "W",
        "Battery too low to charge (2)"
      ],
      [
        34,
        "W",
        "PV3 voltage high"
      ],
      [
        35,
        "W",
        "MPPT3 overload fault"
      ],
      [
        36,
        "W",
        "MPPT3 overload warning"
      ],
      [
        37,
        "W",
        "Battery too low to charge (3)"
      ]
    ]
  }
] as unknown as readonly QpiwsVariant[];

export const LCD_FAULT: Record<string, string> = {
  "01": "Fan is locked when inverter is off",
  "02": "Over temperature",
  "03": "Battery voltage is too high",
  "04": "Battery voltage is too low",
  "05": "Output short circuited or over temperature is detected by internal converter components",
  "06": "Output voltage is too high",
  "07": "Overload time out",
  "08": "Bus voltage is too high",
  "09": "Bus soft start failed",
  "51": "Over current or surge",
  "52": "Bus voltage is too low",
  "53": "Inverter soft start failed",
  "55": "Over DC voltage in AC output",
  "57": "Current sensor failed",
  "58": "Output voltage is too low",
  "59": "PV voltage is over limitation"
};

export const VOLTRONIC_FAULT_EXTRA: Record<string, string> = {
  "10": "PV over current (PMAX) / PV charger current over (PGK)",
  "11": "Main relay failed (P2014) / PV over voltage (PMAX)",
  "12": "DC/DC over current (PMAX)",
  "13": "Battery discharge over current (PMAX)",
  "54": "Self-test failed (P2014)",
  "56": "Battery connection open (P2014)",
  "60": "Inverter negative power / power feedback protection"
};

export const LCD_WARNING: Record<string, string> = {
  "01": "Fan is locked when inverter is on",
  "02": "Over temperature",
  "03": "Battery is over-charged",
  "04": "Low battery",
  "07": "Overload",
  "08": "Discharge current limiting",
  "10": "Output power derating",
  "15": "PV energy is low",
  "16": "High AC input (>280VAC) during BUS soft start",
  "E9": "Battery equalization",
  "bP": "Battery is not connected"
};

export const REVO_WARNING: Record<string, RevoCode> = {
  "00": {
    "text": "No warning"
  },
  "01": {
    "text": "Low battery"
  },
  "02": {
    "text": "Mains low voltage"
  },
  "03": {
    "text": "Mains high voltage"
  },
  "04": {
    "text": "Overload"
  },
  "05": {
    "text": "Over temperature"
  },
  "06": {
    "text": "Fan lock"
  },
  "07": {
    "text": "Battery overvoltage"
  },
  "21": {
    "text": "PV low pressure (low voltage)"
  },
  "22": {
    "text": "PV overvoltage"
  },
  "23": {
    "text": "PV overcurrent"
  },
  "24": {
    "text": "PV over temperature"
  },
  "25": {
    "text": "PV overload"
  },
  "26": {
    "text": "PV boost failed"
  },
  "08": {
    "text": "Discharge overcurrent (mpp-solar addition, not in the Sorotec doc)",
    "secondary": true
  },
  "39": {
    "text": "BMS communication lost (mpp-solar addition, not in the Sorotec doc)",
    "secondary": true
  }
};

export const REVO_FAULT: Record<string, RevoCode> = {
  "00": {
    "text": "No fault"
  },
  "01": {
    "text": "BUS overvoltage"
  },
  "02": {
    "text": "Inverter overvoltage"
  },
  "03": {
    "text": "Inverter low voltage"
  },
  "04": {
    "text": "BUS failure"
  },
  "05": {
    "text": "Overload fault"
  },
  "06": {
    "text": "Output short circuit"
  },
  "07": {
    "text": "Low battery voltage failure (battery too low at start-up, start-up not allowed)"
  },
  "08": {
    "text": "Inverter failed"
  },
  "09": {
    "text": "BUS low voltage"
  },
  "10": {
    "text": "Parallel failure"
  },
  "11": {
    "text": "Over temperature fault"
  },
  "12": {
    "text": "Battery over-voltage fault (battery too high at start-up; cell count may be wrong)"
  }
};

export const SEVERITY_NAMES: readonly string[] = [
  "Debug",
  "Info",
  "Notice",
  "Warning",
  "Alert",
  "Critical"
];

export const EVENT_TYPES: Record<string, { sev: number; label: string; clears?: string }> = {
  "INVERTER_FAULT": {
    "sev": 5,
    "label": "Inverter fault"
  },
  "BATT_CRITICAL": {
    "sev": 5,
    "label": "Battery critical"
  },
  "BATT_LOW": {
    "sev": 4,
    "label": "Battery low"
  },
  "BATT_HIGH": {
    "sev": 4,
    "label": "Battery voltage high"
  },
  "GRID_LOSS": {
    "sev": 4,
    "label": "Grid lost"
  },
  "GRID_RESTORED": {
    "sev": 3,
    "label": "Grid restored",
    "clears": "GRID_LOSS"
  },
  "GRID_ABNORMAL": {
    "sev": 2,
    "label": "Grid voltage abnormal"
  },
  "OVERLOAD": {
    "sev": 4,
    "label": "Overload"
  },
  "OVER_TEMP": {
    "sev": 4,
    "label": "Over temperature"
  },
  "PV_OVERVOLT": {
    "sev": 4,
    "label": "PV over-voltage"
  },
  "PV_LOW_DAY": {
    "sev": 2,
    "label": "Low solar today"
  },
  "LINK_LOST": {
    "sev": 3,
    "label": "Inverter link lost"
  },
  "LOGGER_REBOOT": {
    "sev": 2,
    "label": "Logger rebooted"
  },
  "QUOTA_GUARD": {
    "sev": 2,
    "label": "Quota guard active"
  },
  "DATA_DROPPED": {
    "sev": 2,
    "label": "Data dropped"
  },
  "BACKLOG_STUCK": {
    "sev": 2,
    "label": "Upload backlog stuck"
  },
  "DAILY_SUMMARY": {
    "sev": 1,
    "label": "Daily summary"
  },
  "TEST_ALERT": {
    "sev": 1,
    "label": "Test alert"
  },
  "RULE": {
    "sev": 3,
    "label": "Rule alert"
  },
  "RULE_INVALID": {
    "sev": 2,
    "label": "Rule ignored by the logger"
  }
};
