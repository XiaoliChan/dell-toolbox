# Dell Toolbox

Open-source control center for Dell G-series laptops: thermal profiles, live
mode monitoring, fan control, charging modes and power overrides. A free
replacement for Alienware Command Center (AWCC) and Dell Power Manager, built
on the native Dell firmware interfaces those apps use - no Dell services, no
Dell Command | Configure, no telemetry.

Primary target: **Dell G3 3590** (i5-9300H / i7-9750H + GTX 1650/1660 Ti).
Other AWCC-capable Dell G / Alienware machines likely work - check with
`dell-toolbox.exe --doctor`.

## Features

**Thermal**
- Six profiles executed by the BIOS: Quiet / Cool / Optimized (Balanced) /
  Ultra Performance / G-Mode / Custom - the same profiles AWCC and Dell Power
  Manager expose, written through the native AWCC WMI interface
- Live profile monitoring: a mode set in AWCC or Dell Power Manager is picked
  up within a second (G-Mode included) and shown in the app and tray
- Fan curves: read-only reference curves per mode, plus Custom with Easy
  (per-fan minimum speed) and Advanced (temperature/fan curve editor) styles,
  with reset to defaults
- Windows power plan follows the thermal profile (toggleable):

| Scenario | Thermal profile | Windows plan |
|---|---|---|
| Gaming / full power | G-Mode | High Performance |
| Sustained heavy load | Ultra Performance | High Performance |
| Everyday use | Optimized | Balanced |
| Quiet / cool | Quiet or Cool | Balanced |

**Performance**
- Windows power plan switching (powercfg)
- Named manual power-limit profiles (New / Rename / Delete / Save)

**Battery**
- Charging modes (Adaptive / Standard / Express / Primarily AC / Custom with
  start-stop limits) through the Dell ACPI-WMI channel - the same path Dell
  Power Manager's service uses
- Full battery information: health, cycle count, design / full-charge /
  remaining capacity, voltage, live power flow

**Everywhere**
- Coexists with AWCC / Dell Power Manager: while they run, the app switches
  to monitor-only and follows their state; control resumes when they close
- Tray icon with live CPU/GPU temperatures, synced profile radios and
  change notifications
- `--doctor` hardware triage: administrator check, every Dell interface,
  live thermal profile and charging mode readback

## Download

Portable zip (no installer) from
[Releases](https://github.com/XiaoliChan/dell-toolbox/releases): unzip, run
`dell-toolbox.exe`, accept the administrator prompt. Settings and logs live
next to the executable.

## Building

Windows (MSVC + Qt 6.8, Qt Charts not required):

```
cmake -B build -DDTB_HAL=win
cmake --build build --config Release
```

Development on Linux builds the core with a scripted mock HAL (win-only
backends compile in CI):

```
cmake -B build -DDTB_HAL=mock
cmake --build build
ctest --test-dir build
```

## Documentation

- [`docs/awcc-analysis/`](docs/awcc-analysis/) - byte-level reverse
  engineering of AWCC's WMI protocol, Dell Power Manager's ACPI "DA" channel
  (BDat/BFn), the OC/power-limit path with its safety rails, and how the two
  apps' profile systems relate. This is the knowledge base behind every
  hardware call in the code and the reference for implementing the remaining
  power-limit writes safely.
- [`docs/checklists/win-smoke.md`](docs/checklists/win-smoke.md) - real-machine
  smoke checklist run before each release.

## License

GPLv3 - see [LICENSE](LICENSE).
