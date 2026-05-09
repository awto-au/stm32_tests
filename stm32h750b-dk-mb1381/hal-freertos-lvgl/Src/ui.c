/**
 * ui.c — STM32H750B-DK display alignment / readability test pattern
 *
 * Layout (480x272):
 *  - Dark background
 *  - White border 2px inset from all edges
 *  - Red cross-hairs at all four corners + screen centre
 *  - Blue-on-yellow title block (top centre)
 *  - Arc indicator (centre-right)
 *  - Info text (centre-left)
 *  - Green status bar (bottom)
 */

#include "ui.h"
#include "lvgl.h"
#include "main.h"   /* LCD_WIDTH, LCD_HEIGHT */
#include "app_log.h"

static lv_obj_t *g_arc;
static lv_obj_t *g_arc_val;
static lv_obj_t *g_status_bar;
static lv_obj_t *g_status_text;
static lv_obj_t *g_video_box;
static lv_obj_t *g_video_text;

#define MEDIA_TILE_COUNT 8
static lv_obj_t *g_media_tiles[MEDIA_TILE_COUNT];

static uint32_t g_stress_frames;
static uint32_t g_stress_last_log_ms;
static uint32_t g_stress_cb_count;
static uint32_t g_video_frame;
static uint32_t g_image_updates;

void ui_stress_tick(void)
{
    static int32_t value = 0;
    static int32_t dir = 1;

    if ((g_status_bar == NULL) || (g_status_text == NULL)) {
        return;
    }

    value += (dir * 3);
    if (value >= 100) {
        value = 100;
        dir = -1;
    } else if (value <= 0) {
        value = 0;
        dir = 1;
    }

    if (g_arc != NULL) {
        lv_arc_set_value(g_arc, (int16_t)value);
    }
    if (g_arc_val != NULL) {
        lv_label_set_text_fmt(g_arc_val, "%ld%%", (long)value);
    }
    lv_label_set_text_fmt(g_status_text, "UI STRESS ACTIVE - load=%ld%%", (long)value);

    lv_color_t stress_color = lv_color_make(
        (uint8_t)((value * 255) / 100),
        (uint8_t)(((100 - value) * 255) / 100),
        0U
    );
    lv_obj_set_style_bg_color(g_status_bar, stress_color, 0);

    /* Media-style stress: churn image-like tiles and a pseudo-video frame panel. */
    if ((g_stress_cb_count % 2U) == 0U) {
        uint32_t tile_idx = (g_stress_cb_count / 2U) % MEDIA_TILE_COUNT;
        if (g_media_tiles[tile_idx] != NULL) {
            uint32_t tile_idx_u32 = tile_idx;
            uint8_t r = (uint8_t)((uint32_t)((value * 2) + (int32_t)(tile_idx_u32 * 20U)) & 0xFFU);
            uint8_t g = (uint8_t)((uint32_t)(((100 - value) * 2) + (int32_t)(tile_idx_u32 * 13U)) & 0xFFU);
            uint8_t b = (uint8_t)((g_stress_cb_count + tile_idx * 31U) & 0xFFU);
            lv_obj_set_style_bg_color(g_media_tiles[tile_idx], lv_color_make(r, g, b), 0);
            g_image_updates++;
        }
    }

    if ((g_stress_cb_count % 3U) == 0U) {
        g_video_frame++;
        if (g_video_box != NULL) {
            lv_color_t vf_col = lv_color_make(
                (uint8_t)((g_video_frame * 7U) & 0xFFU),
                (uint8_t)((g_video_frame * 11U) & 0xFFU),
                (uint8_t)((g_video_frame * 17U) & 0xFFU)
            );
            lv_obj_set_style_bg_color(g_video_box, vf_col, 0);
        }
        if (g_video_text != NULL) {
            lv_label_set_text_fmt(g_video_text, "VIDEO frame=%lu", (unsigned long)g_video_frame);
        }
    }

    g_stress_frames++;
    g_stress_cb_count++;
    uint32_t now_ms = lv_tick_get();
    if ((g_stress_cb_count == 1U) || ((g_stress_cb_count % 120U) == 0U) || ((now_ms - g_stress_last_log_ms) >= 1000U)) {
        uint32_t elapsed_ms = now_ms - g_stress_last_log_ms;
        uint32_t fps_x100 = (elapsed_ms > 0U) ? ((g_stress_frames * 100000U) / elapsed_ms) : 0U;
        APP_LOGI("UISTRESS", "cb=%lu frames=%lu elapsed_ms=%lu fps=%lu.%02lu arc=%ld video=%lu img_updates=%lu", 
                 (unsigned long)g_stress_cb_count,
                 (unsigned long)g_stress_frames,
                 (unsigned long)elapsed_ms,
                 (unsigned long)(fps_x100 / 100U),
                 (unsigned long)(fps_x100 % 100U),
                 (long)value,
                 (unsigned long)g_video_frame,
                 (unsigned long)g_image_updates);
        g_stress_frames = 0U;
        g_stress_last_log_ms = now_ms;
    }
}

/* ── helper: draw a cross-hair (horiz + vert lines) centred at (cx,cy) ─── */
static void draw_crosshair(lv_obj_t *parent,
                           int32_t cx, int32_t cy, int32_t len,
                           lv_color_t col)
{
    lv_obj_t *hbar = lv_obj_create(parent);
    lv_obj_remove_style_all(hbar);
    lv_obj_set_size(hbar, len * 2, 2);
    lv_obj_set_pos(hbar, cx - len, cy - 1);
    lv_obj_set_style_bg_color(hbar, col, 0);
    lv_obj_set_style_bg_opa(hbar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(hbar, 0, 0);
    lv_obj_set_style_border_width(hbar, 0, 0);

    lv_obj_t *vbar = lv_obj_create(parent);
    lv_obj_remove_style_all(vbar);
    lv_obj_set_size(vbar, 2, len * 2);
    lv_obj_set_pos(vbar, cx - 1, cy - len);
    lv_obj_set_style_bg_color(vbar, col, 0);
    lv_obj_set_style_bg_opa(vbar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(vbar, 0, 0);
    lv_obj_set_style_border_width(vbar, 0, 0);
}

void ui_init(void)
{
    APP_LOGI("UIINIT", "stage=enter");

    int32_t sw    = (int32_t)LCD_WIDTH;    /* 480 */
    int32_t sh    = (int32_t)LCD_HEIGHT;   /* 272 */
    int32_t inset = 2;
    int32_t clen  = 20;   /* cross-hair arm half-length */

    lv_color_t c_red    = lv_color_hex(0xff0000);
    lv_color_t c_white  = lv_color_white();
    lv_color_t c_yellow = lv_color_hex(0xffee00);
    lv_color_t c_blue   = lv_color_hex(0x0033cc);
    lv_color_t c_green  = lv_color_hex(0x00cc44);

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    APP_LOGI("UIINIT", "stage=screen-ready");

    /* ── White border ─────────────────────────────────────────────────────── */
    lv_obj_t *bord = lv_obj_create(scr);
    lv_obj_remove_style_all(bord);
    lv_obj_set_pos(bord, inset, inset);
    lv_obj_set_size(bord, sw - 2*inset, sh - 2*inset);
    lv_obj_set_style_bg_opa(bord, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(bord, c_white, 0);
    lv_obj_set_style_border_width(bord, 2, 0);
    lv_obj_set_style_radius(bord, 0, 0);
    APP_LOGI("UIINIT", "stage=border-ready");

    /* ── Red cross-hairs: four corners + centre ───────────────────────────── */
    draw_crosshair(scr, inset+clen,    inset+clen,    clen, c_red);  /* TL */
    draw_crosshair(scr, sw-inset-clen, inset+clen,    clen, c_red);  /* TR */
    draw_crosshair(scr, inset+clen,    sh-inset-clen, clen, c_red);  /* BL */
    draw_crosshair(scr, sw-inset-clen, sh-inset-clen, clen, c_red);  /* BR */
    draw_crosshair(scr, sw/2,          sh/2,          clen, c_red);  /* C  */
    APP_LOGI("UIINIT", "stage=crosshair-ready");

    /* ── Blue-on-yellow title block ───────────────────────────────────────── */
    lv_obj_t *tbg = lv_obj_create(scr);
    lv_obj_remove_style_all(tbg);
    lv_obj_set_size(tbg, 300, 56);
    lv_obj_align(tbg, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_bg_color(tbg, c_yellow, 0);
    lv_obj_set_style_bg_opa(tbg, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(tbg, 4, 0);
    lv_obj_set_style_border_width(tbg, 0, 0);

    lv_obj_t *ttl = lv_label_create(tbg);
    lv_label_set_text(ttl, "STM32H750B-DK  LVGL 9.x");
    lv_obj_set_style_text_color(ttl, c_blue, 0);
    lv_obj_set_style_text_font(ttl, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_align(ttl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(ttl, LV_ALIGN_TOP_MID, 0, 4);

    lv_obj_t *sub = lv_label_create(tbg);
    lv_label_set_text(sub, "QSPI XIP  |  FreeRTOS");
    lv_obj_set_style_text_color(sub, c_blue, 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(sub, LV_ALIGN_BOTTOM_MID, 0, -4);
    APP_LOGI("UIINIT", "stage=title-ready");

    /* ── Lightweight load indicator (replaces heavy arc path) ────────────── */
    lv_obj_t *arc = NULL;
    lv_obj_t *arc_frame = lv_obj_create(scr);
    lv_obj_remove_style_all(arc_frame);
    lv_obj_set_size(arc_frame, 132, 56);
    lv_obj_align(arc_frame, LV_ALIGN_CENTER, 100, 20);
    lv_obj_set_style_bg_color(arc_frame, lv_color_hex(0x202020), 0);
    lv_obj_set_style_bg_opa(arc_frame, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(arc_frame, lv_color_hex(0x00aaff), 0);
    lv_obj_set_style_border_width(arc_frame, 2, 0);

    lv_obj_t *arc_val = lv_label_create(arc_frame);
    lv_label_set_text(arc_val, "75%");
    lv_obj_set_style_text_color(arc_val, c_white, 0);
    lv_obj_set_style_text_font(arc_val, &lv_font_montserrat_22, 0);
    lv_obj_align(arc_val, LV_ALIGN_CENTER, 0, 0);
    APP_LOGI("UIINIT", "stage=load-indicator-ready");

    /* ── Info text (centre-left) ──────────────────────────────────────────── */
    lv_obj_t *info = lv_label_create(scr);
    lv_label_set_text(info,
        "Cortex-M7 @ 400 MHz\n"
        "128 KB flash  512 KB AXI\n"
        "64 MB QSPI NOR\n"
        "480 x 272  RGB565");
    lv_obj_set_style_text_color(info, lv_color_hex(0xdddddd), 0);
    lv_obj_set_style_text_font(info, &lv_font_montserrat_18, 0);
    lv_obj_align(info, LV_ALIGN_CENTER, -90, 30);
    APP_LOGI("UIINIT", "stage=main-widgets-ready");

    /* ── Media stress area: image churn + pseudo-video (video-only mode) ─── */
    lv_obj_t *media_bg = lv_obj_create(scr);
    lv_obj_remove_style_all(media_bg);
    lv_obj_set_size(media_bg, 220, 88);
    lv_obj_align(media_bg, LV_ALIGN_BOTTOM_LEFT, 8, -40);
    lv_obj_set_style_bg_color(media_bg, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_bg_opa(media_bg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(media_bg, lv_color_hex(0x4a4a4a), 0);
    lv_obj_set_style_border_width(media_bg, 1, 0);
    lv_obj_set_style_radius(media_bg, 2, 0);
    APP_LOGI("UIINIT", "stage=media-bg-ready");

    int32_t tile_w = 14;
    int32_t tile_h = 14;
    int32_t tile_gap = 4;
    for (uint32_t i = 0; i < MEDIA_TILE_COUNT; i++) {
        lv_obj_t *tile = lv_obj_create(media_bg);
        lv_obj_remove_style_all(tile);
        lv_obj_set_size(tile, tile_w, tile_h);
        lv_obj_set_pos(
            tile,
            6 + (int32_t)(i % 4U) * (tile_w + tile_gap),
            6 + (int32_t)(i / 4U) * (tile_h + tile_gap)
        );
        lv_obj_set_style_bg_color(tile, lv_color_hex(0x404040), 0);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(tile, 0, 0);
        lv_obj_set_style_border_width(tile, 0, 0);
        g_media_tiles[i] = tile;
    }
    APP_LOGI("UIINIT", "stage=media-tiles-ready");

    lv_obj_t *video_box = lv_obj_create(media_bg);
    lv_obj_remove_style_all(video_box);
    lv_obj_set_size(video_box, 142, 34);
    lv_obj_set_pos(video_box, 70, 6);
    lv_obj_set_style_bg_color(video_box, lv_color_hex(0x204060), 0);
    lv_obj_set_style_bg_opa(video_box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(video_box, 0, 0);

    lv_obj_t *video_text = lv_label_create(media_bg);
    lv_label_set_text(video_text, "VIDEO frame=0");
    lv_obj_set_style_text_color(video_text, lv_color_white(), 0);
    lv_obj_set_style_text_font(video_text, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(video_text, 74, 18);

    lv_obj_t *video_mode_text = lv_label_create(media_bg);
    lv_label_set_text(video_mode_text, "VIDEO-ONLY soak mode");
    lv_obj_set_style_text_color(video_mode_text, lv_color_hex(0x66d9ff), 0);
    lv_obj_set_style_text_font(video_mode_text, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(video_mode_text, 6, 50);
    APP_LOGI("UIINIT", "stage=video-panel-ready");

    /* ── Green status bar (bottom) ────────────────────────────────────────── */
    lv_obj_t *sbg = lv_obj_create(scr);
    lv_obj_remove_style_all(sbg);
    lv_obj_set_size(sbg, sw - 2*inset - 4, 30);
    lv_obj_align(sbg, LV_ALIGN_BOTTOM_MID, 0, -(inset + 2));
    lv_obj_set_style_bg_color(sbg, c_green, 0);
    lv_obj_set_style_bg_opa(sbg, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(sbg, 0, 0);
    lv_obj_set_style_border_width(sbg, 0, 0);

    lv_obj_t *stxt = lv_label_create(sbg);
    lv_label_set_text(stxt, "DISPLAY OK - alignment test pattern");
    lv_obj_set_style_text_color(stxt, lv_color_hex(0x001a00), 0);
    lv_obj_set_style_text_font(stxt, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_align(stxt, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(stxt, LV_ALIGN_CENTER, 0, 0);
    APP_LOGI("UIINIT", "stage=status-ready");

    g_arc = arc;
    g_arc_val = arc_val;
    g_status_bar = sbg;
    g_status_text = stxt;
    g_video_box = video_box;
    g_video_text = video_text;
    g_stress_frames = 0U;
    g_stress_last_log_ms = lv_tick_get();
    g_stress_cb_count = 0U;
    g_video_frame = 0U;
    g_image_updates = 0U;
    APP_LOGI("UISTRESS", "loop-driven stress enabled");
    APP_LOGI("UIMEDIA", "image stress enabled tiles=%u pseudo-video enabled mode=video-only", (unsigned)MEDIA_TILE_COUNT);
    APP_LOGI("UIINIT", "stage=done");
}


