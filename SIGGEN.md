# Function generator + scope (Vulcan)

Standout watch feature: **reverse oscilloscope** (AWG) and **normal oscilloscope**.

| Mode | What | Hardware |
|------|------|----------|
| Reverse | You define a wave → it comes out a GPIO | LEDC PWM. Sine/tri/saw = duty DDS + **RC LPF** (1kΩ + 100nF). Square is hardware PWM. |
| Normal | GPIO in → millivolts / web plot | ADC1 oneshot (ADC2 fights Wi‑Fi) |

ESP32-S3 has **no DAC**. Corz.org used GPIO 25/26 DACs on classic ESP32. We keep the *commands*, not the analog block.

One `siggen_play` state. The **SigGen** / **Scope** watch apps, the HTML console, and `sh("wave …")` / `sh("scope")` all drive the same generator and ADC.

## Watch apps

Menu → **Utilities**:

| App | Name | Controls |
|-----|------|----------|
| SigGen | `SigGenApp` | L/R field, U/D value, ENTER start/stop, BACK menu |
| Scope | `ScopeApp` | U/D pin, ENTER freeze/run, BACK menu |

Sparkline is 2×2 `SHAPE_RECT` dots (line shapes can't go up-left). Both apps poll `siggen_play_cfg()` / last ADC pin so a web Start shows up on the watch immediately.

## Web console

On boot (after Wi‑Fi): `http://<watch-ip>/`

- STA from `autoconnect.conf`, else AP **OIRIA-vulcan** / **vulcanvulcan**
- **Script** — drop or paste a `.vul`, save to `/sdcard/inbox` or RAM, run
- **Wave** — live status, sine/square/triangle/saw/noise + Corz one-liners, **Open on watch**
- **Scope** — live ADC plot (opens live), **Open on watch**

| HTTP | |
|------|--|
| `GET /status` | role, ip, wave, hz, gpio, duty, amp, running, app, scope_gpio |
| `POST /wave?wave=&hz=&duty=&pin=&amp=` | start generator |
| `POST /wave/stop` | stop |
| `POST /cmd` | Corz one-liner |
| `GET /scope.json?pin=&n=` | millivolt samples |
| `GET /apps` | registered apps + focused |
| `POST /app?name=SigGenApp` | `close_current_and_open` |

Works on **head (solo/tyrant)** and **secondary (puppet)** — same firmware. Tyrant `native("wave",…)` also UART-sends the same call to the puppet so the worker can drive the pin.

## Terminal / Vulcan

```
print(sh("apps"));
print(sh("open_app SigGenApp"));   // or open_app("SigGenApp")
print(sh("wave 2k"));
print(sh("s"));                    // sine
print(sh("scope 1"));              // ADC1 GPIO1, last/min/max mV
print(sh("stop"));
open_app("ScopeApp");
```

## Vulcan natives

```
native("wave", kind, hz, duty, pin, amp)   // 0 sine 1 square 2 tri 3 saw 4 noise
native("wave_stop")
native("wave_freq", hz)
native("wave_duty", pct)
native("sweep", f0, f1, ms)
native("adc", pin)                         // millivolts
native("scope", pin, n)
native_seq(native("wave", …), native("delay", ms), native("wave_stop"))
```

`native_seq` is the trapdoor: interned nids in one array, C loop on-device,
**one** `NSQ1` UART blob to the puppet instead of N sprintf-of-source
translates. See `os_code/core/rs_vm/NSEQ.md`.

Examples: `os_code/core/rs_vm/examples/wave.vul`, `nseq.vul`

## Corz → Vulcan

| Corz | Vulcan |
|------|--------|
| `s` / `r` / `t` | `native("wave", 0\|1\|2, …)` |
| `2000` `2k` | `native("wave_freq", 2000)` |
| `p25` | `native("wave_duty", 25)` |
| `a1..4` | amp 12/25/50/100 |
| `stop` `.` | `native("wave_stop")` |
| `scope` / `scope 1` | ADC capture status |
| loops/macros | a `.vul` file |
| musical `*a` | not ported (use Hz) |

Web `POST /cmd` still accepts the one-letter Corz line for muscle memory.

## Pins

- Default **out** GPIO **4** (LEDC)
- Default **scope** GPIO **1** (ADC1). Must be an ADC1 pad.

## Missed AWG extras (later)

Burst, gated sync, DC offset (needs extra analog), I2S PDM path (`siggen_i2s_pdm` already compiled). Sweep **is** implemented.
