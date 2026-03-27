/*
 * SAMURAI STANDOFF - Arcade Reaction Game
 * McMaster University - MECHTRON 2TA4: Embedded Systems Design I
 *
 * A samurai standoff arcade game on a 16x2 LCD. Each round, two samurai
 * face off. After a random delay, a cue ("ATK!" or "BLK!") appears.
 * Press the matching button to defeat your opponent. Wrong button or
 * too slow = death. Difficulty increases each round.
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
#define WIN_DISPLAY_TIME     1200  // How long "VICTORY" message shows (ms)
#define DEATH_DISPLAY_TIME   2500  // How long death screen shows (ms)
#define DEATH_FLASH_RATE     4     // Timer2 ticks per LED toggle (~10 Hz / 4)

// ============================================================================
// CUSTOM LCD CHARACTERS
// ============================================================================

// Samurai facing right (player — left side of screen)
byte samurai_r[8] = {
  0b00110,
  0b00110,
  0b01110,
  0b10111,
  0b00110,
  0b01110,
  0b01010,
  0b11011
};

// Samurai facing left (enemy — right side of screen)
byte samurai_l[8] = {
  0b01100,
  0b01100,
  0b01110,
  0b11101,
  0b01100,
  0b01110,
  0b01010,
  0b11011
};

// Katana pointing right
byte katana_r[8] = {
  0b00000,
  0b00000,
  0b00001,
  0b00010,
  0b00100,
  0b01100,
  0b01000,
  0b00000
};

// Katana pointing left
byte katana_l[8] = {
  0b00000,
  0b00000,
  0b10000,
  0b01000,
  0b00100,
  0b00110,
  0b00010,
  0b00000
};

// Skull (death)
byte skull[8] = {
  0b01110,
  0b10001,
  0b11011,
  0b10001,
  0b01110,
  0b00100,
  0b01110,
  0b00000
};

// Crossed swords (clash)
byte clash[8] = {
  0b10001,
  0b01010,
  0b00100,
  0b01110,
  0b00100,
  0b01010,
  0b10001,
  0b00000
};

#define CHAR_SAMURAI_R 0
#define CHAR_SAMURAI_L 1
#define CHAR_KATANA_R  2
#define CHAR_KATANA_L  3
#define CHAR_SKULL     4
#define CHAR_CLASH     5

// ============================================================================
// FSM DEFINITIONS (Garbini Method)
// ============================================================================

// State enumeration
typedef enum {
  TITLE_SCREEN,
  STANDOFF,
  CUE_SHOWN,
  ROUND_WIN,
  PLAYER_DEAD,
  GAME_OVER
} State;

// Event enumeration
typedef enum {
  EVENT_NONE,
  EVENT_ENTRY,
  EVENT_ATK_PRESS,
  EVENT_BLK_PRESS,
  EVENT_TIMER_DELAY,     // Random standoff delay expired
  EVENT_TIMER_REACTION,  // Reaction window expired (too slow)
  EVENT_TIMER_DISPLAY,   // Win/death display timer expired
  EVENT_TIMER_FLASH      // LED flash tick
} Event;

// Action table structure (Garbini)
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
volatile bool atk_pressed = false;
volatile bool blk_pressed = false;
volatile bool timer_delay_expired = false;
volatile bool timer_reaction_expired = false;
volatile bool timer_display_expired = false;
volatile bool timer_flash_tick = false;

// Timing — all countdown values decremented by Timer1 ISR (1 ms tick)
volatile uint16_t delay_countdown = 0;      // Standoff random delay
volatile uint16_t reaction_countdown = 0;   // Reaction window
volatile uint16_t display_countdown = 0;    // Message display time

// Game state
uint16_t score = 0;
uint16_t high_score = 0;
uint16_t current_reaction_window = REACTION_START;
bool cue_is_attack = true;  // true = "ATK!", false = "BLK!"

// LED flash state
volatile bool led_flash_on = false;
volatile uint8_t flash_sub_count = 0;
bool flash_active = false;

// ============================================================================
// FUNCTION PROTOTYPES
// ============================================================================

// Action functions
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

// Helper functions
void setup_timers(void);
void clear_event_flags(void);
void leds_off(void);
void draw_standoff_scene(void);

// ISRs
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
  { STANDOFF,     EVENT_ENTRY,      action_none,            STANDOFF     },
  { STANDOFF,     EVENT_ATK_PRESS,  action_premature_press, PLAYER_DEAD  },
  { STANDOFF,     EVENT_BLK_PRESS,  action_premature_press, PLAYER_DEAD  },
  { STANDOFF,     EVENT_TIMER_DELAY,action_show_cue,        CUE_SHOWN    },

  // --- CUE_SHOWN (player must react) ---
  { CUE_SHOWN,    EVENT_ENTRY,           action_none,         CUE_SHOWN    },
  { CUE_SHOWN,    EVENT_ATK_PRESS,       action_check_atk,    ROUND_WIN    },
  { CUE_SHOWN,    EVENT_BLK_PRESS,       action_check_blk,    ROUND_WIN    },
  { CUE_SHOWN,    EVENT_TIMER_REACTION,  action_timeout_death, PLAYER_DEAD },

  // --- ROUND_WIN ---
  { ROUND_WIN,    EVENT_ENTRY,          action_show_win,       ROUND_WIN },
  { ROUND_WIN,    EVENT_TIMER_DISPLAY,  action_next_round,     STANDOFF  },

  // --- PLAYER_DEAD (LED flash + death message) ---
  { PLAYER_DEAD,  EVENT_ENTRY,          action_show_death,     PLAYER_DEAD },
  { PLAYER_DEAD,  EVENT_TIMER_FLASH,    action_flash_leds,     PLAYER_DEAD },
  { PLAYER_DEAD,  EVENT_TIMER_DISPLAY,  action_show_game_over, GAME_OVER   },

  // --- GAME_OVER ---
  { GAME_OVER,    EVENT_ENTRY,     action_none,    GAME_OVER    },
  { GAME_OVER,    EVENT_ATK_PRESS, action_restart,  TITLE_SCREEN },
  { GAME_OVER,    EVENT_BLK_PRESS, action_restart,  TITLE_SCREEN },
};

const uint8_t ACTION_TABLE_SIZE = sizeof(action_table) / sizeof(StateTransition);

// ============================================================================
// ARDUINO SETUP
// ============================================================================

void setup() {
  // Pin modes
  pinMode(ATK_BTN_PIN, INPUT_PULLUP);
  pinMode(BLK_BTN_PIN, INPUT_PULLUP);
  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  digitalWrite(LED1_PIN, LOW);
  digitalWrite(LED2_PIN, LOW);

  // LCD init
  lcd.init();
  // lcd.setRGB(255, 80, 0);  // Warm orange backlight

  // Custom characters
  lcd.customSymbol(CHAR_SAMURAI_R, samurai_r);
  lcd.customSymbol(CHAR_SAMURAI_L, samurai_l);
  lcd.customSymbol(CHAR_KATANA_R,  katana_r);
  lcd.customSymbol(CHAR_KATANA_L,  katana_l);
  lcd.customSymbol(CHAR_SKULL,     skull);
  lcd.customSymbol(CHAR_CLASH,     clash);

  // Seed RNG
  randomSeed(analogRead(A0));

  // Hardware timers
  setup_timers();

  // External interrupts
  attachInterrupt(digitalPinToInterrupt(ATK_BTN_PIN), isr_atk, FALLING);
  attachInterrupt(digitalPinToInterrupt(BLK_BTN_PIN), isr_blk, FALLING);

  sei();

  // Init state machine
  current_state = TITLE_SCREEN;
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
  // Entry event (state just changed)
  if (current_state != previous_state) {
    return EVENT_ENTRY;
  }

  // Button events — check attack first for slight priority
  if (atk_pressed) {
    atk_pressed = false;
    return EVENT_ATK_PRESS;
  }
  if (blk_pressed) {
    blk_pressed = false;
    return EVENT_BLK_PRESS;
  }

  // Timer events
  if (timer_delay_expired) {
    timer_delay_expired = false;
    return EVENT_TIMER_DELAY;
  }
  if (timer_reaction_expired) {
    timer_reaction_expired = false;
    return EVENT_TIMER_REACTION;
  }
  if (timer_display_expired) {
    timer_display_expired = false;
    return EVENT_TIMER_DISPLAY;
  }
  if (timer_flash_tick) {
    timer_flash_tick = false;
    return EVENT_TIMER_FLASH;
  }

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

      // Save state before action runs
      State pre_action_state = current_state;

      // Execute action
      if (action_table[i].action != NULL) {
        action_table[i].action();
      }

      // If the action function manually changed current_state
      // (e.g., wrong button detected), respect that override.
      // Otherwise, apply the table's default next_state.
      if (current_state == pre_action_state) {
        previous_state = current_state;
        current_state = action_table[i].next_state;
      } else {
        previous_state = pre_action_state;
      }

      return;
    }
  }
  // No matching transition — event is ignored
}

// ============================================================================
// ACTION FUNCTIONS
// ============================================================================

void action_show_title(void) {
  lcd.clear();
  // lcd.setRGB(255, 80, 0);  // Orange
  leds_off();
  flash_active = false;

  // Line 1:  samurai  SAMURAI  samurai
  lcd.setCursor(0, 0);
  lcd.write((uint8_t)CHAR_SAMURAI_R);
  lcd.print(" SAMURAI    ");
  lcd.write((uint8_t)CHAR_SAMURAI_L);

  // Line 2:  katana  STANDOFF!  katana
  lcd.setCursor(0, 1);
  lcd.write((uint8_t)CHAR_KATANA_R);
  lcd.print(" STANDOFF!  ");
  lcd.write((uint8_t)CHAR_KATANA_L);

  clear_event_flags();
}

void action_start_standoff(void) {
  // Reset game state
  score = 0;
  current_reaction_window = REACTION_START;

  // Clear flags and begin standoff
  clear_event_flags();
  draw_standoff_scene();
  // lcd.setRGB(60, 60, 120);  // Cool blue for tension

  // Start random delay countdown
  uint16_t rand_delay = random(STANDOFF_DELAY_MIN, STANDOFF_DELAY_MAX + 1);
  noInterrupts();
  delay_countdown = rand_delay;
  reaction_countdown = 0;
  display_countdown = 0;
  interrupts();
}

void action_show_cue(void) {
  // Randomly choose ATK or BLK cue
  cue_is_attack = (random(0, 2) == 0);

  lcd.clear();
  lcd.setCursor(0, 0);

  if (cue_is_attack) {
    // Show ATK! cue
    lcd.write((uint8_t)CHAR_SAMURAI_R);
    lcd.print("  ");
    lcd.write((uint8_t)CHAR_CLASH);
    lcd.print(" ATK! ");
    lcd.write((uint8_t)CHAR_CLASH);
    lcd.print("  ");
    lcd.write((uint8_t)CHAR_SAMURAI_L);
  } else {
    // Show BLK! cue
    lcd.write((uint8_t)CHAR_SAMURAI_R);
    lcd.print("  ");
    lcd.write((uint8_t)CHAR_CLASH);
    lcd.print(" BLK! ");
    lcd.write((uint8_t)CHAR_CLASH);
    lcd.print("  ");
    lcd.write((uint8_t)CHAR_SAMURAI_L);
  }

  // Line 2: show which button to press
  lcd.setCursor(0, 1);
  if (cue_is_attack) {
    lcd.print("  >> ATTACK! << ");
  } else {
    lcd.print("  >> BLOCK!  << ");
  }

  // lcd.setRGB(255, 0, 0);  // Red — urgency!

  // Start reaction countdown
  noInterrupts();
  reaction_countdown = current_reaction_window;
  interrupts();

  clear_event_flags();
}

void action_check_atk(void) {
  // Player pressed ATK — correct only if cue was ATK
  if (!cue_is_attack) {
    // Wrong button! Override state to PLAYER_DEAD.
    // execute_state_machine detects the change and skips the
    // table's default next_state (ROUND_WIN).
    // The PLAYER_DEAD entry event will fire on the next loop
    // iteration, which calls action_show_death automatically.
    current_state = PLAYER_DEAD;
    return;
  }
  // Correct! Stop reaction timer
  noInterrupts();
  reaction_countdown = 0;
  interrupts();
  score++;
}

void action_check_blk(void) {
  // Player pressed BLK — correct only if cue was BLK
  if (cue_is_attack) {
    // Wrong button! Override state to PLAYER_DEAD.
    current_state = PLAYER_DEAD;
    return;
  }
  // Correct!
  noInterrupts();
  reaction_countdown = 0;
  interrupts();
  score++;
}

void action_timeout_death(void) {
  // Player was too slow — no action needed here,
  // transition to PLAYER_DEAD handles the rest via entry action
}

void action_premature_press(void) {
  // Player pressed a button during the standoff (before cue appeared)
  // This is dishonourable! Death.
}

void action_show_win(void) {
  lcd.clear();
  // lcd.setRGB(0, 255, 0);  // Green — victory!

  lcd.setCursor(0, 0);
  lcd.write((uint8_t)CHAR_SAMURAI_R);
  lcd.write((uint8_t)CHAR_KATANA_R);
  lcd.print(" VICTORY! ");
  lcd.write((uint8_t)CHAR_KATANA_L);
  lcd.write((uint8_t)CHAR_SAMURAI_L);

  lcd.setCursor(0, 1);
  lcd.print("Defeated: ");
  lcd.print(score);

  // Start display timer
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
  draw_standoff_scene();
  // lcd.setRGB(60, 60, 120);  // Blue tension

  // New random delay
  uint16_t rand_delay = random(STANDOFF_DELAY_MIN, STANDOFF_DELAY_MAX + 1);
  noInterrupts();
  delay_countdown = rand_delay;
  reaction_countdown = 0;
  display_countdown = 0;
  interrupts();
}

void action_show_death(void) {
  // Update high score
  if (score > high_score) {
    high_score = score;
  }

  lcd.clear();
  // lcd.setRGB(255, 0, 0);  // Red

  lcd.setCursor(0, 0);
  lcd.print("  ");
  lcd.write((uint8_t)CHAR_SKULL);
  lcd.print(" YOU DIED ");
  lcd.write((uint8_t)CHAR_SKULL);

  lcd.setCursor(0, 1);
  lcd.print("Score: ");
  lcd.print(score);
  lcd.print(" Hi:");
  lcd.print(high_score);

  // Start death LED flash and display timer
  flash_active = true;
  led_flash_on = false;
  flash_sub_count = 0;
  TIMSK2 |= (1 << OCIE2A);  // Enable Timer2 for LED flash

  noInterrupts();
  display_countdown = DEATH_DISPLAY_TIME;
  // Stop other timers
  delay_countdown = 0;
  reaction_countdown = 0;
  interrupts();

  clear_event_flags();
}

void action_flash_leds(void) {
  // Toggle LEDs (called by Timer2 flash tick)
  if (flash_active) {
    led_flash_on = !led_flash_on;
    digitalWrite(LED1_PIN, led_flash_on ? HIGH : LOW);
    digitalWrite(LED2_PIN, led_flash_on ? HIGH : LOW);
  }
}

void action_show_game_over(void) {
  // Stop LED flash
  flash_active = false;
  leds_off();
  TIMSK2 &= ~(1 << OCIE2A);  // Disable Timer2 interrupt

  lcd.clear();
  // lcd.setRGB(80, 0, 0);  // Dark red

  lcd.setCursor(0, 0);
  lcd.print("  GAME  OVER  ");

  lcd.setCursor(0, 1);
  lcd.print("Hi:");
  lcd.print(high_score);
  lcd.print(" Btn=Retry");

  clear_event_flags();
}

void action_restart(void) {
  // Reset for new game — title screen entry will handle display
  score = 0;
  current_reaction_window = REACTION_START;
  flash_active = false;
  leds_off();
  clear_event_flags();
}

void action_none(void) {
  // No action required
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

void draw_standoff_scene(void) {
  lcd.clear();

  // Line 1: two samurai facing each other
  lcd.setCursor(0, 0);
  lcd.write((uint8_t)CHAR_SAMURAI_R);
  lcd.write((uint8_t)CHAR_KATANA_R);
  lcd.print("  ......  ");
  lcd.write((uint8_t)CHAR_KATANA_L);
  lcd.write((uint8_t)CHAR_SAMURAI_L);

  // Line 2: round info
  lcd.setCursor(0, 1);
  lcd.print("Round ");
  lcd.print(score + 1);
  lcd.print("  READY");
}

void clear_event_flags(void) {
  noInterrupts();
  atk_pressed = false;
  blk_pressed = false;
  timer_delay_expired = false;
  timer_reaction_expired = false;
  timer_display_expired = false;
  timer_flash_tick = false;
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
  cli();  // Disable interrupts during setup

  // ==== TIMER1 — 1 ms system tick (CTC mode) ====
  // 16 MHz / 64 prescaler = 250 kHz
  // 250 counts = 1 ms
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1 = 0;
  OCR1A = 249;                              // 250 counts (0-indexed)
  TCCR1B |= (1 << WGM12);                  // CTC mode
  TCCR1B |= (1 << CS11) | (1 << CS10);     // Prescaler 64
  TIMSK1 |= (1 << OCIE1A);                 // Enable compare interrupt

  // ==== TIMER2 — LED flash tick (~10 Hz, used during death) ====
  // 16 MHz / 1024 prescaler = 15625 Hz
  // OCR2A = 195 → interrupt every ~12.5 ms
  // We count DEATH_FLASH_RATE ticks for each toggle (~50 ms per toggle)
  TCCR2A = 0;
  TCCR2B = 0;
  TCNT2 = 0;
  OCR2A = 195;
  TCCR2A |= (1 << WGM21);                  // CTC mode
  TCCR2B |= (1 << CS22) | (1 << CS21) | (1 << CS20);  // Prescaler 1024
  // Timer2 interrupt is enabled/disabled by action functions
  // Start disabled
  TIMSK2 &= ~(1 << OCIE2A);

  sei();  // Re-enable interrupts
}

// ============================================================================
// INTERRUPT SERVICE ROUTINES
// ============================================================================

// Timer1 ISR — 1 ms system tick
ISR(TIMER1_COMPA_vect) {
  // Standoff random delay countdown
  if (delay_countdown > 0) {
    delay_countdown--;
    if (delay_countdown == 0) {
      timer_delay_expired = true;
    }
  }

  // Reaction window countdown
  if (reaction_countdown > 0) {
    reaction_countdown--;
    if (reaction_countdown == 0) {
      timer_reaction_expired = true;
    }
  }

  // Display message countdown
  if (display_countdown > 0) {
    display_countdown--;
    if (display_countdown == 0) {
      timer_display_expired = true;
    }
  }
}

// Timer2 ISR — LED flash (~12.5 ms base tick)
ISR(TIMER2_COMPA_vect) {
  flash_sub_count++;
  if (flash_sub_count >= DEATH_FLASH_RATE) {
    flash_sub_count = 0;
    timer_flash_tick = true;
  }
}

// External Interrupt — ATK button (pin 2, INT0)
void isr_atk(void) {
  static unsigned long last_time = 0;
  unsigned long now = millis();
  if (now - last_time > 50) {  // 50 ms debounce
    atk_pressed = true;
  }
  last_time = now;
}

// External Interrupt — BLK button (pin 3, INT1)
void isr_blk(void) {
  static unsigned long last_time = 0;
  unsigned long now = millis();
  if (now - last_time > 50) {  // 50 ms debounce
    blk_pressed = true;
  }
  last_time = now;
}
