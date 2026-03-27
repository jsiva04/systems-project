/*
 * GRAVITY GUY — Endless Side-Scrolling Runner
 * McMaster University — MECHTRON 2TA4: Embedded Systems Design I
 *
 * A character runs automatically across a 16×2 LCD. Solid-block obstacles
 * scroll in from the right along the floor or ceiling. Press either button
 * to flip gravity — the character snaps between the floor (row 1) and the
 * ceiling (row 0). Survive as long as possible. The game accelerates
 * progressively until reaction time drops to ~110 ms per column.
 *
 * There is no text on screen during gameplay. The entire 16×2 grid is the
 * level. Score and high score appear only on the death and game-over screens.
 *
 * Hardware:
 *   - DFRobot DFRduino Uno (ATmega328p, 16 MHz)
 *   - DFRobot Gravity LCD1602 V1.1 (I2C RGB, address 0x6B)
 *   - Flip button A: Pin 2 (INT0)   ← either button flips gravity
 *   - Flip button B: Pin 3 (INT1)
 *   - Red LED 1:     Pin 12
 *   - Red LED 2:     Pin 13
 *
 * Sprite layout (8 CGRAM slots, all used):
 *   Slot 0  CHAR_FLOOR_A   — player on floor, running stride A
 *   Slot 1  CHAR_FLOOR_B   — player on floor, running stride B
 *   Slot 2  CHAR_CEIL_A    — player on ceiling, stride A (vertical flip)
 *   Slot 3  CHAR_CEIL_B    — player on ceiling, stride B (vertical flip)
 *   Slot 4  CHAR_WALL      — full 5×8 solid block (safe against ROM variants)
 *   Slots 5-7              — unused / free for future expansion
 *
 * Screen layout (PLAYER_COL = 1):
 *   Col 0     : always blank (visual margin behind player)
 *   Col 1     : player sprite (floor or ceiling, stride A or B)
 *   Cols 2–15 : scrolling obstacle field (CHAR_WALL or space)
 *   Row 0     : ceiling — player occupies if gravity is flipped
 *   Row 1     : floor   — player occupies normally
 *
 * FSM: Garbini method with action table (StateTransition structs)
 * Timing: All interrupt-driven — no delay() anywhere in the code
 *   Timer1: 1 ms system tick → tick_countdown, display_countdown
 *   Timer2: ~12.5 ms base tick → LED flash during PLAYER_DEAD
 *
 * Author: Janahan
 * Date:   March 2026
 */

#include "DFRobot_RGBLCD1602.h"

// ============================================================================
// HARDWARE DEFINITIONS
// ============================================================================
#define FLIP_BTN_A   2   // INT0 — gravity flip (either button works)
#define FLIP_BTN_B   3   // INT1 — gravity flip
#define LED1_PIN    12
#define LED2_PIN    13

DFRobot_RGBLCD1602 lcd(/*RGBAddr*/ 0x6B, /*lcdCols*/ 16, /*lcdRows*/ 2);

// ============================================================================
// GAME TUNING
// ============================================================================
#define PLAYER_COL        1      // Fixed column the player occupies
#define SPEED_START       350    // ms per game tick at the start
#define SPEED_MIN         110    // Fastest possible tick (ms)
#define DEATH_DISPLAY_MS  2500   // How long the death screen shows (ms)
#define DEATH_FLASH_RATE  4      // Timer2 ticks per LED toggle (~10 Hz)
#define START_RUNWAY      10     // Clear obstacle-free columns at game start

// Obstacle bitfield values stored per column
#define OBS_NONE   0x00
#define OBS_FLOOR  0x01   // wall on row 1 (floor)
#define OBS_CEIL   0x02   // wall on row 0 (ceiling)

// Custom character slot indices
#define CHAR_FLOOR_A  0
#define CHAR_FLOOR_B  1
#define CHAR_CEIL_A   2
#define CHAR_CEIL_B   3
#define CHAR_WALL     4

// ============================================================================
// CUSTOM LCD CHARACTERS — 5×8 pixel sprites
//
// Bit layout per row: 0bABCDE  (A = leftmost pixel, E = rightmost)
// Character runs rightward (toward col 15 / oncoming obstacles).
// Row 0 is the top of the cell; row 7 is the bottom.
//
// Floor sprites: head at top (row 0), feet at bottom (row 7) — normal gravity
// Ceiling sprites: feet at top (row 0, touching ceiling), head at bottom (row 7)
//
// Two stride frames per gravity state animate the running legs at game speed.
// ============================================================================

// CHAR_FLOOR_A — floor, stride A (right leg forward)
byte floor_a[8] = {
  0b01100,   // head
  0b01100,   // head
  0b11110,   // torso + right arm extending toward obstacles
  0b01100,   // lower body
  0b01110,   // hips
  0b11010,   // right leg forward, left leg back
  0b10000,   // left shin trailing
  0b01100    // right foot, forward plant
};

// CHAR_FLOOR_B — floor, stride B (left leg forward, opposite stride)
byte floor_b[8] = {
  0b01100,   // head
  0b01100,   // head
  0b11110,   // torso + arm
  0b01100,   // lower body
  0b01110,   // hips
  0b01011,   // left leg forward, right leg back
  0b00001,   // right shin trailing
  0b00110    // left foot, forward plant
};

// CHAR_CEIL_A — ceiling, stride A (vertical flip of floor_a)
// Feet at top (touching ceiling), head at bottom (hanging toward floor)
byte ceil_a[8] = {
  0b01100,   // right foot (at ceiling)
  0b10000,   // left shin
  0b11010,   // legs
  0b01110,   // hips
  0b01100,   // lower body
  0b11110,   // torso + arm
  0b01100,   // head
  0b01100    // head (hanging down)
};

// CHAR_CEIL_B — ceiling, stride B (vertical flip of floor_b)
byte ceil_b[8] = {
  0b00110,   // left foot (at ceiling)
  0b00001,   // right shin
  0b01011,   // legs
  0b01110,   // hips
  0b01100,   // lower body
  0b11110,   // torso + arm
  0b01100,   // head
  0b01100    // head (hanging down)
};

// CHAR_WALL — full solid block for obstacles
// Custom character avoids dependency on HD44780 ROM variant for 0xFF
byte wall_block[8] = {
  0b11111,
  0b11111,
  0b11111,
  0b11111,
  0b11111,
  0b11111,
  0b11111,
  0b11111
};

// ============================================================================
// FSM DEFINITIONS (Garbini Method)
// ============================================================================

typedef enum {
  TITLE_SCREEN,
  PLAYING,
  PLAYER_DEAD,
  GAME_OVER
} State;

typedef enum {
  EVENT_NONE,
  EVENT_ENTRY,
  EVENT_FLIP_PRESS,      // Either flip button pressed (INT0 or INT1)
  EVENT_TICK,            // Game scroll tick (tick_countdown reached zero)
  EVENT_TIMER_FLASH,     // LED flash tick (Timer2, active during PLAYER_DEAD)
  EVENT_TIMER_DISPLAY    // Death/game-over display timer expired
} Event;

typedef struct {
  State   state;
  Event   event;
  void    (*action)(void);
  State   next_state;
} StateTransition;

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================

// State machine
volatile State current_state  = TITLE_SCREEN;
State          previous_state = GAME_OVER;    // forces entry on first run

// Event flags (set by ISRs or Timer ISRs, cleared by main loop)
volatile bool flip_pressed          = false;
volatile bool game_tick_flag        = false;
volatile bool timer_display_expired = false;
volatile bool timer_flash_tick      = false;

// Timing
volatile uint16_t tick_countdown    = 0;   // counts down for next scroll tick
volatile uint16_t display_countdown = 0;   // death/game-over display timer
volatile uint8_t  flash_sub_count   = 0;   // sub-counter for Timer2 LED flash
volatile bool     led_flash_on      = false;

// Game state
bool     player_on_ceiling = false;       // false = floor (row 1), true = ceiling (row 0)
bool     anim_frame        = false;       // toggled each tick: selects stride A or B
uint8_t  obstacles[16];                   // per-column obstacle flags (OBS_NONE/FLOOR/CEIL)
uint8_t  gap_counter       = 0;           // columns until next obstacle allowed
uint8_t  last_obs          = OBS_NONE;    // last obstacle type generated (for alternation)
uint16_t score             = 0;           // columns survived this run
uint16_t high_score        = 0;           // all-time best
uint16_t current_speed     = SPEED_START; // current ms-per-tick
bool     flash_active      = false;

// ============================================================================
// FUNCTION PROTOTYPES
// ============================================================================

void action_show_title(void);
void action_start_game(void);
void action_flip_gravity(void);
void action_game_tick(void);
void action_show_death(void);
void action_flash_leds(void);
void action_show_game_over(void);
void action_restart(void);
void action_none(void);

void setup_timers(void);
void clear_event_flags(void);
void leds_off(void);
void redraw_game(void);
uint8_t get_player_char(void);
uint8_t generate_obstacle(void);
void    update_speed(void);

void isr_flip_a(void);
void isr_flip_b(void);

// ============================================================================
// ACTION TABLE (Garbini Method)
// ============================================================================

const StateTransition action_table[] = {

  // ---- TITLE_SCREEN --------------------------------------------------------
  { TITLE_SCREEN, EVENT_ENTRY,      action_show_title, TITLE_SCREEN },
  { TITLE_SCREEN, EVENT_FLIP_PRESS, action_start_game, PLAYING      },

  // ---- PLAYING -------------------------------------------------------------
  // EVENT_ENTRY fires once on state change; action_none lets action_start_game
  // (called from TITLE_SCREEN) handle all initialisation before we arrive here.
  { PLAYING, EVENT_ENTRY,      action_none,         PLAYING },
  { PLAYING, EVENT_FLIP_PRESS, action_flip_gravity, PLAYING },
  { PLAYING, EVENT_TICK,       action_game_tick,    PLAYING },

  // ---- PLAYER_DEAD ---------------------------------------------------------
  { PLAYER_DEAD, EVENT_ENTRY,         action_show_death,     PLAYER_DEAD },
  { PLAYER_DEAD, EVENT_TIMER_FLASH,   action_flash_leds,     PLAYER_DEAD },
  { PLAYER_DEAD, EVENT_TIMER_DISPLAY, action_show_game_over, GAME_OVER   },

  // ---- GAME_OVER -----------------------------------------------------------
  { GAME_OVER, EVENT_ENTRY,      action_none,    GAME_OVER    },
  { GAME_OVER, EVENT_FLIP_PRESS, action_restart, TITLE_SCREEN },
};

const uint8_t ACTION_TABLE_SIZE = sizeof(action_table) / sizeof(StateTransition);

// ============================================================================
// ARDUINO SETUP
// ============================================================================

void setup() {
  pinMode(FLIP_BTN_A, INPUT_PULLUP);
  pinMode(FLIP_BTN_B, INPUT_PULLUP);
  pinMode(LED1_PIN,   OUTPUT);
  pinMode(LED2_PIN,   OUTPUT);
  digitalWrite(LED1_PIN, LOW);
  digitalWrite(LED2_PIN, LOW);

  lcd.init();
  // lcd.setRGB(80, 80, 80);   // Uncomment for neutral grey backlight

  // Load all five active custom characters into CGRAM
  lcd.customSymbol(CHAR_FLOOR_A, floor_a);
  lcd.customSymbol(CHAR_FLOOR_B, floor_b);
  lcd.customSymbol(CHAR_CEIL_A,  ceil_a);
  lcd.customSymbol(CHAR_CEIL_B,  ceil_b);
  lcd.customSymbol(CHAR_WALL,    wall_block);

  randomSeed(analogRead(A0));

  setup_timers();

  attachInterrupt(digitalPinToInterrupt(FLIP_BTN_A), isr_flip_a, FALLING);
  attachInterrupt(digitalPinToInterrupt(FLIP_BTN_B), isr_flip_b, FALLING);

  sei();

  current_state  = TITLE_SCREEN;
  previous_state = GAME_OVER;  // force entry action on first loop iteration
}

// ============================================================================
// MAIN LOOP (Garbini Method — do not modify structure)
// ============================================================================

void loop() {
  Event event = get_event();
  execute_state_machine(event);
}

// ============================================================================
// EVENT DETECTION
// ============================================================================

Event get_event(void) {
  // Entry event fires exactly once whenever the state changes
  if (current_state != previous_state) return EVENT_ENTRY;

  // Priority order: player input > game tick > LED flash > display timer
  if (flip_pressed)          { flip_pressed          = false; return EVENT_FLIP_PRESS;   }
  if (game_tick_flag)        { game_tick_flag        = false; return EVENT_TICK;          }
  if (timer_flash_tick)      { timer_flash_tick      = false; return EVENT_TIMER_FLASH;   }
  if (timer_display_expired) { timer_display_expired = false; return EVENT_TIMER_DISPLAY; }

  return EVENT_NONE;
}

// ============================================================================
// FSM EXECUTION (Garbini Method)
// ============================================================================

void execute_state_machine(Event event) {
  if (event == EVENT_NONE) return;

  for (uint8_t i = 0; i < ACTION_TABLE_SIZE; i++) {
    if (action_table[i].state == current_state &&
        action_table[i].event == event) {

      State pre = current_state;
      if (action_table[i].action) action_table[i].action();

      // Respect any state override set by the action function itself
      // (e.g. action_flip_gravity or action_game_tick detecting a collision).
      // If the action didn't change state, apply the table's next_state.
      if (current_state == pre) {
        previous_state = current_state;
        current_state  = action_table[i].next_state;
      } else {
        previous_state = pre;
      }

      return;
    }
  }
  // No matching row — event silently ignored for this state
}

// ============================================================================
// ACTION FUNCTIONS
// ============================================================================

// action_show_title --------------------------------------------------------
// Displays the title screen with both player states shown simultaneously
// in column 0: ceiling sprite (row 0) and floor sprite (row 1).
void action_show_title(void) {
  lcd.clear();
  // lcd.setRGB(60, 60, 120);  // Uncomment for cool-blue backlight
  leds_off();
  flash_active = false;

  // Row 0: ceiling-player at col 0 | title text cols 1-15
  lcd.setCursor(0, 0);
  lcd.write((uint8_t)CHAR_CEIL_A);
  lcd.print(" GRAVITY GUY   ");   // 15 chars (cols 1-15)

  // Row 1: floor-player at col 0 | prompt text cols 1-15
  lcd.setCursor(0, 1);
  lcd.write((uint8_t)CHAR_FLOOR_A);
  lcd.print("   FLIP=START  ");   // 15 chars

  clear_event_flags();
}

// action_start_game --------------------------------------------------------
// Resets all game state, fills the screen with blank space, and starts
// the scroll tick timer. Called from TITLE_SCREEN on first flip press.
void action_start_game(void) {
  score              = 0;
  current_speed      = SPEED_START;
  player_on_ceiling  = false;
  anim_frame         = false;
  gap_counter        = START_RUNWAY;
  last_obs           = OBS_NONE;

  memset(obstacles, OBS_NONE, sizeof(obstacles));

  clear_event_flags();
  redraw_game();
  // lcd.setRGB(80, 80, 80);

  noInterrupts();
  tick_countdown    = current_speed;
  display_countdown = 0;
  interrupts();
}

// action_flip_gravity ------------------------------------------------------
// Toggles player_on_ceiling immediately. Checks whether the new position
// overlaps an existing obstacle (flipping into a wall = instant death).
// If safe, redraws only column 1 so the flip feels instantaneous.
void action_flip_gravity(void) {
  player_on_ceiling = !player_on_ceiling;

  // Check for immediate collision caused by the flip itself
  uint8_t at_player = obstacles[PLAYER_COL];
  if (( player_on_ceiling && (at_player & OBS_CEIL))  ||
      (!player_on_ceiling && (at_player & OBS_FLOOR))) {
    current_state = PLAYER_DEAD;
    return;
  }

  // Safe — update only the player column for instant visual response.
  // The rest of the screen is unchanged until the next tick redraw.
  uint8_t ch = get_player_char();

  lcd.setCursor(PLAYER_COL, 0);
  if (player_on_ceiling) {
    lcd.write(ch);
  } else {
    // Player moved to floor; ceiling row at col 1 shows obstacle if any
    lcd.write((at_player & OBS_CEIL) ? (uint8_t)CHAR_WALL : ' ');
  }

  lcd.setCursor(PLAYER_COL, 1);
  if (!player_on_ceiling) {
    lcd.write(ch);
  } else {
    // Player moved to ceiling; floor row at col 1 shows obstacle if any
    lcd.write((at_player & OBS_FLOOR) ? (uint8_t)CHAR_WALL : ' ');
  }
}

// action_game_tick ---------------------------------------------------------
// Core game loop, fired by tick_countdown reaching zero via Timer1 ISR.
//   1. Shift obstacles one column left
//   2. Generate a new obstacle at col 15
//   3. Increment score and update speed tier
//   4. Full screen redraw (2 setCursor + 32 sequential writes, ~8 ms)
//   5. Collision check at player column
//   6. If alive: toggle animation frame and reload tick countdown
//
// Redraw happens BEFORE collision check so the player briefly sees the
// obstacle arrive at their column before the death screen takes over.
void action_game_tick(void) {
  // Step 1 — scroll left
  for (uint8_t i = 0; i < 15; i++) {
    obstacles[i] = obstacles[i + 1];
  }
  obstacles[15] = generate_obstacle();

  // Step 2 — advance score and adjust difficulty
  score++;
  update_speed();

  // Step 3 — redraw the full screen
  anim_frame = !anim_frame;
  redraw_game();

  // Step 4 — collision check (after redraw so frame is visible)
  uint8_t at_player = obstacles[PLAYER_COL];
  if (( player_on_ceiling && (at_player & OBS_CEIL))  ||
      (!player_on_ceiling && (at_player & OBS_FLOOR))) {
    current_state = PLAYER_DEAD;
    return;   // do NOT reload tick_countdown — game is over
  }

  // Step 5 — reload tick for next frame
  noInterrupts();
  tick_countdown = current_speed;
  interrupts();
}

// action_show_death --------------------------------------------------------
// Freezes the game display, overlays death text, and starts LED flash.
// High score is updated here.
void action_show_death(void) {
  if (score > high_score) high_score = score;

  lcd.clear();
  // lcd.setRGB(200, 0, 0);

  lcd.setCursor(0, 0);
  lcd.print("  YOU CRASHED!  ");   // exactly 16 chars

  // "S:XXXX  Hi:XXXX" — left-justified, fixed-width fields
  char buf[17];
  snprintf(buf, sizeof(buf), " S:%-4u  Hi:%-4u", score, high_score);
  lcd.setCursor(0, 1);
  lcd.print(buf);

  flash_active    = true;
  led_flash_on    = false;
  flash_sub_count = 0;
  TIMSK2 |= (1 << OCIE2A);   // enable Timer2 LED flash interrupt

  noInterrupts();
  display_countdown = DEATH_DISPLAY_MS;
  tick_countdown    = 0;      // stop any pending game tick
  interrupts();

  clear_event_flags();
}

// action_flash_leds --------------------------------------------------------
// Toggles both LEDs. Called by Timer2 flash tick during PLAYER_DEAD.
void action_flash_leds(void) {
  if (flash_active) {
    led_flash_on = !led_flash_on;
    digitalWrite(LED1_PIN, led_flash_on ? HIGH : LOW);
    digitalWrite(LED2_PIN, led_flash_on ? HIGH : LOW);
  }
}

// action_show_game_over ----------------------------------------------------
void action_show_game_over(void) {
  flash_active = false;
  leds_off();
  TIMSK2 &= ~(1 << OCIE2A);   // disable Timer2 LED flash

  lcd.clear();
  // lcd.setRGB(60, 20, 0);

  lcd.setCursor(0, 0);
  lcd.print("   GAME  OVER   ");   // 16 chars

  char buf[17];
  snprintf(buf, sizeof(buf), "Hi:%-4u  flip=go", high_score);
  lcd.setCursor(0, 1);
  lcd.print(buf);

  clear_event_flags();
}

// action_restart -----------------------------------------------------------
// Resets score and speed so action_show_title on entry gets a clean state.
void action_restart(void) {
  score         = 0;
  current_speed = SPEED_START;
  flash_active  = false;
  leds_off();
  clear_event_flags();
}

void action_none(void) { }

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

// get_player_char ----------------------------------------------------------
// Returns the correct CGRAM index for the current gravity state and stride.
uint8_t get_player_char(void) {
  if (!player_on_ceiling) {
    return anim_frame ? (uint8_t)CHAR_FLOOR_B : (uint8_t)CHAR_FLOOR_A;
  } else {
    return anim_frame ? (uint8_t)CHAR_CEIL_B  : (uint8_t)CHAR_CEIL_A;
  }
}

// redraw_game --------------------------------------------------------------
// Rewrites the entire display in two row passes.
//
// Row 0 pass (ceiling):
//   - PLAYER_COL shows ceiling sprite if player is on ceiling
//   - All other cols show CHAR_WALL if OBS_CEIL, else space
// Row 1 pass (floor):
//   - PLAYER_COL shows floor sprite if player is on floor
//   - All other cols show CHAR_WALL if OBS_FLOOR, else space
//
// Two setCursor calls + 32 sequential write calls ≈ 8 ms on 100 kHz I2C.
// Safe even at SPEED_MIN (110 ms tick) with 93% idle margin.
void redraw_game(void) {
  uint8_t ch = get_player_char();

  // Row 0 — ceiling
  lcd.setCursor(0, 0);
  for (uint8_t col = 0; col < 16; col++) {
    if (col == PLAYER_COL && player_on_ceiling) {
      lcd.write(ch);
    } else if (obstacles[col] & OBS_CEIL) {
      lcd.write((uint8_t)CHAR_WALL);
    } else {
      lcd.write(' ');
    }
  }

  // Row 1 — floor
  lcd.setCursor(0, 1);
  for (uint8_t col = 0; col < 16; col++) {
    if (col == PLAYER_COL && !player_on_ceiling) {
      lcd.write(ch);
    } else if (obstacles[col] & OBS_FLOOR) {
      lcd.write((uint8_t)CHAR_WALL);
    } else {
      lcd.write(' ');
    }
  }
}

// generate_obstacle --------------------------------------------------------
// Returns the obstacle flag for the new column being added at col 15.
//
// Design principles:
//   - gap_counter enforces a mandatory clear-column gap after each obstacle,
//     preventing impassable pairs and shrinking with difficulty.
//   - After a floor obstacle, the next one biases 80% toward the ceiling
//     (forces a flip). After a ceiling obstacle, 80% toward floor.
//     This creates readable, reactive obstacle sequences.
//   - Obstacle frequency increases with score from 35% to 60%.
uint8_t generate_obstacle(void) {
  if (gap_counter > 0) {
    gap_counter--;
    return OBS_NONE;
  }

  // Frequency tier based on score
  uint8_t chance = 35;
  if      (score >= 80) chance = 60;
  else if (score >= 50) chance = 55;
  else if (score >= 30) chance = 50;
  else if (score >= 15) chance = 45;
  else if (score >= 5)  chance = 40;

  if ((uint8_t)random(100) < chance) {
    uint8_t new_obs;

    if (last_obs == OBS_FLOOR) {
      // 80% ceiling, 20% floor — encourage alternation
      new_obs = ((uint8_t)random(100) < 80) ? OBS_CEIL : OBS_FLOOR;
    } else if (last_obs == OBS_CEIL) {
      new_obs = ((uint8_t)random(100) < 80) ? OBS_FLOOR : OBS_CEIL;
    } else {
      new_obs = (random(2) == 0) ? OBS_FLOOR : OBS_CEIL;
    }

    last_obs = new_obs;
    // Mandatory gap shrinks as score rises (min 1 column at high speed)
    gap_counter = (score >= 50) ? 1 : 2;
    return new_obs;
  }

  last_obs = OBS_NONE;
  return OBS_NONE;
}

// update_speed -------------------------------------------------------------
// Recalculates current_speed based on score. Called after each score tick.
// Only updates tick_countdown if the new speed is lower, to avoid resetting
// an already-counting-down tick unexpectedly.
void update_speed(void) {
  uint16_t new_speed;
  if      (score >= 100) new_speed = SPEED_MIN;
  else if (score >= 75)  new_speed = 130;
  else if (score >= 50)  new_speed = 160;
  else if (score >= 30)  new_speed = 200;
  else if (score >= 15)  new_speed = 250;
  else if (score >= 5)   new_speed = 300;
  else                   new_speed = SPEED_START;

  current_speed = new_speed;
}

// clear_event_flags --------------------------------------------------------
void clear_event_flags(void) {
  noInterrupts();
  flip_pressed          = false;
  game_tick_flag        = false;
  timer_display_expired = false;
  timer_flash_tick      = false;
  interrupts();
}

// leds_off -----------------------------------------------------------------
void leds_off(void) {
  digitalWrite(LED1_PIN, LOW);
  digitalWrite(LED2_PIN, LOW);
  led_flash_on = false;
}

// ============================================================================
// TIMER SETUP (Register-Level Configuration)
// ============================================================================

void setup_timers(void) {
  cli();

  // ==== TIMER1 — 1 ms system tick (CTC mode) ================================
  // 16 MHz / prescaler 64 = 250 kHz clock
  // OCR1A = 249 → interrupt every 250 counts = 1 ms exactly
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1  = 0;
  OCR1A  = 249;
  TCCR1B |= (1 << WGM12);                  // CTC mode
  TCCR1B |= (1 << CS11) | (1 << CS10);     // Prescaler 64
  TIMSK1 |= (1 << OCIE1A);                 // Enable compare match interrupt

  // ==== TIMER2 — LED flash base tick (~12.5 ms per tick) ====================
  // 16 MHz / prescaler 1024 = 15625 Hz
  // OCR2A = 195 → interrupt every ~12.5 ms
  // DEATH_FLASH_RATE ticks per toggle → ~50 ms per toggle ≈ 10 Hz LED flash
  TCCR2A = 0;
  TCCR2B = 0;
  TCNT2  = 0;
  OCR2A  = 195;
  TCCR2A |= (1 << WGM21);                                     // CTC mode
  TCCR2B |= (1 << CS22) | (1 << CS21) | (1 << CS20);         // Prescaler 1024
  TIMSK2 &= ~(1 << OCIE2A);   // Start disabled; action_show_death enables it

  sei();
}

// ============================================================================
// INTERRUPT SERVICE ROUTINES
// ============================================================================

// Timer1 ISR — fires every 1 ms
// Decrements tick_countdown (game scroll speed) and display_countdown.
// Sets event flags when either reaches zero.
ISR(TIMER1_COMPA_vect) {
  if (tick_countdown > 0) {
    if (--tick_countdown == 0) game_tick_flag = true;
  }
  if (display_countdown > 0) {
    if (--display_countdown == 0) timer_display_expired = true;
  }
}

// Timer2 ISR — fires every ~12.5 ms (active only during PLAYER_DEAD)
// Counts up to DEATH_FLASH_RATE, then sets timer_flash_tick.
ISR(TIMER2_COMPA_vect) {
  if (++flash_sub_count >= DEATH_FLASH_RATE) {
    flash_sub_count  = 0;
    timer_flash_tick = true;
  }
}

// External interrupt — Flip button A (pin 2, INT0)
// 50 ms debounce via timestamp comparison. Only set flip_pressed if enough
// time has passed AND the button is still LOW (pressed). This prevents false
// triggers from noise or bounce.
void isr_flip_a(void) {
  static unsigned long last_time = 0;
  unsigned long now = millis();
  if (now - last_time > 100 && digitalRead(FLIP_BTN_A) == LOW) {
    flip_pressed = true;
    last_time = now;
  }
}

// External interrupt — Flip button B (pin 3, INT1)
// Both buttons set the same flag — either one flips gravity.
// Only set flip_pressed if enough time has passed AND the button is still LOW.
void isr_flip_b(void) {
  static unsigned long last_time = 0;
  unsigned long now = millis();
  if (now - last_time > 100 && digitalRead(FLIP_BTN_B) == LOW) {
    flip_pressed = true;
    last_time = now;
  }
}
