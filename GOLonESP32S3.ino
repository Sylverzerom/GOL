#include <JC3248W535EN.h>

JC3248W535EN lcd;

// -----------------------------------------------------------------------------
// Grid & simulation parameters
// -----------------------------------------------------------------------------
// The world is a 32x24 toroidal grid (wrap-around edges), which means cells that
// leave one edge reappear on the opposite edge. This keeps the system moving and
// avoids a border effect that would otherwise freeze at the edges.
const uint8_t COLS = 32;
const uint8_t ROWS = 24;

// Each cell is drawn as a square with side length 10 pixels.
// With a 320x480 display, 32x24 tiles at 10px each gives a 320x240 play area,
// leaving room for the status bar and buttons below the grid.
const uint8_t CELL = 10;

// Cell states:
//   DEAD = empty cell
//   NEW_CELL = a cell that just came alive
//   MATURE = a stable living cell
//   DYING = a recently alive cell that is about to die
// The extra DYING state makes the simulation feel more "organic" and prevents
// the population from collapsing instantly; it introduces a brief transition.
const uint8_t DEAD = 0;
const uint8_t NEW_CELL = 1;
const uint8_t MATURE = 2;
const uint8_t DYING = 3;

// History size is limited so we can detect repeated states without using too much
// memory. The system stores compact serialized bitmaps of previous worlds.
const uint8_t HISTORY_LIMIT = 12;

// The simulation keeps a short memory of recent grid states so it can detect loops.
// If the same pattern repeats, it injects a small mutation to keep the system alive.
// The serialization uses one bit per cell, so 32*24 = 768 cells fits in 96 bytes.
uint8_t grid[ROWS][COLS];
uint8_t nextGrid[ROWS][COLS];
uint8_t history[HISTORY_LIMIT][((COLS * ROWS + 7) / 8)];
uint8_t historyCount = 0;

// Run loop state.
bool running = false;
uint32_t generation = 0;
uint32_t lastStep = 0;

// A new generation is advanced every 200 ms.
// This is a deliberate pacing choice: it feels alive without running too fast to
// observe on a small screen.
const uint32_t STEP_MS = 200;

// -----------------------------------------------------------------------------
// Button layout for touch controls.
// -----------------------------------------------------------------------------
struct Button {
  int x;
  int y;
  int w;
  int h;
  const char* label;
  uint16_t color;
};

// Buttons sit along the lower part of the screen, below the main 32x24 simulation.
// Their positions are tuned for the 320x480 display used by the JC3248W535EN.
Button buttons[] = {
  { 10, 420, 54, 32, "Start", 0x07E0 },
  { 74, 420, 54, 32, "Stop", 0xF800 },
  { 138, 420, 54, 32, "Step", 0x001F },
  { 202, 420, 54, 32, "Reset", 0xFC00 },
  { 266, 420, 54, 32, "Rand", 0xFFE0 }
};

// -----------------------------------------------------------------------------
// Core helper functions.
// -----------------------------------------------------------------------------
bool isAlive(uint8_t value) {
  // A cell is alive if it is either a freshly born cell or a mature living cell.
  // DYING is not considered alive; it is only a temporary transition state.
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
  // density controls how many cells start alive.
  // 0.35 means roughly 35% of cells begin alive.
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
  // Count the number of living neighbors in the 8 surrounding cells.
  // Because the grid is toroidal, neighbors wrap around the edges.
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
  // Before the next evolution step, convert all DYING cells into DEAD.
  // This gives a short decay period before removal and makes the system visually
  // feel like living cells fade away rather than vanishing instantly.
  for (int y = 0; y < ROWS; y++) {
    for (int x = 0; x < COLS; x++) {
      if (g[y][x] == DYING) g[y][x] = DEAD;
    }
  }
}

void injectMutation(uint8_t g[ROWS][COLS], uint8_t count) {
  // Randomly mutate a handful of cells.
  // If a cell is alive, mark it as DYING; otherwise, create a new life cell.
  // This is a creative “pressure” mechanism to avoid stagnation.
  for (uint8_t i = 0; i < count; i++) {
    int x = random(COLS);
    int y = random(ROWS);
    g[y][x] = isAlive(g[y][x]) ? DYING : NEW_CELL;
  }
}

void serializeGrid(uint8_t g[ROWS][COLS], uint8_t out[]) {
  // Turn the grid into a compact bit pattern:
  // one bit per cell, only marking living cells.
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
  // Compare the serialized current state against recent states in history.
  // If the same state reappears, the simulation loop is detected.
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
  // Keep a ring buffer of recent states.
  // When the buffer fills, drop the oldest state.
  if (historyCount >= HISTORY_LIMIT) {
    memmove(history, history + 1, (HISTORY_LIMIT - 1) * ((COLS * ROWS + 7) / 8));
    historyCount--;
  }

  memcpy(history[historyCount], state, ((COLS * ROWS + 7) / 8));
  historyCount++;
}

void reviveIfNeeded(uint8_t g[ROWS][COLS]) {
  // This is the simulation's “survival pressure” system.
  // If the population drops too low, the world is re-seeded with activity.
  uint16_t alive = countAlive(g);
  if (alive == 0) {
    // Total extinction: start a fresh random field.
    randomGrid(g, 0.35f);
  } else if (alive < 20) {
    // Very sparse population: inject many mutations to reintroduce life.
    injectMutation(g, 20);
  } else if (alive < 60) {
    // Moderate population: smaller mutation bursts.
    injectMutation(g, 6);
  }
}

uint16_t cellColor(uint8_t v) {
  switch (v) {
    case NEW_CELL: return 0xB6DA;   // light green: newly born life
    case MATURE:   return 0x8F60;   // bright green: healthy living cells
    case DYING:    return 0x1C40;   // dark green: fading / temporary dying cells
    default:       return 0x0000;   // black: empty space
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
  // Clear state completely and redraw.
  makeGrid(grid, DEAD);
  generation = 0;
  historyCount = 0;
  memset(history, 0, sizeof(history));
  drawAll();
}

void randomizeGame() {
  // Quick random start pattern.
  randomGrid(grid, 0.35f);
  generation = 0;
  historyCount = 0;
  memset(history, 0, sizeof(history));
  drawAll();
}

void stepSimulation() {
  // Phase 1: remove all cells that are in DYING state, making their fade-out final.
  clearDyingCells(grid);

  // Advance generation count.
  generation++;

  // Phase 2: apply the update rules to produce the next generation.
  // HighLife-like rules: births on 3 or 6 neighbors, survival on 2 or 3 neighbors.
  // This exact rule set is intentionally different from plain Conway Life and is
  // key to the interesting “persistent” behavior of the simulation.
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

  // Check whether the next state matches one seen in recent history.
  uint8_t state[((COLS * ROWS + 7) / 8)];
  serializeGrid(nextGrid, state);

  if (stateSeen(state)) {
    // If the simulation loops back to a repeated state, inject a mutation to keep
    // it evolving instead of getting stuck in a repeating cycle.
    injectMutation(nextGrid, 12);
    serializeGrid(nextGrid, state);
  }

  // Re-vivification logic to keep the world alive when it becomes too sparse.
  reviveIfNeeded(nextGrid);

  // Every 50 generations, add a small extra mutation burst.
  // This prevents highly stable patterns from becoming permanently static.
  if (generation % 50 == 0) {
    injectMutation(nextGrid, 3);
  }

  // Commit the computed world and record it in the history buffer.
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

  // The touch library uses directly measured screen coordinates.
  // Buttons occupy the lower section of the display.
  if (ty >= 420) {
    if (insideButton(tx, ty, buttons[0])) { running = true; }
    else if (insideButton(tx, ty, buttons[1])) { running = false; }
    else if (insideButton(tx, ty, buttons[2])) { stepSimulation(); }
    else if (insideButton(tx, ty, buttons[3])) { running = false; resetGame(); }
    else if (insideButton(tx, ty, buttons[4])) { running = false; randomizeGame(); }
  } else {
    // Tap inside the world to toggle a single cell.
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

  // Start the population with a random but not too dense field.
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
