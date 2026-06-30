/*
 * Metrônomo Digital
 * ------------------
 * Potenciômetro controla o BPM (40–240), buzzer marca o tempo (beat
 * acentuado no primeiro tempo do compasso) e a OLED exibe o BPM em
 * fonte grande + o compasso atual + indicador visual de batida.
 * Botão alterna entre os compassos 2/4, 3/4 e 4/4.
 *
 * Pinout:
 *   BTN Compasso  -> D8  (INPUT_PULLUP)
 *   POT BPM       -> A0
 *   Buzzer        -> D13
 *   OLED SDA      -> A4 (I2C)
 *   OLED SCL      -> A5 (I2C)
 *
 * Bibliotecas:
 *   Adafruit SSD1306
 *   Adafruit GFX
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---------- Pinout ----------
#define PIN_BTN_COMPASSO 8
#define PIN_POT_BPM      A0
#define PIN_BUZZER       13

// ---------- OLED ----------
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define OLED_ADDR     0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ---------- BPM ----------
const int BPM_MIN = 40;
const int BPM_MAX = 240;
float potSmooth = 0;
int bpmAtual = 120;

// ---------- Compasso ----------
const uint8_t compassos[] = {2, 3, 4};      // tempos por compasso
const uint8_t NUM_COMPASSOS = sizeof(compassos) / sizeof(compassos[0]);
uint8_t indiceCompasso = 2;                 // começa em 4/4
uint8_t beatAtual = 1;                      // 1..beatsPorCompasso (próximo a soar)
uint8_t ultimoBeatTocado = 0;               // beat que soou por último (para o flash)

// ---------- Timing do metrônomo (não-bloqueante) ----------
unsigned long proximoBeat = 0;

// ---------- Debounce do botão (edge detection) ----------
struct Botao {
  uint8_t pino;
  bool leituraAnterior;     // última leitura BRUTA (para detectar mudança e resetar o timer)
  bool estadoDebounced;     // último estado já CONFIRMADO (usado para detectar a borda)
  unsigned long ultimoDebounce;
};
Botao btnCompasso = {PIN_BTN_COMPASSO, HIGH, HIGH, 0};
const unsigned long DEBOUNCE_MS = 30;

// ---------- Flash visual do beat na OLED ----------
unsigned long flashAte = 0;
const unsigned long FLASH_DURACAO_MS = 80;

// ---------- Taxa de atualização do display ----------
unsigned long proximoRefreshDisplay = 0;
const unsigned long DISPLAY_RATE_MS = 50;

void setup() {
  pinMode(PIN_BTN_COMPASSO, INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    // Trava aqui se a OLED não inicializar — sinal de erro de fiação/endereço I2C
    for (;;) {}
  }
  display.clearDisplay();
  display.display();

  potSmooth = analogRead(PIN_POT_BPM);
  bpmAtual = lerBPM();
  proximoBeat = millis();
}

void loop() {
  unsigned long agora = millis();

  lerBotaoCompasso(agora);
  bpmAtual = lerBPM();

  unsigned long intervaloBeat = 60000UL / bpmAtual;

  if ((long)(agora - proximoBeat) >= 0) {
    dispararBeat();
    // soma o intervalo (não reseta para "agora") para não acumular atraso
    proximoBeat += intervaloBeat;
    // se ficou muito atrasado (ex: BPM mudou bruscamente), resincroniza
    if ((long)(agora - proximoBeat) > (long)intervaloBeat) {
      proximoBeat = agora + intervaloBeat;
    }
  }

  if ((long)(agora - proximoRefreshDisplay) >= 0) {
    atualizarDisplay();
    proximoRefreshDisplay = agora + DISPLAY_RATE_MS;
  }
}

// ---------------------------------------------------------------
// Leitura suavizada do potenciômetro -> BPM (40–240)
// ---------------------------------------------------------------
int lerBPM() {
  int bruto = analogRead(PIN_POT_BPM);
  potSmooth = potSmooth * 0.9 + bruto * 0.1;  // filtro exponencial simples
  int bpm = map((int)potSmooth, 0, 1023, BPM_MIN, BPM_MAX);
  return constrain(bpm, BPM_MIN, BPM_MAX);
}

// ---------------------------------------------------------------
// Botão de compasso com debounce por edge detection
// ---------------------------------------------------------------
void lerBotaoCompasso(unsigned long agora) {
  bool leitura = digitalRead(btnCompasso.pino);

  // leitura bruta mudou -> reinicia a janela de debounce
  if (leitura != btnCompasso.leituraAnterior) {
    btnCompasso.ultimoDebounce = agora;
  }

  // só aceita a leitura como "estável" depois de passar DEBOUNCE_MS sem oscilar
  if ((agora - btnCompasso.ultimoDebounce) > DEBOUNCE_MS) {
    if (leitura != btnCompasso.estadoDebounced) {
      btnCompasso.estadoDebounced = leitura;

      // borda de descida = botão pressionado (INPUT_PULLUP)
      if (btnCompasso.estadoDebounced == LOW) {
        indiceCompasso = (indiceCompasso + 1) % NUM_COMPASSOS;
        beatAtual = 1;                 // reseta a contagem do compasso novo
        proximoBeat = agora;           // próximo beat dispara já no novo compasso
        tone(PIN_BUZZER, 2000, 30);    // bip curto de confirmação
      }
    }
  }

  btnCompasso.leituraAnterior = leitura;
}

// ---------------------------------------------------------------
// Dispara o beat: som (acentuado no tempo 1) + flash visual
// ---------------------------------------------------------------
void dispararBeat() {
  uint8_t beatsPorCompasso = compassos[indiceCompasso];

  if (beatAtual == 1) {
    tone(PIN_BUZZER, 1568, 60);   // beat forte (tempo 1) — tom mais agudo/longo
  } else {
    tone(PIN_BUZZER, 880, 40);    // beat fraco — tom mais grave/curto
  }

  ultimoBeatTocado = beatAtual;
  flashAte = millis() + FLASH_DURACAO_MS;

  beatAtual++;
  if (beatAtual > beatsPorCompasso) {
    beatAtual = 1;
  }
}

// ---------------------------------------------------------------
// Layout OLED (128x64)
//   y=0..15   "METRONOMO"           compasso (ex: 4/4)
//   y=16..47  BPM em fonte grande (size 3), centralizado
//   y=50..52  rótulo "BPM"
//   y=55..63  bolinhas indicando o beat atual dentro do compasso
// ---------------------------------------------------------------
void atualizarDisplay() {
  uint8_t beatsPorCompasso = compassos[indiceCompasso];
  bool emFlash = (long)(millis() - flashAte) < 0;

  display.clearDisplay();

  // Cabeçalho: título + compasso
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(F("METRONOMO"));

  display.setCursor(100, 0);
  display.print(compassos[indiceCompasso]);
  display.print(F("/4"));

  // BPM grande, centralizado
  char bufBpm[4];
  snprintf(bufBpm, sizeof(bufBpm), "%d", bpmAtual);
  display.setTextSize(3);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(bufBpm, 0, 0, &x1, &y1, &w, &h);
  int16_t cursorX = (SCREEN_WIDTH - w) / 2;
  display.setCursor(cursorX, 18);
  display.print(bufBpm);

  // Rótulo BPM
  display.setTextSize(1);
  display.setCursor(SCREEN_WIDTH / 2 - 10, 50);
  display.print(F("BPM"));

  // Indicador de beat: uma bolinha por tempo do compasso
  uint8_t totalDots = beatsPorCompasso;
  uint8_t dotSpacing = 18;
  uint8_t startX = (SCREEN_WIDTH - (totalDots - 1) * dotSpacing) / 2;
  for (uint8_t i = 1; i <= totalDots; i++) {
    int16_t cx = startX + (i - 1) * dotSpacing;
    int16_t cy = 59;
    bool destacarAgora = emFlash && (i == ultimoBeatTocado);
    if (destacarAgora) {
      display.fillCircle(cx, cy, 3, SSD1306_WHITE);
    } else {
      display.drawCircle(cx, cy, 3, SSD1306_WHITE);
    }
  }

  display.display();
}
