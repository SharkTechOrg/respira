#include <LiquidCrystal.h>

// ---------- Pines ----------
const int PIN_CO   = A0;   // Potenciómetro CO
const int PIN_CH4  = A1;   // Potenciómetro CH4

// LED RGB (COM cátodo)
const int PIN_LED_R = 3;   // PWM
const int PIN_LED_G = 5;   // PWM
const int PIN_LED_B = 6;   // PWM

// Buzzer y botón ACK (opcional)
const int PIN_BUZZ    = 2;   // tone()
const int PIN_BTN_ACK = A2;  // INPUT_PULLUP → a GND al presionar

// LCD 4 bits (según cableado final)
LiquidCrystal lcd(7, 8, 9, 10, 11, 12);

// ---------- Estados (sin enum para evitar el error) ----------
const uint8_t ST_SAFE    = 0;
const uint8_t ST_WARN    = 1;
const uint8_t ST_DANGER  = 2;

// ---------- Parámetros ----------
const unsigned long WARMUP_MS       = 7000;
const unsigned long BASELINE_MS     = 8000;
const int           SMOOTH_N        = 10;
const unsigned long HOLD_UP_MS      = 10000;
const unsigned long HOLD_DOWN_MS    = 8000;
const unsigned long ACK_SILENCE_MS  = 120000;

// ---------- Estado ----------
uint8_t state = ST_SAFE;

unsigned long tStart, tStateSince = 0;
unsigned long ackSilencedUntil = 0;
bool calibratedOnce = false;

int   bufCO[SMOOTH_N], bufCH4[SMOOTH_N];
int   idx = 0, filled = 0;
float baseCO = 0, baseCH4 = 0;

// ---------- Utilidades ----------
int clampPWM(int v){ if (v<0) return 0; if (v>255) return 255; return v; }

void setRGB(int r, int g, int b){
  analogWrite(PIN_LED_R, clampPWM(r));
  analogWrite(PIN_LED_G, clampPWM(g));
  analogWrite(PIN_LED_B, clampPWM(b));
}

void beepPattern(uint8_t s){
  unsigned long now = millis();
  if (now < ackSilencedUntil) { noTone(PIN_BUZZ); return; }

  if (s == ST_SAFE) {
    noTone(PIN_BUZZ);
  } else if (s == ST_WARN) {
    static unsigned long last = 0;
    if (now - last > 5000) { tone(PIN_BUZZ, 3800, 400); last = now; }
  } else if (s == ST_DANGER) {
    static unsigned long last = 0; static bool on = false;
    if (now - last > 250) {
      on = !on;
      if (on) tone(PIN_BUZZ, 4000, 500); else noTone(PIN_BUZZ);
      last = now;
    }
  }
}

float movingAvg(int *buf, int len){
  long s=0; for(int i=0;i<len;i++) s+=buf[i];
  return (float)s / (float)len;
}

void printLCD(uint8_t s, int coPPM, int ch4LEL, bool basing){
  if (basing){
    lcd.clear();
    lcd.setCursor(0,0); lcd.print("CALIBRANDO BASE ");
    lcd.setCursor(0,1); lcd.print("Espere...");
    return;
  }

  lcd.setCursor(0,0);
  lcd.print("CO:");  lcd.print(coPPM);  lcd.print("ppm ");
  lcd.print("CH4:"); lcd.print(ch4LEL); lcd.print("%   ");

  lcd.setCursor(0,1);
  if      (s==ST_SAFE)   lcd.print("ESTADO:  SEGURO  ");
  else if (s==ST_WARN)   lcd.print("ESTADO:  ALERTA  ");
  else                   lcd.print("ESTADO: PELIGRO! ");
}

// ---------- Setup ----------
void setup(){
  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);
  pinMode(PIN_BUZZ, OUTPUT);
  pinMode(PIN_BTN_ACK, INPUT_PULLUP); // si no hay botón, queda en HIGH

  setRGB(0,0,0);
  lcd.begin(16,2);
  lcd.clear();

  lcd.print("   Respira+   ");
  lcd.setCursor(1,1); lcd.print("By SharkTech ");
  delay(2000);
  lcd.clear();

  for(int i=0;i<SMOOTH_N;i++){ bufCO[i]=0; bufCH4[i]=0; }

  // ¡Clave para la calibración/histéresis!
  tStart = millis();
  tStateSince = tStart;
  calibratedOnce = false;

  delay(300);
  lcd.clear();
}

// ---------- Loop ----------
void loop(){
  unsigned long now = millis();

  int co = analogRead(PIN_CO);
  int ch = analogRead(PIN_CH4);

  bufCO[idx]  = co;
  bufCH4[idx] = ch;
  idx = (idx+1)%SMOOTH_N;
  if (filled < SMOOTH_N) filled++;

  float coAvg = movingAvg(bufCO, filled);
  float chAvg = movingAvg(bufCH4, filled);

  // Calibración UNA SOLA VEZ
  if (!calibratedOnce){
    bool warming = (now - tStart < WARMUP_MS);
    bool basing  = (!warming) && (now - tStart < WARMUP_MS + BASELINE_MS);

    if (basing){
      static float accCO=0, accCH=0; static int n=0;
      accCO += coAvg; accCH += chAvg; n++;
      baseCO = accCO / n;
      baseCH4= accCH / n;

      setRGB(0,0,40);
      printLCD(state, 0, 0, true);
      beepPattern(ST_SAFE);
      delay(50);
      return;
    }

    if (now - tStart >= WARMUP_MS + BASELINE_MS){
      calibratedOnce = true;
    }
  }

  // Escala "demo" para mostrar en LCD
  int coPPM  = map((int)coAvg, 0, 1023, 0, 400);  // 0–400 ppm
  int ch4LEL = map((int)chAvg, 0, 1023, 0, 400);  // 0–100 %LEL (demo)

  // Lógica de estado
  uint8_t newState = ST_SAFE;
  if (coPPM > 70 || ch4LEL >= 10) {
    newState = ST_DANGER;
  } else if ((coPPM >= 35 && coPPM <= 70) || (ch4LEL >= 5 && ch4LEL < 10)) {
    newState = ST_WARN;
  } else {
    newState = ST_SAFE;
  }

  // Histéresis temporal
  if (newState > state) {
    if (now - tStateSince >= HOLD_UP_MS) { state = newState; tStateSince = now; }
  } else if (newState < state) {
    if (now - tStateSince >= HOLD_DOWN_MS){ state = newState; tStateSince = now; }
  }

  // LED RGB
  if (state == ST_SAFE) {
    setRGB(0, 180, 0);
  } else if (state == ST_WARN) {
    setRGB(255, 80, 0);
  } else {
    if (((now / 300) % 2) == 0) setRGB(255, 0, 0);
    else setRGB(30, 0, 0);
  }

  // Botón ACK (opcional)
  if (digitalRead(PIN_BTN_ACK) == LOW)
    ackSilencedUntil = now + ACK_SILENCE_MS;

  beepPattern(state);
  printLCD(state, coPPM, ch4LEL, false);
  delay(50);
}