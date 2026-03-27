# Gravity Guy — Build Guide

## Overview

A side-scrolling endless runner on the DFRobot DFRduino Uno with a 16×2 LCD. A character runs automatically across the screen while solid-block obstacles scroll in from the right along the floor and ceiling. Press either button to flip gravity — the character instantly snaps between the floor (row 1) and the ceiling (row 0). Survive as long as possible. The game accelerates over time.

There is no text on screen during gameplay. The full 16×2 grid is the level. Score and high score only appear on the death and game-over screens.

---

## Parts List

| Component | Qty | Notes |
|-----------|-----|-------|
| DFRobot DFRduino Uno | 1 | ATmega328p, 16 MHz |
| DFRobot Gravity LCD1602 V1.1 | 1 | I2C RGB, address 0x6B |
| Small breadboard | 1 | 30-row half-size is fine |
| 4-pin pushbutton | 2 | Normally open, tactile |
| Red LED (5mm) | 2 | Standard, ~1.8V forward voltage |
| 220Ω resistor | 2 | For LEDs (anything 220–330Ω works) |
| Jumper wires | ~12 | Male-to-male recommended |

---

## Step-by-Step Hardware Wiring Procedure

The hardware is identical to the Samurai Standoff build. If you already have it wired, no changes are needed.

### Step 1 — Set Up the Breadboard Power Rails

1. Place the breadboard in front of you with the Arduino to its left.
2. Run a wire from the Arduino **5V** pin to the breadboard **+ (red) power rail**.
3. Run a wire from the Arduino **GND** pin to the breadboard **− (blue) ground rail**.
4. If your breadboard has split rails (gap in the middle), bridge them with short jumper wires.

### Step 2 — Wire the LCD (I2C Connection)

1. **LCD GND** → Arduino **GND** (or breadboard ground rail).
2. **LCD VCC** → Arduino **5V** (or breadboard power rail).
3. **LCD SDA** → Arduino **A4** (SDA).
4. **LCD SCL** → Arduino **A5** (SCL).

### Step 3 — Wire Flip Button A (Pin 2 / INT0)

This is the same button that was "ATK" in the previous project.

1. Place the first pushbutton across the **centre channel** of the breadboard (rows 5–6, straddling columns e and f).
2. Connect one leg to the breadboard **ground rail**.
3. Connect the diagonally opposite leg to Arduino **digital pin 2**.
4. No external pull-up resistor needed — the sketch enables the ATmega's internal pull-up.

### Step 4 — Wire Flip Button B (Pin 3 / INT1)

This is the same button that was "BLK" in the previous project. Both buttons now do the same thing — either one flips gravity.

1. Place the second pushbutton across the centre channel a few rows below the first.
2. Connect one leg to the **ground rail**.
3. Connect the diagonally opposite leg to Arduino **digital pin 3**.

### Step 5 — Wire LED 1 (Pin 12)

1. Place a red LED in the breadboard (e.g., row 18). The **longer leg is the anode (+)**.
2. Connect a **220Ω resistor** from Arduino **digital pin 12** to the LED's **anode**.
3. Connect the **cathode** (short leg) to the **ground rail**.

**Resistor calculation:** R = (5V − 1.8V) / 0.02A = 160Ω. A 220Ω is safe and well within limits.

### Step 6 — Wire LED 2 (Pin 13)

1. Place the second red LED below LED 1 (e.g., row 22).
2. Connect a **220Ω resistor** from Arduino **digital pin 13** to the anode.
3. Connect the cathode to the **ground rail**.

Note: Pin 13 also drives the onboard LED, so both flash together on death.

### Step 7 — Final Check Before Power-On

1. **No short circuits** — check that no bare wire ends touch each other.
2. **LED polarity** — long leg (anode) toward resistor/pin, short leg to GND.
3. **Button orientation** — button straddles the centre channel gap.
4. **LCD connections** — SDA → A4, SCL → A5, VCC → 5V, GND → GND.
5. **Power rails** — 5V to red rail, GND to blue rail.

### Wiring Summary Table

| Component | Arduino Pin | Breadboard Connection |
|-----------|------------|----------------------|
| LCD SDA | A4 | Direct wire |
| LCD SCL | A5 | Direct wire |
| LCD VCC | 5V | Direct wire |
| LCD GND | GND | Direct wire |
| Flip button A leg 1 | — | Ground rail |
| Flip button A leg 2 | Pin 2 (INT0) | Direct wire |
| Flip button B leg 1 | — | Ground rail |
| Flip button B leg 2 | Pin 3 (INT1) | Direct wire |
| LED 1 anode | Pin 12 (via 220Ω) | Resistor in series |
| LED 1 cathode | — | Ground rail |
| LED 2 anode | Pin 13 (via 220Ω) | Resistor in series |
| LED 2 cathode | — | Ground rail |

---

## Software Setup

### Step 1 — Install the Arduino IDE

Download from [https://www.arduino.cc/en/software/](https://www.arduino.cc/en/software/) if you don't have it already.

### Step 2 — Install the DFRobot LCD Library

1. Open Arduino IDE.
2. Go to **Sketch → Include Library → Manage Libraries**.
3. Search for **DFRobot_RGBLCD1602**.
4. Install the library by DFRobot.

### Step 3 — Load the Sketch

1. Open `gravity_guy.ino` in the Arduino IDE.
2. Go to **Tools → Board** and select **Arduino Uno**.
3. Go to **Tools → Port** and select the COM port your Arduino is connected to.
4. Click **Verify** (checkmark icon) to compile. It should compile with no errors.
5. Click **Upload** (arrow icon) to flash the program to the board.

---

## Sprite System

All 8 HD44780 CGRAM custom character slots are used. Characters are loaded once in `setup()` and never modified at runtime.

### Slot Allocation

| Slot | Name | Description |
|------|------|-------------|
| 0 | `CHAR_FLOOR_A` | Player on floor — stride A (right leg forward) |
| 1 | `CHAR_FLOOR_B` | Player on floor — stride B (left leg forward) |
| 2 | `CHAR_CEIL_A` | Player on ceiling — stride A (vertical flip of floor A, feet at top) |
| 3 | `CHAR_CEIL_B` | Player on ceiling — stride B (vertical flip of floor B, feet at top) |
| 4 | `CHAR_WALL` | Full 5×8 solid block for obstacles |
| 5–7 | — | Unused / reserved |

### Why a Custom Wall Character

The HD44780 controller ships in two ROM variants (A00 and A02). Character code 0xFF maps to a full-fill block on A02 but to a different glyph on A00. A custom `CHAR_WALL` character — all 40 pixels set — is used instead to guarantee consistent obstacle rendering regardless of which ROM variant is on your specific LCD.

### Running Animation

`CHAR_FLOOR_A` and `CHAR_FLOOR_B` show opposite leg-stride positions. The active frame is toggled by `anim_frame` every game tick, so the character's legs step in sync with the scroll speed — they run faster as the game accelerates.

Ceiling sprites are exact vertical flips of their floor counterparts: feet at the top of the cell (touching the ceiling), head at the bottom hanging toward the floor.

---

## Screen Layout

```
Col:  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
Row0: _  P  ·  ·  ·  ·  ·  ·  ·  ·  ·  ·  ·  ·  ·  ·   ← ceiling
Row1: _  P  ·  ·  ·  ·  ·  ·  ·  ·  ·  ·  ·  ·  ·  ·   ← floor

_ = always blank (visual margin behind player)
P = player sprite (one row occupied based on current gravity)
· = CHAR_WALL block or space, scrolling left each tick
```

Column 0 is always blank. Column 1 is always the player. Columns 2–15 are the obstacle field. No text is visible during gameplay.

---

## Finite State Machine Design

The game uses the **Garbini FSM method** with a state transition action table.

### States

| State | Description |
|-------|-------------|
| TITLE_SCREEN | Ceiling sprite on row 0, floor sprite on row 1, both at col 0. Title text in cols 1–15. Press either button to start. |
| PLAYING | Obstacles scroll right-to-left every game tick. Player at col 1. Press either button to flip gravity. |
| PLAYER_DEAD | Collision detected. "YOU CRASHED!" + score shown. Both LEDs flash ~10 Hz for 2.5 s. |
| GAME_OVER | High score displayed. Press either button to return to title. |

### Events

| Event | Source |
|-------|--------|
| EVENT_ENTRY | State just changed (detected in `get_event()`) |
| EVENT_FLIP_PRESS | External interrupt on pin 2 (INT0) or pin 3 (INT1), 50 ms debounce |
| EVENT_TICK | Timer1 ISR: `tick_countdown` reached zero |
| EVENT_TIMER_FLASH | Timer2 ISR: LED flash sub-counter fired |
| EVENT_TIMER_DISPLAY | Timer1 ISR: `display_countdown` reached zero |

### Two Collision Paths

Collisions can be detected at two points in the FSM:

**1. On tick** — `action_game_tick` shifts obstacles left, redraws the screen (the fatal obstacle appears at col 1), then checks whether `obstacles[PLAYER_COL]` overlaps the player's row. If yes: `current_state = PLAYER_DEAD`.

**2. On flip** — `action_flip_gravity` toggles `player_on_ceiling`, then immediately checks whether the new row at col 1 already has an obstacle. If the player flipped into a wall: `current_state = PLAYER_DEAD`. The flip-into-wall kill is intentional — it punishes mistimed flips.

### State Transition Diagram

```
            ┌──────────────┐
 flip press │ TITLE_SCREEN │
 ┌─────────►│  ceil sprite │
 │          │  floor sprite│
 │          └──────┬───────┘
 │                 │ flip press
 │                 ▼
 │          ┌──────────────┐◄── flip press (gravity toggle)
 │          │   PLAYING    │◄── tick (scroll + redraw + check)
 │          └──────┬───────┘
 │     collision   │
 │  (tick or flip) │
 │                 ▼
 │          ┌──────────────┐
 │          │ PLAYER_DEAD  │
 │          │  LED flash   │
 │          └──────┬───────┘
 │                 │ display timer expired
 │                 ▼
 │          ┌──────────────┐
 └──────────│  GAME_OVER   │
 flip press │   "Hi:XX"    │
            └──────────────┘
```

### Obstacle Generation

`generate_obstacle()` is called once per tick, producing the new column at col 15.

- `gap_counter` enforces mandatory clear-column gaps after each obstacle, preventing impassable pairs. The gap shrinks from 2 to 1 at score ≥ 50.
- After a floor obstacle, the next one is 80% likely to be a ceiling obstacle (vice versa). This creates readable alternating sequences — you see a floor wall coming and know to prepare a flip toward the ceiling.
- Base obstacle probability scales from 35% (score 0–4) to 60% (score ≥ 80).

### Speed / Difficulty Tiers

| Score | ms per tick | Approach time (14 cols) |
|-------|------------|------------------------|
| 0–4 | 350 ms | 4.9 s |
| 5–14 | 300 ms | 4.2 s |
| 15–29 | 250 ms | 3.5 s |
| 30–49 | 200 ms | 2.8 s |
| 50–74 | 160 ms | 2.2 s |
| 75–99 | 130 ms | 1.8 s |
| 100+ | 110 ms | 1.5 s (minimum) |

---

## Testing Procedure

### Test 1 — Power-On and Title Screen

1. Plug in the USB cable.
2. **Expected:**
   - Row 0, col 0: ceiling player sprite (upside-down, feet at top).
   - Row 1, col 0: floor player sprite (upright, head at top).
   - Cols 1–15: `" GRAVITY GUY   "` on row 0, `"   FLIP=START  "` on row 1.
3. Both LEDs should be **OFF**.

### Test 2 — Game Start and Clear Runway

1. Press either button.
2. **Expected:** Title clears. Player sprite appears at col 1, row 1 (floor). Columns 2–15 are blank for the first 10 ticks (clear runway from `START_RUNWAY = 10`).
3. The player sprite should alternate between two leg positions each scroll tick.

### Test 3 — First Obstacle Appears

1. After 10+ ticks, a solid block (`CHAR_WALL`) appears at col 15 on either row 0 or row 1.
2. Watch it scroll one column left per tick.
3. **Expected:** The block moves steadily from right to left. If it's on your row, flip before it reaches col 1.

### Test 4 — Correct Flip

1. While on the floor (row 1), spot an obstacle on row 1 several columns away.
2. Press either button to flip to the ceiling (row 0).
3. **Expected:** Player sprite immediately moves to row 0. The obstacle scrolls through col 1 on row 1 without triggering death.

### Test 5 — Timeout Death (No Flip)

1. Start a game and let an obstacle approach your current row without pressing any button.
2. **Expected:** When the obstacle reaches col 1, the game shows "YOU CRASHED!" on row 0, score on row 1, and both LEDs begin flashing at ~10 Hz. After ~2.5 s, game over screen appears.

### Test 6 — Flip-Into-Wall Death

1. Wait for an obstacle on the opposite row to be sitting at column 1 (or a nearby column).
2. Press the flip button to jump into it.
3. **Expected:** Immediate death — `action_flip_gravity` detects the col-1 obstacle in the new row. Same death sequence as Test 5.

### Test 7 — Leg Animation Check

1. Start a game and do not press any button.
2. Observe the player sprite at column 1.
3. **Expected:** The sprite's lower half alternates between two leg positions on every tick. At slow speed (score 0–4), the animation is slow. At high speed, it runs faster.

### Test 8 — Difficulty Scaling

1. Survive 30+ ticks. The scroll speed should increase noticeably.
2. At score 50+, the game requires quick reflexes.
3. At score 100+, you are at the minimum 110 ms tick rate.
4. Verify no freezing, display corruption, or sprite glitches at high speed.

### Test 9 — High Score Persistence

1. Die with a score of 20. Note "Hi:20" on game over.
2. Restart and die with a score of 8. **Expected:** Game over still shows "Hi:20".
3. Restart and score 25. **Expected:** High score updates to 25.

### Test 10 — Rapid Button Mashing

1. During gameplay, press both buttons rapidly and alternately.
2. **Expected:** No crash, no garbage display. The 50 ms debounce limits flips to one per 50 ms. The game remains stable.

### Test 11 — Full Stress Run

1. Play at least 10 full games including reaching score 50+.
2. Verify the LCD updates correctly every tick, LEDs flash correctly on death, and the game restarts cleanly from game over to title screen.

---

## Troubleshooting

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| LCD blank / no backlight | Wrong I2C address or wiring | Check SDA→A4, SCL→A5; try address 0x60 instead of 0x6B |
| Obstacles appear as wrong glyph | CGRAM slot mismatch or ROM variant issue | Power cycle; confirm `CHAR_WALL = 4` is the 5th `customSymbol()` call in `setup()` |
| Sprites look corrupted | CGRAM write interrupted | Power cycle; all `customSymbol()` calls must complete before gameplay |
| Player doesn't flip | Button wired wrong | Confirm button straddles centre gap; test continuity with multimeter |
| Flip feels delayed | Debounce window too long | Reduce `50` ms in `isr_flip_a` / `isr_flip_b` to `30` ms |
| Game starts at wrong speed | `SPEED_START` value changed | Confirm `#define SPEED_START 350` in tuning constants |
| Death triggers on startup | Button bounce during power-on | Add `delay(200)` at end of `setup()` before `attachInterrupt()` calls |
| LEDs don't flash on death | Reversed polarity | Long leg (anode) toward resistor, short leg to GND |
| `snprintf` output looks garbled | AVR stdlib quirk | Replace `snprintf` + `lcd.print(buf)` with sequential `lcd.print()` calls for each field |
| Compile error: library not found | Missing DFRobot library | Install DFRobot_RGBLCD1602 via Library Manager |
