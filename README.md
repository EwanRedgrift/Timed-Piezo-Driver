# Timed Piezo Driver

A tiny, battery-powered device that plays about 10 seconds of a song every few hours, hidden somewhere in a friend's apartment, to slowly drive him insane.

I built it to teach myself PCB design over the summer. It was my first project where I did everything myself: schematic, layout, firmware, and bring-up. It works, although I made plenty of mistakes (see [What went wrong](#what-went-wrong)).

## How it works

The board is built around an **STM32L031** microcontroller, chosen because the L0 family is low power and I wanted the thing to last for ages on a single **CR2032 coin cell**. Three piezo buzzers make the noise. Each one is switched on and off by its own MOSFET, which is driven by a PWM output from the STM32.

The firmware loop is simple:

1. Pick one of the songs at random and play it.
2. Read the delay switch (**S1**) to choose a long or short delay.
3. Go to sleep in STOP mode, with the RTC wake-up timer set for that delay.
4. Wake up, restore the clocks, and repeat.

Between songs, the MCU is asleep and only the RTC is running.

## Songs

Each note is a table entry giving the pitch of voice 1, the pitch of the shared voice, and the duration in milliseconds:

```c
{ NOTE_G3, NOTE_G4, 167 },  /* voice 1 (PA1), voices 2 + 3 (PA2/PA3), ms */
```

One piezo plays one note while the other two play a different one, so you get a two-note texture from three buzzers. I first tried MIDI-to-Arduino piezo converters, but they didn't suit this setup, so most of the arrangements were written by hand.

| Song | Device recording | Original |
|------|------------------|----------|
| Stay Schemin' (Rick Ross) | [![Device](https://img.youtube.com/vi/VIDEO_ID/0.jpg)](https://youtu.be/VIDEO_ID) | [![Original](https://img.youtube.com/vi/Ygb8BRdl2kI/maxresdefault.jpg)](https://youtu.be/Ygb8BRdl2kI?si=_YeiLvKVq5jGS4ok) |
| Solo (Future) | [![Device](https://img.youtube.com/vi/VIDEO_ID/0.jpg)](https://youtu.be/VIDEO_ID) | [![Original](https://img.youtube.com/vi/X2DTROC4JCI/maxresdefault.jpg)](https://youtu.be/X2DTROC4JCI?si=rSgvXdhBM6xabJ5C) |

## Hardware

The board was designed in **Autodesk Fusion** and assembled by **JLCPCB** for an exorbitant amount of money. The full parts list is in the BOM.

The buzzers are on PA1 (TIM2 CH2) and PA2/PA3 (TIM21 CH1/CH2). The delay switch S1 is on PA7. Programming is through a 6-pin header.

### Power and current draw

The whole thing runs from the CR2032, so the supply is roughly 3 V. Piezos are capacitive loads, so they draw very little current on average compared to something like an electromagnetic buzzer.

The microcontroller numbers below are from ST's STM32L031 datasheet, so treat them as typical values, not measurements of this board:

- **Asleep (STOP mode with the RTC running):** about 0.6 µA typical, and closer to 1 µA when the RTC is clocked from the internal LSI oscillator like mine.
- **Awake:** the core uses down to about 76 µA per MHz, so a couple of hundred µA at my roughly 2 MHz clock. It's only awake while a song plays.

With a delay of hours, the average current is dominated by the sleep current, so the cell should last a very long time. In practice the battery's own shelf life and any leakage elsewhere on the board matter more than the chip does.

<!-- TODO: add a measured sleep current and playing current here once I've put a meter on it -->

## Firmware

The firmware is a single `main.c` written in C using the STM32 HAL, in an **STM32CubeIDE** project. It follows the CubeMX file layout, so all hand-written code lives inside `USER CODE` blocks and survives regenerating from the `.ioc`.

### Changing the delay between songs

The two delays are compile-time constants near the top of `main.c`:

```c
#define SLEEP_LONG_SECONDS    (10U * 60U) /* Switch in the "long" position  */
#define SLEEP_SHORT_SECONDS   (1U * 60U)  /* Switch in the "short" position */
```

Edit them and rebuild. The RTC counter supports delays from 1 second up to about 18 hours. For example, `(3U * 60U * 60U)` gives roughly three hours.

The delays are approximate. The RTC runs from the chip's internal low-speed oscillator (LSI), which drifts noticeably with temperature and voltage, so "10 minutes" could easily be a minute or two off.

### Building and flashing

1. In STM32CubeIDE, choose **File → Open Projects from File System** and select the `firmware/` folder.
2. Build the project (the hammer icon).
3. Connect a ST-LINK V2 to the 6-pin header and run or debug. I use a knock-off ST-LINK V2 from Amazon, and it works fine.

## What went wrong

This was my first full project, so there were mistakes:

- **The piezos were very quiet, and the fix isn't in the PCB files.** Without a pull-down the piezos were extremely quiet. I only found this out after the boards arrived, so the schematic and layout in this repo **do not include the resistors**. I fixed it by soldering a 68 kΩ resistor across the legs of each piezo by hand, and the board works fine with them. **If you build this board, you need to add these three resistors yourself.** The 68 kΩ value has no special meaning, they were just what I had lying around. (The buzzer manufacturer's reference circuit also includes a resistor across the piezo for charging and discharging it, so it's a known part of the design.)
- **Two piezos share a timer.** I meant to give each piezo its own timer channel so they could be controlled individually, but I accidentally put two of them on the same one (TIM21). That's why the firmware has one independent voice plus one shared voice instead of three separate ones.
- **The board is an odd size.** There's no real reason. I was experimenting.
- **LSI timing is loose.** See the note above on delays. If I did it again I'd consider a proper 32.768 kHz crystal for the RTC.

## License

See [LICENSE](LICENSE).

---

*Please prank responsibly.*
