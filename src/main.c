#include "sc360se.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *p)
{
    fprintf(stderr,
"AULA SC360SE — Linux user-space driver\n"
"\n"
"  %s info\n"
"  %s status                   quick: battery%% + active DPI stage\n"
"  %s read                     read current config from device\n"
"  %s battery                  read battery percentage\n"
"  %s polling   <125|250|500|1000>\n"
"  %s dpi       <active 0-5> <count 1-6> <X1[/Y1]> ...\n"
"               (cpi snaps to 100 below 5000, to 500 at/above 5000)\n"
"  %s color     <stage 0-5> <#RRGGBB>\n"
"  %s sleep     <seconds>\n"
"  %s static-light             restore static DPI LED mode\n"
"  %s commit                   alias for static-light\n"
"  %s factory-reset              restore firmware defaults (repairs inert DPI key)\n"
"\n"
"Profiles (host-side; switching = rewrite full config):\n"
"  %s apply        <profile.cfg>     write all 6 frames in one go\n"
"  %s save-default <profile.cfg>     emit a default-config template\n"
"\n"
"Button mapping:\n"
"  %s button    <slot> mouse    <left|right|middle|forward|back>\n"
"  %s button    <slot> dpi      <cycle|up|down>\n"
"  %s button    <slot> disable\n"
"  %s button    <slot> key      <hid-usage> [mod=ctrl,shift,alt,gui,...]\n"
"  %s button    <slot> consumer <usage-hex>\n"
"\n"
"  %s write-colors <s0_r/g/b/flag> ... <s5_...>   write all 6 color stages at once\n"
"  %s write-buttons <s0_type/p1/p2> ... <s5_...>  write all 6 button slots at once\n"
"\n"
"  slot: 0=L 1=R 2=wheel 3=back-side 4=front-side 5=DPI-button\n"
"\n"
"Low-level:\n"
"  %s send      <up to 28 hex bytes>\n"
"  %s recv      [timeout-ms]\n"
"  %s monitor\n"
"  %s watch     monitor decoded battery & DPI events\n",
    p,p,p,p,p,p,p,p,p,p,p,p,p,p,p,p,p,p,p,p,p,p,p,p);
}

static int hex_to_byte(const char *s, uint8_t *out)
{
    char *e = NULL;
    unsigned long v = strtoul(s, &e, 16);
    if (*e || v > 0xff) return -1;
    *out = (uint8_t)v;
    return 0;
}

static int parse_color(const char *s, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (s[0] == '#') s++;
    if (strlen(s) != 6) return -1;
    char rs[3]={s[0],s[1],0}, gs[3]={s[2],s[3],0}, bs[3]={s[4],s[5],0};
    *r = (uint8_t)strtoul(rs, NULL, 16);
    *g = (uint8_t)strtoul(gs, NULL, 16);
    *b = (uint8_t)strtoul(bs, NULL, 16);
    return 0;
}

static const struct { const char *name; uint8_t bit; } mods[] = {
    {"ctrl",  SC360SE_MOD_LCTRL},  {"lctrl",  SC360SE_MOD_LCTRL},
    {"shift", SC360SE_MOD_LSHIFT}, {"lshift", SC360SE_MOD_LSHIFT},
    {"alt",   SC360SE_MOD_LALT},   {"lalt",   SC360SE_MOD_LALT},
    {"gui",   SC360SE_MOD_LGUI},   {"win",    SC360SE_MOD_LGUI},
    {"rctrl", SC360SE_MOD_RCTRL},  {"rshift", SC360SE_MOD_RSHIFT},
    {"ralt",  SC360SE_MOD_RALT},   {"rgui",   SC360SE_MOD_RGUI},
};

static uint8_t parse_modifiers(const char *s)
{
    uint8_t m = 0;
    char buf[64];
    strncpy(buf, s, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    char *tok = strtok(buf, ",+|");
    while (tok) {
        for (size_t i = 0; i < sizeof(mods)/sizeof(mods[0]); i++)
            if (!strcasecmp(tok, mods[i].name)) { m |= mods[i].bit; break; }
        tok = strtok(NULL, ",+|");
    }
    return m;
}

static int default_buttons(struct sc360se_button_action a[SC360SE_NBUTTONS])
{
    a[0] = (struct sc360se_button_action){SC360SE_ACT_MOUSE, SC360SE_MB_LEFT, 0};
    a[1] = (struct sc360se_button_action){SC360SE_ACT_MOUSE, SC360SE_MB_RIGHT, 0};
    a[2] = (struct sc360se_button_action){SC360SE_ACT_MOUSE, SC360SE_MB_MIDDLE, 0};
    a[3] = (struct sc360se_button_action){SC360SE_ACT_MOUSE, SC360SE_MB_FORWARD, 0};
    a[4] = (struct sc360se_button_action){SC360SE_ACT_MOUSE, SC360SE_MB_BACK, 0};
    a[5] = (struct sc360se_button_action){SC360SE_ACT_DPI, SC360SE_DPI_CYCLE, 0};
    return 0;
}

static int do_button(struct sc360se_device *dev, int argc, char **argv)
{
    if (argc < 4) return -EINVAL;
    int slot = atoi(argv[2]);
    if (slot < 0 || slot >= SC360SE_NBUTTONS) return -EINVAL;
    if (slot == SC360SE_BTN_DPI) {
        fprintf(stderr,
                "Refusing to remap the dedicated DPI key. Captures show that "
                "doing so can put the firmware in a hidden remap state where "
                "local DPI switching stops until factory reset. Use slot 3/4 "
                "for remappable side buttons, or `sc360se factory-reset` to "
                "repair an already broken DPI key.\n");
        return -EINVAL;
    }

    struct sc360se_button_action acts[SC360SE_NBUTTONS];
    default_buttons(acts);

    const char *kind = argv[3];
    if (!strcmp(kind, "mouse") && argc == 5) {
        const char *m = argv[4];
        uint8_t bit = 0;
        if      (!strcmp(m, "left"))    bit = SC360SE_MB_LEFT;
        else if (!strcmp(m, "right"))   bit = SC360SE_MB_RIGHT;
        else if (!strcmp(m, "middle"))  bit = SC360SE_MB_MIDDLE;
        else if (!strcmp(m, "forward")) bit = SC360SE_MB_FORWARD;
        else if (!strcmp(m, "back"))    bit = SC360SE_MB_BACK;
        else return -EINVAL;
        acts[slot].type = SC360SE_ACT_MOUSE;
        acts[slot].p1 = bit;
        acts[slot].p2 = 0;
    } else if (!strcmp(kind, "dpi") && argc == 5) {
        const char *m = argv[4];
        acts[slot].type = SC360SE_ACT_DPI;
        if      (!strcmp(m, "cycle")) acts[slot].p1 = SC360SE_DPI_CYCLE;
        else if (!strcmp(m, "up"))    acts[slot].p1 = SC360SE_DPI_UP;
        else if (!strcmp(m, "down"))  acts[slot].p1 = SC360SE_DPI_DOWN;
        else return -EINVAL;
        acts[slot].p2 = 0;
    } else if (!strcmp(kind, "disable") && argc == 4) {
        acts[slot].type = SC360SE_ACT_DISABLE;
        acts[slot].p1 = acts[slot].p2 = 0;
    } else if (!strcmp(kind, "key") && argc >= 5) {
        uint8_t usage = (uint8_t)strtoul(argv[4], NULL, 0);
        uint8_t mod = 0;
        if (argc >= 6) {
            const char *p = argv[5];
            if (!strncmp(p, "mod=", 4)) p += 4;
            mod = parse_modifiers(p);
        }
        acts[slot].type = SC360SE_ACT_KEY;
        acts[slot].p1 = mod;
        acts[slot].p2 = usage;
    } else if (!strcmp(kind, "consumer") && argc == 5) {
        unsigned u = (unsigned)strtoul(argv[4], NULL, 0);
        acts[slot].type = SC360SE_ACT_CONSUMER;
        acts[slot].p1 = (uint8_t)(u & 0xff);
        acts[slot].p2 = (uint8_t)(u >> 8);
    } else {
        return -EINVAL;
    }
    return sc360se_set_buttons(dev, acts);
}

static int do_dpi(struct sc360se_device *dev, int argc, char **argv)
{
    if (argc < 4) return -EINVAL;
    struct sc360se_dpi_config c = {0};
    c.active = (uint8_t)atoi(argv[2]);
    c.count  = (uint8_t)atoi(argv[3]);
    int n = argc - 4;
    if (n > SC360SE_NSTAGES) n = SC360SE_NSTAGES;
    for (int i = 0; i < n; i++) {
        char *slash = strchr(argv[4 + i], '/');
        int x = atoi(argv[4 + i]);
        int y = slash ? atoi(slash + 1) : x;
        c.stage[i].x_cpi = (uint16_t)x;
        c.stage[i].y_cpi = (uint16_t)y;
        /* Color flags are not part of the 0x03 DPI frame; static LED
         * restoration is performed by sc360se_set_dpi() after the write. */
        c.stage[i].flag = 0xff;
    }
    return sc360se_set_dpi(dev, &c);
}

static int do_color(struct sc360se_device *dev, int argc, char **argv)
{
    if (argc != 4) return -EINVAL;
    int idx = atoi(argv[2]);
    if (idx < 0 || idx >= SC360SE_NSTAGES) return -EINVAL;
    struct sc360se_dpi_config c = {0};
    for (int i = 0; i < SC360SE_NSTAGES; i++) {
        c.stage[i].r = c.stage[i].g = c.stage[i].b = 0xff;
        c.stage[i].flag = 0xff;
    }
    if (parse_color(argv[3], &c.stage[idx].r, &c.stage[idx].g,
                    &c.stage[idx].b) < 0) return -EINVAL;
    return sc360se_set_dpi_colors(dev, &c);
}

static int do_send(struct sc360se_device *dev, int argc, char **argv)
{
    uint8_t f[SC360SE_FRAME_LEN] = {0};
    int n = argc < SC360SE_FRAME_LEN - 1 ? argc : SC360SE_FRAME_LEN - 1;
    for (int i = 0; i < n; i++)
        if (hex_to_byte(argv[i], &f[i]) < 0) return -EINVAL;
    return sc360se_send(dev, f);
}

static int do_recv(struct sc360se_device *dev, int timeout_ms)
{
    uint8_t f[SC360SE_FRAME_LEN];
    int r = sc360se_recv(dev, f, timeout_ms);
    if (r < 0) return r;
    for (int i = 0; i < SC360SE_FRAME_LEN; i++)
        printf("%02x%s", f[i], (i == SC360SE_FRAME_LEN - 1) ? "\n" : " ");
    return 0;
}

static int do_monitor(struct sc360se_device *dev)
{
    uint8_t f[SC360SE_FRAME_LEN];
    for (;;) {
        int r = sc360se_recv(dev, f, 60000);
        if (r == -ETIMEDOUT) continue;
        if (r < 0) return r;
        for (int i = 0; i < SC360SE_FRAME_LEN; i++)
            printf("%02x%s", f[i], (i == SC360SE_FRAME_LEN - 1) ? "\n" : " ");
        fflush(stdout);
    }
}

static int do_read(struct sc360se_device *dev)
{
    printf("hidraw    : %s\n", dev->path);
    printf("vid:pid   : %04x:%04x\n", dev->vid, dev->pid);
    printf("link      : %s\n",
           dev->link == SC360SE_LINK_USB ? "USB wired" : "2.4G dongle");

    struct sc360se_profile p;
    int rc = sc360se_read_config(dev, &p);
    if (rc < 0) return rc;

    printf("device    : %s\n", dev->dev_id);
    int hz = (p.polling == SC360SE_HZ_1000) ? 1000 :
             (p.polling == SC360SE_HZ_500)  ? 500  :
             (p.polling == SC360SE_HZ_250)  ? 250  : 125;
    printf("polling    : %d Hz\n", hz);
    printf("sleep      : %u s\n", p.sleep_seconds);
    printf("dpi.active : %u\n", p.dpi.active);
    printf("dpi.count  : %u\n", p.dpi.count);
    for (int i = 0; i < SC360SE_NSTAGES; i++) {
        const struct sc360se_dpi_stage *s = &p.dpi.stage[i];
        printf("dpi[%d]     : %u/%u  #%02X%02X%02X  flag=%02x\n",
               i, s->x_cpi, s->y_cpi, s->r, s->g, s->b, s->flag);
    }
    static const char *bnames[] = {
        "left", "right", "wheel", "back-side", "front-side", "dpi-key"};
    for (int i = 0; i < SC360SE_NBUTTONS; i++) {
        const struct sc360se_button_action *a = &p.buttons[i];
        const char *tn;
        switch (a->type) {
        case SC360SE_ACT_MOUSE:    tn = "mouse";    break;
        case SC360SE_ACT_DPI:      tn = "dpi";      break;
        case SC360SE_ACT_DISABLE:  tn = "disable";  break;
        case SC360SE_ACT_KEY:      tn = "key";      break;
        case SC360SE_ACT_CONSUMER: tn = "consumer"; break;
        default:                   tn = "?";        break;
        }
        printf("button[%d %s] : %s %02x %02x\n",
               i, bnames[i], tn, a->p1, a->p2);
    }
    return 0;
}

static int do_watch(struct sc360se_device *dev)
{
    uint8_t f[SC360SE_FRAME_LEN];
    for (int i = 0; i < 16; i++) {
        int r = sc360se_try_recv(dev, f);
        if (r == -ETIMEDOUT) break;
        if (r < 0) return r;
    }

    struct sc360se_event evt;
    for (;;) {
        int r = sc360se_recv(dev, f, 60000);
        if (r == -ETIMEDOUT) continue;
        if (r < 0) return r;
        if (sc360se_decode_event(f, &evt) < 0) continue;
        switch (evt.type) {
        case SC360SE_EVT_BATTERY:
            printf("link: %s\n", evt.link_online ? "connected" : "disconnected");
            printf("battery: %u%%\n", evt.battery_pct);
            break;
        case SC360SE_EVT_DPI:
            printf("dpi: stage %u/%u → %u cpi\n",
                   evt.dpi_active, evt.dpi_count, evt.dpi_cpi);
            break;
        }
        fflush(stdout);
    }
}

static int do_write_colors(struct sc360se_device *dev, int argc, char **argv)
{
    if (argc != 8) return -EINVAL;   /* cmd + 6 stages */

    struct sc360se_dpi_config c = {0};
    for (int i = 0; i < SC360SE_NSTAGES; i++) {
        unsigned r = 0, g = 0, b = 0, flag = 0;
        if (sscanf(argv[2 + i], "%2x/%2x/%2x/%2x", &r, &g, &b, &flag) != 4)
            return -EINVAL;
        c.stage[i].r    = (uint8_t)r;
        c.stage[i].g    = (uint8_t)g;
        c.stage[i].b    = (uint8_t)b;
        c.stage[i].flag = (uint8_t)flag;
    }
    return sc360se_set_dpi_colors(dev, &c);
}

static int do_write_buttons(struct sc360se_device *dev, int argc, char **argv)
{
    if (argc != 8) return -EINVAL;   /* cmd + 6 slots */

    struct sc360se_button_action acts[SC360SE_NBUTTONS];
    for (int i = 0; i < SC360SE_NBUTTONS; i++) {
        unsigned type = 0, p1 = 0, p2 = 0;
        if (sscanf(argv[2 + i], "%2x/%2x/%2x", &type, &p1, &p2) != 3)
            return -EINVAL;
        acts[i].type = (enum sc360se_action_type)type;
        acts[i].p1   = (uint8_t)p1;
        acts[i].p2   = (uint8_t)p2;
    }
    return sc360se_set_buttons(dev, acts);
}

int main(int argc, char **argv)
{
    if (argc < 2) { usage(argv[0]); return 1; }

    struct sc360se_device dev;
    int rc = sc360se_open(&dev);
    if (rc < 0) {
        fprintf(stderr, "open failed: %s\n", strerror(-rc));
        return 1;
    }

    int r = 0;
    const char *cmd = argv[1];

    if      (!strcmp(cmd, "info"))   r = do_read(&dev);
    else if (!strcmp(cmd, "read"))   r = do_read(&dev);
    else if (!strcmp(cmd, "battery")) {
        int bat = sc360se_read_battery(&dev);
        if (bat >= 0 && bat <= 100) {
            printf("%d%%\n", bat);
            r = 0;
        } else {
            r = bat;
        }
    }
    else if (!strcmp(cmd, "status")) {
        int bat = -1;
        uint8_t active = 0;
        r = sc360se_read_status(&dev, &bat, &active);
        if (r == 0)
            printf("battery=%d dpi_active=%u\n", bat, active);
    }
    else if (!strcmp(cmd, "polling") && argc == 3) {
        int hz = atoi(argv[2]);
        enum sc360se_polling_rate code;
        switch (hz) {
            case 1000: code = SC360SE_HZ_1000; break;
            case 500:  code = SC360SE_HZ_500;  break;
            case 250:  code = SC360SE_HZ_250;  break;
            case 125:  code = SC360SE_HZ_125;  break;
            default:   r = -EINVAL; goto out;
        }
        r = sc360se_set_polling_rate(&dev, code);
    }
    else if (!strcmp(cmd, "dpi"))     r = do_dpi(&dev, argc, argv);
    else if (!strcmp(cmd, "color"))   r = do_color(&dev, argc, argv);
    else if (!strcmp(cmd, "sleep") && argc == 3)
        r = sc360se_set_sleep_seconds(&dev, (uint16_t)atoi(argv[2]));
    else if (!strcmp(cmd, "button"))  r = do_button(&dev, argc, argv);
    else if (!strcmp(cmd, "static-light")) r = sc360se_set_static_light(&dev);
    else if (!strcmp(cmd, "commit"))  r = sc360se_commit(&dev);
    else if (!strcmp(cmd, "factory-reset")) r = sc360se_factory_reset(&dev);
    else if (!strcmp(cmd, "apply") && argc == 3) {
        struct sc360se_profile p;
        r = sc360se_profile_load(argv[2], &p);
        if (r == 0) r = sc360se_apply_profile(&dev, &p);
    }
    else if (!strcmp(cmd, "save-default") && argc == 3) {
        struct sc360se_profile p;
        sc360se_profile_default(&p);
        r = sc360se_profile_save(argv[2], &p);
    }
    else if (!strcmp(cmd, "write-colors"))  r = do_write_colors(&dev, argc, argv);
    else if (!strcmp(cmd, "write-buttons")) r = do_write_buttons(&dev, argc, argv);
    else if (!strcmp(cmd, "send"))    r = do_send(&dev, argc - 2, argv + 2);
    else if (!strcmp(cmd, "recv"))
        r = do_recv(&dev, argc > 2 ? atoi(argv[2]) : 1000);
    else if (!strcmp(cmd, "monitor")) r = do_monitor(&dev);
    else if (!strcmp(cmd, "watch"))   r = do_watch(&dev);
    else { usage(argv[0]); r = -EINVAL; }

out:
    if (r == -EHOSTDOWN)
        fprintf(stderr,
                "error: 2.4G receiver is reachable, but the mouse is not ready "
                "for config readback. Move/click/wake the mouse, then retry.\n");
    else if (r == -ENODATA)
        fprintf(stderr,
                "error: incomplete config readback; device returned no usable "
                "buttons/polling/DPI/colors/sleep data.\n");
    else if (r < 0)
        fprintf(stderr, "error: %s\n", strerror(-r));
    sc360se_close(&dev);
    return r ? 1 : 0;
}
