# Wiring and Electrical Integration

This document describes the connection plan represented by the supplied firmware and photographs. Confirm every module's printed voltage rating and pin labels before applying power.

## Signal connections

### Shared I2C bus

| ESP32 | LCD backpack | SHT31 | DS3231 |
|---|---|---|---|
| GPIO 21 SDA | SDA | SDA | SDA |
| GPIO 22 SCL | SCL | SCL | SCL |
| GND | GND | GND | GND |

The SHT31 normally appears at address `0x44` or `0x45`, the LCD at `0x27`, and the DS3231 at `0x68`.

**Logic-level safeguard:** many 5 V LCD backpacks include pull-up resistors that raise SDA and SCL to 5 V. ESP32 pins are 3.3 V only. Use a bidirectional I2C level shifter, power a compatible backpack at 3.3 V, or modify the pull-ups so the bus is pulled to 3.3 V.

### PMS5003 UART

| PMS5003 | ESP32 | Note |
|---|---|---|
| TX | GPIO 16 / RX2 | Sensor data into ESP32 |
| RX | Not used | Not required by this firmware |
| VCC | Regulated 5 V | Follow the sensor's datasheet |
| GND | Common ground | Required for the UART reference |

Do not connect a 5 V logic output directly to an ESP32 input. Verify that the selected PMS board exposes 3.3 V-compatible UART signaling.

### MicroSD SPI

| MicroSD module | ESP32 |
|---|---:|
| CS | GPIO 5 |
| SCK | GPIO 18 |
| MISO | GPIO 19 |
| MOSI | GPIO 23 |
| GND | GND |

Power the module according to its design. A board with a regulator and level shifting may accept 5 V power; a bare 3.3 V module must not. Its logic presented to the ESP32 must remain 3.3 V-compatible.

### MOSFET and centrifugal blower

| Connection | Destination |
|---|---|
| ESP32 GPIO 25 | MOSFET/PWM control input |
| ESP32 GND | MOSFET control ground and external-supply ground |
| External supply positive | Blower positive, through an appropriately rated fuse |
| Blower negative | MOSFET switched load terminal, according to the module labels |
| External supply negative | MOSFET power ground/common ground |

The blower must use an external supply matching its rated voltage and current. Never power the blower from the ESP32's 3.3 V or 5 V pin.

## Protection and assembly notes

- Fit a fuse close to the external blower supply.
- Use a MOSFET module rated above the blower's startup current and supply voltage.
- Confirm whether the selected module already includes a flyback diode or transient suppression; add suitable protection when required by the load/module design.
- Keep the blower's high-current wiring physically separated from I2C, UART, and sensor wires.
- Use a common reference ground, short ground returns, and adequate wire gauge for the load current.
- Add strain relief at chamber wall penetrations and protect wiring from sharp edges, moisture, animal access, and moving fan parts.
- Place exposed electronics outside the animal/exposure space or inside a suitable enclosure.
- Verify airflow direction before enabling automatic control. The firmware defaults to a blower that supplies polluted air; an exhaust/dilution blower requires the opposite fault response and may require reversed control logic.

## Recommended bring-up sequence

1. With power disconnected, check continuity and confirm there is no short between supply and ground.
2. Power the ESP32 and low-voltage sensors without the blower connected; verify UART, I2C, LCD, RTC, and SD operation.
3. Test the external blower and MOSFET from a current-limited supply, keeping the chamber disconnected.
4. Join the grounds and verify the PWM command at low duty before increasing load.
5. Connect the airflow path and confirm that increased blower duty changes chamber concentration in the expected direction.
6. Validate the configured fault state by disconnecting the PMS data lead and confirming that the system responds safely.


