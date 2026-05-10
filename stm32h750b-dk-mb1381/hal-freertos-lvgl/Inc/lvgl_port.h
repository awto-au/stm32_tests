#ifndef LVGL_PORT_H
#define LVGL_PORT_H

void lvgl_port_init(void);   /* lv_init + display registration, no task */
void lvgl_port_start(void);  /* create and start the LVGL scheduler task */

#endif
