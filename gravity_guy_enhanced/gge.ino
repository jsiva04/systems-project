/*
 * GRAVITY GUY — Enhanced Edition
 * McMaster University — MECHTRON 2TA4: Embedded Systems Design I
 *
 * Gravity-flip endless runner on a 16×2 LCD.
 *
 * Core gameplay (unchanged):
 *   Press either button OR wave hand in front of ultrasonic sensor to flip
 *   gravity. Solid-block obstacles scroll right-to-left on both the floor
 *   and ceiling. Survive as long as possible. Speed increases with score.
 *
 * Hardware additions over the base version:
 *   ┌─────────────────────────────────────────────────────────────────────┐
 *   │ SN74HC164N shift register → 6 red LEDs (speed-tier bar graph)      │
 *   │   Off → 1 → 2 → 3 → 4 → 5 → 6 LEDs as speed tier increases        │
 *   │   All 6 flash in sync during death sequence (replaces pin 12/13)   │
 *   │                                                                     │
 *   │ HC-SR04 ultrasonic sensor → gesture gravity flip                   │
 *   │   Wave hand closer than ~20 cm to flip (300 ms debounce)           │
 *   │                                                                     │
 *   │ 24BYJ48S stepper motor via SN754410NE H-bridge → live speedometer  │
 *   │   Spins CW. RPM scales with game speed (~2.4 RPM slow → 7.7 fast). │
 *   │   Spins slowly on title screen. Deenergizes on death.              │
 *   │                                                                     │
 *   │ Small fan via NPN transistor + flyback diode → crash wind-blast    │
 *   │   Activates at full speed on crash, shuts off at game-over screen. │
 *   └─────────────────────────────────────────────────────────────────────┘
 *
 * Pin mapping:
 *   Pin 2  (INT0)      — Gravity flip button A (active-low, internal pull-up)
 *   Pin 3  (INT1)      — Gravity flip button B (same function)
 *   Pin A0 (PC0)       — SR_DATA: SN74HC164N serial data (pins 1+2 tied)
 *   Pin A1 (PC1)       — SR_CLK:  SN74HC164N clock (pin 8)
 *   Pin 4              — SONAR_TRIG: HC-SR04 trigger
 *   Pin 7              — SONAR_ECHO: HC-SR04 echo
 *   Pins 8–11 (PB0–3)  — STEP_IN1–4: SN754410NE coil inputs
 *   Pin ~5  (PWM)      — FAN_PIN: fan via transistor (active-HIGH)
 *   Pin A4             — LCD SDA  (I2C, do not use)
 *   Pin A5             — LCD SCL  (I2C, do not use)
 *   Pin A2             — Floating (used only for randomSeed() entropy)
 *
 * FSM:   Garbini method with action table (StateTransition structs)
 * Timers (all interrupt-driven, no delay() anywhere):
 *   Timer1: 1 ms tick → game scroll, display timer, sonar debounce, stepper steps
 *   Timer2: ~12.5 ms tick → LED flash sub-counter during PLAYER_DEAD
 *
 * Stepper motor detail:
 *   Full-step bipolar drive. 2048 steps/revolution (32 steps × 64:1 gear).
 *   Each step advanced from Timer1 ISR via direct PORTB register write.
 *   Step interval: 12 ms (score 0–4) down to 4 ms (score 100+), giving
 *   ~2.4 RPM to ~7.7 RPM — clearly visible rotation.
 *
 * Shift register detail:
 *   MSB-first serial write to SN74HC164N via bit-banged PORTC.
 *   Bit 0 (LSB) of the byte maps to QA → LED 1 (leftmost),
 *   Bit 5 maps to QF → LED 6 (rightmost). QG/QH unused.
 *
 * Author: Janahan
 * Date:   March 2026
 */

#include "DFRobot_RGBLCD1602.h"
#include <string.h>    // memset

// ============================================================================
// HARDWARE DEFINITIONS
// ============================================================================
#define FLIP_BTN_A    2    // INT0
#define FLIP_BTN_B    3    // INT1
// A0 = PC0 → SR_DATA   (defined via PORTC bitmasks, not #define)
// A1 = PC1 → SR_CLK
#define SONAR_TRIG    4
#define SONAR_ECHO    7
#define FAN_PIN       5    // PWM-capable ~5
// Stepper: pins 8–11 = PB0–PB3 (driven via PORTB directly)
#define STEP_IN1      8    // PB0
#define STEP_IN2      9    // PB1
#define STEP_IN3     10    // PB2
#define STEP_IN4     11    // PB3

DFRobot_RGBLCD1602 lcd(/*RGBAddr*/ 0x6B, /*lcdCols*/ 16, /*lcdRows*/ 2);

// ============================================================================
// GAME TUNING CONSTANTS
// ============================================================================
#define PLAYER_COL          1       // Fixed column the player occupies
#define SPEED_START         350     // ms per game tick at game start
#define SPEED_MIN           110     // Minimum ms per game tick (max difficulty)
#define DEATH_DISPLAY_MS    2500    // Death screen display duration (ms)
#define DEATH_FLASH_RATE    4       // Timer2 ticks per LED toggle (~10 Hz)
#define START_RUNWAY        10      // Clear columns before first obstacle

// Sonar
#define SONAR_FLIP_US       1200    // Echo duration threshold for flip (~20 cm)
                                    //   distance = duration × 0.034 / 2 cm
                                    //   1200 µs ≈ 20.4 cm
#define SONAR_DEBOUNCE_MS   300     // Lockout after sonar-triggered flip (ms)

// Obstacles
#define OBS_NONE    0x00
#define OBS_FLOOR   0x01
#define OBS_CEIL    0x02

// Custom character slots
#define CHAR_FLOOR_A    0
#define CHAR_FLOOR_B    1
#define CHAR_CEIL_A     2
#define CHAR_CEIL_B     3
#define CHAR_WALL       4

// ============================================================================
// STEPPER MOTOR — FULL-STEP SEQUENCE (CW, bipolar drive via SN754410NE)
//
// Coil connections:  IN1=PB0, IN2=PB1, IN3=PB2, IN4=PB3
// Full-step sequence (each entry is the nibble written to PORTB[3:0]):
//   Step 0: A+/B+ — IN1=1 IN2=0 IN3=1 IN4=0 → 0b0101 = 0x05
//   Step 1: A-/B+ — IN1=0 IN2=1 IN3=1 IN4=0 → 0b0110 = 0x06
//   Step 2: A-/B- — IN1=0 IN2=1 IN3=0 IN4=1 → 0b1010 = 0x0A
//   Step 3: A+/B- — IN1=1 IN2=0 IN3=0 IN4=1 → 0b1001 = 0x09
// ============================================================================
const uint8_t STEP_SEQ[4] = { 0x05, 0x06, 0x0A, 0x09 };

// ============================================================================
// CUSTOM LCD CHARACTERS — 5×8 pixel sprites
//
// Floor sprites: upright runner (head row 0, feet row 7)
// Ceiling sprites: upside-down runner (feet row 0 at ceiling, head row 7)
// Two stride frames per gravity state animate the legs in sync with game speed.
// CHAR_WALL: full 5×8 block — custom to avoid HD44780 ROM variant issues
//   (0xFF is a full block on ROM A02 but a different glyph on ROM A00)
// ============================================================================

byte floor_a[8] = {
  0b01100,   // head
  0b01100,   // head
  0b11110,   // torso + right arm reaching forward
  0b01100,   // lower body
  0b01110,   // hips
  0b11010,   // right leg forward, left trailing
  0b10000,   // left shin back
  0b01100    // right foot, forward plant
};

byte floor_b[8] = {
  0b01100,   // head
  0b01100,   // head
  0b11110,   // torso + arm
  0b01100,   // lower body
  0b01110,   // hips
  0b01011,   // left leg forward, right trailing
  0b00001,   // right shin back
  0b00110    // left foot, forward plant
};

// Ceiling sprites are vertical flips of floor sprites —
// feet touch the ceiling (row 0), head hangs down (row 7)
byte ceil_a[8] = {
  0b01100,   // right foot (at ceiling)
  0b10000,   // left shin
  0b11010,   // legs
  0b01110,   // hips
  0b01100,   // lower body
  0b11110,   // torso + arm
  0b01100,   // head
  0b01100    // head (hanging)
};

byte ceil_b[8] = {
  0b00110,   // left foot (at ceiling)
  0b00001,   // right shin
  0b01011,   // legs
  0b01110,   // hips
  0b01100,   // lower body
  0b11110,   // torso + arm
  0b01100,   // head
  0b01100    // head (hanging)
};

byte wall_block[8] = {
  0b11111, 0b11111, 0b11111, 0b11111,
  0b11111, 0b11111, 0b11111, 0b11111
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
  EVENT_FLIP_PRESS,       // Button A, Button B, or sonar gesture
  EVENT_TICK,             // Game scroll tick (tick_countdown → 0)
  EVENT_TIMER_FLASH,      // LED flash sub-tick (Timer2, PLAYER_DEAD only)
  EVENT_TIMER_DISPLAY     // Death/game-over display timer expired
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

// --- FSM state ---
volatile State current_state  = TITLE_SCREEN;
State          previous_state = GAME_OVER;    // forces entry on first iteration

// --- Event flags (set by ISRs or peripheral checks, cleared in get_event) ---
volatile bool flip_pressed          = false;
volatile bool game_tick_flag        = false;
volatile bool timer_display_expired = false;
volatile bool timer_flash_tick      = false;

// --- Countdown timers (decremented by Timer1 ISR, 1 ms resolution) ---
volatile uint16_t tick_countdown    = 0;
volatile uint16_t display_countdown = 0;

// --- Timer2 LED flash state ---
volatile uint8_t flash_sub_count = 0;
volatile bool    led_flash_on    = false;
bool             flash_active    = false;

// --- Game logic ---
bool     player_on_ceiling = false;
bool     anim_frame        = false;    // toggles each tick → leg animation
uint8_t  obstacles[16];                // per-column obstacle bitmask
uint8_t  gap_counter       = 0;        // clear-column cooldown after obstacle
uint8_t  last_obs          = OBS_NONE; // for 80/20 floor/ceil alternation
uint16_t score             = 0;
uint16_t high_score        = 0;
uint16_t current_speed     = SPEED_START;

// --- Stepper state ---
// stepper_countdown and stepper_step_idx are written inside Timer1 ISR.
// stepper_interval is written by main loop (uint8_t → atomic on AVR).
// stepper_active is written by main loop; read by Timer1 ISR (bool = 1 byte → atomic).
volatile uint8_t stepper_countdown = 0;   // counts down to next step (ms)
volatile uint8_t stepper_step_idx  = 0;   // current index in STEP_SEQ[4]
volatile bool    stepper_active    = false;
         uint8_t stepper_interval  = 12;  // ms per step — updated from score tier

// --- Sonar debounce ---
// uint16_t, not atomic on AVR — always access with noInterrupts/interrupts.
volatile uint16_t sonar_debounce = 0;  // lockout countdown (ms)

// ============================================================================
// FUNCTION PROTOTYPES
// ============================================================================

// Actions
void action_show_title(void);
void action_start_game(void);
void action_flip_gravity(void);
void action_game_tick(void);
void action_show_death(void);
void action_flash_leds(void);
void action_show_game_over(void);
void action_restart(void);
void action_none(void);

// Helpers
void     setup_timers(void);
void     clear_event_flags(void);
void     deenergize_stepper(void);
void     shift_write(uint8_t bits);
uint8_t  speed_bar_bits(void);
uint8_t  get_stepper_interval(void);
uint32_t ping_sonar(void);
void     redraw_game(void);
uint8_t  get_player_char(void);
uint8_t  generate_obstacle(void);
void     update_speed(void);

// ISRs
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
  // EVENT_ENTRY fires once on state change; action_start_game handles init.
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
  Serial.begin(9600);

  // Flip buttons — internal pull-up, active-low
  pinMode(FLIP_BTN_A, INPUT_PULLUP);
  pinMode(FLIP_BTN_B, INPUT_PULLUP);

  // Sonar
  pinMode(SONAR_TRIG, OUTPUT);
  pinMode(SONAR_ECHO, INPUT);
  digitalWrite(SONAR_TRIG, LOW);

  // Fan — start off
  pinMode(FAN_PIN, OUTPUT);
  analogWrite(FAN_PIN, 0);

  // Shift register — A0 (PC0) = DATA, A1 (PC1) = CLK
  // Configure as outputs via direct register write
  DDRC  |=  (1 << 0) | (1 << 1);   // PC0, PC1 → output
  PORTC &= ~((1 << 0) | (1 << 1)); // both LOW
  shift_write(0x00);                // all LEDs off

  // Stepper motor — pins 8–11 = PB0–PB3 → output, deenergized
  DDRB  |=  0x0F;   // PB0–PB3 as outputs
  PORTB &= ~0x0F;   // all LOW

  Serial.println(F("DEBUG: before lcd.init()"));
  // LCD
  lcd.init();
  Serial.println(F("DEBUG: after lcd.init()"));
  // lcd.setRGB(80, 80, 80);   // Uncomment to enable grey backlight

  // Custom characters (loaded once into CGRAM)
  lcd.customSymbol(CHAR_FLOOR_A, floor_a);
  lcd.customSymbol(CHAR_FLOOR_B, floor_b);
  lcd.customSymbol(CHAR_CEIL_A,  ceil_a);
  lcd.customSymbol(CHAR_CEIL_B,  ceil_b);
  lcd.customSymbol(CHAR_WALL,    wall_block);

  // A2 left floating — use as entropy source (A0/A1 now used for shift register)
  randomSeed(analogRead(A2));

  setup_timers();

  attachInterrupt(digitalPinToInterrupt(FLIP_BTN_A), isr_flip_a, FALLING);
  attachInterrupt(digitalPinToInterrupt(FLIP_BTN_B), isr_flip_b, FALLING);

  sei();

  current_state  = TITLE_SCREEN;
  previous_state = GAME_OVER;   // force ENTRY event on first loop iteration
  Serial.println(F("DEBUG: setup() complete, entering loop()"));
}

// ============================================================================
// MAIN LOOP (Garbini Method — do not modify structure)
// ============================================================================

// DEBUG: standalone sonar test — prints a reading every 1 s regardless of game state.
// Remove this block once sonar is confirmed working.
static uint32_t _sonar_debug_last = 0;
void sonar_debug_tick(void) {
  uint32_t now = millis();
  if (now - _sonar_debug_last < 1000UL) return;
  _sonar_debug_last = now;
  uint32_t us = ping_sonar();
  if (us == 0) {
    Serial.println(F("[SONAR TEST] no echo"));
  } else {
    Serial.print(F("[SONAR TEST] "));
    Serial.print(us);
    Serial.print(F(" us = "));
    Serial.print(us * 0.034f / 2.0f, 1);
    Serial.println(F(" cm"));
  }
}

void loop() {
  sonar_debug_tick();   // DEBUG — remove once confirmed working
  Event event = get_event();
  execute_state_machine(event);
}

// ============================================================================
// EVENT DETECTION
// ============================================================================

Event get_event(void) {
  // Entry event fires exactly once whenever state changes
  if (current_state != previous_state) return EVENT_ENTRY;

  // Priority: player input > game tick > LED flash > display timer
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

      // Respect any state override set inside the action function
      // (e.g. action_flip_gravity and action_game_tick detect collisions
      //  and set current_state = PLAYER_DEAD directly)
      if (current_state == pre) {
        previous_state = current_state;
        current_state  = action_table[i].next_state;
      } else {
        previous_state = pre;
      }
      return;
    }
  }
  // No matching row → event silently ignored
}

// ============================================================================
// ACTION FUNCTIONS
// ============================================================================

// action_show_title ----------------------------------------------------------
// Shows the title screen with both player sprite orientations in column 0.
// Stepper spins slowly as a "heartbeat" visual indicator.
void action_show_title(void) {
  lcd.clear();
  // lcd.setRGB(60, 60, 120);

  // Stepper spins at idle speed during title screen
  stepper_interval = 15;   // 15 ms/step ≈ 1.6 RPM — slow and visible
  stepper_active   = true;

  shift_write(0x00);        // LEDs off on title
  analogWrite(FAN_PIN, 0);  // fan off
  flash_active = false;

  // Row 0: ceiling sprite | title text (15 chars)
  lcd.setCursor(0, 0);
  lcd.write((uint8_t)CHAR_CEIL_A);
  lcd.print(" GRAVITY GUY   ");

  // Row 1: floor sprite | prompt (15 chars)
  lcd.setCursor(0, 1);
  lcd.write((uint8_t)CHAR_FLOOR_A);
  lcd.print("   FLIP=START  ");

  clear_event_flags();
}

// action_start_game ----------------------------------------------------------
// Resets all game state, initialises peripherals, and starts scroll timer.
void action_start_game(void) {
  score             = 0;
  current_speed     = SPEED_START;
  player_on_ceiling = false;
  anim_frame        = false;
  gap_counter       = START_RUNWAY;
  last_obs          = OBS_NONE;

  memset(obstacles, OBS_NONE, sizeof(obstacles));

  // Stepper: update to starting game speed
  stepper_interval = get_stepper_interval();
  stepper_active   = true;

  // Speed bar: 0 LEDs at start
  shift_write(speed_bar_bits());

  clear_event_flags();
  redraw_game();
  // lcd.setRGB(80, 80, 80);

  noInterrupts();
  tick_countdown    = current_speed;
  display_countdown = 0;
  interrupts();
}

// action_flip_gravity --------------------------------------------------------
// Toggles gravity. Checks for immediate collision caused by the flip itself
// (player jumped into an existing wall at col 1). If safe, redraws col 1 only
// for an instantaneous visual response without disturbing the rest of the screen.
void action_flip_gravity(void) {
  player_on_ceiling = !player_on_ceiling;

  uint8_t at_player = obstacles[PLAYER_COL];
  if (( player_on_ceiling && (at_player & OBS_CEIL))  ||
      (!player_on_ceiling && (at_player & OBS_FLOOR))) {
    current_state = PLAYER_DEAD;
    return;
  }

  // Partial redraw — only update col 1 (player column)
  uint8_t ch = get_player_char();

  lcd.setCursor(PLAYER_COL, 0);
  if (player_on_ceiling) {
    lcd.write(ch);
  } else {
    lcd.write((at_player & OBS_CEIL) ? (uint8_t)CHAR_WALL : ' ');
  }

  lcd.setCursor(PLAYER_COL, 1);
  if (!player_on_ceiling) {
    lcd.write(ch);
  } else {
    lcd.write((at_player & OBS_FLOOR) ? (uint8_t)CHAR_WALL : ' ');
  }
}

// action_game_tick -----------------------------------------------------------
// Core game loop, fired each time tick_countdown reaches zero.
//   1. Sonar gesture check (before scrolling so flip affects this tick)
//   2. Shift obstacles left; generate new obstacle at col 15
//   3. Increment score; update game speed, stepper interval, and LED bar
//   4. Toggle animation frame; full-screen redraw
//   5. Collision check at player column (after redraw — player sees the hit)
//   6. If alive: reload tick countdown for next frame
void action_game_tick(void) {

  // 1 — Sonar gesture check
  // Wrapped in noInterrupts for atomic uint16_t read of sonar_debounce
  noInterrupts();
  bool can_ping = (sonar_debounce == 0);
  interrupts();

  if (can_ping) {
    uint32_t dist_us = ping_sonar();   // blocks ≤ 10 ms (10000 µs timeout)
    // DEBUG: print raw echo duration and computed distance
    Serial.print(F("SONAR: "));
    if (dist_us == 0) {
      Serial.println(F("no echo (>51 cm or timeout)"));
    } else {
      float dist_cm = dist_us * 0.034f / 2.0f;
      Serial.print(dist_us);
      Serial.print(F(" us = "));
      Serial.print(dist_cm, 1);
      Serial.print(F(" cm"));
      if (dist_us < SONAR_FLIP_US) {
        Serial.print(F("  *** FLIP TRIGGERED ***"));
      }
      Serial.println();
    }
    if (dist_us > 0 && dist_us < SONAR_FLIP_US) {
      // Hand within ~20 cm — treat as a flip input
      flip_pressed = true;   // consumed on next get_event() call
      noInterrupts();
      sonar_debounce = SONAR_DEBOUNCE_MS;
      interrupts();
    }
  }

  // 2 — Scroll obstacles one column left; fill new column at right
  for (uint8_t i = 0; i < 15; i++) obstacles[i] = obstacles[i + 1];
  obstacles[15] = generate_obstacle();

  // 3 — Advance score and update all speed-dependent peripherals
  score++;
  update_speed();
  stepper_interval = get_stepper_interval();   // stepper_interval is uint8_t (atomic)
  shift_write(speed_bar_bits());

  // 4 — Animate legs and redraw full screen
  anim_frame = !anim_frame;
  redraw_game();

  // 5 — Collision check (redraw happens first so the fatal frame is visible)
  uint8_t at_player = obstacles[PLAYER_COL];
  if (( player_on_ceiling && (at_player & OBS_CEIL))  ||
      (!player_on_ceiling && (at_player & OBS_FLOOR))) {
    current_state = PLAYER_DEAD;
    return;   // do NOT reload tick_countdown — game is over
  }

  // 6 — Reload tick timer
  noInterrupts();
  tick_countdown = current_speed;
  interrupts();
}

// action_show_death ----------------------------------------------------------
// Freezes game. Stops and deenergizes stepper (prevents coil heating).
// Blasts fan. Starts 6-LED flash via Timer2.
void action_show_death(void) {
  if (score > high_score) high_score = score;

  // Stop stepper and deenergize coils immediately
  stepper_active = false;
  deenergize_stepper();

  // Wind of defeat
  analogWrite(FAN_PIN, 200);   // ~78% duty, enough to spin the fan

  lcd.clear();
  // lcd.setRGB(200, 0, 0);

  lcd.setCursor(0, 0);
  lcd.print("  YOU CRASHED!  ");   // exactly 16 chars

  // "S:XXXX  Hi:XXXX" — snprintf with fixed-width %-4u fields
  char buf[17];
  snprintf(buf, sizeof(buf), " S:%-4u  Hi:%-4u", score, high_score);
  lcd.setCursor(0, 1);
  lcd.print(buf);

  // Enable 6-LED flash (replaces pin 12/13 from base version)
  flash_active    = true;
  led_flash_on    = false;
  flash_sub_count = 0;
  TIMSK2 |= (1 << OCIE2A);   // enable Timer2 interrupt

  noInterrupts();
  display_countdown = DEATH_DISPLAY_MS;
  tick_countdown    = 0;      // prevent stale tick from firing
  interrupts();

  clear_event_flags();
}

// action_flash_leds ----------------------------------------------------------
// Toggles all 6 shift-register LEDs. Called by Timer2 flash tick (~10 Hz).
// Replaces the digitalWrite(pin12/13) calls from the base version.
void action_flash_leds(void) {
  if (flash_active) {
    led_flash_on = !led_flash_on;
    shift_write(led_flash_on ? 0b00111111 : 0b00000000);
  }
}

// action_show_game_over ------------------------------------------------------
void action_show_game_over(void) {
  flash_active = false;
  shift_write(0x00);            // LEDs off
  analogWrite(FAN_PIN, 0);      // fan off
  TIMSK2 &= ~(1 << OCIE2A);    // disable Timer2

  lcd.clear();
  // lcd.setRGB(60, 20, 0);

  lcd.setCursor(0, 0);
  lcd.print("   GAME  OVER   ");

  char buf[17];
  snprintf(buf, sizeof(buf), "Hi:%-4u  flip=go", high_score);
  lcd.setCursor(0, 1);
  lcd.print(buf);

  clear_event_flags();
}

// action_restart -------------------------------------------------------------
// Cleans up peripherals before returning to TITLE_SCREEN (which will re-init them).
void action_restart(void) {
  score         = 0;
  current_speed = SPEED_START;
  flash_active  = false;
  shift_write(0x00);
  analogWrite(FAN_PIN, 0);
  deenergize_stepper();
  clear_event_flags();
}

void action_none(void) { }

// ============================================================================
// PERIPHERAL DRIVERS
// ============================================================================

// shift_write ----------------------------------------------------------------
// Send 8 bits MSB-first to SN74HC164N via direct PORTC bit-bang.
// PC0 = SR_DATA (A0), PC1 = SR_CLK (A1).
//
// Bit ordering: the last bit shifted in (bit 0, LSB) ends up at QA.
// So bit 0 → LED 1 (QA), bit 1 → LED 2 (QB), ... bit 5 → LED 6 (QF).
// QG (bit 6) and QH (bit 7) are not connected to LEDs and are ignored.
//
// Note: bits 4 and 5 of PORTC are I2C SDA/SCL. The bitmask operations
// only touch bits 0 and 1, so I2C is unaffected.
void shift_write(uint8_t bits) {
  for (int8_t i = 7; i >= 0; i--) {
    if ((bits >> i) & 1) {
      PORTC |=  (1 << 0);   // DATA = 1
    } else {
      PORTC &= ~(1 << 0);   // DATA = 0
    }
    PORTC |=  (1 << 1);     // CLK rising edge → data captured by 74HC164
    PORTC &= ~(1 << 1);     // CLK falling edge
  }
}

// speed_bar_bits -------------------------------------------------------------
// Returns the 6-bit LED pattern corresponding to the current speed tier.
// One additional LED lights up as each speed tier is reached (bar graph style).
uint8_t speed_bar_bits(void) {
  if      (score >= 100) return 0b00111111;   // tier 6 — max speed: 6 LEDs
  else if (score >= 75)  return 0b00011111;   // tier 5: 5 LEDs
  else if (score >= 50)  return 0b00001111;   // tier 4: 4 LEDs
  else if (score >= 30)  return 0b00000111;   // tier 3: 3 LEDs
  else if (score >= 15)  return 0b00000011;   // tier 2: 2 LEDs
  else if (score >= 5)   return 0b00000001;   // tier 1: 1 LED
  else                   return 0b00000000;   // tier 0: 0 LEDs (fresh start)
}

// get_stepper_interval -------------------------------------------------------
// Returns the stepper step period (ms) for the current score tier.
// Faster game → shorter interval → higher RPM.
//
// RPM = (1000 ms/s / interval ms/step) / 2048 steps/rev × 60 s/min
// At interval = 12 ms: ~2.4 RPM; at interval = 4 ms: ~7.3 RPM.
uint8_t get_stepper_interval(void) {
  if      (score >= 100) return 4;
  else if (score >= 75)  return 5;
  else if (score >= 50)  return 7;
  else if (score >= 30)  return 8;
  else if (score >= 15)  return 9;
  else if (score >= 5)   return 11;
  else                   return 12;
}

// ping_sonar -----------------------------------------------------------------
// Fire one HC-SR04 ping and return echo duration in microseconds.
// Returns 0 if no echo received within 10 ms (~170 cm).
// Uses pulseInLong() instead of pulseIn() so that Timer1 ISR firings
// do not corrupt the measurement (pulseInLong uses micros() internally).
uint32_t ping_sonar(void) {
  digitalWrite(SONAR_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(SONAR_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(SONAR_TRIG, LOW);
  return pulseInLong(SONAR_ECHO, HIGH, 10000UL);   // 10 ms timeout (~170 cm)
}

// deenergize_stepper ---------------------------------------------------------
// Drives all four coil outputs LOW to stop current flow and prevent heating
// when the motor is stationary. Called on death and restart.
void deenergize_stepper(void) {
  PORTB &= ~0x0F;   // clear PB0–PB3, preserve PB4–PB7
}

// ============================================================================
// GAME HELPER FUNCTIONS
// ============================================================================

uint8_t get_player_char(void) {
  if (!player_on_ceiling) {
    return anim_frame ? (uint8_t)CHAR_FLOOR_B : (uint8_t)CHAR_FLOOR_A;
  } else {
    return anim_frame ? (uint8_t)CHAR_CEIL_B  : (uint8_t)CHAR_CEIL_A;
  }
}

// redraw_game ----------------------------------------------------------------
// Full 32-cell screen redraw in two row passes.
// Two setCursor + 32 write calls ≈ 8 ms on 100 kHz I2C.
// Safe at all speed tiers (min tick = 110 ms → 7% overhead after sonar).
void redraw_game(void) {
  uint8_t ch = get_player_char();

  // Row 0 — ceiling row
  lcd.setCursor(0, 0);
  for (uint8_t col = 0; col < 16; col++) {
    if      (col == PLAYER_COL && player_on_ceiling) lcd.write(ch);
    else if (obstacles[col] & OBS_CEIL)               lcd.write((uint8_t)CHAR_WALL);
    else                                               lcd.write(' ');
  }

  // Row 1 — floor row
  lcd.setCursor(0, 1);
  for (uint8_t col = 0; col < 16; col++) {
    if      (col == PLAYER_COL && !player_on_ceiling) lcd.write(ch);
    else if (obstacles[col] & OBS_FLOOR)               lcd.write((uint8_t)CHAR_WALL);
    else                                               lcd.write(' ');
  }
}

// generate_obstacle ----------------------------------------------------------
// Returns the obstacle flag for the new column entering at the right edge.
// Enforces gap_counter (mandatory clear columns), 80/20 floor-ceil alternation,
// and score-based frequency scaling.
uint8_t generate_obstacle(void) {
  if (gap_counter > 0) { gap_counter--; return OBS_NONE; }

  uint8_t chance = 35;
  if      (score >= 80) chance = 60;
  else if (score >= 50) chance = 55;
  else if (score >= 30) chance = 50;
  else if (score >= 15) chance = 45;
  else if (score >= 5)  chance = 40;

  if ((uint8_t)random(100) < chance) {
    uint8_t new_obs;
    if      (last_obs == OBS_FLOOR) new_obs = ((uint8_t)random(100) < 80) ? OBS_CEIL  : OBS_FLOOR;
    else if (last_obs == OBS_CEIL)  new_obs = ((uint8_t)random(100) < 80) ? OBS_FLOOR : OBS_CEIL;
    else                            new_obs = (random(2) == 0)             ? OBS_FLOOR : OBS_CEIL;

    last_obs    = new_obs;
    gap_counter = (score >= 50) ? 1 : 2;
    return new_obs;
  }

  last_obs = OBS_NONE;
  return OBS_NONE;
}

// update_speed ---------------------------------------------------------------
// Recalculates current_speed for the current score. Called once per tick.
void update_speed(void) {
  if      (score >= 100) current_speed = SPEED_MIN;
  else if (score >= 75)  current_speed = 130;
  else if (score >= 50)  current_speed = 160;
  else if (score >= 30)  current_speed = 200;
  else if (score >= 15)  current_speed = 250;
  else if (score >= 5)   current_speed = 300;
  else                   current_speed = SPEED_START;
}

void clear_event_flags(void) {
  noInterrupts();
  flip_pressed          = false;
  game_tick_flag        = false;
  timer_display_expired = false;
  timer_flash_tick      = false;
  interrupts();
}

// ============================================================================
// TIMER SETUP (Register-Level Configuration)
// ============================================================================

void setup_timers(void) {
  cli();

  // ==== TIMER1 — 1 ms system tick (CTC mode) ================================
  // 16 MHz / prescaler 64 = 250 kHz clock
  // OCR1A = 249 → compare match every 250 counts = 1 ms exactly
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1  = 0;
  OCR1A  = 249;
  TCCR1B |= (1 << WGM12);                  // CTC mode
  TCCR1B |= (1 << CS11) | (1 << CS10);     // Prescaler /64
  TIMSK1 |= (1 << OCIE1A);                 // Enable compare match interrupt

  // ==== TIMER2 — LED flash base tick (~12.5 ms per tick) ====================
  // 16 MHz / prescaler 1024 = 15 625 Hz
  // OCR2A = 195 → interrupt every ~12.5 ms
  // DEATH_FLASH_RATE (4) ticks per toggle → ~50 ms per toggle ≈ 10 Hz
  TCCR2A = 0;
  TCCR2B = 0;
  TCNT2  = 0;
  OCR2A  = 195;
  TCCR2A |= (1 << WGM21);                                       // CTC mode
  TCCR2B |= (1 << CS22) | (1 << CS21) | (1 << CS20);           // Prescaler /1024
  TIMSK2 &= ~(1 << OCIE2A);   // Start disabled; action_show_death enables it

  sei();
}

// ============================================================================
// INTERRUPT SERVICE ROUTINES
// ============================================================================

// Timer1 ISR — fires every 1 ms
// Handles four independent countdown tasks in a single ISR:
//   1. Game scroll tick
//   2. Death/game-over display timer
//   3. Sonar gesture debounce lockout
//   4. Stepper motor step timer (advances coil state via direct PORTB write)
//
// Stepper step timing:
//   stepper_interval is a uint8_t — reads are atomic on AVR, safe to access
//   from ISR even though it is written by the main loop.
//   PORTB write is: (PORTB & 0xF0) preserves PB4–PB7 (pins 12–13, crystal),
//   then OR-in the 4-bit step pattern for PB0–PB3 (stepper IN1–IN4).
//   Four register operations ≈ 4 µs — well within the 1 ms ISR budget.
ISR(TIMER1_COMPA_vect) {
  // 1 — Game scroll tick
  if (tick_countdown > 0) {
    if (--tick_countdown == 0) game_tick_flag = true;
  }

  // 2 — Display timer
  if (display_countdown > 0) {
    if (--display_countdown == 0) timer_display_expired = true;
  }

  // 3 — Sonar debounce lockout
  if (sonar_debounce > 0) sonar_debounce--;

  // 4 — Stepper motor step
  if (stepper_active) {
    if (stepper_countdown > 0) {
      stepper_countdown--;
    } else {
      stepper_countdown = stepper_interval;                       // reload
      stepper_step_idx  = (stepper_step_idx + 1) & 3;            // advance CW
      PORTB = (PORTB & 0xF0) | STEP_SEQ[stepper_step_idx];       // drive coils
    }
  }
}

// Timer2 ISR — fires every ~12.5 ms (active only during PLAYER_DEAD)
// Sub-counts to DEATH_FLASH_RATE, then sets timer_flash_tick.
ISR(TIMER2_COMPA_vect) {
  if (++flash_sub_count >= DEATH_FLASH_RATE) {
    flash_sub_count  = 0;
    timer_flash_tick = true;
  }
}

// External interrupt — Flip button A (pin 2, INT0)
// 50 ms hardware debounce via timestamp comparison.
void isr_flip_a(void) {
  static unsigned long last_time = 0;
  unsigned long now = millis();
  if (now - last_time > 50) flip_pressed = true;
  last_time = now;
}

// External interrupt — Flip button B (pin 3, INT1)
// Identical to isr_flip_a — both buttons set the same flag.
void isr_flip_b(void) {
  static unsigned long last_time = 0;
  unsigned long now = millis();
  if (now - last_time > 50) flip_pressed = true;
  last_time = now;
}
