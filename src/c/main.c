// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Darachnid

#include <pebble.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAGE_COUNT 4
#define PAGE_FIELD 0
#define PAGE_RATE 1
#define PAGE_NODES 2
#define PAGE_LOG 3
#define NODE_SLOTS 10
#define LOG_LINES 11
#define LOG_CHARS 48

static const char *PAGE_TITLES[] = {"Field", "Rate", "Nodes", "Monitor"};

typedef struct {
  char units[12];
  char last[16];
  char avg[12];
  char g24[8];
  char g5[8];
  char wifi[8];
  char bt[8];
  char rate[8];
  int window;
  bool scanning;
  bool have;
} Summary;

typedef struct {
  char id[16];
  char role[28];
  char last[24];
  int on;
  int wifi;
  int bt;
  int g24;
  int g5;
  int scan;
  bool used;
} NodeRow;

static Window *s_window;
static Layer *s_canvas;
static Summary s_sum;
static NodeRow s_nodes[NODE_SLOTS];
static char s_log[LOG_LINES][LOG_CHARS];
static char s_body[1400];
static int s_log_count;
static int s_page;
static int s_node;
static bool s_detail;

static GFont s_font14;
static GFont s_font14b;
static GFont s_font18b;
static GFont s_font28b;
static GFont s_font_rate;

static void scopy(char *dst, size_t n, const char *src) {
  size_t i = 0;
  if (!src) {
    src = "";
  }
  while (src[i] && i + 1 < n) {
    dst[i] = src[i];
    i++;
  }
  dst[i] = '\0';
}

static int split_char(char *s, char sep, char **out, int max) {
  int n = 0;
  if (!s || max < 1) {
    return 0;
  }
  out[n++] = s;
  for (char *p = s; *p && n < max; p++) {
    if (*p == sep) {
      *p = '\0';
      out[n++] = p + 1;
    }
  }
  return n;
}

static void draw_text(GContext *ctx, const char *text, GFont font, GColor color,
                      int x, int y, int w, int h, GTextAlignment align) {
  if (!text) {
    text = "";
  }
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, text, font, GRect(x, y, w, h),
                     GTextOverflowModeTrailingEllipsis, align, NULL);
}

static void draw_footer(GContext *ctx, GRect bounds) {
  int dots_w = PAGE_COUNT * 5 + (PAGE_COUNT - 1) * 4;
  int x = (bounds.size.w - dots_w) / 2;
  int y = bounds.size.h - 12;
  for (int i = 0; i < PAGE_COUNT; i++) {
    graphics_context_set_fill_color(ctx, i == s_page ? GColorYellow : GColorLightGray);
    graphics_fill_rect(ctx, GRect(x, y, 5, 5), 0, GCornerNone);
    x += 9;
  }
}

static GColor rate_color(void) {
  int n = atoi(s_sum.rate);
  if (n <= 0) {
    return GColorLightGray;
  }
  if (n < 12) {
    return GColorYellow;
  }
  return GColorGreen;
}

static void seed_nodes(void) {
  for (int i = 0; i < NODE_SLOTS; i++) {
    NodeRow *row = &s_nodes[i];
    snprintf(row->id, sizeof(row->id), "DPS-%02d", i + 1);
    if (i == 0) {
      scopy(row->role, sizeof(row->role), "2.4 GHz 1-7");
    } else if (i == 1) {
      scopy(row->role, sizeof(row->role), "2.4 GHz 8-13");
    } else if (i == 2) {
      scopy(row->role, sizeof(row->role), "5 GHz 36-165");
    } else {
      scopy(row->role, sizeof(row->role), "BLE");
    }
    scopy(row->last, sizeof(row->last), "-");
    row->scan = -1;
  }
}

static const char *node_num(const NodeRow *row) {
  if (strncmp(row->id, "DPS-", 4) == 0 && row->id[4]) {
    return row->id + 4;
  }
  return row->id;
}

static int online_count(void) {
  int n = 0;
  for (int i = 0; i < NODE_SLOTS; i++) {
    if (s_nodes[i].on) {
      n++;
    }
  }
  return n;
}

static void draw_pair(GContext *ctx, const char *label, const char *value,
                      int x, int y, int w) {
  draw_text(ctx, label, s_font14, GColorLightGray, x, y, w, 16, GTextAlignmentLeft);
  draw_text(ctx, (value && value[0]) ? value : "--", s_font18b, GColorWhite, x, y + 14, w, 22,
            GTextAlignmentLeft);
}

static void draw_waiting(GContext *ctx, GRect bounds) {
  draw_text(ctx, PAGE_TITLES[s_page], s_font14b, GColorYellow, 0, 48, bounds.size.w, 20,
            GTextAlignmentCenter);
  draw_text(ctx, "Waiting for DPS app", s_font14, GColorWhite, 8, 78, bounds.size.w - 16, 18,
            GTextAlignmentCenter);
  draw_text(ctx, "UP / DOWN  pages", s_font14, GColorLightGray, 0, 132, bounds.size.w, 18,
            GTextAlignmentCenter);
}

static void draw_field(GContext *ctx, GRect bounds) {
  draw_text(ctx, "DPS FIELD", s_font14b, GColorYellow, 4, 0, 120, 18, GTextAlignmentLeft);
  draw_text(ctx, s_sum.scanning ? "LIVE" : "IDLE", s_font14b,
            s_sum.scanning ? GColorGreen : GColorLightGray, bounds.size.w - 64, 0, 60, 18,
            GTextAlignmentRight);

  draw_text(ctx, "UNITS", s_font14, GColorLightGray, 4, 20, bounds.size.w - 8, 16,
            GTextAlignmentLeft);
  draw_text(ctx, s_sum.units[0] ? s_sum.units : "0", s_font28b, GColorWhite, 4, 34,
            bounds.size.w - 8, 32, GTextAlignmentLeft);

  char avg[16];
  snprintf(avg, sizeof(avg), "%s/m", s_sum.avg[0] ? s_sum.avg : "0");
  int half = bounds.size.w / 2;
  draw_pair(ctx, "LAST", s_sum.last, 4, 70, half - 8);
  draw_pair(ctx, "AVG", avg, half, 70, half - 8);

  graphics_context_set_stroke_color(ctx, GColorDarkGray);
  graphics_draw_line(ctx, GPoint(8, 118), GPoint(bounds.size.w - 8, 118));

  draw_pair(ctx, "2.4", s_sum.g24, 4, 124, half - 8);
  draw_pair(ctx, "5G", s_sum.g5, half, 124, half - 8);
  draw_pair(ctx, "WIFI", s_sum.wifi, 4, 164, half - 8);
  draw_pair(ctx, "BT", s_sum.bt, half, 164, half - 8);
}

static void draw_rate(GContext *ctx, GRect bounds) {
  GColor ink = rate_color();
  draw_text(ctx, "RATE", s_font14b, GColorYellow, 0, 8, bounds.size.w, 18, GTextAlignmentCenter);
  draw_text(ctx, s_sum.rate[0] ? s_sum.rate : "0", s_font_rate, ink, 0, 36, bounds.size.w, 58,
            GTextAlignmentCenter);
  draw_text(ctx, "/min", s_font18b, GColorLightGray, 0, 98, bounds.size.w, 22,
            GTextAlignmentCenter);

  int blocks = 10;
  int bw = 12;
  int gap = 4;
  int total = blocks * bw + (blocks - 1) * gap;
  int x = (bounds.size.w - total) / 2;
  int y = 132;
  int filled = s_sum.window;
  if (filled < 0) {
    filled = 0;
  }
  if (filled > blocks) {
    filled = blocks;
  }
  for (int i = 0; i < blocks; i++) {
    graphics_context_set_fill_color(ctx, i < filled ? ink : GColorDarkGray);
    graphics_fill_rect(ctx, GRect(x, y, bw, 18), 0, GCornerNone);
    x += bw + gap;
  }
  draw_text(ctx, "15s WINDOW", s_font14, GColorLightGray, 0, 158, bounds.size.w, 18,
            GTextAlignmentCenter);
}

static void draw_node_list(GContext *ctx, GRect bounds) {
  char head[16];
  int online = online_count();
  snprintf(head, sizeof(head), "%d ON", online);
  draw_text(ctx, "NODES", s_font14b, GColorYellow, 4, 0, 90, 18, GTextAlignmentLeft);
  draw_text(ctx, head, s_font14b, online > 0 ? GColorGreen : GColorLightGray, bounds.size.w - 70, 0,
            66, 18, GTextAlignmentRight);

  int row_h = 18;
  int visible = 8;
  if (visible > NODE_SLOTS) {
    visible = NODE_SLOTS;
  }
  int first = s_node - visible / 2;
  if (first < 0) {
    first = 0;
  }
  if (first > NODE_SLOTS - visible) {
    first = NODE_SLOTS - visible;
  }
  int top = 22;
  for (int i = 0; i < visible; i++) {
    int index = first + i;
    NodeRow *row = &s_nodes[index];
    int y = top + i * row_h;
    if (index == s_node) {
      graphics_context_set_fill_color(ctx, GColorYellow);
      graphics_fill_rect(ctx, GRect(2, y + 4, 4, 10), 0, GCornerNone);
    }
    draw_text(ctx, node_num(row), s_font14b, index == s_node ? GColorYellow : GColorWhite, 8, y, 28,
              18, GTextAlignmentLeft);
    draw_text(ctx, row->role[0] ? row->role : "—", s_font14, GColorWhite, 38, y,
              bounds.size.w - 78, 18, GTextAlignmentLeft);
    draw_text(ctx, row->on ? "ON" : "OFF", s_font14b, row->on ? GColorGreen : GColorLightGray,
              bounds.size.w - 36, y, 32, 18, GTextAlignmentRight);
  }
  draw_text(ctx, "HOLD for details", s_font14, GColorLightGray, 0, bounds.size.h - 30,
            bounds.size.w, 18, GTextAlignmentCenter);
}

static void draw_count(GContext *ctx, const char *label, int value, int x, int y, int w) {
  char buf[12];
  snprintf(buf, sizeof(buf), "%d", value);
  draw_text(ctx, label, s_font14, GColorLightGray, x, y, w, 16, GTextAlignmentLeft);
  draw_text(ctx, buf, s_font18b, GColorWhite, x, y + 14, w, 22, GTextAlignmentLeft);
}

static void draw_node_detail(GContext *ctx, GRect bounds) {
  if (s_node < 0) {
    s_node = 0;
  }
  if (s_node >= NODE_SLOTS) {
    s_node = NODE_SLOTS - 1;
  }
  NodeRow *row = &s_nodes[s_node];
  char which[16];
  snprintf(which, sizeof(which), "%d/10", s_node + 1);

  draw_text(ctx, row->id[0] ? row->id : "DPS", s_font14b, GColorYellow, 4, 0, 100, 18,
            GTextAlignmentLeft);
  draw_text(ctx, which, s_font14, GColorLightGray, 90, 0, 50, 18, GTextAlignmentCenter);
  draw_text(ctx, row->on ? "ON" : "OFF", s_font14b, row->on ? GColorGreen : GColorLightGray,
            bounds.size.w - 46, 0, 42, 18, GTextAlignmentRight);

  draw_text(ctx, "ROLE", s_font14b, GColorYellow, 4, 22, 80, 16, GTextAlignmentLeft);
  draw_text(ctx, row->role[0] ? row->role : "—", s_font18b, GColorWhite, 4, 38,
            bounds.size.w - 8, 22, GTextAlignmentLeft);

  int y = 62;
  if (row->scan >= 0) {
    draw_text(ctx, row->scan ? "SCANNING" : "STANDBY", s_font14b,
              row->scan ? GColorGreen : GColorLightGray, 4, y, bounds.size.w - 8, 16,
              GTextAlignmentLeft);
    y += 18;
  }

  draw_text(ctx, "SCANNED", s_font14b, GColorYellow, 4, y, 100, 16, GTextAlignmentLeft);
  y += 16;
  int half = bounds.size.w / 2;
  draw_count(ctx, "WIFI", row->wifi, 4, y, half - 8);
  draw_count(ctx, "BT", row->bt, half, y, half - 8);
  y += 36;
  draw_count(ctx, "2.4", row->g24, 4, y, half - 8);
  draw_count(ctx, "5G", row->g5, half, y, half - 8);
  y += 38;
  draw_text(ctx, "LAST", s_font14, GColorLightGray, 4, y, 50, 16, GTextAlignmentLeft);
  draw_text(ctx, row->last[0] ? row->last : "-", s_font14b, GColorWhite, 52, y,
            bounds.size.w - 56, 16, GTextAlignmentLeft);

  draw_text(ctx, "BACK", s_font14, GColorLightGray, 0, bounds.size.h - 30,
            bounds.size.w, 16, GTextAlignmentCenter);
}

static void draw_log(GContext *ctx, GRect bounds) {
  draw_text(ctx, "MONITOR", s_font14b, GColorYellow, 4, 0, bounds.size.w - 8, 18,
            GTextAlignmentLeft);
  if (s_log_count <= 0) {
    draw_text(ctx, "Waiting for traffic", s_font14, GColorLightGray, 8, 80, bounds.size.w - 16, 18,
              GTextAlignmentCenter);
    return;
  }
  int row_h = 16;
  int bottom = bounds.size.h - 24;
  int y = bottom - s_log_count * row_h;
  if (y < 20) {
    y = 20;
  }
  for (int i = 0; i < s_log_count; i++) {
    draw_text(ctx, s_log[i], s_font14, GColorWhite, 4, y, bounds.size.w - 8, 16,
              GTextAlignmentLeft);
    y += row_h;
  }
}

static void draw_canvas(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  if (!s_sum.have) {
    draw_waiting(ctx, bounds);
  } else if (s_page == PAGE_FIELD) {
    draw_field(ctx, bounds);
  } else if (s_page == PAGE_RATE) {
    draw_rate(ctx, bounds);
  } else if (s_page == PAGE_NODES) {
    if (s_detail) {
      draw_node_detail(ctx, bounds);
    } else {
      draw_node_list(ctx, bounds);
    }
  } else {
    draw_log(ctx, bounds);
  }
  draw_footer(ctx, bounds);
}

static void apply_sum(char *body) {
  char *fields[12];
  int n = split_char(body, '|', fields, 12);
  if (n < 10) {
    return;
  }
  scopy(s_sum.units, sizeof(s_sum.units), fields[0]);
  scopy(s_sum.last, sizeof(s_sum.last), fields[1]);
  scopy(s_sum.avg, sizeof(s_sum.avg), fields[2]);
  scopy(s_sum.g24, sizeof(s_sum.g24), fields[3]);
  scopy(s_sum.g5, sizeof(s_sum.g5), fields[4]);
  scopy(s_sum.wifi, sizeof(s_sum.wifi), fields[5]);
  scopy(s_sum.bt, sizeof(s_sum.bt), fields[6]);
  scopy(s_sum.rate, sizeof(s_sum.rate), fields[7]);
  s_sum.window = atoi(fields[8]);
  s_sum.scanning = fields[9][0] == '1';
  s_sum.have = true;
}

static void apply_node_line(int index, char *line) {
  char *fields[9];
  int n = split_char(line, '^', fields, 9);
  if (index < 0 || index >= NODE_SLOTS || n < 8) {
    return;
  }
  NodeRow *row = &s_nodes[index];
  scopy(row->id, sizeof(row->id), fields[0]);
  row->on = atoi(fields[1]);
  scopy(row->role, sizeof(row->role), fields[2]);
  row->wifi = atoi(fields[3]);
  row->bt = atoi(fields[4]);
  row->g24 = atoi(fields[5]);
  row->g5 = atoi(fields[6]);
  scopy(row->last, sizeof(row->last), fields[7][0] ? fields[7] : "-");
  row->scan = (n >= 9 && fields[8][0]) ? atoi(fields[8]) : -1;
  row->used = true;
}

static void apply_nodes(char *body) {
  if (!body[0]) {
    return;
  }
  char *lines[NODE_SLOTS];
  int n = split_char(body, '\n', lines, NODE_SLOTS);
  for (int i = 0; i < n; i++) {
    if (lines[i][0]) {
      apply_node_line(i, lines[i]);
    }
  }
}

static void apply_log(char *body) {
  s_log_count = 0;
  if (!body[0]) {
    return;
  }
  char *lines[LOG_LINES];
  int n = split_char(body, '\n', lines, LOG_LINES);
  for (int i = 0; i < n && s_log_count < LOG_LINES; i++) {
    if (!lines[i][0]) {
      continue;
    }
    scopy(s_log[s_log_count], LOG_CHARS, lines[i]);
    s_log_count++;
  }
}

static void inbox_received(DictionaryIterator *iter, void *context) {
  Tuple *kind = dict_find(iter, MESSAGE_KEY_KIND);
  Tuple *body = dict_find(iter, MESSAGE_KEY_BODY);
  if (!kind || kind->type != TUPLE_CSTRING || !body || body->type != TUPLE_CSTRING) {
    return;
  }
  scopy(s_body, sizeof(s_body), body->value->cstring);
  if (strcmp(kind->value->cstring, "sum") == 0) {
    apply_sum(s_body);
  } else if (strcmp(kind->value->cstring, "nodes") == 0) {
    apply_nodes(s_body);
  } else if (strcmp(kind->value->cstring, "log") == 0) {
    apply_log(s_body);
  }
  if (s_canvas) {
    layer_mark_dirty(s_canvas);
  }
}

static void inbox_dropped(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_WARNING, "inbox dropped %d", reason);
}

static bool on_nodes(void) {
  return s_sum.have && s_page == PAGE_NODES;
}

static void change_page(int dir) {
  s_page = (s_page + dir + PAGE_COUNT) % PAGE_COUNT;
}

static void move_node(int dir) {
  s_node = (s_node + dir + NODE_SLOTS) % NODE_SLOTS;
}

static void mark(void) {
  if (s_canvas) {
    layer_mark_dirty(s_canvas);
  }
}

static void up_click(ClickRecognizerRef recognizer, void *context) {
  if (s_detail) {
    move_node(-1);
  } else {
    change_page(-1);
  }
  mark();
}

static void down_click(ClickRecognizerRef recognizer, void *context) {
  if (s_detail) {
    move_node(1);
  } else {
    change_page(1);
  }
  mark();
}

static void select_long(ClickRecognizerRef recognizer, void *context) {
  if (on_nodes() && !s_detail) {
    s_detail = true;
    mark();
  }
}

static void back_click(ClickRecognizerRef recognizer, void *context) {
  if (s_detail) {
    s_detail = false;
    mark();
    return;
  }
  window_stack_pop_all(true);
}

static void click_config(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click);
  window_single_click_subscribe(BUTTON_ID_BACK, back_click);
  window_long_click_subscribe(BUTTON_ID_SELECT, 500, select_long, NULL);
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, draw_canvas);
  layer_add_child(root, s_canvas);
}

static void window_unload(Window *window) {
  layer_destroy(s_canvas);
  s_canvas = NULL;
}

static void init(void) {
  s_font14 = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  s_font14b = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  s_font18b = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  s_font28b = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  s_font_rate = fonts_get_system_font(FONT_KEY_ROBOTO_BOLD_SUBSET_49);
  scopy(s_sum.last, sizeof(s_sum.last), "--");
  scopy(s_sum.avg, sizeof(s_sum.avg), "0");
  scopy(s_sum.units, sizeof(s_sum.units), "0");
  scopy(s_sum.rate, sizeof(s_sum.rate), "0");
  seed_nodes();

  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_set_click_config_provider(s_window, click_config);
  window_stack_push(s_window, true);

  app_message_register_inbox_received(inbox_received);
  app_message_register_inbox_dropped(inbox_dropped);
  app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());
}

static void deinit(void) {
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
