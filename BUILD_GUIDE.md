# Samurai Standoff — Build Guide

## Overview

A samurai standoff arcade game for the DFRobot DFRduino Uno with a 16x2 LCD. Two samurai face off each round. After a random delay, a cue ("ATK!" or "BLK!") flashes on screen. You must press the correct button before the timer runs out. Wrong button or too slow means death. Difficulty increases every round. Survive as long as you can and set a high score.

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

## Finite State Machine Design

The game uses the **Garbini FSM method** with a state transition action table. Each entry in the table is a struct: `{current_state, event, action_function, next_state}`.

### States

| State | Description |
|-------|-------------|
| TITLE_SCREEN | Shows game title. Press any button to start. |
| STANDOFF | Two samurai face each other. Random delay counting down. |
| CUE_SHOWN | "ATK!" or "BLK!" cue displayed. Reaction window counting down. |
| ROUND_WIN | Player pressed the correct button. Brief victory message. |
| PLAYER_DEAD | Wrong button, too slow, or premature press. LEDs flash. |
| GAME_OVER | Final score displayed. Press any button to restart. |

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

### State Transition Diagram

```
                    ┌──────────────┐
         any btn    │ TITLE_SCREEN │
        ┌──────────►│  "SAMURAI    │
        │           │  STANDOFF!"  │
        │           └──────┬───────┘
        │                  │ any button press
        │                  ▼
        │           ┌──────────────┐◄─── display timer
        │           │   STANDOFF   │     expired
        │           │  (random     │        │
        │           │   delay)     │        │
        │           └──┬───┬───────┘  ┌─────┴──────┐
        │    premature │   │ delay    │ ROUND_WIN  │
        │    press     │   │ expired  │ "VICTORY!" │
        │              │   ▼         └─────────────┘
        │              │ ┌──────────────┐     ▲
        │              │ │  CUE_SHOWN   │     │
        │              │ │ "ATK!"/"BLK!"│     │ correct
        │              │ └──┬───┬───────┘     │ button
        │              │    │   │ correct ────┘
        │              │    │   │
        │    timeout   │    │   │ wrong button
        │    or wrong  ▼    ▼   │
        │           ┌──────────────┐
        │           │ PLAYER_DEAD  │
        │           │ "YOU DIED"   │
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
2. **Expected:** The LCD backlight turns orange and displays:
   - Line 1: `♦ SAMURAI    ♦` (with custom samurai characters on each end)
   - Line 2: `♦ STANDOFF!  ♦` (with custom katana characters)
3. Both red LEDs should be **OFF**.

### Test 2 — Starting the Game

1. Press either the ATK or BLK button.
2. **Expected:** LCD changes to blue backlight showing:
   - Line 1: `♦♦  ......  ♦♦` (samurai and katanas facing each other)
   - Line 2: `Round 1  READY`
3. Do **not** press any buttons yet. Wait for the cue.

### Test 3 — Cue Appears and Correct Response

1. Wait 1–3.5 seconds. A cue will appear with a red backlight.
2. **If "ATK!" appears:** press the ATK button (pin 2).
3. **If "BLK!" appears:** press the BLK button (pin 3).
4. **Expected:** LCD turns green and shows:
   - Line 1: `♦♦ VICTORY! ♦♦`
   - Line 2: `Defeated: 1`
5. After ~1.2 seconds, it automatically advances to round 2 with a new standoff.

### Test 4 — Wrong Button Press (Death)

1. Start a new round and wait for the cue.
2. Deliberately press the **wrong** button (ATK when it says BLK, or vice versa).
3. **Expected:**
   - LCD turns red: `☠ YOU DIED ☠` with your score
   - Both red LEDs **flash rapidly** (~10 Hz) for about 2.5 seconds
4. After the flash, LCD shows dark red `GAME OVER` with your high score.

### Test 5 — Timeout Death (Too Slow)

1. Start a new game and wait for the cue.
2. Do **not** press any button. Let the reaction window expire (~1.2 seconds for round 1).
3. **Expected:** Same death sequence as Test 4 — LEDs flash, "YOU DIED", then "GAME OVER".

### Test 6 — Premature Press (Dishonourable Death)

1. Start a new game. During the standoff phase (samurai facing each other, before any cue appears), press either button.
2. **Expected:** Immediate death — "YOU DIED", LED flash, then "GAME OVER". Pressing before the cue is a dishonourable move.

### Test 7 — Difficulty Scaling

1. Play through several rounds successfully.
2. **Notice:** The reaction window gets shorter each round. By round 5, you'll feel the pressure. By round 10+, you need very fast reflexes (~350 ms).
3. Verify the game does not freeze or glitch after many rounds.

### Test 8 — High Score Persistence

1. Play a game and get a score (e.g., 5 kills).
2. Die and go to GAME_OVER. Note the high score.
3. Press a button to restart. Play again.
4. Die with a lower score (e.g., 2 kills).
5. **Expected:** GAME_OVER screen still shows your original high score of 5, not 2.
6. Play again and beat 5 kills.
7. **Expected:** High score updates to the new record.

### Test 9 — Rapid Button Mashing

1. During any phase, rapidly press both buttons.
2. **Expected:** The game should not crash, freeze, or display garbage. Debouncing (50 ms in ISRs) prevents false triggers.

### Test 10 — Full Playthrough

1. Play at least 10 consecutive rounds to confirm stability.
2. Verify the LCD updates correctly every round, LEDs work on death, and the game restarts cleanly from GAME_OVER.

---

## Troubleshooting

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| LCD is blank / no backlight | Wrong I2C address or wiring | Check SDA→A4, SCL→A5, try address 0x60 instead of 0x6B |
| LCD shows garbage characters | Loose I2C wires | Reseat SDA and SCL connections |
| Button presses not detected | Button wired wrong | Ensure button straddles the breadboard gap; test with a multimeter |
| LEDs don't flash on death | Reversed polarity | Long leg (anode) toward the resistor, short leg to GND |
| LEDs always on | Pin 13 onboard LED conflict | Normal for pin 13 — the onboard LED mirrors it |
| Game skips standoff (instant cue) | Random seed issue | Ensure A0 is floating (not connected to anything) for `randomSeed()` |
| "YOU DIED" immediately on start | Noisy button bounce | Check button wiring; increase debounce time in ISR if needed |
| Compile error: library not found | Missing DFRobot library | Install DFRobot_RGBLCD1602 via Library Manager |
