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

    /* ── White border ─────────────────────────────────────────────────────── */
    lv_obj_t *bord = lv_obj_create(scr);
    lv_obj_remove_style_all(bord);
    lv_obj_set_pos(bord, inset, inset);
    lv_obj_set_size(bord, sw - 2*inset, sh - 2*inset);
    lv_obj_set_style_bg_opa(bord, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(bord, c_white, 0);
    lv_obj_set_style_border_width(bord, 2, 0);
    lv_obj_set_style_radius(bord, 0, 0);

    /* ── Red cross-hairs: four corners + centre ───────────────────────────── */
    draw_crosshair(scr, inset+clen,    inset+clen,    clen, c_red);  /* TL */
    draw_crosshair(scr, sw-inset-clen, inset+clen,    clen, c_red);  /* TR */
    draw_crosshair(scr, inset+clen,    sh-inset-clen, clen, c_red);  /* BL */
    draw_crosshair(scr, sw-inset-clen, sh-inset-clen, clen, c_red);  /* BR */
    draw_crosshair(scr, sw/2,          sh/2,          clen, c_red);  /* C  */

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

    /* ── Arc indicator (centre-right) ─────────────────────────────────────── */
    lv_obj_t *arc = lv_arc_create(scr);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, 75);
    lv_obj_set_size(arc, 130, 130);
    lv_obj_align(arc, LV_ALIGN_CENTER, 100, 20);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x00aaff), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, 14, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 14, LV_PART_MAIN);

    lv_obj_t *arc_val = lv_label_create(scr);
    lv_label_set_text(arc_val, "75%");
    lv_obj_set_style_text_color(arc_val, c_white, 0);
    lv_obj_set_style_text_font(arc_val, &lv_font_montserrat_28, 0);
    lv_obj_align_to(arc_val, arc, LV_ALIGN_CENTER, 0, 0);

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
    lv_label_set_text(stxt, "DISPLAY OK — alignment test pattern");
    lv_obj_set_style_text_color(stxt, lv_color_hex(0x001a00), 0);
    lv_obj_set_style_text_font(stxt, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_align(stxt, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(stxt, LV_ALIGN_CENTER, 0, 0);
}


