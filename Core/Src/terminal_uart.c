#include "terminal_internal.h"

#include <memory.h>
#include <stdarg.h>
#include <stdio.h>

#define PRINTF_BUFFER_SIZE 64

static void clear_csi_params(struct terminal *terminal) {
  memset(terminal->csi_params, 0, CSI_MAX_PARAMS_COUNT * CSI_MAX_PARAM_LENGTH);
  terminal->csi_params_count = 0;
  terminal->csi_last_param_length = 0;
}

static uint16_t get_csi_param(struct terminal *terminal, size_t index) {
  return atoi((const char *)terminal->csi_params[index]);
}

static const receive_table_t default_receive_table;

static void clear_receive_table(struct terminal *terminal) {
  terminal->receive_table = &default_receive_table;
}

static void receive_unexpected(struct terminal *terminal,
                               character_t character) {
  clear_receive_table(terminal);
}

static const receive_table_t esc_receive_table;

static void receive_esc(struct terminal *terminal, character_t character) {
  terminal->receive_table = &esc_receive_table;
}

static void receive_cr(struct terminal *terminal, character_t character) {
  terminal_screen_carriage_return(terminal);
}

static void receive_lf(struct terminal *terminal, character_t character) {
  terminal_screen_line_feed(terminal, 1);
}

static void receive_bs(struct terminal *terminal, character_t character) {
  terminal_screen_move_cursor(terminal, terminal->cursor_row,
                              terminal->cursor_col - 1);
}

static void receive_nel(struct terminal *terminal, character_t character) {
  terminal_screen_carriage_return(terminal);
  terminal_screen_line_feed(terminal, 1);
  clear_receive_table(terminal);
}

static void receive_ind(struct terminal *terminal, character_t character) {
  terminal_screen_line_feed(terminal, 1);
  clear_receive_table(terminal);
}

static void receive_ri(struct terminal *terminal, character_t character) {
  terminal_screen_reverse_line_feed(terminal, 1);
  clear_receive_table(terminal);
}

static const receive_table_t csi_receive_table;

static void receive_csi(struct terminal *terminal, character_t character) {
  terminal->receive_table = &csi_receive_table;
  clear_csi_params(terminal);
}

static const receive_table_t hash_receive_table;

static void receive_hash(struct terminal *terminal, character_t character) {
  terminal->receive_table = &hash_receive_table;
}

static void receive_ris(struct terminal *terminal, character_t character) {
  terminal->callbacks->system_reset();
}

static void receive_csi_param(struct terminal *terminal,
                              character_t character) {
  if (!terminal->csi_last_param_length) {
    if (terminal->csi_params_count == CSI_MAX_PARAMS_COUNT)
      return;

    terminal->csi_params_count++;
  }

  // Keep zero for the end of the string for atoi
  if (terminal->csi_last_param_length == CSI_MAX_PARAM_LENGTH - 1)
    return;

  terminal->csi_last_param_length++;
  terminal->csi_params[terminal->csi_params_count - 1]
                      [terminal->csi_last_param_length - 1] = character;
}

static void receive_csi_param_delimiter(struct terminal *terminal,
                                        character_t character) {
  if (!terminal->csi_last_param_length) {
    if (terminal->csi_params_count == CSI_MAX_PARAMS_COUNT)
      return;

    terminal->csi_params_count++;
    return;
  }

  terminal->csi_last_param_length = 0;
}

static void receive_da(struct terminal *terminal, character_t character) {
  terminal_uart_transmit_string(terminal, "\33[?1;0c");
  clear_receive_table(terminal);
}

static void receive_hvp(struct terminal *terminal, character_t character) {
  int16_t row = get_csi_param(terminal, 0);
  int16_t col = get_csi_param(terminal, 1);

  terminal_screen_move_cursor(terminal, row - 1, col - 1);
  clear_receive_table(terminal);
}

static void receive_sm(struct terminal *terminal, character_t character) {
  int16_t mode = get_csi_param(terminal, 0);

  switch (mode) {
  case 20:
    terminal->new_line_mode = true;
    break;
  }
  clear_receive_table(terminal);
}

static void receive_rm(struct terminal *terminal, character_t character) {
  int16_t mode = get_csi_param(terminal, 0);

  switch (mode) {
  case 20:
    terminal->new_line_mode = false;
    break;
  }
  clear_receive_table(terminal);
}

static void receive_dsr(struct terminal *terminal, character_t character) {
  uint16_t code = get_csi_param(terminal, 0);

  switch (code) {
  case 5:
    terminal_uart_transmit_string(terminal, "\33[0n");
    break;

  case 6:
    terminal_uart_transmit_printf(terminal, "\33[%d;%dR",
                                  terminal->cursor_row + 1,
                                  terminal->cursor_col + 1);
    break;
  }

  clear_receive_table(terminal);
}

static void receive_dectst(struct terminal *terminal, character_t character) {
  clear_receive_table(terminal);
}

static void receive_cup(struct terminal *terminal, character_t character) {
  int16_t row = get_csi_param(terminal, 0);
  int16_t col = get_csi_param(terminal, 1);

  terminal_screen_move_cursor(terminal, row - 1, col - 1);
  clear_receive_table(terminal);
}

static color_t get_sgr_color(struct terminal *terminal, size_t *i) {
  uint16_t code = get_csi_param(terminal, (*i)++);

  if (code == 5) {
    return get_csi_param(terminal, (*i)++);
  } else if (code == 2) {
    (*i) += 3;
  }
  return DEFAULT_ACTIVE_COLOR;
}

static void handle_sgr(struct terminal *terminal, size_t *i) {
  uint16_t code = get_csi_param(terminal, (*i)++);

  switch (code) {
  case 0:
    terminal->font = FONT_NORMAL;
    terminal->italic = false;
    terminal->underlined = false;
    terminal->blink = NO_BLINK;
    terminal->negative = false;
    terminal->concealed = false;
    terminal->crossedout = false;
    terminal->active_color = DEFAULT_ACTIVE_COLOR;
    terminal->inactive_color = DEFAULT_INACTIVE_COLOR;
    break;

  case 1:
    terminal->font = FONT_BOLD;
    break;

  case 2:
    terminal->font = FONT_THIN;
    break;

  case 3:
    terminal->italic = true;
    break;

  case 4:
    terminal->underlined = true;
    break;

  case 5:
    terminal->blink = SLOW_BLINK;
    break;

  case 6:
    terminal->blink = RAPID_BLINK;
    break;

  case 7:
    terminal->negative = true;
    break;

  case 8:
    terminal->concealed = true;
    break;

  case 9:
    terminal->crossedout = true;
    break;

  case 10:
  case 21:
  case 22:
    terminal->font = FONT_NORMAL;
    break;

  case 23:
    terminal->italic = false;
    break;

  case 24:
    terminal->underlined = false;
    break;

  case 25:
    terminal->blink = NO_BLINK;
    break;

  case 27:
    terminal->negative = false;
    break;

  case 28:
    terminal->concealed = false;
    break;

  case 29:
    terminal->crossedout = false;
    break;

  case 38:
    terminal->active_color = get_sgr_color(terminal, i);
    break;

  case 39:
    terminal->active_color = DEFAULT_ACTIVE_COLOR;
    break;

  case 48:
    terminal->inactive_color = get_sgr_color(terminal, i);
    break;

  case 49:
    terminal->inactive_color = DEFAULT_INACTIVE_COLOR;
    break;
  }

  if (code >= 30 && code < 38)
    terminal->active_color = code - 30;

  if (code >= 40 && code < 48)
    terminal->inactive_color = code - 40;

  if (code >= 90 && code < 98)
    terminal->active_color = code - 90 + 8;

  if (code >= 100 && code < 108)
    terminal->inactive_color = code - 100 + 8;
}

static void receive_sgr(struct terminal *terminal, character_t character) {
  size_t i = 0;
  if (terminal->csi_params_count) {
    while (i < terminal->csi_params_count)
      handle_sgr(terminal, &i);
  } else
    handle_sgr(terminal, &i);

  clear_receive_table(terminal);
}

static void receive_cuu(struct terminal *terminal, character_t character) {
  int16_t rows = get_csi_param(terminal, 0);
  if (!rows)
    rows = 1;

  terminal_screen_move_cursor(terminal, terminal->cursor_row - rows,
                              terminal->cursor_col);
  clear_receive_table(terminal);
}

static void receive_cud(struct terminal *terminal, character_t character) {
  int16_t rows = get_csi_param(terminal, 0);
  if (!rows)
    rows = 1;

  terminal_screen_move_cursor(terminal, terminal->cursor_row + rows,
                              terminal->cursor_col);
  clear_receive_table(terminal);
}

static void receive_cuf(struct terminal *terminal, character_t character) {
  int16_t cols = get_csi_param(terminal, 0);
  if (!cols)
    cols = 1;

  terminal_screen_move_cursor(terminal, terminal->cursor_row,
                              terminal->cursor_col + cols);
  clear_receive_table(terminal);
}

static void receive_cub(struct terminal *terminal, character_t character) {
  int16_t cols = get_csi_param(terminal, 0);
  if (!cols)
    cols = 1;

  terminal_screen_move_cursor(terminal, terminal->cursor_row,
                              terminal->cursor_col - cols);
  clear_receive_table(terminal);
}

static void receive_cnl(struct terminal *terminal, character_t character) {
  int16_t rows = get_csi_param(terminal, 0);
  if (!rows)
    rows = 1;

  terminal_screen_carriage_return(terminal);
  terminal_screen_line_feed(terminal, rows);
  clear_receive_table(terminal);
}

static void receive_cpl(struct terminal *terminal, character_t character) {
  int16_t rows = get_csi_param(terminal, 0);
  if (!rows)
    rows = 1;

  terminal_screen_carriage_return(terminal);
  terminal_screen_reverse_line_feed(terminal, rows);
  clear_receive_table(terminal);
}

static void receive_cha(struct terminal *terminal, character_t character) {
  int16_t col = get_csi_param(terminal, 0);

  terminal_screen_move_cursor(terminal, terminal->cursor_row, col - 1);
  clear_receive_table(terminal);
}

static void receive_sd(struct terminal *terminal, character_t character) {
  int16_t rows = get_csi_param(terminal, 0);
  if (!rows)
    rows = 1;

  terminal_screen_scroll(terminal, SCROLL_DOWN, 0, ROWS, rows);
  clear_receive_table(terminal);
}

static void receive_su(struct terminal *terminal, character_t character) {
  int16_t rows = get_csi_param(terminal, 0);
  if (!rows)
    rows = 1;

  terminal_screen_scroll(terminal, SCROLL_UP, 0, ROWS, rows);
  clear_receive_table(terminal);
}

static void receive_ed(struct terminal *terminal, character_t character) {
  uint16_t code = get_csi_param(terminal, 0);

  switch (code) {
  case 0:
    if (terminal->cursor_col != COLS - 1)
      terminal_screen_clear_cols(terminal, terminal->cursor_row,
                                 terminal->cursor_col, COLS);

    if (terminal->cursor_row != ROWS - 1)
      terminal_screen_clear_rows(terminal, terminal->cursor_row + 1, ROWS);

    break;

  case 1:
    if (terminal->cursor_col)
      terminal_screen_clear_cols(terminal, terminal->cursor_row, 0,
                                 terminal->cursor_col + 1);

    if (terminal->cursor_row)
      terminal_screen_clear_rows(terminal, 0, terminal->cursor_row);

    break;

  case 2:
  case 3:
    terminal_screen_clear_rows(terminal, 0, ROWS);

    break;
  }

  clear_receive_table(terminal);
}

static void receive_el(struct terminal *terminal, character_t character) {
  uint16_t code = get_csi_param(terminal, 0);

  switch (code) {
  case 0:
    if (terminal->cursor_col != COLS - 1)
      terminal_screen_clear_cols(terminal, terminal->cursor_row,
                                 terminal->cursor_col, COLS);
    break;

  case 1:
    if (terminal->cursor_col)
      terminal_screen_clear_cols(terminal, terminal->cursor_row, 0,
                                 terminal->cursor_col + 1);

    break;

  case 2:
    terminal_screen_clear_cols(terminal, terminal->cursor_row, 0, COLS);

    break;
  }

  clear_receive_table(terminal);
}

static void receive_decaln(struct terminal *terminal, character_t character) {
  for (size_t row = 0; row < ROWS; ++row)
    for (size_t col = 0; col < COLS; ++col) {
      terminal_screen_move_cursor(terminal, row, col);
      terminal_screen_put_character(terminal, 'E');
    }

  terminal_screen_move_cursor(terminal, 0, 0);
  clear_receive_table(terminal);
}

static const receive_table_t csi_decmod_receive_table;

static void receive_csi_decmod(struct terminal *terminal,
                               character_t character) {
  terminal->receive_table = &csi_decmod_receive_table;
}

static void receive_decsm(struct terminal *terminal, character_t character) {
  int16_t mode = get_csi_param(terminal, 0);

  switch (mode) {
  case 25:
    terminal_screen_enable_cursor(terminal, true);
    break;
  }
  clear_receive_table(terminal);
}

static void receive_decrm(struct terminal *terminal, character_t character) {
  int16_t mode = get_csi_param(terminal, 0);

  switch (mode) {
  case 25:
    terminal_screen_enable_cursor(terminal, false);
    break;
  }
  clear_receive_table(terminal);
}

static void receive_printable(struct terminal *terminal,
                              character_t character) {
  terminal_screen_put_character(terminal, character);
}

static void receive_ignore(struct terminal *terminal, character_t character) {}

static void receive_character(struct terminal *terminal,
                              character_t character) {
  receive_t receive = (*terminal->receive_table)[character];

  if (!receive) {
    receive = (*terminal->receive_table)[DEFAULT_RECEIVE];
  }

  receive(terminal, character);
}

#define RECEIVE_HANDLER(c, h) [c] = h
#define DEFAULT_RECEIVE_HANDLER(h) [DEFAULT_RECEIVE] = h

#define DEFAULT_RECEIVE_TABLE                                               \
  RECEIVE_HANDLER('\x00', receive_ignore),                                     \
      RECEIVE_HANDLER('\x01', receive_ignore),                                 \
      RECEIVE_HANDLER('\x02', receive_ignore),                                 \
      RECEIVE_HANDLER('\x03', receive_ignore),                                 \
      RECEIVE_HANDLER('\x04', receive_ignore),                                 \
      RECEIVE_HANDLER('\x05', receive_ignore),                                 \
      RECEIVE_HANDLER('\x06', receive_ignore),                                 \
      RECEIVE_HANDLER('\x07', receive_ignore),                                 \
      RECEIVE_HANDLER('\x08', receive_bs),                                     \
      RECEIVE_HANDLER('\x09', receive_ignore),                                 \
      RECEIVE_HANDLER('\x0a', receive_lf),                                     \
      RECEIVE_HANDLER('\x0b', receive_lf),                                     \
      RECEIVE_HANDLER('\x0c', receive_lf),                                     \
      RECEIVE_HANDLER('\x0d', receive_cr),                                     \
      RECEIVE_HANDLER('\x0e', receive_ignore),                                 \
      RECEIVE_HANDLER('\x0f', receive_ignore),                                 \
      RECEIVE_HANDLER('\x10', receive_ignore),                                 \
      RECEIVE_HANDLER('\x11', receive_ignore),                                 \
      RECEIVE_HANDLER('\x12', receive_ignore),                                 \
      RECEIVE_HANDLER('\x13', receive_ignore),                                 \
      RECEIVE_HANDLER('\x14', receive_ignore),                                 \
      RECEIVE_HANDLER('\x15', receive_ignore),                                 \
      RECEIVE_HANDLER('\x16', receive_ignore),                                 \
      RECEIVE_HANDLER('\x17', receive_ignore),                                 \
      RECEIVE_HANDLER('\x18', receive_ignore),                                 \
      RECEIVE_HANDLER('\x19', receive_ignore),                                 \
      RECEIVE_HANDLER('\x1a', receive_ignore),                                 \
      RECEIVE_HANDLER('\x1b', receive_esc),                                    \
      RECEIVE_HANDLER('\x1c', receive_ignore),                                 \
      RECEIVE_HANDLER('\x1d', receive_ignore),                                 \
      RECEIVE_HANDLER('\x1e', receive_ignore),                                 \
      RECEIVE_HANDLER('\x1f', receive_ignore),                                 \
      RECEIVE_HANDLER('\x7f', receive_bs)

static const receive_table_t default_receive_table = {
    DEFAULT_RECEIVE_TABLE,
    DEFAULT_RECEIVE_HANDLER(receive_printable),
};

static const receive_table_t esc_receive_table = {
    DEFAULT_RECEIVE_TABLE,
    RECEIVE_HANDLER('[', receive_csi),
    RECEIVE_HANDLER('#', receive_hash),
    RECEIVE_HANDLER('c', receive_ris),
    RECEIVE_HANDLER('E', receive_nel),
    RECEIVE_HANDLER('D', receive_ind),
    RECEIVE_HANDLER('M', receive_ri),
    DEFAULT_RECEIVE_HANDLER(receive_unexpected),
};

#define CSI_RECEIVE_TABLE                                                      \
  RECEIVE_HANDLER('0', receive_csi_param),                                     \
      RECEIVE_HANDLER('1', receive_csi_param),                                 \
      RECEIVE_HANDLER('2', receive_csi_param),                                 \
      RECEIVE_HANDLER('3', receive_csi_param),                                 \
      RECEIVE_HANDLER('4', receive_csi_param),                                 \
      RECEIVE_HANDLER('5', receive_csi_param),                                 \
      RECEIVE_HANDLER('6', receive_csi_param),                                 \
      RECEIVE_HANDLER('7', receive_csi_param),                                 \
      RECEIVE_HANDLER('8', receive_csi_param),                                 \
      RECEIVE_HANDLER('9', receive_csi_param),                                 \
      RECEIVE_HANDLER(';', receive_csi_param_delimiter)

static const receive_table_t csi_receive_table = {
    DEFAULT_RECEIVE_TABLE,
    CSI_RECEIVE_TABLE,
    RECEIVE_HANDLER('?', receive_csi_decmod),
    RECEIVE_HANDLER('c', receive_da),
    RECEIVE_HANDLER('f', receive_hvp),
    RECEIVE_HANDLER('h', receive_sm),
    RECEIVE_HANDLER('l', receive_rm),
    RECEIVE_HANDLER('m', receive_sgr),
    RECEIVE_HANDLER('n', receive_dsr),
    RECEIVE_HANDLER('y', receive_dectst),
    RECEIVE_HANDLER('A', receive_cuu),
    RECEIVE_HANDLER('B', receive_cud),
    RECEIVE_HANDLER('C', receive_cuf),
    RECEIVE_HANDLER('D', receive_cub),
    RECEIVE_HANDLER('E', receive_cnl),
    RECEIVE_HANDLER('F', receive_cpl),
    RECEIVE_HANDLER('G', receive_cha),
    RECEIVE_HANDLER('H', receive_cup),
    RECEIVE_HANDLER('J', receive_ed),
    RECEIVE_HANDLER('K', receive_el),
    RECEIVE_HANDLER('S', receive_su),
    RECEIVE_HANDLER('T', receive_sd),
    DEFAULT_RECEIVE_HANDLER(receive_unexpected),
};

static const receive_table_t csi_decmod_receive_table = {
    DEFAULT_RECEIVE_TABLE,
    CSI_RECEIVE_TABLE,
    RECEIVE_HANDLER('h', receive_decsm),
    RECEIVE_HANDLER('l', receive_decrm),
    DEFAULT_RECEIVE_HANDLER(receive_unexpected),
};

static const receive_table_t hash_receive_table = {
    DEFAULT_RECEIVE_TABLE,
    RECEIVE_HANDLER('8', receive_decaln),
    DEFAULT_RECEIVE_HANDLER(receive_unexpected),
};

void terminal_uart_receive_string(struct terminal *terminal,
                                  const char *string) {
  while (*string) {
    receive_character(terminal, *string);
    string++;
  }
}

void terminal_uart_receive(struct terminal *terminal, uint32_t count) {
  if (terminal->uart_receive_count == count)
    return;

  uint32_t i = terminal->uart_receive_count;

  while (i != count) {
    character_t character = terminal->receive_buffer[RECEIVE_BUFFER_SIZE - i];
    receive_character(terminal, character);
    i--;

    if (i == 0)
      i = RECEIVE_BUFFER_SIZE;
  }

  terminal->uart_receive_count = count;
}

void terminal_uart_transmit_character(struct terminal *terminal,
                                      character_t character) {
  memcpy(terminal->transmit_buffer, &character, 1);
  terminal->callbacks->uart_transmit(terminal->transmit_buffer, 1);
}

void terminal_uart_transmit_string(struct terminal *terminal,
                                   const char *string) {
  size_t len = strlen(string);
  memcpy(terminal->transmit_buffer, string, len);
  terminal->callbacks->uart_transmit(terminal->transmit_buffer, len);
}

void terminal_uart_transmit_printf(struct terminal *terminal,
                                   const char *format, ...) {
  va_list args;
  va_start(args, format);

  char buffer[PRINTF_BUFFER_SIZE];
  snprintf(buffer, PRINTF_BUFFER_SIZE, format, args);
  terminal_uart_transmit_string(terminal, buffer);

  va_end(args);
}

void terminal_uart_init(struct terminal *terminal) {
  terminal->receive_table = &default_receive_table;

  memset(terminal->csi_params, 0, CSI_MAX_PARAMS_COUNT * CSI_MAX_PARAM_LENGTH);
  terminal->csi_params_count = 0;
  terminal->csi_last_param_length = 0;

  terminal->uart_receive_count = RECEIVE_BUFFER_SIZE;
  terminal->callbacks->uart_receive(terminal->receive_buffer,
                                    sizeof(terminal->receive_buffer));
}
