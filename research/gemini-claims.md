# User-supplied claims from Gemini (unsourced) — to be reviewed in the final docs

## 1. Hardware Interface Pinouts
RS232 Port (Standard RJ45 Connection): Pin 2 Inverter TX, Pin 3 Inverter RX, Pin 5 GND
RS485 Interface (Optional Expansion Card): Pin 1 A(+), Pin 2 B(-)

## 2. QPIGS fields (as claimed)
1 Grid Voltage V | 2 Grid Frequency Hz | 3 Output Voltage V | 4 Output Frequency Hz | 5 Output Apparent Power VA |
6 Output Active Power W | 7 Output Load % | 8 BUS Voltage V | 9 Battery Voltage V | 10 Battery Charging Current A |
11 Battery Capacity % | 12 Heat Sink Temp C | 13 PV Input Current A | 14 PV Input Voltage V | 15 Battery Discharge Current A

## 3. Modbus RTU map (as claimed, FC 0x03 or 0x04)
30001 PV1 Power W x1 | 30002 PV1 Voltage x0.1 | 30003 PV1 Current x0.1 | 30010 Grid V x0.1 | 30011 Grid Hz x0.01 |
30012 Out V x0.1 | 30013 Out Hz x0.01 | 30014 Load W | 30016 Load % | 30020 Batt V x0.1 | 30021 SOC % |
30022 Batt charge A x0.1 | 30023 Batt discharge A x0.1

## 4. Serial: 2400 or 9600 baud, 8N1 ("2400 is standard for Axpert/F4 lineages")
