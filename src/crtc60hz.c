#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <x68k/iocs.h>

/*
 * X68000 CRTC 60 Hz V-DISP measurement.
 *
 * Starts from IOCS mode 12, changes only the vertical timing,
 * and measures 600 frames with IOCS _ONTIME.
 */

#define CRTC_R04       ((void *)0x00E80008UL)
#define CRTC_R05       ((void *)0x00E8000AUL)
#define CRTC_R06       ((void *)0x00E8000CUL)
#define CRTC_R07       ((void *)0x00E8000EUL)
#define MFP_GPIP       ((const void *)0x00E88001UL)

#define GPIP_VDISP     0x10
#define SCREEN_WIDTH   512
#define SCREEN_HEIGHT  512
#define COLOR_BLACK    0x0000
#define COLOR_WHITE    0xFFFF
#define ESC_SCANCODE   0x01
#define MEASURE_FRAMES 600

/* Update FPS every 0.2 seconds and the flow line every two frames. */
#define FPS_UPDATE_INTERVAL_CS   20
#define FLOW_UPDATE_EVERY_FRAMES 2
#define FLOW_SWEEP_STEPS         30

/* _ONTIME sec uses centiseconds and wraps once per day. */
#define CENTISEC_PER_DAY      8640000L
#define WAIT_VDISP_TIMEOUT_CS 100

static void *const crtc_regs[] = {
  CRTC_R04, CRTC_R05, CRTC_R06, CRTC_R07
};
static uint16_t saved_crtc[4];

static uint8_t read_gpip(void)
{
  return (uint8_t)_iocs_b_bpeek(MFP_GPIP);
}

static uint16_t read_crtc(const void *addr)
{
  return (uint16_t)_iocs_b_wpeek(addr);
}

static void write_crtc(void *addr, uint16_t value)
{
  _iocs_b_wpoke(addr, value);
}

static long ontime_diff_cs(struct iocs_time start, struct iocs_time end)
{
  return ((long)end.day - (long)start.day) * CENTISEC_PER_DAY
    + (long)end.sec - (long)start.sec;
}

/* Wait for the next V-DISP rising edge. */
static int wait_vdisp(void)
{
  struct iocs_time start;
  int level;

  start = _iocs_ontime();
  level = read_gpip() & GPIP_VDISP;

  for (;;) {
    int next;

    next = read_gpip() & GPIP_VDISP;

    if (ontime_diff_cs(start, _iocs_ontime()) > WAIT_VDISP_TIMEOUT_CS) {
      return -1;
    }

    if (next != level) {
      level = next;

      if (level != 0) {
        return 0;
      }
    }
  }
}

static void save_crtc_vertical(void)
{
  int i;

  for (i = 0; i < 4; ++i) {
    saved_crtc[i] = read_crtc(crtc_regs[i]);
  }
}

static void restore_crtc_now(void)
{
  int i;

  /* Restore the total period before the display positions. */
  for (i = 0; i < 4; ++i) {
    write_crtc(crtc_regs[i], saved_crtc[i]);
  }
}

/*
 * Set 525 total lines with 480 visible lines.
 * 31.5 kHz / 525 lines is approximately 60 Hz.
 */
static int set_60hz(void)
{
  if (wait_vdisp() != 0) {
    return -1;
  }

  /* Set display positions before reducing the total line count. */
  write_crtc(CRTC_R05, 0x0001);
  write_crtc(CRTC_R06, 0x0022);
  write_crtc(CRTC_R07, 0x0202);

  /* R04 is total lines minus one: 525 - 1 = 0x020c. */
  write_crtc(CRTC_R04, 0x020C);

  return 0;
}

static void restore_mode_and_crtc(int mode)
{
  if (mode < 0 || mode > 0x7F) {
    mode = 12;
  }

  _iocs_crtmod(mode);

  if (mode == 12) {
    restore_crtc_now();
  }

  _iocs_g_clr_on();
}

static void draw_flow_line(int y, uint16_t color)
{
  struct iocs_fillptr rect;

  rect.x1 = 0;
  rect.y1 = (short)y;
  rect.x2 = SCREEN_WIDTH - 1;
  rect.y2 = (short)y;
  rect.color = color;

  _iocs_fill(&rect);
}

static void put_line(int row, const char *text)
{
  char line[61];
  size_t len;

  memset(line, ' ', 60);
  line[60] = '\0';
  len = strlen(text);

  if (len > 60) {
    len = 60;
  }

  memcpy(line, text, len);
  _iocs_b_putmes(3, 2, row, 59, line);
}

static int escape_pressed(void)
{
  return _iocs_b_keysns() != 0
    && (((_iocs_b_keyinp() >> 8) & 0xFF) == ESC_SCANCODE);
}

/* Return a measured frequency multiplied by 100. */
static long rate_x100(long frames, long elapsed_cs)
{
  return elapsed_cs > 0
    ? (frames * 10000L + elapsed_cs / 2L) / elapsed_cs
    : 0;
}

static void show_result(long elapsed_cs)
{
  char buf[64];
  long hz_x100;

  hz_x100 = rate_x100(MEASURE_FRAMES, elapsed_cs);

  sprintf(
    buf,
    "600 V-DISP : %ld.%02ld sec",
    elapsed_cs / 100,
    elapsed_cs % 100);
  put_line(3, buf);

  sprintf(
    buf,
    "Measured   : %ld.%02ld Hz",
    hz_x100 / 100,
    hz_x100 % 100);
  put_line(4, buf);

  put_line(5, "Target     : 60.00 Hz");
  put_line(7, "ESC : exit");
}

int main(void)
{
  int old_mode;
  int frame;
  int aborted;
  int fps_count;
  int flow_prev_y;
  long fps_elapsed_cs;
  long last_fps_x100;
  long elapsed_cs;
  long hz_x100;
  struct iocs_time start_time;
  struct iocs_time end_time;
  struct iocs_time fps_time;
  struct iocs_time now_time;
  char buf[64];

  old_mode = _iocs_crtmod(-1);
  _iocs_b_curoff();
  _iocs_os_curof();

  /* Start from the standard 512 x 512, 65536-color, 31 kHz mode. */
  _iocs_crtmod(12);
  _iocs_g_clr_on();
  save_crtc_vertical();

  aborted = set_60hz() != 0;

  put_line(1, "X68000 60 Hz V-DISP measurement");
  put_line(3, "Measuring 600 frames...");
  put_line(4, "Progress   : 0 / 600");
  put_line(5, "FPS   : --");
  put_line(7, "ESC : abort");

  if (aborted) {
    put_line(3, "V-DISP timeout at 60Hz setup");
  }

  fps_count = 0;
  flow_prev_y = -1;
  last_fps_x100 = -1;
  elapsed_cs = 0;

  /* Start timing immediately after a V-DISP boundary. */
  if (!aborted) {
    if (wait_vdisp() != 0) {
      aborted = 1;
      put_line(3, "V-DISP timeout at start");
    } else {
      start_time = _iocs_ontime();
      fps_time = start_time;
    }
  }

  for (frame = 1; frame <= MEASURE_FRAMES && !aborted; ++frame) {
    if (wait_vdisp() != 0) {
      aborted = 1;
      put_line(7, "V-DISP timeout during measurement");
      break;
    }

    if (escape_pressed()) {
      aborted = 1;
      break;
    }

    /* Capture frame 600 before doing its drawing work. */
    if (frame == MEASURE_FRAMES) {
      end_time = _iocs_ontime();
    }

    if ((frame % FLOW_UPDATE_EVERY_FRAMES) == 0) {
      int flow_y;

      flow_y =
        ((frame / FLOW_UPDATE_EVERY_FRAMES) % FLOW_SWEEP_STEPS)
        * SCREEN_HEIGHT / FLOW_SWEEP_STEPS;

      if (flow_prev_y >= 0 && flow_prev_y != flow_y) {
        draw_flow_line(flow_prev_y, COLOR_BLACK);
      }

      draw_flow_line(flow_y, COLOR_WHITE);
      flow_prev_y = flow_y;
    }

    now_time = _iocs_ontime();
    ++fps_count;
    fps_elapsed_cs = ontime_diff_cs(fps_time, now_time);

    if (fps_elapsed_cs >= FPS_UPDATE_INTERVAL_CS) {
      long fps_x100;

      fps_x100 = rate_x100(fps_count, fps_elapsed_cs);

      if (fps_x100 != last_fps_x100) {
        sprintf(
          buf,
          "FPS : %ld.%02ld",
          fps_x100 / 100,
          fps_x100 % 100);
        put_line(5, buf);
        last_fps_x100 = fps_x100;
      }

      fps_time = now_time;
      fps_count = 0;
    }

    if ((frame % 60) == 0) {
      sprintf(buf, "Progress   : %d / 600", frame);
      put_line(4, buf);
    }
  }

  if (flow_prev_y >= 0) {
    draw_flow_line(flow_prev_y, COLOR_BLACK);
    (void)wait_vdisp();
  }

  if (!aborted) {
    elapsed_cs = ontime_diff_cs(start_time, end_time);
    show_result(elapsed_cs);

    while (wait_vdisp() == 0 && !escape_pressed()) {
    }
  }

  _iocs_g_clr_on();
  (void)wait_vdisp();
  restore_mode_and_crtc(old_mode);
  _iocs_os_curon();
  _iocs_b_curon();

  if (!aborted) {
    hz_x100 = rate_x100(MEASURE_FRAMES, elapsed_cs);

    printf(
      "600 V-DISP = %ld.%02ld sec\n",
      elapsed_cs / 100,
      elapsed_cs % 100);

    printf(
      "Measured refresh = %ld.%02ld Hz\n",
      hz_x100 / 100,
      hz_x100 % 100);
  } else {
    printf("Measurement aborted.\n");
  }

  return 0;
}