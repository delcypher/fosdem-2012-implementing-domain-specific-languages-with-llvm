#include <stdint.h>

// Prototype.  The real function will be inserted by the JIT.
int16_t cell(int16_t *oldgrid, int16_t *newgrid, int16_t width, int16_t height, int16_t x, int16_t y, int16_t v, int16_t *g);

void automaton(int16_t *oldgrid, int16_t *newgrid, int16_t width, int16_t
    height) {
  int16_t g[10] = {0};
  int16_t i=0;
  for (int16_t x=0 ; x<width ; x++) {
    for (int16_t y=0 ; y<height ; y++,i++) {
      newgrid[i] = cell(oldgrid, newgrid, width, height, x, y, oldgrid[i], g);
    }
  }
}
