# Changes

Local modifications on top of upstream Lora-net/sx1302_hal (v2.0.1).
Target setup: SX1302 gateway on a Raspberry Pi with an L76x GPS module.

## libloragw

- `libloragw/src/loragw_gps.c`
  - `lgw_gps_enable`: no longer sends the UBX `NAV-TIMEGPS` configuration
    message, since the L76x does not understand UBX (the code is commented out).
  - `lgw_parse_nmea`: the RMC sentence is now accepted with 13 or 14 fields
    (NMEA 4.10 adds a field) instead of exactly 13.

## packet_forwarder

- `packet_forwarder/src/lora_pkt_fwd.c`
  - `gps_process_sync`: `lgw_gps_sync` receives the UTC time as the GPS time,
    because the L76x does not provide native GPS time.
  - `thread_gps`: `gps_process_sync()` is also called after each RMC frame,
    so the time reference is refreshed with every RMC sentence.
- `packet_forwarder/global_conf.json` (new): gateway configuration
  (SPI `/dev/spidev0.0`, channel plan, and Things Network EU1 server).
- `packet_forwarder/reset_lgw.sh` (new): resets the SX1302 through GPIO 23
  using `pinctrl` (Raspberry Pi OS), replacing the sysfs-based reset.

## util_chip_id

- `util_chip_id/reset_lgw.sh` (new content): same `pinctrl`-based reset script
  as in `packet_forwarder`.
- `util_chip_id/reset_lgw.sh.bkp` (new): backup of the original sysfs-based
  reset script shipped by upstream.
