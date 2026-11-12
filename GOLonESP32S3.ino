#include <JC3248W535EN.h>

JC3248W535EN lcd;

const uint8_t COLS = 32;
const uint8_t ROWS = 24;
const uint8_t CELL = 10; // 320x480 fits nicely

const uint8_t DEAD = 0;
const uint8_t NEW_CELL = 1;
const uint8_t MATURE = 2;
const uint8_t DYING = 3;

const uint8_t HISTORY_LIMIT = 12;

uint8_t grid[ROWS][COLS];
uint8_t nextGrid[ROWS][COLS];
uint8_t history[HISTORY_LIMIT][((COLS * ROWS + 7) / 8)];
uint8_t historyCount = 0;

bool running = false;
uint32_t generation = 0;
uint32_t lastStep = 0;
const uint32_t STEP_MS = 200;

struct Button {
  int x;
  int y;
  int w;
  int h;
  const char* label;
  uint16_t color;
};

Button buttons[] = {
  { 10, 420, 54, 32, "Start", 0x07E0 },
  { 74, 420, 54, 32, "Stop", 0xF800 },
  { 138, 420, 54, 32, "Step", 0x001F },
  { 202, 420, 54, 32, "Reset", 0xFC00 },
  { 266, 420, 54, 32, "Rand", 0xFFE0 }
};

bool isAlive(uint8_t value) {
  return value == NEW_CELL || value == MATURE;
}

void makeGrid(uint8_t target[ROWS][COLS], uint8_t fill) {
  for (int y = 0; y < ROWS; y++) {
    for (int x = 0; x < COLS; x++) {
      target[y][x] = fill;
    }
  }
}

void randomGrid(uint8_t target[ROWS][COLS], float density = 0.35f) {
  for (int y = 0; y < ROWS; y++) {
    for (int x = 0; x < COLS; x++) {
      target[y][x] = (random(100) / 100.0f) < density ? NEW_CELL : DEAD;
    }
  }
}

uint16_t countAlive(uint8_t g[ROWS][COLS]) {
  uint16_t total = 0;
  for (int y = 0; y < ROWS; y++) {
    for (int x = 0; x < COLS; x++) {
      if (isAlive(g[y][x])) total++;
    }
  }
  return total;
}

uint8_t neighbors(uint8_t g[ROWS][COLS], int x, int y) {
  uint8_t n = 0;
  for (int dy = -1; dy <= 1; dy++) {
    for (int dx = -1; dx <= 1; dx++) {
      if (dx == 0 && dy == 0) continue;
      int nx = (x + dx + COLS) % COLS;
      int ny = (y + dy + ROWS) % ROWS;
      if (isAlive(g[ny][nx])) n++;
    }
  }
  return n;
}

void clearDyingCells(uint8_t g[ROWS][COLS]) {
  for (int y = 0; y < ROWS; y++) {
    for (int x = 0; x < COLS; x++) {
      if (g[y][x] == DYING) g[y][x] = DEAD;
    }
  }
}

void injectMutation(uint8_t g[ROWS][COLS], uint8_t count) {
  for (uint8_t i = 0; i < count; i++) {
    int x = random(COLS);
    int y = random(ROWS);
    g[y][x] = isAlive(g[y][x]) ? DYING : NEW_CELL;
  }
}

void serializeGrid(uint8_t g[ROWS][COLS], uint8_t out[]) {
  memset(out, 0, ((COLS * ROWS + 7) / 8));
  for (int i = 0; i < COLS * ROWS; i++) {
    int x = i % COLS;
    int y = i / COLS;
    if (isAlive(g[y][x])) {
      out[i / 8] |= (1 << (i % 8));
    }
  }
}

bool stateSeen(const uint8_t state[]) {
  for (int i = 0; i < historyCount; i++) {
    bool same = true;
    for (int b = 0; b < ((COLS * ROWS + 7) / 8); b++) {
      if (history[i][b] != state[b]) {
        same = false;
        break;
      }
    }
    if (same) return true;
  }
  return false;
}

void addHistory(const uint8_t state[]) {
  if (historyCount >= HISTORY_LIMIT) {
    memmove(history, history + 1, (HISTORY_LIMIT - 1) * ((COLS * ROWS + 7) / 8));
    historyCount--;
  }

  memcpy(history[historyCount], state, ((COLS * ROWS + 7) / 8));
  historyCount++;
}

void reviveIfNeeded(uint8_t g[ROWS][COLS]) {
  uint16_t alive = countAlive(g);
  if (alive == 0) {
    randomGrid(g, 0.35f);
  } else if (alive < 20) {
    injectMutation(g, 20);
  } else if (alive < 60) {
    injectMutation(g, 6);
  }
}

uint16_t cellColor(uint8_t v) {
  switch (v) {
    case NEW_CELL: return 0xB6DA;   // light green
    case MATURE:   return 0x8F60;   // bright green
    case DYING:    return 0x1C40;   // dark green
    default:       return 0x0000;   // black
  }
}

void drawCell(int x, int y, uint8_t value) {
  lcd.fillRect(x * CELL, y * CELL, CELL, CELL, cellColor(value));
  lcd.drawRect(x * CELL, y * CELL, CELL, CELL, 0x2222);
}

void drawGrid() {
  for (int y = 0; y < ROWS; y++) {
    for (int x = 0; x < COLS; x++) {
      drawCell(x, y, grid[y][x]);
    }
  }
}

void drawStatus() {
  lcd.fillRect(0, 390, 320, 30, 0x1111);
  char text[64];
  snprintf(text, sizeof(text), "Gen %lu  Alive %u  Hist %u", generation, countAlive(grid), historyCount);
  lcd.setTextColor(0xFFFF);
  lcd.setTextSize(2);
  lcd.setCursor(8, 392);
  lcd.print(text);
}

void drawButtons() {
  for (int i = 0; i < 5; i++) {
    Button b = buttons[i];
    lcd.fillRoundRect(b.x, b.y, b.w, b.h, 6, b.color);
    lcd.drawRoundRect(b.x, b.y, b.w, b.h, 6, 0xFFFF);
    lcd.setTextColor(0x0000);
    lcd.setTextSize(2);
    lcd.setCursor(b.x + 8, b.y + 8);
    lcd.print(b.label);
  }
}

void drawAll() {
  drawGrid();
  drawStatus();
  drawButtons();
}

void resetGame() {
  makeGrid(grid, DEAD);
  generation = 0;
  historyCount = 0;
  memset(history, 0, sizeof(history));
  drawAll();
}

void randomizeGame() {
  randomGrid(grid, 0.35f);
  generation = 0;
  historyCount = 0;
  memset(history, 0, sizeof(history));
  drawAll();
}

void stepSimulation() {
  clearDyingCells(grid);

  generation++;

  for (int y = 0; y < ROWS; y++) {
    for (int x = 0; x < COLS; x++) {
      bool alive = isAlive(grid[y][x]);
      uint8_t n = neighbors(grid, x, y);

      if (!alive && (n == 3 || n == 6)) {
        nextGrid[y][x] = NEW_CELL;
      } else if (alive && (n == 2 || n == 3)) {
        nextGrid[y][x] = MATURE;
      } else if (alive) {
        nextGrid[y][x] = DYING;
      } else {
        nextGrid[y][x] = DEAD;
      }
    }
  }

  uint8_t state[((COLS * ROWS + 7) / 8)];
  serializeGrid(nextGrid, state);

  if (stateSeen(state)) {
    injectMutation(nextGrid, 12);
    serializeGrid(nextGrid, state);
  }

  reviveIfNeeded(nextGrid);

  if (generation % 50 == 0) {
    injectMutation(nextGrid, 3);
  }

  memcpy(grid, nextGrid, sizeof(grid));
  addHistory(state);
  drawAll();
}

bool insideButton(int x, int y, Button b) {
  return x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h;
}

void handleTouch() {
  if (!lcd.touchAvailable()) return;

  int16_t tx, ty;
  lcd.getTouch(&tx, &ty);

  // The touch library on this board uses screen coordinates directly.
  // Buttons are in the bottom part of the display.
  if (ty >= 420) {
    if (insideButton(tx, ty, buttons[0])) { running = true; }
    else if (insideButton(tx, ty, buttons[1])) { running = false; }
    else if (insideButton(tx, ty, buttons[2])) { stepSimulation(); }
    else if (insideButton(tx, ty, buttons[3])) { running = false; resetGame(); }
    else if (insideButton(tx, ty, buttons[4])) { running = false; randomizeGame(); }
  } else {
    // tap on the field toggles a cell
    int cellX = tx / CELL;
    int cellY = ty / CELL;

    if (cellX >= 0 && cellX < COLS && cellY >= 0 && cellY < ROWS) {
      grid[cellY][cellX] = isAlive(grid[cellY][cellX]) ? DYING : NEW_CELL;
      drawCell(cellX, cellY, grid[cellY][cellX]);
      drawStatus();
    }
  }
}

void setup() {
  Serial.begin(115200);

  randomSeed(analogRead(0));

  lcd.begin();
  lcd.setRotation(0);
  lcd.fillScreen(0x0000);

  randomGrid(grid, 0.35f);
  generation = 0;
  historyCount = 0;
  memset(history, 0, sizeof(history));

  drawAll();
}

void loop() {
  handleTouch();

  uint32_t now = millis();
  if (running && now - lastStep >= STEP_MS) {
    lastStep = now;
    stepSimulation();
  }
}
