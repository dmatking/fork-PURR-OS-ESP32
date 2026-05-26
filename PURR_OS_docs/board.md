# PURR OS Hardware — Board Reference

Source: Schematic PDF (KiCad), 6 pages.

---

## Page 1 — Top-Level / Power

### Power Input
| Ref | Part | Notes |
|-----|------|-------|
| J101 | USB_C_Receptacle_USB2.0 | TypeC power input |
| C101, C102 | 100n | VBUS filter caps |
| R101 | 2.2K 1% | Discharge resistor — required for Type-C PSUs |
| D101 | RCLAMP0502BA | ESD on USB_C D+/D- |
| TP101 | — | VBUS test point |

**Signals:** VBUS → +5V rail. CC1/CC2 passthrough to CM5.

### GPIO Voltage Select
- Jumper selects 3V3 or 1V8 for CM5 GPIO_VREF
- TP103 = +3V3, +1V8 available

### CM5 Connections (Page 1 summary)
| Signal | Direction | Notes |
|--------|-----------|-------|
| +5V_PI | → CM5 | Via TP102, discharge R101 |
| USB_C[USB] | ↔ CM5 | USB2 data passthrough |
| GPIO6, GPIO7 | CM5 | General IO |
| GPIO4 = UART2_RX | CM5 ← LoRa | LoRaWAN UART |
| GPIO5 = UART2_TX | CM5 → LoRa | LoRaWAN UART |
| MCU_SDAC / MCU_SCLC | CM5 ↔ LoRa | I2C to LoRa module |
| CID_SD / CID_SC | CM5 | Card ID |
| GLOBAL_EN, PWR_BUT | CM5 | Power control |
| nRESET, PWR_BUT | IO header | Physical buttons |
| PWM, TACHO | CM5 ↔ IO | Fan control |
| SDA, SCL | CM5 ↔ IO | I2C bus |
| nPWR_LED, nACTIVITY_LED | CM5 → IO | Active-low LED drives |
| IO5, IO6 | CM5 ↔ IO | Breakout |
| EEPROM_nWP | — | 10K pull-up to 3V3 |
| VBUS_EN, USB2P[USB] | CM5 ↔ USB | Downstream USB2 enable |

---

## Page 2 — CM5 Connectors

**Part:** Amphenol 2× 10164227-1001A1RLF (Module301 + Module302), 200 pins total.

### Module301 (pins 1–100)
Ethernet (4 pairs), GPIO0–GPIO27, SD card (CMD/CLK/DAT0–7), ID_SC/SD, FAN, LED, EEPROM_nWP, PWR_BUT, CC1/CC2, GLOBAL_EN, VBAT, GPIO_VREF, 3V3/1V8 outputs, BT_disable, WiFi_disable, CAM_GPIO0/1, PMIC_ENABLE.

### Module302 (pins 101–200)
PCIe (CLK, TX, RX, EN, nRST, nWAKE, PWR_EN), VBUS_EN, USB_OTG_ID, USB2 (P/N), USB3-0/1 (RX/TX pairs), HDMI0/1 (full differential pairs + CEC + hotplug + SDA/SCL), MIPI DSI (4 lanes), MIPI CSI (4 lanes), High Speed Serial.

---

## Page 3 — Keyboard Matrix + ESP32

### Keyboard Matrix
- **6 rows × 14 columns = 84 keys** (MX1–MX83)
- Each key: D_Small diode (anti-ghosting), values TBD
- Labels: ROW0–ROW5, COLUMN0–COLUMN13

### ESP32-S2-WROVER (U1) — Keyboard Controller
| Pin | Signal | Notes |
|-----|--------|-------|
| IO0–IO6 | COLUMN8–COLUMN13, ROW0 | Column/row scan |
| IO7–IO13 | COLUMN7–COLUMN1 | Column scan |
| IO14–IO18 | ROW1–ROW5 | Row scan |
| IO21 | COLUMN0 | |
| IO22 | USB_D+ | Native USB HID output |
| IO23 | USB_D- | Native USB HID output |
| C103, C104 | 100n | Decoupling |
| VDD | 3V3 | |

**Role:** Pure USB HID keyboard controller. Not running PURR OS.

---

## Page 4 — IOs

### Buttons
| Ref | Part | Signal |
|-----|------|--------|
| SW601 | B3U-3000P | nRESET (R601 DNI — optional pull) |
| SW602 | B3U-3000P | PWR_BUT |

### LEDs
| Ref | Driver | Signal | Color |
|-----|--------|--------|-------|
| D605 | 74LVC1G075E-7 (U602), R605 3K3 | nPWR_LED | Green |
| D606 | 74LVC1G075E-7 (U603), R606 3K3 | nACTIVITY_LED | Green |

Both active-low, open-drain buffers.

### Fan
- **J601:** JST_SHM04B-SRSS-1B — 4-pin (5V, PWM, TACHO, GND)
- PWM + TACHO routed to CM5

### Temperature Sensor
- **U601:** SHTC3 — I2C (SDA/SCL), 4.7µF + 100n decoupling

### I2C Breakout
- **J502:** 6-pin I2C header (3V3, SDA, SCL, IO5, IO6, GND)
- ESD: D501, D502 (RCLAMP0502BA)
- Pull-ups: R501, R502 (to 3V3)

---

## Page 5 — LoRaWAN

### Module
- **U502:** RAK3172-B-SM-I (STM32WL inside)

| Pin | Signal | Notes |
|-----|--------|-------|
| PA2/UART2_TX | → UART2_RXD (CM5) | AT command TX |
| PA3/UART2_RX | ← UART2_TXD (CM5) | AT command RX |
| PA12/I2C_SCL | MCU_SCLD | I2C |
| PA11/I2C_SDA | MCU_SDAD | I2C |
| RST | RAK_RST | Reset line |
| BOOT0 | JP501 | Boot mode jumper |
| VDD | 3V3 | |

**UART protection:** D503, D504 series resistors (1K) on RX/TX lines.

### SWD Debug
- **U503:** SWD interface (RESET, SWCLK, SWDIO, BOOT)
- **R503:** 10K pull-down on BOOT

### Reset + LED
- **SW501:** Push button → RAK_RST (R504 10K, C506 100nF debounce)
- **D505:** LED status, driven via Q501 (DTC043ZEBTL NPN) + R507 4.7K

### Decoupling
C503 4.7µF, C504/C505 100n on 3V3 rail.

---

## Page 6 — USB 2.0 Downstream

### Connector
- **USB701:** TYPEC-305-ACP16H458 — USB-C downstream port
- Data: USB2C_D_N / USB2C_D_P
- R702, R703: 22Ω series on D+/D-

### Current Limiting
- **U701:** AP22653W6
  - IN ← +5V, EN ← VBUS_END (CM5-controlled)
  - OUT → VBUS (downstream)
  - ILIM: 15K resistor
  - nFault: open-drain fault flag
- C701 10µF input, C702 100µF + C703 10µF output

### ESD
- **D701:** RCLAMP0502BA on USB2C_D_N / USB2C_D_P

### CC Lines
CC1, CC2 passthrough with 22Ω (R702/R703 area).

---

## Key Signal Summary

| Signal | From | To | Purpose |
|--------|------|----|---------|
| UART2_RX/TX | CM5 GPIO4/5 | RAK3172 | LoRaWAN AT commands |
| MCU_SDA/SCL | CM5 | RAK3172 + SHTC3 + J502 | I2C bus |
| USB_C[USB] | TypeC input J101 | CM5 USB2 | USB2 passthrough |
| USB2P[USB] | CM5 | USB701 downstream | Peripheral USB-C port |
| VBUS_EN | CM5 | AP22653W6 EN | Downstream USB power gate |
| PWM/TACHO | CM5 | J601 fan | Fan speed control |
| nPWR_LED / nACTIVITY_LED | CM5 | D605/D606 | Status LEDs |
| nRESET / PWR_BUT | SW601/SW602 | CM5 | Physical controls |
| EEPROM_nWP | — | 10K → 3V3 | EEPROM write protect |
| GPIO_VREF | Jumper | CM5 | 1V8 or 3V3 GPIO voltage |
| USB HID | ESP32-S2 IO22/23 | Host PC/CM5 | Keyboard input |

---

## Power Rails
| Rail | Source | Consumers |
|------|--------|-----------|
| +5V | J101 VBUS | CM5 +5V_PI, fan J601, USB downstream |
| +3V3 | CM5 output | RAK3172, SHTC3, ESP32-S2, LEDs, I2C pull-ups |
| +1V8 | CM5 output | Optional GPIO_VREF (jumper selectable) |
