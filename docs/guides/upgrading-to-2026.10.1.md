# Upgrading to NereusSDR 2026.10.1

Use this note when updating a station from 2026.10.0. It lists what changes
when you first launch, what to do on a Core, and a short list of things to
try. If you are coming from 0.5.2, read the
[2026.10.0 checklist](upgrading-to-2026.10.0.md) first. When reporting a
problem, include the installed version, operating system, radio model and
firmware, and whether you use a local or remote Core.

## Update the Core and the desktop together

The Core owns the radio, the station DSP and the accessories. The console runs
where you operate. Update both. Rotor control, the **CAT Control** page on the
phone and the Core speaker need a 2026.10.1 Core. An older Core grays them out
with a reason such as "This Core does not control a rotor. Updating the Core
may help.", and older Cores, desktops and phone apps still connect to each
other. The phone shows the new Display pages only with a 2026.10.1 Core; with
an older Core it keeps the earlier pages.

## What changes on first launch

Keep a backup of your settings before the first launch, and close the console
normally so the new version can migrate them once.

- **Two-tone is one setting.** The two-tone level (**Test > Two-Tone IMD**) is
  now a single value for the radio, and TX profiles no longer save any
  two-tone values. If your saved level was the old default of -6 dB, it
  becomes 0 dB. That is 6 dB more two-tone drive than before for anyone who
  never changed it, and it lets PureSignal calibrate. **If you use PureSignal,
  check your two-tone level and drive before the first calibration.** Any saved
  level other than -6 dB is left alone.
- **Audio choices move to the new Driver list.** Each device card now has one
  **Driver** list above **Device**, replacing the Driver API list and the
  Windows audio checkboxes. Your saved choices carry over once, and the old
  settings are kept. On a headless Linux Core, every profile moves to ALSA
  direct. After the first launch, open **File > Settings... > Audio** and
  confirm that your speaker and microphone picks are still the ones you
  expect.
- **The workspace look is the default.** Containers dock on all four edges,
  headers show or hide with an eye button, and there is no container **Lock**
  with this look. Locks you saved earlier open unlocked. Your saved layout is
  copied the first time you start, so nothing is lost.
- **On the Mac, the Mic Mode goes to Standard.** NereusSDR sets its own Mic
  Mode to Standard at each launch, which ends the long wait for the microphone
  after launch. If you picked Voice Isolation or Wide Spectrum for NereusSDR
  in Control Center, it is back at Standard. The first window can appear up to
  about a second later.
- **Attenuator and preamp are remembered.** They are saved and sent to the
  radio each time you connect. Connecting never keys the radio.
- **Each receiver keeps more of its own settings.** L/R pan, binaural,
  squelch, APF, the FM settings, the DIGL and DIGU offsets and the RTTY mark
  and shift are now saved for each receiver, so they start saving from the
  first launch. AGC-T AUTO and its offset move from each band to each
  receiver: the first time, a receiver takes the value it had saved for the
  band it opens on. Receivers B, C and D now run on the settings they show,
  so one whose saved settings differ from the defaults may sound different
  from before.
- **FM squelch reads 0 to 100.** **Squelch Threshold** on
  **File > Settings... > DSP > FM** now shows 0 to 100, where 100 is the
  tightest, instead of dB. 2026.10.0 did not save this threshold, so every
  receiver now starts at 100, the tightest. With FM squelch on it now opens;
  lower the threshold if it stays closed.
- **Display settings are now defaults plus per-pan values.** Setup's Display
  pages (now Spectrum, Levels, Waterfall, View, Peaks & Markers, FFT & Engine,
  Transmit Display and Multimeter) set the default for every pan, and each pan's
  Display pill can keep its own value, marked with an amber dot. Old page names
  such as Spectrum Defaults and Grid & Scales no longer exist. **Reset this pan**
  returns one pan to the defaults.
- **Waterfall Black Level and Color Gain are converted once.** With Clarity,
  Black Level now runs 0 to 100 with 50 at the noise floor, and Color Gain
  sets how many dB the colors cover; in the other choices they trim Low and
  High. Your saved values, and each pan's, are converted so the waterfall
  looks the same as before. If it looks different, check Levels.
- **Cal Offset is reset and stored noise floors are cleared once.** The saved
  display Cal Offset values (the Setup default and each pan's) held the
  station's offset, so they are removed and Cal Offset starts at 0 dB; the
  control stays on Setup's Spectrum page. The stored per-band noise floors
  that Clarity measured before calibration are removed, so Clarity measures
  each band again the first time you tune to it. Your radio's own calibration
  is not touched.
- **FFT size has a Sizing choice.** Sizing is **Auto**, **Hz/bin** or **Fixed**.
  If you had a Hz/bin target above zero, you start in **Hz/bin**. Otherwise you
  start in **Auto**, and the FFT Size you saved applies only if you choose
  **Fixed**. Check FFT & Engine if your spectrum looks coarser or finer than
  before. The setting is shared by every pan and every window connected to the
  Core.
- **AGC Off is quieter.** With AGC set to Off, a receiver now runs the
  **Fixed Gain** on **File > Settings... > DSP > AGC/ALC**, 20 dB unless you
  changed it. In 2026.10.0 it ran 60 dB until you changed the value, so AGC
  Off is about 40 dB quieter. Raise **Fixed Gain** if you want the old level.
- **Every receiver starts with your DSP Options sizes.** Receivers B, C and D
  did not take the block size, filter size and filter type on **File >
  Settings... > DSP > Options** until their next mode change, and ran a
  4096-sample block with a Linear Phase filter until then. They now use the
  saved values for their mode, 64, 4096 and Low Latency unless you changed
  them, so they may sound and respond differently. The FM row on that page now
  starts at 64 for receive and transmit.
- **PureSignal looks different.** The applet and the Bench show **CORRECTION**
  and **CALIBRATION FEEDBACK** with a class (Too low to High), **Advanced** now
  hides the bottom strip too, and AmpView names its curves differently. Two-tone
  IMD readouts on the display are off until you check **Show 2Tone
  measurements**.

## Core installs on a Raspberry Pi, Rock or other SBC

The Core runs as a service account with no groups of its own, so Linux sound
cards and serial adapters each need a small systemd drop-in.

- **Audio.** Every SBC install and every `.deb` package now ships or writes the
  audio drop-in. An older install that lacks it cannot open a sound card, so
  the Core speaker will not work until you add
  `/etc/systemd/system/nereusd.service.d/audio.conf` with `[Service]` and
  `SupplementaryGroups=audio`.
- **Serial.** A rotor on a GS-232 adapter, or serial CAT on the Core, needs
  `/etc/systemd/system/nereusd.service.d/serial.conf` with `[Service]` and
  `SupplementaryGroups=dialout`. The Pi 4 and Rock installers and
  the station image write it; the
  `.deb` packages do not. After adding either file, run
  `sudo systemctl daemon-reload` and `sudo systemctl restart nereusd`.
- **Rock real-time priority.** On a Rock, run the 2026.10.1 installer once and
  then restart the board. It adds a drop-in meant to keep an ssh login from
  costing the Core its real-time priority. See What to try.
- **Network receive buffer.** The 2026.10.1 packages and the Pi 4 and Rock
  scripts raise Linux's `net.core.rmem_max` to 4 MB through
  `/usr/lib/sysctl.d/10-nereusd.conf`, and never lower a limit that is already
  higher. Removing the package removes it. The README section "The system
  setting the Core's installer changes" says how to check or undo it.

The [SBC install guide](install-core-sbc.md) has the details, including the
sound card's own mixer and kernels that refuse real-time priority.
Building the PulseAudio engine from source on Linux needs
`libpulse-dev`.

## What to try

Each item is a short check you can do in a few minutes. Note the result,
including anything that surprised you.

1. **Rotor.** Open **File > Settings... > CAT & Network > Rotor**, choose your
   controller (**Yaesu GS-232A, serial port**, **Yaesu GS-232B, serial port**, **Hamlib rotctld, already running** or **Hamlib rotctld, started by the Core**), set the
   end stop and travel, and select **Save and connect**. Open the Rotor
   applet, turn to a heading, stop it, and try a preset. Right-click a spot
   and choose **Turn beam to** the call. On the iPhone or iPad, open the Rotor
   page and confirm it shows the same heading. Release a held turn button and
   confirm the rotor stops.
2. **CAT.** Open **Tools > CAT Control...** (or the CAT pages in **File >
   Settings... > CAT & Network**), assign a channel to a slice, enable one
   listener, and connect a logging or digital-mode program. Change frequency
   from the program and from NereusSDR and confirm both follow. Try the
   Hamlib rigctld port with a program that speaks it. On a headless Core, set
   CAT up from a connected desktop or from the phone's Tools tab. Confirm
   that a refused setting says why.
3. **Native audio.** Check that the **Driver** lists show your system's audio
   system and that devices appear and disappear as you plug them in. With a
   Core, pick the **Core speaker** in **Audio > Outputs** and confirm the
   level and mute match the phone's Sound panel. Disconnect and reconnect your
   microphone and confirm transmit audio returns.
4. **Workspace look.** Hide and show a container header with the eye button.
   Hold Shift (or press Ctrl+Shift+A) to arrange, dock a container on each
   edge, pop one out, pin it and return it. Save a workspace profile and
   switch to it with **Containers > Manage Workspace Profiles…**. Choose
   **Reset Default Layout** and confirm the S-Meter comes back.
5. **Receivers after a relaunch.** Give two receivers different squelch, L/R
   pan, APF and AGC-T AUTO settings, close NereusSDR normally, open it again
   and confirm each receiver comes back as you left it and sounds the way it
   did.
6. **Hermes Lite 2 through a Pi Core.** Key the radio from a window or the
   phone through a Core on a Raspberry Pi and hold it for half a minute.
   Confirm the radio stays keyed for the whole time.
7. **Rock real-time priority.** After installing on a Rock and restarting it,
   log in over ssh and run `cat /sys/fs/cgroup/cgroup.subtree_control`.
   Confirm that `cpu` is not in the list, and that the Core's log has no
   "Raised thread priority was refused" line from after the restart.
8. **Two-tone and PureSignal.** If you use PureSignal, run a two-tone test and
   a calibration at your new two-tone level and compare it with what you
   expect.
   Then check **Show 2Tone measurements** (Bench > IMD measurements), turn on
   DUP, run a brief two-tone test and read **WORST IMD3** and **WORST IMD5** on
   the rail's **Inspect** readout.
9. **RADE waveforms.** In **Containers > Edit Container**, add **Microphone
   waveform** and **Decoded speech waveform** to a container whose **Slice**
   is the transmit slice and is in RADE-U or RADE-L. Speak while transmitting
   in RADE and confirm the microphone trace moves; receive a RADE signal and
   confirm the decoded speech trace moves. On a remote desktop, confirm an
   older Core shows a reason instead of a trace.

## Reporting

Report expected behavior, actual behavior, a minimal reproduction, desktop and
Core versions, platform, radio model and firmware, the connection path and a
support bundle. Keep credentials and pairing tokens out of public reports.
Bench results for physical serial ports, a physical rotor and logging
programs on Windows and Linux are still open. Say which hardware and program
you used.
