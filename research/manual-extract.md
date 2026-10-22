# Savx F4 paper manual — extracted facts (PRIMARY SOURCE for this unit)
Source PDF: the paper manual (PDF scan) (20 scanned pages; viewable with Read + pages param)
Cover: "USER MANUAL — 1.6KW/3.2KW SOLAR INVERTER/CHARGER", Version 3.0; back cover part no. "614.C0556-02". Cover drawing = same round dial with red/orange ring, LEDs "AC/INV, CHG, FAULT", keys ESC/UP/DOWN/ENTER. Generic OEM manual (no brand).

## Ports (sec 2.3 Product Overview, Figure 2 1.6K/3.2K model)
Bottom panel items: 1 AC input terminal, 2 AC output terminal, 3 Battery input, 4 PV input terminal, 5 COMM communication port, 6 BMS communication port (Option), 7 Power on/off switch, 8 Dust cover.
"Communication port definition" table (two RJ45 jacks drawn side by side, labelled COMM and BMS, pins numbered 1..8):
- COMM: RS232 -> 1: RXD, 2: TXD, 4: +VCC, 8: GND      (perspective of RXD/TXD NOT stated; +VCC voltage NOT stated)
- BMS: RS485, CAN -> 1: 485-B, 2: 485-A, 4: CAN-H, 5: CAN-L
User reports: BMS port is occupied by the lithium battery cable; the other port is free. User described labels as "RS485" and "BMS/CAN" — conflicts with the manual (COMM = RS232). Must be resolved by photo or by the electrical idle-level probe.

## Monitoring (sec 3.8 / 3.9)
- 3.8 "RS232 Communication Connection": download software "Watch Power" from the official website; screenshot is WatchPower (Voltronic tool) showing Basic information (AC voltage, AC frequency, PV input voltage, Battery voltage, Battery capacity, Battery discharge current, Output voltage/frequency/apparent power/active power, Load percent), Product information (Model type: Stand alone, Topology: transformerless, Main CPU version 00056.14, Secondary CPU version 00000.00), Rated information (Nominal AC voltage 230, Nominal AC current 26.0 A, Rated battery voltage 48.0V, Nominal output voltage 230, nominal output frequency 50, nominal output current 26.1, nominal apparent power 6000 VA, active power 6000 W) — screenshot is reference only (a 6 kW 48 V unit).
  => WatchPower compatibility strongly implies the Voltronic "PI30"-family ASCII protocol (QPIGS, QPIRI, QMOD, QPIWS...), RS232, typically 2400 8N1.
- 3.9 "Wi-Fi Connection (Optional)": "The device has its own standard WIFI port ... must connect to the WIFI collector. Users can download 'SOLAR OF THINGS' WiFi monitoring software from the app store. ... Wi-Fi Dongle is Optional". (No WiFi collector was purchased; user cannot buy more.)
- Features list: "External WIFI devices (option)".

## Front panel (4.2) LEDs: AC/INV green solid = output powered by utility, flashing = powered by battery; CHG green solid = battery fully charged, flashing = charging; FAULT red solid = fault, flashing = warning.
## Display (4.5) cycles: input voltage, input frequency, PV voltage, charging current, charging power (MPPT), battery voltage, output voltage, output frequency, load %, load VA, load W, DC discharging current, Main CPU version (e.g. U1 14 04 = 00014.04), Secondary CPU version (U2 03 03 = 00003.03).
## LCD icons (4.3) include: AC, PV, BYPASS, GRD ("grid connection in progress"), OVERLOAD, battery "Li" + "communication with the battery" icon, mute.

## Settings programs (4.4) — 00 ESC; 01 output source priority UtI (default)/SOL/SbU; 02 max charging current default 60A (10-100A step 10); 03 AC input range APL (90-280V, default)/UPS (170-280V); 05 battery type AGM (default)/FLd/USE/LIb; 06 overload auto restart LtD/LtE; 07 over-temp auto restart; 09 output freq 50/60; 10 output voltage 220/230(default)/240; 11 max utility charging current default 30A (2,10..80A); 12 back-to-utility point 23.0V default (24V model; 22-25.5V) or SOC 40% for lithium (10-80%); 13 back-to-battery point 27.0V default (24-29V) or FUL, SOC 80% for lithium (50-100%); 16 charger priority CUt/CSO/SNU(default)/OSO; 18 alarm; 19 auto return display ESP/HEP; 20 backlight; 22 beep on source interrupt; 23 overload bypass byd/byE; 25 record fault code FEn/FdS; 26 bulk (CV) voltage 28.2V default 24V (25-31.5); 27 float 27.0V default; 29 low DC cut-off 21.0V default 24V (21-26V) or SOC 15% lithium (5-50%); 30 equalization EEN/EdS; 31 EQ voltage 29.2V; 33 EQ time 60 min; 34 EQ timeout 120 min; 35 EQ interval 30d; 36 EQ now AEn/AdS; 40 discharge limited current OFF/10-200A; 42 EARTH LED adjust (-30..30); 43 solar feed to grid GTd (default)/GTE; 44 reconnection delay 0-999 s; 45 zero-export to load 0-500; 50 battery activation bdS/bEn.

## Fault codes (sec 5): 01 Fan locked when inverter off; 02 Over temperature; 03 Battery voltage too high; 04 Battery voltage too low; 05 Output short circuited or over temperature detected by internal converter components; 06 Output voltage too high; 07 Overload time out; 08 Bus voltage too high; 09 Bus soft start failed; 51 Over current or surge; 52 Bus voltage too low; 53 Inverter soft start failed; 55 Over DC voltage in AC output; 57 Current sensor failed; 58 Output voltage too low; 59 PV voltage over limitation.
## Warning codes (sec 6): 01 Fan locked when inverter on; 02 Over temperature; 03 Battery over-charged; 04 Low battery; 07 Overload; 08 Discharge current limiting; 10 Output power derating; 15 PV energy is low; 16 High AC input (>280VAC) during BUS soft start; E9 Battery equalization; bP Battery not connected.

## Specs (3.2K24V column)
Line mode: 230Vac nominal; low loss 170±7 (narrow)/90±7 (wide); low return 180/100; high loss 280±7; high return 270±7; max AC input 300Vac; 50/60Hz auto; low loss freq 40±1, return 42±1; high loss 65±1, return 63±1; transfer 10ms (narrow)/20ms (wide); efficiency >95%; derating below 170V input.
Inverter mode: 3200VA/3200W pure sine; 230Vac±5%; 50/60Hz; peak eff 93%; overload 10s@110-130%, 5s@130-200%, 200ms@>=200%; surge 2x for 5s; cold start 23.0Vdc; low DC warning 22.0V (<50% load)/21.0V (>=50%); warning return 22.5/22.0V; low DC cut-off 20.5V (<50%)/20.0V (>=50%); high DC recovery 32.0V; high DC cut-off 33.0V.
Charge mode: 3-step; AC charging current 2/10/20/30/40/50/60/70/80A; bulk flooded 29.2V, AGM 28.2V; float 27.0V; max charging current (solar+AC) 100A.
Solar mode: PV max power 3500W; MPPT range 30-450VDC; start-up 70±5VDC; nominal 260VDC; max PV Voc 500VDC; max PV input current 15A.
General: CE; -10..50C operating; -15..60C storage; 5-95% RH non-condensing; 344x255x105 mm; net 5.5 kg, gross 7 kg (3.2K). Battery cable 1x3AWG/35mm2, 145A max discharge; AC breaker 30A for 3.2KW; AC/PV wire 12AWG/4mm2.

## Chassis photo (images/6.jpg) — CONFIRMED on the actual unit
Labels under the two RJ45 jacks: "COM" (left, free) and "BMS" (right, grey battery cable plugged in). Silkscreen diagram: COM: RXD->pin 1, TXD->pin 2, GND->pin 8. BMS: 485-B->pin 1, 485-A->pin 2, CAN-H->pin 4, CAN-L->pin 5. Matches the manual. Also visible: AC IN L/N, AC OUT L/N, BATTERY +/-, PV IN +/-, two earth screws, ON/OFF rocker, two fans. Bottom-entry wiring; a universal multi-socket is at the left of the installation.
