# GOL

This repository contains a small browser-based implementation of Conway's Game of Life, built as a single HTML file: `GOL.browser`.

#Links to ESP32 board
https://de.aliexpress.com/item/1005007566332450.html#nav-specification
https://pan.jczn1688.com/directlink/1/HMI%20display/JC3248W535EN.zip

## What it does

The page renders a 32x24 cellular automata grid on an HTML `<canvas>`. Each cell can be alive or dead, and the simulation evolves over time using a variant of the Game of Life rules.

### Simulation behavior

- Cells are updated according to a HighLife-like rule set: births on 3 or 6 neighbors and survival on 2 or 3 neighbors
- Dead cells can be brought back to life
- Alive cells that no longer match survival rules become "dying" before disappearing
- The simulation can detect repeated patterns and inject random mutations to keep it evolving
- The grid occasionally reintroduces activity if the population becomes too low

### Controls

The page includes buttons for:

- Start: begin the simulation loop
- Stop: pause the simulation
- Step: advance one generation manually
- Reset: clear the grid
- Randomize: generate a new random starting pattern

### Interaction

- Click on any cell in the canvas to toggle it on or off
- The display includes generation count, living cell count, and history information

### File overview

- `GOL.browser` – the complete HTML app, including styles and JavaScript logic
- `README.md` – project notes and usage information

## How to use it

1. Open `GOL.browser` in a browser.
2. Use the controls to start or pause the simulation.
3. Click cells to create custom patterns.
4. Experiment with random states and watch the system evolve.

This project is a lightweight, self-contained demo of a dynamic life simulation without any external libraries or build setup.
