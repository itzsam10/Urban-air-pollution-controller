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

### MOSFET and Chamber 1 fresh-air blower

| Connection | Destination |
|---|---|
| ESP32 GPIO 25 | MOSFET/PWM control input |
| ESP32 GND | MOSFET control ground and external-supply ground |
| External supply positive | Blower positive, through an appropriately rated fuse |
| Blower negative | MOSFET switched load terminal, according to the module labels |
| External supply negative | MOSFET power ground/common ground |

The PWM-controlled blower is part of the Chamber 1 filtered-air path. It must use an external supply matching its rated voltage and current. Never power the blower from the ESP32's 3.3 V or 5 V pin.

The mixing/circulation fan associated with Chamber 2 should use its own correctly rated supply or driver. No separate ESP32 control pin for that fan is documented in the supplied firmware.

## Protection and assembly notes

- Fit a fuse close to the external blower supply.
- Use a MOSFET module rated above the blower's startup current and supply voltage.
- Confirm whether the selected module already includes a flyback diode or transient suppression; add suitable protection when required by the load/module design.
- Keep the blower's high-current wiring physically separated from I2C, UART, and sensor wires.
- Use a common reference ground, short ground returns, and adequate wire gauge for the load current.
- Add strain relief at chamber wall penetrations and protect wiring from sharp edges, moisture, animal access, and moving fan parts.
- Place exposed electronics outside the animal/exposure space or inside a suitable enclosure.
- Verify airflow direction and driver polarity before enabling automatic control. Confirm experimentally whether a larger PWM value increases or decreases filtered-air delivery from Chamber 1.

## Recommended bring-up sequence

1. With power disconnected, check continuity and confirm there is no short between supply and ground.
2. Power the ESP32 and low-voltage sensors without the blower connected; verify UART, I2C, LCD, RTC, and SD operation.
3. Test the external blower and MOSFET from a current-limited supply, keeping the chamber disconnected.
4. Join the grounds and verify the PWM command at low duty before increasing load.
5. Connect Chamber 1 to the filtered-air inlet of Chamber 2 and confirm how the PWM command changes airflow and Chamber 3 particulate concentration.
6. Validate the configured fault state by disconnecting the PMS data lead and confirming that the system responds safely.

## Chamber placement

- **Chamber 1:** filtered ambient-air intake and PWM-controlled airflow stage.
- **Chamber 2:** three-port mixing chamber with diesel-exhaust inlet, filtered-air inlet, and mixed-stream outlet.
- **Chamber 3:** exposure chamber containing the particulate and environmental measurement points.

The PMS5003 and SHT31 should sample representative Chamber 3 air without being placed directly in a high-velocity inlet jet. Electronics and exposed conductors should remain outside the occupied chamber volume.


