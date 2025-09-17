#include <LowPower.h>

// -------------------- Easy timing knobs (in seconds) --------------------
#define MINUTE 60UL
const unsigned long CYCLE_LEN_S      = 60 * MINUTE; // Measurement loop 60m
const unsigned long SIGNAL_TIMEOUT_S = 8 * MINUTE;  // if no D7 by 7m force OFF

const unsigned long IGNORE_SIG_S     = 5;           // ignore D7 for first 5 s
const unsigned long POST_OFF_WAIT_S  = 2;           // wait 5 s after OFF signal

// -------------------- Pins --------------------
const int RELAY_PIN  = 3;  // Relay (active HIGH)
const int SIGNAL_PIN = 7;  // External HIGH signal (use external 10k pulldown)

// -------------------- Helpers --------------------
inline unsigned long s2ms(unsigned long s) { return s * 1000UL; }

// Virtual time so sleep() doesn’t stall millis()
unsigned long timeBase = 0;
inline unsigned long nowVirt() { return millis() + timeBase; }

// Sleep for ms using watchdog chunks; advance timeBase accordingly
void sleep_ms(unsigned long ms) {
  while (ms >= 8000UL) { LowPower.powerDown(SLEEP_8S,   ADC_OFF, BOD_OFF); timeBase += 8000UL; ms -= 8000UL; }
  if    (ms >= 4000UL) { LowPower.powerDown(SLEEP_4S,   ADC_OFF, BOD_OFF); timeBase += 4000UL; ms -= 4000UL; }
  if    (ms >= 2000UL) { LowPower.powerDown(SLEEP_2S,   ADC_OFF, BOD_OFF); timeBase += 2000UL; ms -= 2000UL; }
  if    (ms >= 1000UL) { LowPower.powerDown(SLEEP_1S,   ADC_OFF, BOD_OFF); timeBase += 1000UL; ms -= 1000UL; }
  if    (ms >= 500UL)  { LowPower.powerDown(SLEEP_500MS,ADC_OFF, BOD_OFF); timeBase += 500UL;  ms -= 500UL;  }
  if    (ms >= 250UL)  { LowPower.powerDown(SLEEP_250MS,ADC_OFF, BOD_OFF); timeBase += 250UL;  ms -= 250UL;  }
  if    (ms >= 120UL)  { LowPower.powerDown(SLEEP_120MS,ADC_OFF, BOD_OFF); timeBase += 120UL;  ms -= 120UL;  }
  if    (ms >= 60UL)   { LowPower.powerDown(SLEEP_60MS, ADC_OFF, BOD_OFF); timeBase += 60UL;   ms -= 60UL;   }
  if    (ms >= 30UL)   { LowPower.powerDown(SLEEP_30MS, ADC_OFF, BOD_OFF); timeBase += 30UL;   ms -= 30UL;   }
  if    (ms >= 15UL)   { LowPower.powerDown(SLEEP_15MS, ADC_OFF, BOD_OFF); timeBase += 15UL;   ms -= 15UL;   }
}

// -------------------- State --------------------
unsigned long cycleStartMS = 0;
unsigned long cycleEndMS   = 0;
unsigned long ignoreEndMS  = 0;
unsigned long timeoutMS    = 0;

bool relayOn = false;

void relay_on()  { digitalWrite(RELAY_PIN, HIGH); relayOn = true;  }
void relay_off() { digitalWrite(RELAY_PIN, LOW);  relayOn = false; }

// Start a new cycle: ON immediately, set windows
void start_cycle() {
  cycleStartMS = nowVirt();
  cycleEndMS   = cycleStartMS + s2ms(CYCLE_LEN_S);
  ignoreEndMS  = cycleStartMS + s2ms(IGNORE_SIG_S);
  timeoutMS    = cycleStartMS + s2ms(SIGNAL_TIMEOUT_S);
  relay_on();
}

void setup() {
  Serial.begin(9600);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(SIGNAL_PIN, INPUT); // external pulldown to GND
  relay_off();
  start_cycle();
  Serial.println("Cycle START -> Relay ON");
}

void sleep_until(unsigned long targetMS) {
  unsigned long now = nowVirt();
  if (targetMS > now) sleep_ms(targetMS - now);
}

void next_cycle_after_sleep() {
  sleep_until(cycleEndMS);
  start_cycle();
  Serial.println("Next cycle -> Relay ON");
}

void loop() {
  unsigned long now = nowVirt();
  int sig = digitalRead(SIGNAL_PIN);

  if (relayOn) {
    // 1) Ignore D7 during initial window
    if (now >= ignoreEndMS) {
      // 2) After ignore window, if D7 HIGH -> OFF, wait, sleep to end-of-cycle
      if (sig == HIGH) {
        relay_off();
        Serial.println("Signal received -> Relay OFF");
        sleep_ms(s2ms(POST_OFF_WAIT_S));
        next_cycle_after_sleep();
        return;
      }
    }

    // 3) If no D7 by timeout -> OFF, wait, sleep to end-of-cycle
    if (now >= timeoutMS) {
      relay_off();
      Serial.println("Timeout -> Relay OFF");
      sleep_ms(s2ms(POST_OFF_WAIT_S));
      next_cycle_after_sleep();
      return;
    }

    // 4) NEW: If we reached the cycle end with relay still ON,
    //    do a 5s OFF pulse, then immediately begin the next cycle.
    if (now >= cycleEndMS) {
      Serial.println("Cycle end with relay still ON -> OFF pulse");
      relay_off();
      sleep_ms(s2ms(POST_OFF_WAIT_S));  // 5 s OFF
      start_cycle();                     // ON again and new windows
      Serial.println("Pulse complete -> New cycle ON");
      return;
    }

  } else {
    // Safety: if somehow OFF and cycle ended, start next cycle
    if (now >= cycleEndMS) {
      start_cycle();
      return;
    }
  }

  delay(5);
}
