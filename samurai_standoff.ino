/*
 * SAMURAI STANDOFF - Arcade Reaction Game
 * McMaster University - MECHTRON 2TA4: Embedded Systems Design I
 *
 * A samurai standoff arcade game on a 16x2 LCD. Each round, two full-height
 * samurai sprites face off across the screen. After a random delay, the enemy
 * changes pose — a LUNGE means you must BLOCK; a GUARD means you must ATTACK.
 * React to the visual cue before the timer runs out. Wrong button or too
 * slow = death. Difficulty increases each round. Survive as long as you can.
 *
 * Visual design: all 8 HD44780 custom char slots are used for sprites.
 *   Slots 0-1 : player (top/bot) — never changes
 *   Slots 2-3 : enemy IDLE pose (top/bot) — shown during standoff
 *   Slots 4-5 : enemy ATTACK pose (top/bot) — enemy lunges → player must BLOCK
 *   Slots 6-7 : enemy GUARD pose (top/bot)  — enemy guards  → player must ATTACK
 *
 * Hardware:
 *   - DFRobot DFRduino Uno (ATmega328p, 16 MHz)
 *   - DFRobot Gravity LCD1602 V1.1 (I2C RGB, address 0x6B)
 *   - ATK button: Pin 2 (INT0)
 *   - BLK button: Pin 3 (INT1)
 *   - Red LED 1:  Pin 12
 *   - Red LED 2:  Pin 13
 *
 * FSM: Garbini method with action table (StateTransition structs)
 * Timing: All interrupt-driven — no delay() in loop
 *   - Timer1: 1 ms system tick (countdown timers, reaction timing)
 *   - Timer2: ~10 Hz LED flash tick (death effect)
 *
 * Author: Janahan
 * Date:   March 2026
 */

#include "DFRobot_RGBLCD1602.h"

// ============================================================================
// HARDWARE DEFINITIONS
// ============================================================================
#define ATK_BTN_PIN  2   // INT0 — Attack button
#define BLK_BTN_PIN  3   // INT1 — Block button
#define LED1_PIN     12  // Red LED 1
#define LED2_PIN     13  // Red LED 2

// LCD (I2C RGB backlight)
DFRobot_RGBLCD1602 lcd(/*RGBAddr*/ 0x6B, /*lcdCols*/ 16, /*lcdRows*/ 2);

// ============================================================================
// GAME TUNING CONSTANTS
// ============================================================================
#define STANDOFF_DELAY_MIN   1000  // Minimum random delay before cue (ms)
#define STANDOFF_DELAY_MAX   3500  // Maximum random delay before cue (ms)
#define REACTION_START       1200  // Initial reaction window (ms)
#define REACTION_DECREASE    80    // ms subtracted per round
#define REACTION_MINIMUM     350   // Fastest possible reaction window (ms)
#define WIN_DISPLAY_TIME     1200  // How long victory message shows (ms)
#define DEATH_DISPLAY_TIME   2500  // How long death screen shows (ms)
#define DEATH_FLASH_RATE     4     // Timer2 ticks per LED toggle (~10 Hz / 4)

// ============================================================================
// CUSTOM LCD CHARACTERS — full-height sprite system (8 slots)
//
// Each sprite is split into a TOP half (row 0) and BOTTOM half (row 1).
// Player lives at column 0, enemy at column 15, giving a 14-char stage.
//
// Pixel addressing: bit 4 = leftmost pixel, bit 0 = rightmost pixel.
// Player top row 3 (10111): body on left (bit4), sword tip right (bits2-0).
// Enemy is a horizontal mirror — sword tip is on left (bits4-2) pointing at player.
// ============================================================================

// CHAR 0: Player — upper body, facing right (sword points toward col 1)
byte player_top[8] = {
  0b00110,  // head
  0b00110,  // head
  0b01110,  // shoulders
  0b10111,  // torso + sword arm extending right
  0b00110,  // body
  0b01110,  // lower torso
  0b01010,  // waist
  0b01110   // hips
};

// CHAR 1: Player — lower body
byte player_bot[8] = {
  0b01010,  // upper thighs
  0b01010,  // thighs
  0b01010,  // shins
  0b11011,  // feet (wide stance)
  0b00000,
  0b00000,
  0b00000,
  0b00000
};

// CHAR 2: Enemy — upper body, IDLE/standoff, facing left (mirror of player)
byte enemy_idle_top[8] = {
  0b01100,  // head
  0b01100,  // head
  0b01110,  // shoulders
  0b11101,  // torso + sword tip pointing LEFT toward player
  0b01100,  // body
  0b01110,  // lower torso
  0b01010,  // waist
  0b01110   // hips
};

// CHAR 3: Enemy — lower body, IDLE (symmetric, same as player)
byte enemy_idle_bot[8] = {
  0b01010,  // upper thighs
  0b01010,  // thighs
  0b01010,  // shins
  0b11011,  // feet (wide stance)
  0b00000,
  0b00000,
  0b00000,
  0b00000
};

// CHAR 4: Enemy — upper body, ATTACK LUNGE
// Head lunges FAR LEFT (11000). Row 3 = 11111: sword fully extended, all 5 pixels.
// Player must BLOCK (press BLK) when this cue appears.
byte enemy_atk_top[8] = {
  0b11000,  // head leaning hard left (lunging forward)
  0b11000,  // head
  0b11110,  // arm+shoulder extending
  0b11111,  // SWORD TIP TO HILT — full 5-pixel lunge line
  0b01100,  // compressed torso (forward lean)
  0b01100,  // lower torso
  0b01010,  // waist
  0b01110   // hips
};

// CHAR 5: Enemy — lower body, ATTACK LUNGE (front foot forward)
byte enemy_atk_bot[8] = {
  0b01110,  // upper thigh
  0b11000,  // front leg lunging left
  0b11000,  // front knee bent forward
  0b00011,  // back leg (right side)
  0b00011,  // back shin
  0b00001,  // back foot
  0b00000,
  0b00000
};

// CHAR 6: Enemy — upper body, GUARD STANCE
// Row 2 = 11111: arms raised wide, sword horizontal across full width.
// Player must ATTACK (press ATK) when this cue appears.
byte enemy_blk_top[8] = {
  0b01100,  // head (slightly lowered into guard)
  0b01100,  // head
  0b11111,  // ARMS WIDE — sword held horizontal as full guard
  0b01110,  // torso below raised arms
  0b01110,  // body
  0b01110,  // lower body
  0b00100,  // waist (braced)
  0b01110   // hips
};

// CHAR 7: Enemy — lower body, GUARD STANCE (wide planted stance)
byte enemy_blk_bot[8] = {
  0b01110,  // upper thighs
  0b11011,  // wide stance
  0b10001,  // knees spread wide
  0b10001,  // lower legs spread
  0b11011,  // feet planted wide
  0b00000,
  0b00000,
  0b00000
};

// Custom character slot assignments
#define CHAR_PLAYER_TOP    0
#define CHAR_PLAYER_BOT    1
#define CHAR_ENEMY_IDLE_T  2
#define CHAR_ENEMY_IDLE_B  3
#define CHAR_ENEMY_ATK_T   4
#define CHAR_ENEMY_ATK_B   5
#define CHAR_ENEMY_BLK_T   6
#define CHAR_ENEMY_BLK_B   7

// ============================================================================
// FSM DEFINITIONS (Garbini Method)
// ============================================================================

typedef enum {
  TITLE_SCREEN,
  STANDOFF,
  CUE_SHOWN,
  ROUND_WIN,
  PLAYER_DEAD,
  GAME_OVER
} State;

typedef enum {
  EVENT_NONE,
  EVENT_ENTRY,
  EVENT_ATK_PRESS,
  EVENT_BLK_PRESS,
  EVENT_TIMER_DELAY,      // Random standoff delay expired → show cue
  EVENT_TIMER_REACTION,   // Reaction window expired → death
  EVENT_TIMER_DISPLAY,    // Win/death display timer expired
  EVENT_TIMER_FLASH       // LED flash tick
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
volatile State current_state = TITLE_SCREEN;
State previous_state = GAME_OVER;  // Force entry action on first run

// Event flags (set by ISRs, cleared by main loop)
volatile bool atk_pressed         = false;
volatile bool blk_pressed         = false;
volatile bool timer_delay_expired    = false;
volatile bool timer_reaction_expired = false;
volatile bool timer_display_expired  = false;
volatile bool timer_flash_tick       = false;

// Timing — countdown values decremented by Timer1 ISR (1 ms tick)
volatile uint16_t delay_countdown    = 0;
volatile uint16_t reaction_countdown = 0;
volatile uint16_t display_countdown  = 0;

// Game state
uint16_t score                     = 0;
uint16_t high_score                = 0;
uint16_t current_reaction_window   = REACTION_START;

// Cue logic:
//   player_must_attack = true  → enemy shows GUARD pose  → press ATK to win
//   player_must_attack = false → enemy shows ATTACK pose → press BLK to win
bool player_must_attack = true;

// Which enemy chars are currently displayed (tracked for death screen)
uint8_t enemy_top_char = CHAR_ENEMY_IDLE_T;
uint8_t enemy_bot_char = CHAR_ENEMY_IDLE_B;

// LED flash state
volatile bool led_flash_on    = false;
volatile uint8_t flash_sub_count = 0;
bool flash_active             = false;

// ============================================================================
// FUNCTION PROTOTYPES
// ============================================================================

void action_show_title(void);
void action_start_standoff(void);
void action_show_cue(void);
void action_check_atk(void);
void action_check_blk(void);
void action_timeout_death(void);
void action_flash_leds(void);
void action_show_win(void);
void action_next_round(void);
void action_show_death(void);
void action_show_game_over(void);
void action_restart(void);
void action_premature_press(void);
void action_none(void);

void setup_timers(void);
void clear_event_flags(void);
void leds_off(void);
void draw_standoff_scene(void);

void isr_atk(void);
void isr_blk(void);

// ============================================================================
// ACTION TABLE (Garbini Method)
// ============================================================================

const StateTransition action_table[] = {
  // --- TITLE_SCREEN ---
  { TITLE_SCREEN, EVENT_ENTRY,     action_show_title,     TITLE_SCREEN },
  { TITLE_SCREEN, EVENT_ATK_PRESS, action_start_standoff, STANDOFF     },
  { TITLE_SCREEN, EVENT_BLK_PRESS, action_start_standoff, STANDOFF     },

  // --- STANDOFF (waiting for random delay to expire) ---
  { STANDOFF,     EVENT_ENTRY,       action_none,            STANDOFF    },
  { STANDOFF,     EVENT_ATK_PRESS,   action_premature_press, PLAYER_DEAD },
  { STANDOFF,     EVENT_BLK_PRESS,   action_premature_press, PLAYER_DEAD },
  { STANDOFF,     EVENT_TIMER_DELAY, action_show_cue,        CUE_SHOWN   },

  // --- CUE_SHOWN (player must react to enemy pose change) ---
  { CUE_SHOWN,    EVENT_ENTRY,          action_none,          CUE_SHOWN   },
  { CUE_SHOWN,    EVENT_ATK_PRESS,      action_check_atk,     ROUND_WIN   },
  { CUE_SHOWN,    EVENT_BLK_PRESS,      action_check_blk,     ROUND_WIN   },
  { CUE_SHOWN,    EVENT_TIMER_REACTION, action_timeout_death, PLAYER_DEAD },

  // --- ROUND_WIN ---
  { ROUND_WIN,    EVENT_ENTRY,         action_show_win,   ROUND_WIN },
  { ROUND_WIN,    EVENT_TIMER_DISPLAY, action_next_round, STANDOFF  },

  // --- PLAYER_DEAD (LED flash + death display) ---
  { PLAYER_DEAD,  EVENT_ENTRY,         action_show_death,     PLAYER_DEAD },
  { PLAYER_DEAD,  EVENT_TIMER_FLASH,   action_flash_leds,     PLAYER_DEAD },
  { PLAYER_DEAD,  EVENT_TIMER_DISPLAY, action_show_game_over, GAME_OVER   },

  // --- GAME_OVER ---
  { GAME_OVER,    EVENT_ENTRY,     action_none,    GAME_OVER    },
  { GAME_OVER,    EVENT_ATK_PRESS, action_restart, TITLE_SCREEN },
  { GAME_OVER,    EVENT_BLK_PRESS, action_restart, TITLE_SCREEN },
};

const uint8_t ACTION_TABLE_SIZE = sizeof(action_table) / sizeof(StateTransition);

// ============================================================================
// ARDUINO SETUP
// ============================================================================

void setup() {
  pinMode(ATK_BTN_PIN, INPUT_PULLUP);
  pinMode(BLK_BTN_PIN, INPUT_PULLUP);
  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  digitalWrite(LED1_PIN, LOW);
  digitalWrite(LED2_PIN, LOW);

  lcd.init();
  // lcd.setRGB(255, 80, 0);  // Uncomment to enable warm orange title backlight

  // Load all 8 sprite characters into CGRAM once — never changed at runtime
  lcd.customSymbol(CHAR_PLAYER_TOP,   player_top);
  lcd.customSymbol(CHAR_PLAYER_BOT,   player_bot);
  lcd.customSymbol(CHAR_ENEMY_IDLE_T, enemy_idle_top);
  lcd.customSymbol(CHAR_ENEMY_IDLE_B, enemy_idle_bot);
  lcd.customSymbol(CHAR_ENEMY_ATK_T,  enemy_atk_top);
  lcd.customSymbol(CHAR_ENEMY_ATK_B,  enemy_atk_bot);
  lcd.customSymbol(CHAR_ENEMY_BLK_T,  enemy_blk_top);
  lcd.customSymbol(CHAR_ENEMY_BLK_B,  enemy_blk_bot);

  randomSeed(analogRead(A0));

  setup_timers();

  attachInterrupt(digitalPinToInterrupt(ATK_BTN_PIN), isr_atk, FALLING);
  attachInterrupt(digitalPinToInterrupt(BLK_BTN_PIN), isr_blk, FALLING);

  sei();

  current_state  = TITLE_SCREEN;
  previous_state = GAME_OVER;  // Force entry action
}

// ============================================================================
// ARDUINO LOOP (Garbini Method — do not modify structure)
// ============================================================================

void loop() {
  Event event = get_event();
  execute_state_machine(event);
}

// ============================================================================
// EVENT DETECTION
// ============================================================================

Event get_event(void) {
  if (current_state != previous_state) {
    return EVENT_ENTRY;
  }

  if (atk_pressed) { atk_pressed = false; return EVENT_ATK_PRESS; }
  if (blk_pressed) { blk_pressed = false; return EVENT_BLK_PRESS; }

  if (timer_delay_expired)    { timer_delay_expired    = false; return EVENT_TIMER_DELAY;    }
  if (timer_reaction_expired) { timer_reaction_expired = false; return EVENT_TIMER_REACTION; }
  if (timer_display_expired)  { timer_display_expired  = false; return EVENT_TIMER_DISPLAY;  }
  if (timer_flash_tick)       { timer_flash_tick       = false; return EVENT_TIMER_FLASH;    }

  return EVENT_NONE;
}

// ============================================================================
// STATE MACHINE EXECUTION (Garbini Method)
// ============================================================================

void execute_state_machine(Event event) {
  if (event == EVENT_NONE) return;

  for (uint8_t i = 0; i < ACTION_TABLE_SIZE; i++) {
    if (action_table[i].state == current_state &&
        action_table[i].event == event) {

      State pre_action_state = current_state;

      if (action_table[i].action != NULL) {
        action_table[i].action();
      }

      // Respect any state override set inside an action function
      // (e.g. wrong-button detection in action_check_atk/blk)
      if (current_state == pre_action_state) {
        previous_state = current_state;
        current_state  = action_table[i].next_state;
      } else {
        previous_state = pre_action_state;
      }

      return;
    }
  }
}

// ============================================================================
// ACTION FUNCTIONS
// ============================================================================

void action_show_title(void) {
  lcd.clear();
  // lcd.setRGB(255, 80, 0);
  leds_off();
  flash_active = false;

  // Row 0: player top | " SAMURAI    " (14 chars) | enemy idle top
  //        "   SAMURAI   " centers "SAMURAI" in 14 chars: 3 left + 7 + 4 right = 14
  lcd.setCursor(0, 0);
  lcd.write((uint8_t)CHAR_PLAYER_TOP);
  lcd.print("   SAMURAI    ");   // 14 chars fills cols 1-14
  lcd.setCursor(15, 0);
  lcd.write((uint8_t)CHAR_ENEMY_IDLE_T);

  // Row 1: player bot | " STANDOFF! " (14 chars) | enemy idle bot
  //        "  STANDOFF!   " : 2 left + 9 + 3 right = 14
  lcd.setCursor(0, 1);
  lcd.write((uint8_t)CHAR_PLAYER_BOT);
  lcd.print("  STANDOFF!   ");   // 14 chars fills cols 1-14
  lcd.setCursor(15, 1);
  lcd.write((uint8_t)CHAR_ENEMY_IDLE_B);

  clear_event_flags();
}

void action_start_standoff(void) {
  score = 0;
  current_reaction_window = REACTION_START;

  clear_event_flags();
  draw_standoff_scene();
  // lcd.setRGB(60, 60, 120);

  uint16_t rand_delay = random(STANDOFF_DELAY_MIN, STANDOFF_DELAY_MAX + 1);
  noInterrupts();
  delay_countdown    = rand_delay;
  reaction_countdown = 0;
  display_countdown  = 0;
  interrupts();
}

// action_show_cue: ONLY updates column 15 — no lcd.clear().
// The standoff scene stays on screen; the enemy sprite snaps to the new pose.
// This gives a clean, instant visual trigger with no flicker.
void action_show_cue(void) {
  player_must_attack = (random(0, 2) == 0);

  if (player_must_attack) {
    // Enemy raises guard → player must ATTACK
    enemy_top_char = CHAR_ENEMY_BLK_T;
    enemy_bot_char = CHAR_ENEMY_BLK_B;
  } else {
    // Enemy lunges → player must BLOCK
    enemy_top_char = CHAR_ENEMY_ATK_T;
    enemy_bot_char = CHAR_ENEMY_ATK_B;
  }

  // Update only the enemy column — preserve everything else on screen
  lcd.setCursor(15, 0);
  lcd.write(enemy_top_char);
  lcd.setCursor(15, 1);
  lcd.write(enemy_bot_char);

  // lcd.setRGB(255, 0, 0);  // Uncomment for red backlight tension

  noInterrupts();
  reaction_countdown = current_reaction_window;
  interrupts();

  clear_event_flags();
}

void action_check_atk(void) {
  if (!player_must_attack) {
    // Enemy was lunging — player should have blocked, not attacked
    current_state = PLAYER_DEAD;
    return;
  }
  // Correct! Enemy was guarding — attack breaks the guard
  noInterrupts();
  reaction_countdown = 0;
  interrupts();
  score++;
}

void action_check_blk(void) {
  if (player_must_attack) {
    // Enemy was guarding — player should have attacked, not blocked
    current_state = PLAYER_DEAD;
    return;
  }
  // Correct! Enemy was attacking — block deflects the blow
  noInterrupts();
  reaction_countdown = 0;
  interrupts();
  score++;
}

void action_timeout_death(void) {
  // Too slow — PLAYER_DEAD entry action handles the rest
}

void action_premature_press(void) {
  // Button pressed before cue appeared — dishonourable! Death.
  // enemy_top/bot_char still hold IDLE values from draw_standoff_scene()
}

void action_show_win(void) {
  lcd.clear();
  // lcd.setRGB(0, 200, 0);

  // Player (victorious) on left; enemy side left blank (enemy is defeated)
  lcd.setCursor(0, 0);
  lcd.write((uint8_t)CHAR_PLAYER_TOP);
  lcd.print("   VICTORY!   ");  // 14 chars

  lcd.setCursor(0, 1);
  lcd.write((uint8_t)CHAR_PLAYER_BOT);
  lcd.print("   Kills: ");
  lcd.print(score);
  // Col 15 intentionally empty — enemy is gone

  noInterrupts();
  display_countdown = WIN_DISPLAY_TIME;
  interrupts();

  clear_event_flags();
}

void action_next_round(void) {
  // Increase difficulty
  if (current_reaction_window > REACTION_MINIMUM + REACTION_DECREASE) {
    current_reaction_window -= REACTION_DECREASE;
  } else {
    current_reaction_window = REACTION_MINIMUM;
  }

  clear_event_flags();
  draw_standoff_scene();    // resets enemy to idle and shows new round number
  // lcd.setRGB(60, 60, 120);

  uint16_t rand_delay = random(STANDOFF_DELAY_MIN, STANDOFF_DELAY_MAX + 1);
  noInterrupts();
  delay_countdown    = rand_delay;
  reaction_countdown = 0;
  display_countdown  = 0;
  interrupts();
}

void action_show_death(void) {
  if (score > high_score) high_score = score;

  lcd.clear();
  // lcd.setRGB(255, 0, 0);

  // Enemy (victorious) on right, showing the pose that just killed the player
  lcd.setCursor(15, 0);
  lcd.write(enemy_top_char);
  lcd.setCursor(15, 1);
  lcd.write(enemy_bot_char);

  // Death text fills cols 0-14 (player side is blank — player is dead)
  lcd.setCursor(0, 0);
  lcd.print(" YOU DIED      ");  // 15 chars (cols 0-14)

  lcd.setCursor(0, 1);
  lcd.print(" Sc:");
  lcd.print(score);
  lcd.print("  Hi:");
  lcd.print(high_score);
  // Remaining cols up to 14 left blank from lcd.clear()

  flash_active    = true;
  led_flash_on    = false;
  flash_sub_count = 0;
  TIMSK2 |= (1 << OCIE2A);  // Enable Timer2 for LED flash

  noInterrupts();
  display_countdown  = DEATH_DISPLAY_TIME;
  delay_countdown    = 0;
  reaction_countdown = 0;
  interrupts();

  clear_event_flags();
}

void action_flash_leds(void) {
  if (flash_active) {
    led_flash_on = !led_flash_on;
    digitalWrite(LED1_PIN, led_flash_on ? HIGH : LOW);
    digitalWrite(LED2_PIN, led_flash_on ? HIGH : LOW);
  }
}

void action_show_game_over(void) {
  flash_active = false;
  leds_off();
  TIMSK2 &= ~(1 << OCIE2A);  // Disable Timer2 interrupt

  lcd.clear();
  // lcd.setRGB(80, 0, 0);

  lcd.setCursor(0, 0);
  lcd.print("  GAME  OVER  ");

  lcd.setCursor(0, 1);
  lcd.print("Hi:");
  lcd.print(high_score);
  lcd.print("  Btn=Retry");

  clear_event_flags();
}

void action_restart(void) {
  score                   = 0;
  current_reaction_window = REACTION_START;
  flash_active            = false;
  leds_off();
  clear_event_flags();
}

void action_none(void) { }

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

// draw_standoff_scene: draws the complete standoff layout.
// Called on game start and between rounds. Resets enemy to idle pose.
//
// Layout (16 cols × 2 rows):
//   Col 0       = player sprite (top or bot char)
//   Cols 1-14   = stage area: row 0 empty, row 1 shows "R:X"
//   Col 15      = enemy sprite (top or bot char)
//
void draw_standoff_scene(void) {
  lcd.clear();

  // Reset enemy tracking to idle
  enemy_top_char = CHAR_ENEMY_IDLE_T;
  enemy_bot_char = CHAR_ENEMY_IDLE_B;

  // Row 0: player top | empty stage | enemy idle top
  lcd.setCursor(0, 0);
  lcd.write((uint8_t)CHAR_PLAYER_TOP);
  // Cols 1-14: leave blank (14 spaces)
  lcd.print("              ");  // 14 spaces
  lcd.setCursor(15, 0);
  lcd.write((uint8_t)CHAR_ENEMY_IDLE_T);

  // Row 1: player bot | "R:X" + padding | enemy idle bot
  lcd.setCursor(0, 1);
  lcd.write((uint8_t)CHAR_PLAYER_BOT);
  // " R:X" from col 1
  lcd.print(" R:");
  lcd.print(score + 1);
  // Pad remaining cols to keep clear of col 15
  // " R:" = 3 chars, digits = 1 or 2; need to fill cols (4+digits) through 14
  uint8_t written = 3 + ((score + 1 < 10) ? 1 : 2);  // chars written to cols 1..N
  for (uint8_t i = written; i < 14; i++) lcd.print(' ');
  lcd.setCursor(15, 1);
  lcd.write((uint8_t)CHAR_ENEMY_IDLE_B);
}

void clear_event_flags(void) {
  noInterrupts();
  atk_pressed          = false;
  blk_pressed          = false;
  timer_delay_expired    = false;
  timer_reaction_expired = false;
  timer_display_expired  = false;
  timer_flash_tick       = false;
  interrupts();
}

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

  // ==== TIMER1 — 1 ms system tick (CTC mode) ====
  // 16 MHz / 64 prescaler = 250 kHz → OCR1A = 249 → fires every 1 ms
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1  = 0;
  OCR1A  = 249;
  TCCR1B |= (1 << WGM12);                  // CTC mode
  TCCR1B |= (1 << CS11) | (1 << CS10);     // Prescaler 64
  TIMSK1 |= (1 << OCIE1A);                 // Enable compare interrupt

  // ==== TIMER2 — LED flash tick (~10 Hz during death) ====
  // 16 MHz / 1024 prescaler → OCR2A = 195 → ~12.5 ms per tick
  // DEATH_FLASH_RATE ticks per toggle → ~50 ms per toggle (~10 Hz)
  TCCR2A = 0;
  TCCR2B = 0;
  TCNT2  = 0;
  OCR2A  = 195;
  TCCR2A |= (1 << WGM21);                                          // CTC mode
  TCCR2B |= (1 << CS22) | (1 << CS21) | (1 << CS20);              // Prescaler 1024
  TIMSK2 &= ~(1 << OCIE2A);                                        // Start disabled

  sei();
}

// ============================================================================
// INTERRUPT SERVICE ROUTINES
// ============================================================================

// Timer1 ISR — 1 ms system tick
ISR(TIMER1_COMPA_vect) {
  if (delay_countdown > 0) {
    if (--delay_countdown == 0) timer_delay_expired = true;
  }
  if (reaction_countdown > 0) {
    if (--reaction_countdown == 0) timer_reaction_expired = true;
  }
  if (display_countdown > 0) {
    if (--display_countdown == 0) timer_display_expired = true;
  }
}

// Timer2 ISR — LED flash (~12.5 ms base tick)
ISR(TIMER2_COMPA_vect) {
  if (++flash_sub_count >= DEATH_FLASH_RATE) {
    flash_sub_count = 0;
    timer_flash_tick = true;
  }
}

// External Interrupt — ATK button (pin 2, INT0), 50 ms debounce
void isr_atk(void) {
  static unsigned long last_time = 0;
  unsigned long now = millis();
  if (now - last_time > 50) {
    atk_pressed = true;
  }
  last_time = now;
}

// External Interrupt — BLK button (pin 3, INT1), 50 ms debounce
void isr_blk(void) {
  static unsigned long last_time = 0;
  unsigned long now = millis();
  if (now - last_time > 50) {
    blk_pressed = true;
  }
  last_time = now;
}
