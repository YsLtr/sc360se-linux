/*
 * Profile management.
 *
 * The mouse itself has one active configuration slot. The Windows GUI
 * stores multiple "profiles" in the registry and rewrites the whole
 * configuration on each switch. We mirror that: profiles are local
 * files; "switching" means applying a profile by writing the full
 * 6-frame sequence (verified from 配置切换.pcapng):
 *
 *   1. 0x09 0x0f  buttons
 *   2. 0x03 0x25  DPI
 *   3. 0x04 0x12  DPI colors
 *   4. 0x02 0x01  polling rate
 *   5. 0x06 0x05  DPI light mode
 *   6. 0x07 0x04  sleep timeout
 */

#define _GNU_SOURCE
#include "sc360se.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

void sc360se_profile_default(struct sc360se_profile *p)
{
    memset(p, 0, sizeof(*p));
    p->polling          = SC360SE_HZ_1000;
    p->light_mode       = SC360SE_LIGHT_XML_DEFAULT;
    p->sleep_seconds    = 90;
    p->dpi.active       = 0;
    p->dpi.count        = 5;
    static const uint16_t cpis[6] = {800, 1600, 2400, 3200, 5200, 6000};
    for (int i = 0; i < SC360SE_NSTAGES; i++) {
        p->dpi.stage[i].x_cpi = cpis[i];
        p->dpi.stage[i].y_cpi = cpis[i];
        p->dpi.stage[i].r = 0xff;
        p->dpi.stage[i].g = 0xff;
        p->dpi.stage[i].b = 0xff;
        p->dpi.stage[i].flag = 0xff;
    }
    p->buttons[0] = (struct sc360se_button_action){SC360SE_ACT_MOUSE, SC360SE_MB_LEFT,    0};
    p->buttons[1] = (struct sc360se_button_action){SC360SE_ACT_MOUSE, SC360SE_MB_RIGHT,   0};
    p->buttons[2] = (struct sc360se_button_action){SC360SE_ACT_MOUSE, SC360SE_MB_MIDDLE,  0};
    p->buttons[3] = (struct sc360se_button_action){SC360SE_ACT_MOUSE, SC360SE_MB_FORWARD, 0};
    p->buttons[4] = (struct sc360se_button_action){SC360SE_ACT_MOUSE, SC360SE_MB_BACK,    0};
    p->buttons[5] = (struct sc360se_button_action){SC360SE_ACT_DPI,   SC360SE_DPI_CYCLE,  0};
}

int sc360se_apply_profile(struct sc360se_device *dev,
                          const struct sc360se_profile *p)
{
    int rc;
    struct timespec gap = {0, 200 * 1000 * 1000};   /* 200 ms */

    struct sc360se_button_action safe_buttons[SC360SE_NBUTTONS];
    memcpy(safe_buttons, p->buttons, sizeof(safe_buttons));
    /* The physical DPI key is not a normal remappable slot. Captures show
     * remapping it writes a hidden firmware state where local DPI switching
     * becomes inert; rewriting the visible mapping back to "DPI cycle" does
     * not repair it. Keep this slot at its factory local-switch function. */
    safe_buttons[SC360SE_BTN_DPI] =
        (struct sc360se_button_action){SC360SE_ACT_DPI, SC360SE_DPI_CYCLE, 0};
    if ((rc = sc360se_set_buttons(dev, safe_buttons)) < 0) return rc;
    nanosleep(&gap, NULL);
    if ((rc = sc360se_set_dpi(dev, &p->dpi)) < 0) return rc;
    nanosleep(&gap, NULL);
    if ((rc = sc360se_set_dpi_colors(dev, &p->dpi)) < 0) return rc;
    nanosleep(&gap, NULL);
    if ((rc = sc360se_set_polling_rate(dev, p->polling)) < 0) return rc;
    nanosleep(&gap, NULL);
    if ((rc = sc360se_set_light_mode(dev, p->light_mode)) < 0) return rc;
    nanosleep(&gap, NULL);
    if ((rc = sc360se_set_sleep_seconds(dev, p->sleep_seconds)) < 0) return rc;
    return 0;
}

/* ------------------------------------------------------------------ */
/* INI-style serialization. One key per line, very small parser.      */
/* ------------------------------------------------------------------ */

static const char *act_name(enum sc360se_action_type t)
{
    switch (t) {
        case SC360SE_ACT_MOUSE:    return "mouse";
        case SC360SE_ACT_DPI:      return "dpi";
        case SC360SE_ACT_DISABLE:  return "disable";
        case SC360SE_ACT_KEY:      return "key";
        case SC360SE_ACT_CONSUMER: return "consumer";
    }
    return "raw";
}

int sc360se_profile_save(const char *path, const struct sc360se_profile *p)
{
    FILE *f = fopen(path, "w");
    if (!f) return -errno;

    fprintf(f, "# AULA SC360SE profile\n");
    int hz = (p->polling == SC360SE_HZ_1000) ? 1000 :
             (p->polling == SC360SE_HZ_500)  ? 500  :
             (p->polling == SC360SE_HZ_250)  ? 250  : 125;
    fprintf(f, "polling = %d\n", hz);
    fprintf(f, "light.mode = %u\n", p->light_mode);
    fprintf(f, "sleep   = %u\n", p->sleep_seconds);
    fprintf(f, "dpi.active = %u\n", p->dpi.active);
    fprintf(f, "dpi.count  = %u\n", p->dpi.count);
    for (int i = 0; i < SC360SE_NSTAGES; i++) {
        const struct sc360se_dpi_stage *s = &p->dpi.stage[i];
        fprintf(f, "dpi.stage[%d] = %u/%u  #%02X%02X%02X  flag=%02x\n",
                i, s->x_cpi, s->y_cpi, s->r, s->g, s->b, s->flag);
    }
    static const char *names[SC360SE_NBUTTONS] = {
        "left", "right", "wheel", "back-side", "front-side", "dpi-key"};
    for (int i = 0; i < SC360SE_NBUTTONS; i++) {
        const struct sc360se_button_action *a = &p->buttons[i];
        fprintf(f, "button[%d %s] = %s %02x %02x\n",
                i, names[i], act_name(a->type), a->p1, a->p2);
    }
    fclose(f);
    return 0;
}

static char *trim(char *s)
{
    while (*s && isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && (isspace((unsigned char)e[-1]) || e[-1] == '\r')) *--e = 0;
    return s;
}

static int parse_color_hex(const char *s, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (*s == '#') s++;
    if (strlen(s) < 6) return -1;
    char rs[3]={s[0],s[1],0}, gs[3]={s[2],s[3],0}, bs[3]={s[4],s[5],0};
    *r = (uint8_t)strtoul(rs, NULL, 16);
    *g = (uint8_t)strtoul(gs, NULL, 16);
    *b = (uint8_t)strtoul(bs, NULL, 16);
    return 0;
}

static enum sc360se_action_type parse_act(const char *s)
{
    if (!strcmp(s, "mouse"))    return SC360SE_ACT_MOUSE;
    if (!strcmp(s, "dpi"))      return SC360SE_ACT_DPI;
    if (!strcmp(s, "disable"))  return SC360SE_ACT_DISABLE;
    if (!strcmp(s, "key"))      return SC360SE_ACT_KEY;
    if (!strcmp(s, "consumer")) return SC360SE_ACT_CONSUMER;
    return (enum sc360se_action_type)0;
}

int sc360se_profile_load(const char *path, struct sc360se_profile *out)
{
    sc360se_profile_default(out);
    FILE *f = fopen(path, "r");
    if (!f) return -errno;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *s = trim(line);
        if (!*s || *s == '#') continue;
        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = trim(s);
        char *val = trim(eq + 1);

        if (!strcmp(key, "polling")) {
            int hz = atoi(val);
            out->polling = (hz == 1000) ? SC360SE_HZ_1000 :
                           (hz == 500)  ? SC360SE_HZ_500  :
                           (hz == 250)  ? SC360SE_HZ_250  : SC360SE_HZ_125;
        } else if (!strcmp(key, "light.mode")) {
            unsigned mode = (unsigned)strtoul(val, NULL, 0);
            if (mode >= 1 && mode <= 6)
                out->light_mode = (uint8_t)mode;
        } else if (!strcmp(key, "sleep")) {
            out->sleep_seconds = (uint16_t)atoi(val);
        } else if (!strcmp(key, "dpi.active")) {
            out->dpi.active = (uint8_t)atoi(val);
        } else if (!strcmp(key, "dpi.count")) {
            out->dpi.count = (uint8_t)atoi(val);
        } else if (!strncmp(key, "dpi.stage[", 10)) {
            int i = atoi(key + 10);
            if (i < 0 || i >= SC360SE_NSTAGES) continue;
            unsigned x = 0, y = 0;
            char color[8] = "FFFFFF";
            unsigned flag = 0xff;
            sscanf(val, "%u/%u #%6[0-9a-fA-F] flag=%x", &x, &y, color, &flag);
            out->dpi.stage[i].x_cpi = (uint16_t)x;
            out->dpi.stage[i].y_cpi = (uint16_t)y;
            parse_color_hex(color, &out->dpi.stage[i].r,
                            &out->dpi.stage[i].g, &out->dpi.stage[i].b);
            out->dpi.stage[i].flag = (uint8_t)flag;
        } else if (!strncmp(key, "button[", 7)) {
            int i = atoi(key + 7);
            if (i < 0 || i >= SC360SE_NBUTTONS) continue;
            char act[16] = {0};
            unsigned p1 = 0, p2 = 0;
            sscanf(val, "%15s %x %x", act, &p1, &p2);
            out->buttons[i].type = parse_act(act);
            out->buttons[i].p1 = (uint8_t)p1;
            out->buttons[i].p2 = (uint8_t)p2;
        }
    }
    fclose(f);
    return 0;
}
