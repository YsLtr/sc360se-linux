/*
 * Wire format reverse-engineered from:
 *   wireshark_catch1.pdml  — initial broad capture
 *   侧键改为左键.txt          — side button → LMB
 *   侧键2改为左键.txt         — rear side button → LMB
 *   侧键改为A键.txt           — side button → keyboard 'A'
 *   侧键改为Ctrl+C.txt        — side button → Ctrl+C
 *   侧键改为DPI循环和DPI+以及DPI-.txt — DPI cycle / +/-
 *   侧键改为禁用.txt          — disable button
 *   滚轮键改为右键.txt        — wheel button → RMB
 *   将DPI档位1设置为1234.txt  — DPI stage 1 = 1234 (snapped to 1200)
 *   把DPI档位2改为5678.txt    — DPI stage 2 = 5678 (snapped to 5000)
 *   将配置1切换到配置2、3、4.pcapng — profile-switch readback
 *   连接+切换DPI.pcapng — handshake replies, battery (0xc0), DPI
 *                         button notification (0xc2), readback format
 *   插入接收器+识别.pcapng — 2.4G receiver insertion: 0x10 identity
 *                         replies before the later 0xc0 online event
 *
 * Frame: 32 bytes, sent as HID Output Report on EP 0x05, mirrored as
 * HID Input Report on EP 0x84.
 *
 *   [0]      command class
 *   [1]      0x00 = request, 0x01 = ack, 0x02 = error
 *   [2]      0x00 = meta/discovery, 0x01 = config, 0xff = error context
 *   [3]      sub-command
 *   [4..30]  payload
 *   [31]     checksum = sum(bytes[4..30]) mod 256
 */

#include "sc360se.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int debug_enabled(void)
{
    const char *v = getenv("SC360SE_DEBUG");
    return v && *v && strcmp(v, "0") != 0;
}

static void frame_init(uint8_t *f, uint8_t op, uint8_t ns, uint8_t sub)
{
    memset(f, 0, SC360SE_FRAME_LEN);
    f[0] = op;
    f[1] = 0x00;
    f[2] = ns;
    f[3] = sub;
}

/* ------------------------------------------------------------------ */
/* Discovery handshake                                                */
/* ------------------------------------------------------------------ */

int sc360se_handshake(struct sc360se_device *dev)
{
    static const uint8_t seq[] = {0x10, 0x10, 0x11, 0x12, 0x13,
                                  0x14, 0x15, 0x16, 0x17, 0x20};
    uint8_t out[SC360SE_FRAME_LEN], in[SC360SE_FRAME_LEN];
    int rc = 0;
    for (size_t i = 0; i < sizeof(seq); i++) {
        memset(out, 0, SC360SE_FRAME_LEN);
        out[0] = seq[i];
        rc = sc360se_xfer(dev, out, in);
        if (rc == -ETIMEDOUT) { rc = 0; continue; }
        if (rc < 0) return rc;
        if (seq[i] == 0x10 && in[3] == 0x0b) {
            memcpy(dev->dev_id, &in[4], 4);
            dev->dev_id[4] = 0;
        }
    }
    return rc;
}

/* ------------------------------------------------------------------ */
/* Polling rate     (op 0x02, sub 0x01)                               */
/* ------------------------------------------------------------------ */

int sc360se_set_polling_rate(struct sc360se_device *dev,
                             enum sc360se_polling_rate hz)
{
    uint8_t f[SC360SE_FRAME_LEN];
    frame_init(f, 0x02, 0x01, 0x01);
    f[4] = (uint8_t)hz;
    return sc360se_send(dev, f);
}

/* ------------------------------------------------------------------ */
/* DPI configuration  (op 0x03, sub 0x25)                             */
/* ------------------------------------------------------------------ */

int sc360se_set_dpi(struct sc360se_device *dev,
                    const struct sc360se_dpi_config *cfg)
{
    if (cfg->active >= SC360SE_NSTAGES) return -EINVAL;
    if (cfg->count == 0 || cfg->count > SC360SE_NSTAGES) return -EINVAL;
    for (int i = 0; i < SC360SE_NSTAGES; i++) {
        uint16_t x = cfg->stage[i].x_cpi;
        uint16_t y = cfg->stage[i].y_cpi;
        if (i < cfg->count &&
            (x < SC360SE_DPI_MIN_CPI || y < SC360SE_DPI_MIN_CPI))
            return -EINVAL;
        if (x > SC360SE_DPI_MAX_CPI || y > SC360SE_DPI_MAX_CPI)
            return -EINVAL;
    }

    uint8_t f[SC360SE_FRAME_LEN];
    frame_init(f, 0x03, 0x01, 0x25);
    f[4] = (uint8_t)((cfg->active << 4) | (cfg->count & 0x0f));

    /* Hardware DPI granularity (per user observation):
     *   below 5000 cpi: snap to nearest 100
     *   5000 cpi and above: snap to nearest 500
     * The wire format always stores cpi/100 (LE u16). */
    for (int i = 0; i < SC360SE_NSTAGES; i++) {
        uint16_t x = cfg->stage[i].x_cpi / 100;
        uint16_t y = cfg->stage[i].y_cpi / 100;
        f[5 + i*4 + 0] = (uint8_t)(x & 0xff);
        f[5 + i*4 + 1] = (uint8_t)(x >> 8);
        f[5 + i*4 + 2] = (uint8_t)(y & 0xff);
        f[5 + i*4 + 3] = (uint8_t)(y >> 8);
    }

    int rc = sc360se_send(dev, f);
    if (rc < 0) return rc;

    /* Host-side 0x03 DPI writes update the stored active stage but do not
     * run the firmware's physical-DPI-key runtime path. Do not follow this
     * with the old 0x06/0x05 "light-mode" guess; REVERSE_NOTES.md maps that
     * command to sensor-advanced fields instead of visible LED control. */
    return 0;
}

/* ------------------------------------------------------------------ */
/* DPI per-stage colors  (op 0x04, sub 0x12)                          */
/*                                                                    */
/* 6 stages × 3 bytes RGB at [4..21], then 6 flag bytes at [22..27].  */
/*                                                                    */
/* Verified against 连接+切换DPI.pcapng frame #19233: the 0x14 readback*/
/* shows the RGB block first (Red,Green,Blue,Magenta,Yellow,Cyan for  */
/* factory defaults), followed by 6 independent flag bytes.           */
/*                                                                    */
/* The six trailing flag bytes are stored by the firmware. Captures of */
/* normal static DPI lighting use 0x00 for every stage, but rewriting  */
/* this table alone has not restored the live constant-light state.    */
/* ------------------------------------------------------------------ */

int sc360se_set_dpi_colors(struct sc360se_device *dev,
                           const struct sc360se_dpi_config *cfg)
{
    uint8_t f[SC360SE_FRAME_LEN];
    frame_init(f, 0x04, 0x01, 0x12);
    for (int i = 0; i < SC360SE_NSTAGES; i++) {
        f[4 + i*3 + 0] = cfg->stage[i].r;
        f[4 + i*3 + 1] = cfg->stage[i].g;
        f[4 + i*3 + 2] = cfg->stage[i].b;
        f[22 + i]        = cfg->stage[i].flag;
    }
    return sc360se_send(dev, f);
}

/* ------------------------------------------------------------------ */
/* Power management  (op 0x07, sub 0x04)                              */
/*                                                                    */
/* Windows sleep-setting captures show:                               */
/*   [4] = configurable second-stage sleep timeout in 10-second units  */
/*   [5] = move-wakeup toggle                                         */
/*   [6] = move_closelight, not exposed here because no SC360SE live   */
/*         capture proves a useful visible behavior                    */
/*   [7] = button_respondtime, kept at the official default 8          */
/* ------------------------------------------------------------------ */

int sc360se_set_power_management(struct sc360se_device *dev,
                                 uint16_t sleep_seconds,
                                 uint8_t move_wakeup)
{
    if (sleep_seconds > SC360SE_SLEEP_MAX_SECONDS) return -EINVAL;
    if (sleep_seconds % SC360SE_SLEEP_UNIT_SECONDS != 0) return -EINVAL;
    if (move_wakeup > 1) return -EINVAL;

    uint8_t f[SC360SE_FRAME_LEN];
    frame_init(f, 0x07, 0x01, 0x04);
    f[4] = (uint8_t)(sleep_seconds / SC360SE_SLEEP_UNIT_SECONDS);
    f[5] = move_wakeup;
    f[6] = 0x00;
    f[7] = 0x08;
    return sc360se_send(dev, f);
}

int sc360se_set_sleep_seconds(struct sc360se_device *dev, uint16_t seconds)
{
    return sc360se_set_power_management(dev, seconds, 1);
}

/* ------------------------------------------------------------------ */
/* Button mapping  (op 0x09, sub 0x0f)                                */
/*                                                                    */
/* Six 3-byte slots at [4..21]:                                       */
/*    [4..6]   left mouse button                                      */
/*    [7..9]   right mouse button                                     */
/*    [10..12] wheel button                                           */
/*    [13..15] rear side button                                       */
/*    [16..18] front side button                                      */
/*    [19..21] DPI button                                             */
/* Each slot: type, param1, param2 (see sc360se_action_type).         */
/* ------------------------------------------------------------------ */

int sc360se_set_buttons(struct sc360se_device *dev,
                        const struct sc360se_button_action acts[SC360SE_NBUTTONS])
{
    uint8_t f[SC360SE_FRAME_LEN];
    frame_init(f, 0x09, 0x01, 0x0f);
    for (int i = 0; i < SC360SE_NBUTTONS; i++) {
        f[4 + i*3 + 0] = (uint8_t)acts[i].type;
        f[4 + i*3 + 1] = acts[i].p1;
        f[4 + i*3 + 2] = acts[i].p2;
    }
    /* The physical DPI key has a hidden firmware mode bit outside this
     * visible 0x09 table. Writing anything except factory local-cycle can
     * leave the key inert until factory reset; keep high-level writes safe. */
    f[4 + SC360SE_BTN_DPI*3 + 0] = SC360SE_ACT_DPI;
    f[4 + SC360SE_BTN_DPI*3 + 1] = SC360SE_DPI_CYCLE;
    f[4 + SC360SE_BTN_DPI*3 + 2] = 0x00;
    return sc360se_send(dev, f);
}

/* ------------------------------------------------------------------ */
/* Factory reset  (op 0x0f, sub 0x01)                                 */
/*                                                                    */
/* Captured from the Windows app while using "恢复出厂设置":           */
/*   0f 00 01 01 ff 00 ... 00 ff                                      */
/*                                                                    */
/* This is not equivalent to rewriting the visible profile. It also   */
/* clears hidden firmware state used by the dedicated DPI key path.    */
/* If the DPI key was remapped and becomes inert, this command brings  */
/* back local hardware DPI switching.                                  */
/* ------------------------------------------------------------------ */

int sc360se_factory_reset(struct sc360se_device *dev)
{
    uint8_t f[SC360SE_FRAME_LEN];
    frame_init(f, 0x0f, 0x01, 0x01);
    f[4] = 0xff;
    return sc360se_send(dev, f);
}

/* ------------------------------------------------------------------ */
/* Internal query — send bare opcode, get reply                       */
/* ------------------------------------------------------------------ */

static int query(struct sc360se_device *dev, uint8_t op, uint8_t *reply)
{
    uint8_t out[SC360SE_FRAME_LEN];
    memset(out, 0, SC360SE_FRAME_LEN);
    out[0] = op;
    return sc360se_xfer(dev, out, reply);
}

static int64_t monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void sleep_ms(int ms)
{
    if (ms <= 0) return;
    struct timespec ts = {
        .tv_sec = ms / 1000,
        .tv_nsec = (long)(ms % 1000) * 1000000L,
    };
    while (nanosleep(&ts, &ts) < 0 && errno == EINTR) {}
}

static void drain_pending(struct sc360se_device *dev)
{
    uint8_t stale[SC360SE_FRAME_LEN];
    for (int i = 0; i < 16; i++) {
        int rc = sc360se_try_recv(dev, stale);
        if (rc == -ETIMEDOUT) break;
        if (rc < 0) break;
        if (debug_enabled())
            fprintf(stderr, "[sc360se] drained stale frame op=0x%02x\n", stale[0]);
    }
}

static int send_bare_query(struct sc360se_device *dev, uint8_t op)
{
    uint8_t out[SC360SE_FRAME_LEN];
    memset(out, 0, sizeof(out));
    out[0] = op;
    return sc360se_send(dev, out);
}

enum {
    GOT_BUTTONS = 1u << 0,
    GOT_POLLING = 1u << 1,
    GOT_DPI     = 1u << 2,
    GOT_COLORS  = 1u << 3,
    GOT_SLEEP   = 1u << 4,
};

static const unsigned READ_NEED = GOT_BUTTONS | GOT_POLLING | GOT_DPI |
                                  GOT_COLORS  | GOT_SLEEP;

static const char *read_flag_name(unsigned flag)
{
    switch (flag) {
    case GOT_BUTTONS: return "buttons";
    case GOT_POLLING: return "polling";
    case GOT_DPI:     return "dpi";
    case GOT_COLORS:  return "colors";
    case GOT_SLEEP:   return "sleep";
    default:          return "?";
    }
}

static int plausible_reply_header(const uint8_t *p)
{
    if (p[1] != 0x00 || p[2] != 0x01) return 0;
    switch (p[0]) {
    case 0x10: return p[3] == 0x0b;
    case 0x11: return p[3] == 0x12;
    case 0x12: return p[3] == 0x01;
    case 0x13: return p[3] == 0x19;
    case 0x14: return p[3] == 0x12;
    case 0x15: return p[3] == 0x02;
    case 0x17: return p[3] == 0x05;
    case 0x20: return p[3] == 0x02;
    default:   return 0;
    }
}

static void debug_misaligned_reply(const uint8_t *in)
{
    if (!debug_enabled()) return;
    for (int i = 1; i <= SC360SE_FRAME_LEN - 4; i++) {
        if (plausible_reply_header(in + i)) {
            fprintf(stderr,
                    "[sc360se] ignoring partial/misaligned reply window; "
                    "found op=0x%02x header at byte %d\n",
                    in[i], i);
            return;
        }
    }
}

static int ack_only_reply(const uint8_t *in)
{
    return in[1] == 0x01 && in[2] == 0x00 && in[3] == 0x00;
}

static unsigned parse_config_reply(struct sc360se_device *dev,
                                   struct sc360se_profile *out,
                                   const uint8_t *in,
                                   unsigned *got)
{
    if (in[1] == 0x02) {
        if (debug_enabled())
            fprintf(stderr, "[sc360se] query 0x%02x failed with device error reply\n",
                    in[0]);
        return 0;
    }

    if (ack_only_reply(in)) {
        if (debug_enabled())
            fprintf(stderr, "[sc360se] ignoring ack-only frame op=0x%02x\n", in[0]);
        return 0;
    }

    switch (in[0]) {
    case 0x10:
        if (in[3] == 0x0b && !dev->dev_id[0]) {
            memcpy(dev->dev_id, &in[4], 4);
            dev->dev_id[4] = 0;
        }
        return 0;
    case 0x11:
        if (in[3] == 0x12) {
            for (int j = 0; j < SC360SE_NBUTTONS; j++) {
                out->buttons[j].type =
                    (enum sc360se_action_type)in[4 + j*3];
                out->buttons[j].p1 = in[4 + j*3 + 1];
                out->buttons[j].p2 = in[4 + j*3 + 2];
            }
            *got |= GOT_BUTTONS;
            return GOT_BUTTONS;
        }
        break;
    case 0x12:
        if (in[3] == 0x01) {
            out->polling = (enum sc360se_polling_rate)in[4];
            *got |= GOT_POLLING;
            return GOT_POLLING;
        }
        break;
    case 0x13:
        if (in[3] == 0x19) {
            out->dpi.active = (in[4] >> 4) & 0x0f;
            out->dpi.count  = in[4] & 0x0f;
            for (int j = 0; j < SC360SE_NSTAGES; j++) {
                uint16_t x = (uint16_t)(in[5 + j*4] |
                               (in[5 + j*4 + 1] << 8));
                uint16_t y = (uint16_t)(in[5 + j*4 + 2] |
                               (in[5 + j*4 + 3] << 8));
                out->dpi.stage[j].x_cpi = x * 100;
                out->dpi.stage[j].y_cpi = y * 100;
            }
            *got |= GOT_DPI;
            return GOT_DPI;
        }
        break;
    case 0x14:
        if (in[3] == 0x12) {
            for (int j = 0; j < SC360SE_NSTAGES; j++) {
                out->dpi.stage[j].r    = in[4 + j*3];
                out->dpi.stage[j].g    = in[4 + j*3 + 1];
                out->dpi.stage[j].b    = in[4 + j*3 + 2];
                out->dpi.stage[j].flag = in[22 + j];
            }
            *got |= GOT_COLORS;
            return GOT_COLORS;
        }
        break;
    case 0x17:
        if (in[3] == 0x05) {
            out->sleep_seconds =
                (uint16_t)in[4] * SC360SE_SLEEP_UNIT_SECONDS;
            out->move_wakeup = in[5] ? 1 : 0;
            *got |= GOT_SLEEP;
            return GOT_SLEEP;
        }
        break;
    }
    debug_misaligned_reply(in);
    return 0;
}

static int collect_replies(struct sc360se_device *dev,
                           struct sc360se_profile *out,
                           unsigned *got,
                           int timeout_ms,
                           int *ack_only_count)
{
    uint8_t in[SC360SE_FRAME_LEN];
    int64_t deadline = monotonic_ms() + timeout_ms;
    while ((*got & READ_NEED) != READ_NEED) {
        int left = (int)(deadline - monotonic_ms());
        if (left <= 0) break;
        if (left > 80) left = 80;
        int rc = sc360se_recv(dev, in, left);
        if (rc == -ETIMEDOUT) continue;
        if (rc < 0) return rc;
        if (ack_only_reply(in) && ack_only_count) (*ack_only_count)++;
        unsigned parsed = parse_config_reply(dev, out, in, got);
        if (parsed && debug_enabled())
            fprintf(stderr, "[sc360se] parsed config reply: %s\n",
                    read_flag_name(parsed));
    }
    return 0;
}

static int resend_missing_query(struct sc360se_device *dev,
                                struct sc360se_profile *out,
                                unsigned *got,
                                unsigned flag,
                                uint8_t op,
                                int *ack_only_count)
{
    if (*got & flag) return 0;
    for (int attempt = 0; attempt < 2 && !(*got & flag); attempt++) {
        if (debug_enabled())
            fprintf(stderr,
                    "[sc360se] retry missing config query 0x%02x (%s) %d/2\n",
                    op, read_flag_name(flag), attempt + 1);
        int rc = send_bare_query(dev, op);
        if (rc < 0) return rc;
        rc = collect_replies(dev, out, got, 220, ack_only_count);
        if (rc < 0) return rc;
        if (!(*got & flag)) sleep_ms(35);
    }
    return 0;
}

static int info_reply_mouse_ready(const uint8_t *in)
{
    return in[14] != 0x00;
}

static int ready_probe_frame(struct sc360se_device *dev,
                             const uint8_t *in,
                             int *saw_not_ready)
{
    if (in[0] == 0x10 && in[3] == 0x0b) {
        memcpy(dev->dev_id, &in[4], 4);
        dev->dev_id[4] = 0;
        if (info_reply_mouse_ready(in)) return 1;

        *saw_not_ready = 1;
        if (debug_enabled())
            fprintf(stderr,
                    "[sc360se] receiver identified as %.4s, but mouse is "
                    "not online yet (0x10 byte[14]=00)\n",
                    dev->dev_id);
        return 0;
    }

    if (in[0] == SC360SE_EVT_BATTERY) {
        if (in[1] != 0x00) {
            if (debug_enabled())
                fprintf(stderr,
                        "[sc360se] link online notification: battery=%u%%\n",
                        in[2]);
            return 1;
        }

        *saw_not_ready = 1;
        if (debug_enabled())
            fprintf(stderr,
                    "[sc360se] link offline notification: battery=%u%%\n",
                    in[2]);
        return 0;
    }

    return -1;
}

static int collect_ready_probe(struct sc360se_device *dev,
                               uint8_t *in,
                               int timeout_ms,
                               int *saw_not_ready)
{
    int64_t deadline = monotonic_ms() + timeout_ms;
    while (monotonic_ms() < deadline) {
        int left = (int)(deadline - monotonic_ms());
        if (left <= 0) break;
        if (left > 80) left = 80;

        int rc = sc360se_recv(dev, in, left);
        if (rc == -ETIMEDOUT) continue;
        if (rc < 0) return rc;
        if (ready_probe_frame(dev, in, saw_not_ready) > 0) return 0;
    }
    return -ETIMEDOUT;
}

static int read_ready_info(struct sc360se_device *dev, uint8_t *in)
{
    int saw_not_ready = 0;
    int long_online_wait_used = 0;

    for (int attempt = 0; attempt < 4; attempt++) {
        int rc = send_bare_query(dev, 0x10);
        if (rc < 0) return rc;

        rc = collect_ready_probe(dev, in, attempt == 0 ? 450 : 260,
                                 &saw_not_ready);
        if (rc == 0) return 0;
        if (rc < 0 && rc != -ETIMEDOUT) return rc;

        if (dev->link == SC360SE_LINK_24G) {
            /* 插入接收器+识别.pcapng shows the Windows driver can receive
             * S057 0x10 identity replies with byte[14]=00 first, then the
             * real "mouse online" state arrives later as c0 01 battery. */
            int wait_ms;
            if (saw_not_ready && !long_online_wait_used) {
                wait_ms = 1900;
                long_online_wait_used = 1;
            } else {
                wait_ms = attempt == 0 ? 1500 : 350;
            }
            rc = collect_ready_probe(dev, in, wait_ms, &saw_not_ready);
            if (rc == 0) return 0;
            if (rc < 0 && rc != -ETIMEDOUT) return rc;
        } else {
            sleep_ms(120);
        }
    }

    return saw_not_ready ? -EHOSTDOWN : -EIO;
}

/* ------------------------------------------------------------------ */
/* Read full current config from device                               */
/*                                                                    */
/* Sends the same 10-query handshake the Windows driver uses on       */
/* connect and parses every reply into a profile. Verified against    */
/* 连接+切换DPI.pcapng.                                                */
/* ------------------------------------------------------------------ */

int sc360se_read_config(struct sc360se_device *dev,
                         struct sc360se_profile *out)
{
    uint8_t in[SC360SE_FRAME_LEN];
    int rc;
    unsigned got = 0;
    int ack_only_count = 0;

    sc360se_profile_default(out);

    /* The 2.4G dongle can answer 0x10 from its local state even when the
     * mouse is asleep or still coming online after receiver insertion. In
     * that state config queries return no useful data; fail early instead
     * of showing the default profile as if it were real. */
    rc = read_ready_info(dev, in);
    if (rc == -EHOSTDOWN) {
        if (debug_enabled())
            fprintf(stderr,
                    "[sc360se] dongle reports mouse not ready/asleep "
                    "(0x10 reply byte[14]=00); wake/move/click the mouse\n");
    }
    if (rc < 0) return rc;

    drain_pending(dev);

    static const uint8_t seq[] = {0x10, 0x11, 0x12, 0x13,
                                  0x14, 0x15, 0x17, 0x20};
    for (size_t i = 0; i < sizeof(seq); i++) {
        rc = send_bare_query(dev, seq[i]);
        if (rc < 0) return rc;

        int64_t step_deadline = monotonic_ms() + 260;
        for (;;) {
            int left = (int)(step_deadline - monotonic_ms());
            if (left <= 0) break;
            if (left > 60) left = 60;

            rc = sc360se_recv(dev, in, left);
            if (rc == -ETIMEDOUT) break;
            if (rc < 0) return rc;
            if (ack_only_reply(in)) ack_only_count++;
            unsigned parsed = parse_config_reply(dev, out, in, &got);
            if (parsed && debug_enabled())
                fprintf(stderr, "[sc360se] parsed config reply: %s\n",
                        read_flag_name(parsed));
            if ((got & READ_NEED) == READ_NEED) return 0;
        }
    }

    rc = collect_replies(dev, out, &got, 700, &ack_only_count);
    if (rc < 0) return rc;

    if (got == 0 && ack_only_count >= 2) {
        if (debug_enabled())
            fprintf(stderr,
                    "[sc360se] config queries returned only ack frames; "
                    "treating mouse as not ready for readback\n");
        return -EHOSTDOWN;
    }

    for (int attempt = 0; attempt < 2 && (got & READ_NEED) != READ_NEED; attempt++) {
        unsigned before = got;
        if ((rc = resend_missing_query(dev, out, &got, GOT_BUTTONS, 0x11, &ack_only_count)) < 0) return rc;
        if ((rc = resend_missing_query(dev, out, &got, GOT_POLLING, 0x12, &ack_only_count)) < 0) return rc;
        if ((rc = resend_missing_query(dev, out, &got, GOT_DPI,     0x13, &ack_only_count)) < 0) return rc;
        if ((rc = resend_missing_query(dev, out, &got, GOT_COLORS,  0x14, &ack_only_count)) < 0) return rc;
        if ((rc = resend_missing_query(dev, out, &got, GOT_SLEEP,   0x17, &ack_only_count)) < 0) return rc;
        if (got == before) break;
        if ((got & READ_NEED) != READ_NEED) sleep_ms(60);
    }

    if ((got & READ_NEED) == READ_NEED) return 0;
    if (got == 0 && ack_only_count >= 2) {
        if (debug_enabled())
            fprintf(stderr,
                    "[sc360se] config retries returned only ack frames; "
                    "treating mouse as not ready for readback\n");
        return -EHOSTDOWN;
    }

    const unsigned need = READ_NEED;
    if ((got & need) != need) {
        if (debug_enabled()) {
            fprintf(stderr,
                    "[sc360se] incomplete read_config: got=0x%02x need=0x%02x"
                    " missing:%s%s%s%s%s\n",
                    got, need,
                    (got & GOT_BUTTONS) ? "" : " buttons",
                    (got & GOT_POLLING) ? "" : " polling",
                    (got & GOT_DPI)     ? "" : " dpi",
                    (got & GOT_COLORS)  ? "" : " colors",
                    (got & GOT_SLEEP)   ? "" : " sleep");
        }
        return -ENODATA;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Read battery percentage (0-100). Sends 0x10 query, extracts        */
/* byte[13] from the reply. Returns <0 on error.                      */
/* ------------------------------------------------------------------ */

int sc360se_read_battery(struct sc360se_device *dev)
{
    uint8_t in[SC360SE_FRAME_LEN];
    int rc = query(dev, 0x10, in);
    if (rc < 0) return rc;
    if (in[0] != 0x10 || in[3] != 0x0b) return -EIO;
    if (dev->link == SC360SE_LINK_24G && !info_reply_mouse_ready(in))
        return -EHOSTDOWN;
    return (int)in[13];
}

/* ------------------------------------------------------------------ */
/* Decode an unsolicited notification frame                           */
/*                                                                    */
/*   0xc0 — link/battery: byte[1] = online flag, byte[2] = percent    */
/*   0xc2 — DPI change: byte[1] = (active<<4)|count,                  */
/*           byte[2..3] = cpi/100 LE u16                              */
/* ------------------------------------------------------------------ */

int sc360se_decode_event(const uint8_t *frame, struct sc360se_event *evt)
{
    memset(evt, 0, sizeof(*evt));
    switch (frame[0]) {
    case SC360SE_EVT_BATTERY:
        evt->type = SC360SE_EVT_BATTERY;
        evt->link_online = frame[1] ? 1 : 0;
        evt->battery_pct = frame[2];
        return 0;
    case SC360SE_EVT_DPI:
        evt->type = SC360SE_EVT_DPI;
        evt->dpi_active = (frame[1] >> 4) & 0x0f;
        evt->dpi_count  = frame[1] & 0x0f;
        evt->dpi_cpi    = (uint16_t)(frame[2] | (frame[3] << 8)) * 100;
        return 0;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Quick status — battery + active DPI stage (2 queries)             */
/*                                                                   */
/* Returns 0 on success and fills *battery_pct (0-100) and           */
/* *dpi_active. Either pointer may be NULL to skip that value.       */
/* ------------------------------------------------------------------ */

int sc360se_read_status(struct sc360se_device *dev,
                         int *battery_pct, uint8_t *dpi_active)
{
    uint8_t in[SC360SE_FRAME_LEN];
    int rc;

    if (battery_pct) {
        rc = query(dev, 0x10, in);
        if (rc < 0) return rc;
        if (in[0] != 0x10 || in[3] != 0x0b) return -EIO;
        if (dev->link == SC360SE_LINK_24G && !info_reply_mouse_ready(in))
            return -EHOSTDOWN;
        *battery_pct = (int)in[13];
    }

    if (dpi_active) {
        rc = query(dev, 0x13, in);
        if (rc < 0) return rc;
        if (in[0] != 0x13 || in[3] != 0x19) return -EIO;
        *dpi_active = (in[4] >> 4) & 0x0f;
    }

    return 0;
}
