#include "screen.h"

#include "font.h"
#include <complex.h>
#include <memory.h>

void screen_clear(struct screen *screen, color_t inactive) {
  memset(screen->buffer, inactive, SCREEN_WIDTH * SCREEN_HEIGHT);
}

void screen_draw_character(struct screen *screen, size_t row, size_t col,
                           uint8_t character, enum font font, bool underlined,
                           color_t active, color_t inactive) {

  if (character >= 0x20) {
    character -= 0x1f;
  } else {
    character = 0;
  }

  size_t base = ((row * COLS * CHAR_HEIGHT) + col) * CHAR_WIDTH;
  const FontRow *font_data =
      (font == FONT_BOLD ? FontBoldData
                         : (font == FONT_ITALIC ? FontItalicData : FontData));

  for (size_t char_y = 0; char_y < CHAR_HEIGHT; char_y++) {
    for (size_t char_x = 0; char_x < CHAR_WIDTH; char_x++) {

      size_t i = base + COLS * CHAR_WIDTH * char_y + char_x;

      color_t color = inactive;

      if ((underlined && char_y == CHAR_HEIGHT - 1) ||
          (char_x < FONT_WIDTH &&
           font_data[character * FONT_HEIGHT + char_y] & (1 << char_x))) {

        color = active;
      }

      screen->buffer[i] = color;
    }
  }
}

void screen_draw_cursor(struct screen *screen, size_t row, size_t col,
                        color_t color) {
  size_t base = ((row * COLS * CHAR_HEIGHT) + col) * CHAR_WIDTH;

  for (size_t char_y = 0; char_y < CHAR_HEIGHT; char_y++) {
    for (size_t char_x = 0; char_x < CHAR_WIDTH; char_x++) {

      size_t i = base + COLS * CHAR_WIDTH * char_y + char_x;

      screen->buffer[i] ^= color;
    }
  }
}

static const struct {
  color_t active;
  enum font font;
  bool underlined;
} font_tests[] = {{9, FONT_NORMAL, false},  {10, FONT_BOLD, false},
                  {11, FONT_ITALIC, false}, {12, FONT_NORMAL, true},
                  {13, FONT_BOLD, true},    {14, FONT_ITALIC, true}};

void screen_test_fonts(struct screen *screen) {
  for (size_t i = 0; i < 6; i++) {
    for (size_t row = 0; row < 8; row++) {
      for (size_t col = 0; col < 16; col++) {
        uint8_t character = ((row * 16) + col);

        screen_draw_character(screen, row + (8 * (i / 3)), col + (16 * (i % 3)),
                              character, font_tests[i].font,
                              font_tests[i].underlined, font_tests[i].active,
                              0);
      }
    }
  }
}

#define MANDELBROT_ITERATIONS 200

static float mandelbrot(float complex z) {
  float complex v = 0;

  for (size_t n = 0; n < MANDELBROT_ITERATIONS; ++n) {
    v = v * v + z;
    if (cabsf(v) > 2.0) {
      return (float)n / (float)MANDELBROT_ITERATIONS;
    }
  }

  return 0.0;
}

#define MARGIN_X ((SCREEN_WIDTH - SCREEN_HEIGHT) / 2)

void screen_test_mandelbrot(struct screen *screen, float window_x,
                            float window_y, float window_r, bool (*cancel)()) {

  float x_min = window_x - window_r;
  float y_min = window_y - window_r;
  float x_max = window_x + window_r;
  float y_max = window_y + window_r;

  for (size_t screen_x = MARGIN_X; screen_x < SCREEN_WIDTH - MARGIN_X;
       ++screen_x) {

    float x =
        ((float)screen_x / (float)SCREEN_HEIGHT) * (x_max - x_min) + x_min;

    for (size_t screen_y = 0; screen_y < SCREEN_HEIGHT; ++screen_y) {

      float y =
          ((float)screen_y / (float)SCREEN_HEIGHT) * (y_max - y_min) + y_min;

      color_t c = (color_t)(mandelbrot(x + y * I) * 6.0 * 6.0 * 6.0) + 16;

      screen->buffer[screen_y * SCREEN_WIDTH + screen_x] = c;

      if (cancel && cancel()) {
        return;
      }
    }
  }
}

#define COLOR_TEST_BASE_COLORS 16
#define COLOR_TEST_CUBE_SIZE 6
#define COLOR_TEST_GRAYSCALE 24

#define COLOR_TEST_ROWS (2 + COLOR_TEST_CUBE_SIZE)
#define COLOR_TEST_ROW_HEIGHT (SCREEN_HEIGHT / COLOR_TEST_ROWS)
#define COLOR_TEST_ROW_PADDING 2

#define COLOR_TEST_BASE_COLORS_WIDTH (SCREEN_WIDTH / COLOR_TEST_BASE_COLORS)
#define COLOR_TEST_GRAYSCALE_WIDTH (SCREEN_WIDTH / COLOR_TEST_GRAYSCALE)
#define COLOR_TEST_GRAYSCALE_OFFSET 4
#define COLOR_TEST_CUBE_WIDTH                                                  \
  (SCREEN_WIDTH / (COLOR_TEST_CUBE_SIZE * COLOR_TEST_CUBE_SIZE))

#define COLOR_TEST_CUBE_OFFSET 10

void screen_test_colors(struct screen *screen) {
  size_t y = 0;

  for (size_t i = 0; i < COLOR_TEST_ROW_HEIGHT - COLOR_TEST_ROW_PADDING; ++i) {

    for (size_t j = 0; j < COLOR_TEST_BASE_COLORS; ++j) {
      size_t pos = SCREEN_WIDTH * (y + i) + COLOR_TEST_BASE_COLORS_WIDTH * j;

      memset(screen->buffer + pos, j, COLOR_TEST_BASE_COLORS_WIDTH);
    }
  }

  y += COLOR_TEST_ROW_HEIGHT;

  for (size_t k = 0; k < COLOR_TEST_CUBE_SIZE; ++k) {

    for (size_t i = 0; i < COLOR_TEST_ROW_HEIGHT - COLOR_TEST_ROW_PADDING;
         ++i) {

      for (size_t j = 0; j < COLOR_TEST_CUBE_SIZE * COLOR_TEST_CUBE_SIZE; ++j) {

        size_t pos = COLOR_TEST_CUBE_OFFSET + SCREEN_WIDTH * (y + i) +
                     COLOR_TEST_CUBE_WIDTH * j;

        memset(screen->buffer + pos,
               COLOR_TEST_BASE_COLORS +
                   (k * COLOR_TEST_CUBE_SIZE * COLOR_TEST_CUBE_SIZE) + j,
               COLOR_TEST_CUBE_WIDTH);
      }
    }

    y += COLOR_TEST_ROW_HEIGHT;
  }

  for (size_t i = 0; i < COLOR_TEST_ROW_HEIGHT - COLOR_TEST_ROW_PADDING; ++i) {

    for (size_t j = 0; j < COLOR_TEST_GRAYSCALE; ++j) {

      size_t pos = COLOR_TEST_GRAYSCALE_OFFSET + SCREEN_WIDTH * (y + i) +
                   COLOR_TEST_GRAYSCALE_WIDTH * j;

      memset(screen->buffer + pos,
             COLOR_TEST_BASE_COLORS +
                 (COLOR_TEST_CUBE_SIZE * COLOR_TEST_CUBE_SIZE *
                  COLOR_TEST_CUBE_SIZE) +
                 j,
             COLOR_TEST_GRAYSCALE_WIDTH);
    }
  }
}
