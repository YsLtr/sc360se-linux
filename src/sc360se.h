#ifndef SC360SE_H
#define SC360SE_H

#include <stddef.h>
#include <stdint.h>

#define SC360SE_VID_WIRED   0x248A
#define SC360SE_PID_WIRED   0x5D2E
#define SC360SE_VID_24G     0x249A
#define SC360SE_PID_24G     0x5C2F
#define SC360SE_IFACE       0x02

#define SC360SE_FRAME_LEN   32
#define SC360SE_NSTAGES     6
#define SC360SE_NBUTTONS    6

enum sc360se_link {
    SC360SE_LINK_USB = 0,
    SC360SE_LINK_24G = 1,
};

struct sc360se_device {
    int  fd;
    char path[256];
    enum sc360se_link link;
    uint16_t vid;
    uint16_t pid;
    char dev_id[5];
};

enum sc360se_polling_rate {
    SC360SE_HZ_1000 = 0x01,
    SC360SE_HZ_500  = 0x02,
    SC360SE_HZ_250  = 0x04,
    SC360SE_HZ_125  = 0x08,
};

/* Button slot indexes — verified by capture-per-button. */
enum sc360se_button_id {
    SC360SE_BTN_LEFT     = 0,
    SC360SE_BTN_RIGHT    = 1,
    SC360SE_BTN_WHEEL    = 2,
    SC360SE_BTN_BACK     = 3,   /* rear side button */
    SC360SE_BTN_FORWARD  = 4,   /* front side button */
    SC360SE_BTN_DPI      = 5,
};

/* Action types — type byte stored at frame[4 + slot*3]. */
enum sc360se_action_type {
    SC360SE_ACT_MOUSE    = 0x10,  /* param1 = button bitmask */
    SC360SE_ACT_DPI      = 0x40,  /* param1: 1=cycle, 2=up, 3=down */
    SC360SE_ACT_DISABLE  = 0x60,
    SC360SE_ACT_KEY      = 0x70,  /* param1=modifiers, param2=HID usage */
    SC360SE_ACT_CONSUMER = 0x80,  /* param1+param2 = LE u16 consumer usage */
};

enum sc360se_mouse_button {
    SC360SE_MB_LEFT     = 0x01,
    SC360SE_MB_RIGHT    = 0x02,
    SC360SE_MB_MIDDLE   = 0x04,
    SC360SE_MB_FORWARD  = 0x08,
    SC360SE_MB_BACK     = 0x10,
};

enum sc360se_dpi_action {
    SC360SE_DPI_CYCLE = 0x01,
    SC360SE_DPI_UP    = 0x02,
    SC360SE_DPI_DOWN  = 0x03,
};

enum sc360se_kbd_modifier {
    SC360SE_MOD_LCTRL  = 0x01,
    SC360SE_MOD_LSHIFT = 0x02,
    SC360SE_MOD_LALT   = 0x04,
    SC360SE_MOD_LGUI   = 0x08,
    SC360SE_MOD_RCTRL  = 0x10,
    SC360SE_MOD_RSHIFT = 0x20,
    SC360SE_MOD_RALT   = 0x40,
    SC360SE_MOD_RGUI   = 0x80,
};

struct sc360se_button_action {
    enum sc360se_action_type type;
    uint8_t  p1;
    uint8_t  p2;
};

struct sc360se_dpi_stage {
    uint16_t x_cpi;
    uint16_t y_cpi;
    uint8_t  r, g, b;
    uint8_t  flag;
};

struct sc360se_dpi_config {
    uint8_t active;
    uint8_t count;
    struct sc360se_dpi_stage stage[SC360SE_NSTAGES];
};

int  sc360se_open(struct sc360se_device *dev);
void sc360se_close(struct sc360se_device *dev);

int sc360se_send(struct sc360se_device *dev, uint8_t *frame);
int sc360se_recv(struct sc360se_device *dev, uint8_t *frame, int timeout_ms);
int sc360se_xfer(struct sc360se_device *dev, uint8_t *out, uint8_t *in);

uint8_t sc360se_checksum(const uint8_t *frame);

int sc360se_handshake(struct sc360se_device *dev);

/* Notification types for unsolicited IN reports */
#define SC360SE_EVT_BATTERY 0xc0
#define SC360SE_EVT_DPI     0xc2

struct sc360se_event {
    uint8_t  type;          /* SC360SE_EVT_BATTERY or SC360SE_EVT_DPI */
    uint8_t  battery_pct;   /* 0-100, valid when type == BATTERY */
    uint8_t  dpi_active;    /* valid when type == DPI */
    uint8_t  dpi_count;     /* valid when type == DPI */
    uint16_t dpi_cpi;       /* current DPI, valid when type == DPI */
};

/* Forward declaration for read_config */
struct sc360se_profile;

int sc360se_read_config(struct sc360se_device *dev, struct sc360se_profile *out);
int sc360se_read_battery(struct sc360se_device *dev);
int sc360se_try_recv(struct sc360se_device *dev, uint8_t *frame);
int sc360se_decode_event(const uint8_t *frame, struct sc360se_event *evt);
int sc360se_read_status(struct sc360se_device *dev,
                         int *battery_pct, uint8_t *dpi_active);

int sc360se_set_polling_rate(struct sc360se_device *dev,
                             enum sc360se_polling_rate hz);
int sc360se_set_dpi(struct sc360se_device *dev,
                    const struct sc360se_dpi_config *cfg);
int sc360se_set_dpi_colors(struct sc360se_device *dev,
                           const struct sc360se_dpi_config *cfg);
int sc360se_set_static_light(struct sc360se_device *dev);
int sc360se_set_sleep_seconds(struct sc360se_device *dev, uint16_t seconds);
int sc360se_set_buttons(struct sc360se_device *dev,
                        const struct sc360se_button_action acts[SC360SE_NBUTTONS]);
int sc360se_commit(struct sc360se_device *dev);
int sc360se_factory_reset(struct sc360se_device *dev);

/* ------------------------------------------------------------------ */
/* Profiles are a host-side concept — the device has one config slot. */
/* sc360se_apply_profile writes the full sequence of 6 frames the     */
/* Windows driver uses on every "switch profile" click.               */
/* ------------------------------------------------------------------ */

struct sc360se_profile {
    enum sc360se_polling_rate    polling;
    struct sc360se_dpi_config    dpi;
    struct sc360se_button_action buttons[SC360SE_NBUTTONS];
    uint16_t                     sleep_seconds;
};

int sc360se_apply_profile(struct sc360se_device *dev,
                          const struct sc360se_profile *p);
int sc360se_profile_load(const char *path, struct sc360se_profile *out);
int sc360se_profile_save(const char *path, const struct sc360se_profile *p);
void sc360se_profile_default(struct sc360se_profile *p);

#endif
