# Samurai Standoff — Build Guide

## Overview

A samurai standoff arcade game for the DFRobot DFRduino Uno with a 16×2 LCD. Two full-height samurai sprites face off across the screen. After a random delay, the enemy changes pose — a **LUNGE** means you must **BLOCK**; a **GUARD** means you must **ATTACK**. React to the visual cue before the timer runs out. Wrong button or too slow means death. Difficulty increases every round.

There are no text cues ("ATK!" / "BLK!") in the game. You read the enemy's body language and respond. The enemy's idle, attack, and guard poses are all distinct 10×5 pixel sprites occupying both LCD rows.

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

### Step 1 — Set Up the Breadboard Power Rails

1. Place the breadboard in front of you with the Arduino to its left.
2. Run a wire from the Arduino **5V** pin to the breadboard **+ (red) power rail**.
3. Run a wire from the Arduino **GND** pin to the breadboard **− (blue) ground rail**.
4. If your breadboard has split rails (gap in the middle), bridge them with short jumper wires so both halves share power and ground.

### Step 2 — Wire the LCD (I2C Connection)

The DFRobot Gravity LCD1602 V1.1 uses a 4-pin Gravity connector (I2C). Connect it directly to the Arduino:

1. **LCD GND** → Arduino **GND** (or breadboard ground rail).
2. **LCD VCC** → Arduino **5V** (or breadboard power rail).
3. **LCD SDA** → Arduino **A4** (SDA).
4. **LCD SCL** → Arduino **A5** (SCL).

If your LCD has a Gravity cable, plug it into any I2C-compatible Gravity port. Otherwise, use individual jumper wires to A4 and A5.

### Step 3 — Wire the ATTACK Button (Pin 2 / INT0)

1. Place the first pushbutton across the **centre channel** of the breadboard (e.g., rows 5–6, straddling columns e and f). The button should bridge the gap so that each pair of legs is on a separate side.
2. Connect one leg (e.g., row 5, column a) to the breadboard **ground rail** with a wire.
3. Connect the **diagonally opposite leg** (e.g., row 6, column j) to Arduino **digital pin 2** with a wire.
4. No external pull-up resistor is needed — the code enables the ATmega328p's internal pull-up on pin 2.

**How it works:** Pin 2 reads HIGH normally (pulled up). When you press the button, it connects to GND and reads LOW. The falling edge triggers INT0.

### Step 4 — Wire the BLOCK Button (Pin 3 / INT1)

1. Place the second pushbutton across the centre channel a few rows below the first (e.g., rows 10–11).
2. Connect one leg to the breadboard **ground rail**.
3. Connect the diagonally opposite leg to Arduino **digital pin 3**.

Same principle as the attack button — internal pull-up, active-low, triggers INT1 on falling edge.

### Step 5 — Wire LED 1 (Pin 12)

1. Place a red LED in the breadboard (e.g., row 18). The **longer leg is the anode (+)**.
2. Connect a **220Ω resistor** from Arduino **digital pin 12** to the LED's **anode** (long leg). You can do this by placing one end of the resistor in the same row as the anode and running a wire from the other end to pin 12.
3. Connect the LED's **cathode** (short leg) to the breadboard **ground rail**.

**Resistor calculation (from your course slides):** R = (Vs − Vf) / If = (5V − 1.8V) / 0.02A = 160Ω. A 220Ω resistor is safe and keeps the LED well within limits.

### Step 6 — Wire LED 2 (Pin 13)

1. Place the second red LED a couple of rows below LED 1 (e.g., row 22).
2. Connect a **220Ω resistor** from Arduino **digital pin 13** to the anode.
3. Connect the cathode to the **ground rail**.

Note: Pin 13 also has the onboard LED, so you'll see both the onboard and external LED flash together during death — extra dramatic effect.

### Step 7 — Final Check Before Power-On

Before plugging in the USB cable, visually verify:

1. **No short circuits** — no bare wire ends touching each other or adjacent breadboard rows.
2. **LED polarity** — long leg (anode) goes toward the resistor/Arduino pin, short leg (cathode) goes to ground.
3. **Button orientation** — the button straddles the centre channel. Press it and confirm it clicks.
4. **LCD connections** — SDA to A4, SCL to A5, VCC to 5V, GND to GND.
5. **Power rails** — 5V to red rail, GND to blue rail.

### Wiring Summary Table

| Component | Arduino Pin | Breadboard Connection |
|-----------|------------|----------------------|
| LCD SDA | A4 | Direct wire |
| LCD SCL | A5 | Direct wire |
| LCD VCC | 5V | Direct wire |
| LCD GND | GND | Direct wire |
| ATK button leg 1 | — | Ground rail |
| ATK button leg 2 | Pin 2 (INT0) | Direct wire |
| BLK button leg 1 | — | Ground rail |
| BLK button leg 2 | Pin 3 (INT1) | Direct wire |
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

1. Open `samurai_standoff.ino` in the Arduino IDE.
2. Go to **Tools → Board** and select **Arduino Uno**.
3. Go to **Tools → Port** and select the COM port your Arduino is connected to.
4. Click **Verify** (checkmark icon) to compile. It should compile with no errors.
5. Click **Upload** (arrow icon) to flash the program to the board.

---

## Sprite System

The HD44780 controller used by the LCD supports exactly 8 user-defined custom characters (slots 0–7), each 5×8 pixels. The game uses all 8 slots to build two full-height samurai sprites: one for the player (always on the left, at column 0) and one for the enemy (always on the right, at column 15). Each sprite is composed of a top half (row 0) and a bottom half (row 1), giving an effective 5×16 pixel figure.

### Slot Allocation

| Slot | Name | Description |
|------|------|-------------|
| 0 | `CHAR_PLAYER_TOP` | Player upper body — facing right, sword extended |
| 1 | `CHAR_PLAYER_BOT` | Player lower body — wide stance |
| 2 | `CHAR_ENEMY_IDLE_T` | Enemy upper body — neutral standoff pose |
| 3 | `CHAR_ENEMY_IDLE_B` | Enemy lower body — neutral standoff pose |
| 4 | `CHAR_ENEMY_ATK_T` | Enemy upper body — **LUNGE** (head far left, sword fully extended) |
| 5 | `CHAR_ENEMY_ATK_B` | Enemy lower body — front foot forward, lunge stance |
| 6 | `CHAR_ENEMY_BLK_T` | Enemy upper body — **GUARD** (arms raised wide, sword horizontal) |
| 7 | `CHAR_ENEMY_BLK_B` | Enemy lower body — wide planted defensive stance |

### Pixel Layout

All 8 characters are loaded into CGRAM once in `setup()` and never modified at runtime. The three enemy states are visually distinct:

- **IDLE** — symmetric facing-left mirror of the player. Sword tip points left at neutral height (row 3: `11101`).
- **ATTACK LUNGE** — head leaning hard left (`11000`), arm and sword fully extended across all 5 pixels (row 3: `11111`). Bottom half has front leg lunging toward the player.
- **GUARD** — head at normal height, but row 2 shows `11111` (arms spread wide, sword held horizontal as a guard). Bottom half has an extra-wide planted stance (`10001` knee spread vs `01010` idle).

### Screen Layout

```
Col:  0    1 2 3 4 5 6 7 8 9 10 11 12 13 14  15
Row0: [P_top]  [      empty stage       ]   [E_top]
Row1: [P_bot]  [R:X  +  spaces         ]   [E_bot]
```

The `R:X` label (round number) is the only text shown during active gameplay. Everything else is sprites or blank space.

### Cue Delivery

`action_show_cue()` updates **only column 15** — it does not call `lcd.clear()`. The standoff scene remains on screen, and the enemy sprite snaps from idle to either the attack or guard pose in a single frame. This gives a clean, instant visual trigger with no LCD flicker.

---

## Finite State Machine Design

The game uses the **Garbini FSM method** with a state transition action table. Each entry in the table is a struct: `{current_state, event, action_function, next_state}`.

### States

| State | Description |
|-------|-------------|
| TITLE_SCREEN | Full-height samurai face off. "SAMURAI / STANDOFF!" displayed. Press any button to start. |
| STANDOFF | Sprites visible, enemy in idle pose. Round number shown. Random delay counting down. |
| CUE_SHOWN | Enemy sprite snaps to ATTACK or GUARD pose. Reaction window counting down. |
| ROUND_WIN | Player pressed the correct button. Player sprite remains; enemy column goes blank. |
| PLAYER_DEAD | Wrong button, too slow, or premature press. Enemy sprite stays (victorious). LEDs flash. |
| GAME_OVER | High score displayed. Press any button to restart. |

### Events

| Event | Source |
|-------|--------|
| EVENT_ENTRY | State just changed (detected in `get_event()`) |
| EVENT_ATK_PRESS | External interrupt on pin 2 (INT0), debounced |
| EVENT_BLK_PRESS | External interrupt on pin 3 (INT1), debounced |
| EVENT_TIMER_DELAY | Timer1 ISR decremented standoff delay to zero |
| EVENT_TIMER_REACTION | Timer1 ISR decremented reaction window to zero |
| EVENT_TIMER_DISPLAY | Timer1 ISR decremented display timer to zero |
| EVENT_TIMER_FLASH | Timer2 ISR ticked for LED flash toggle |

### Cue Logic

```
player_must_attack = true   →  enemy shows GUARD pose   →  press ATK to survive
player_must_attack = false  →  enemy shows ATTACK pose  →  press BLK to survive
```

This maps naturally to a fighting game: if the enemy is guarding, you break through with an attack; if the enemy is lunging at you, you block. There is no on-screen text prompt — you read the sprite.

### State Transition Diagram

```
                    ┌──────────────┐
         any btn    │ TITLE_SCREEN │
        ┌──────────►│  full-height │
        │           │  standoff    │
        │           └──────┬───────┘
        │                  │ any button press
        │                  ▼
        │           ┌──────────────┐◄─── display timer
        │           │   STANDOFF   │     expired
        │           │ (enemy idle) │        │
        │           └──┬───┬───────┘  ┌─────┴──────┐
        │    premature │   │ delay    │ ROUND_WIN  │
        │    press     │   │ expired  │ player on  │
        │              │   ▼         │ left only  │
        │              │ ┌──────────────┐     ▲
        │              │ │  CUE_SHOWN   │     │
        │              │ │ enemy snaps  │     │ correct
        │              │ │ to ATK/BLK  │     │ button
        │              │ └──┬───┬───────┘     │
        │              │    │   │ correct ────┘
        │              │    │   │
        │    timeout   │    │   │ wrong button
        │    or wrong  ▼    ▼   │
        │           ┌──────────────┐
        │           │ PLAYER_DEAD  │
        │           │ enemy stays  │
        │           │ (LED flash)  │
        │           └──────┬───────┘
        │                  │ display timer expired
        │                  ▼
        │           ┌──────────────┐
        └───────────│  GAME_OVER   │
         any btn    │ "Hi: XX"     │
                    └──────────────┘
```

### Difficulty Scaling

| Rounds Survived | Reaction Window |
|----------------|----------------|
| 0 (first round) | 1200 ms |
| 1 | 1120 ms |
| 2 | 1040 ms |
| 3 | 960 ms |
| 5 | 800 ms |
| 8 | 560 ms |
| 10 | 400 ms |
| 11+ | 350 ms (minimum) |

---

## Testing Procedure

### Test 1 — Power-On and LCD Check

1. Plug in the USB cable.
2. **Expected:** The LCD displays two full-height samurai sprites facing each other. Player sprite on the far left (column 0), enemy sprite on the far right (column 15).
   - Line 1: `[player_top]   SAMURAI    [enemy_idle_top]`
   - Line 2: `[player_bot]  STANDOFF!   [enemy_idle_bot]`
3. Both red LEDs should be **OFF**.

### Test 2 — Starting the Game

1. Press either the ATK or BLK button.
2. **Expected:** LCD shows the standoff scene.
   - Line 1: `[player_top]              [enemy_idle_top]`
   - Line 2: `[player_bot]  R:1         [enemy_idle_bot]`
3. The enemy sprite should be in the neutral/idle pose. Do **not** press any buttons yet. Wait for the cue.

### Test 3 — Visual Cue and Correct Response

1. Wait 1–3.5 seconds. Only column 15 will change — the enemy sprite snaps to a new pose.
2. **If enemy shows LUNGE (head far left, full arm extension):** Press BLK.
3. **If enemy shows GUARD (arms raised wide across full width):** Press ATK.
4. **Expected:** LCD shows victory.
   - Line 1: `[player_top]   VICTORY!   ` (column 15 is blank — enemy defeated)
   - Line 2: `[player_bot]   Kills: 1   `
5. After ~1.2 seconds, the game advances to round 2.

### Test 4 — Wrong Button Press (Death)

1. Start a new round and wait for the cue.
2. Deliberately press the **wrong** button (BLK when enemy guards, ATK when enemy lunges).
3. **Expected:**
   - Line 1: ` YOU DIED     [enemy_top]` (enemy sprite persists, player column is blank)
   - Line 2: ` Sc:0  Hi:0   [enemy_bot]`
   - Both red LEDs **flash rapidly** (~10 Hz) for ~2.5 seconds.
4. After the flash, the GAME_OVER screen appears.

### Test 5 — Timeout Death (Too Slow)

1. Start a new game and wait for the cue.
2. Do **not** press any button. Let the reaction window expire (~1.2 seconds for round 1).
3. **Expected:** Same death sequence as Test 4.

### Test 6 — Premature Press (Dishonourable Death)

1. Start a new game. During the standoff (before the enemy changes pose), press either button.
2. **Expected:** Immediate death. The enemy is still in idle pose on the death screen — you attacked without honour.

### Test 7 — Cue Distinction Check

1. Play several rounds and pay attention to the enemy pose change.
2. **LUNGE cue** — head shifts hard left (was centered-left, now at far-left pixels), and a solid horizontal line appears across the full width of the character cell.
3. **GUARD cue** — head stays at the same position, but a wide horizontal bar appears at the TOP of the character cell (row 2 = both arms raised), and the bottom half shows feet spread significantly wider than idle.
4. Verify these look meaningfully different to you at a glance.

### Test 8 — Difficulty Scaling

1. Play through several rounds successfully.
2. **Notice:** The reaction window gets shorter each round. By round 5, the pressure is noticeable. By round 10+, you need very fast reflexes (~350 ms).
3. Verify the game does not freeze or glitch after many rounds.

### Test 9 — High Score Persistence

1. Play a game and get a score (e.g., 5 kills).
2. Die and note the high score on the GAME_OVER screen.
3. Press a button to restart. Play again and die with a lower score.
4. **Expected:** GAME_OVER still shows your original high score.
5. Play again and beat 5 kills. **Expected:** High score updates.

### Test 10 — Rapid Button Mashing

1. During any phase, rapidly press both buttons.
2. **Expected:** No crash, freeze, or garbled sprites. Debouncing (50 ms) prevents false triggers.

### Test 11 — Full Playthrough

1. Play at least 10 consecutive rounds to confirm stability.
2. Verify sprites update correctly every round, LEDs work on death, and the game restarts cleanly from GAME_OVER.

---

## Troubleshooting

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| LCD blank / no backlight | Wrong I2C address or wiring | Check SDA→A4, SCL→A5, try address 0x60 instead of 0x6B |
| LCD shows garbage characters | Loose I2C wires | Reseat SDA and SCL connections |
| Sprites look wrong / corrupted | CGRAM write error | Power cycle the board; `lcd.customSymbol()` calls run once in `setup()` |
| Button presses not detected | Button wired wrong | Ensure button straddles the breadboard gap; test with a multimeter |
| LEDs don't flash on death | Reversed polarity | Long leg (anode) toward the resistor, short leg to GND |
| Enemy doesn't change pose | `action_show_cue()` not firing | Verify standoff delay countdown is working; check Timer1 setup |
| Cue looks like the idle pose | Custom chars loaded in wrong slots | Confirm slot assignments match `#define` values in the sketch |
| Game skips standoff (instant cue) | Random seed issue | Ensure A0 is floating (not connected) for `randomSeed()` |
| "YOU DIED" immediately on start | Noisy button bounce | Check button wiring; increase debounce time in ISR if needed |
| Compile error: library not found | Missing DFRobot library | Install DFRobot_RGBLCD1602 via Library Manager |
