#define _GNU_SOURCE
#include "sc360se.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int debug_enabled(void)
{
    const char *v = getenv("SC360SE_DEBUG");
    return v && *v && strcmp(v, "0") != 0;
}

static void dump_frame(const char *tag, const uint8_t *frame)
{
    if (!debug_enabled()) return;
    fprintf(stderr, "[sc360se] %s", tag);
    for (int i = 0; i < SC360SE_FRAME_LEN; i++)
        fprintf(stderr, " %02x", frame[i]);
    fprintf(stderr, "\n");
}

static int read_sysfs(const char *path, char *buf, size_t len)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = read(fd, buf, len - 1);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = 0;
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == ' ')) buf[--n] = 0;
    return 0;
}

static int parse_iface(const char *hidraw_name, int *iface)
{
    char path[512], buf[16];
    snprintf(path, sizeof(path),
             "/sys/class/hidraw/%s/device/../bInterfaceNumber",
             hidraw_name);
    if (read_sysfs(path, buf, sizeof(buf)) < 0) return -1;
    *iface = (int)strtol(buf, NULL, 16);
    return 0;
}

static int match_node(const char *hidraw_name, struct sc360se_device *out)
{
    char path[512], buf[64];
    snprintf(path, sizeof(path),
             "/sys/class/hidraw/%s/device/../../idVendor", hidraw_name);
    if (read_sysfs(path, buf, sizeof(buf)) < 0) return -1;
    unsigned int vid = (unsigned int)strtoul(buf, NULL, 16);

    snprintf(path, sizeof(path),
             "/sys/class/hidraw/%s/device/../../idProduct", hidraw_name);
    if (read_sysfs(path, buf, sizeof(buf)) < 0) return -1;
    unsigned int pid = (unsigned int)strtoul(buf, NULL, 16);

    enum sc360se_link link;
    if (vid == SC360SE_VID_WIRED && pid == SC360SE_PID_WIRED)
        link = SC360SE_LINK_USB;
    else if (vid == SC360SE_VID_24G && pid == SC360SE_PID_24G)
        link = SC360SE_LINK_24G;
    else
        return -1;

    int iface = -1;
    if (parse_iface(hidraw_name, &iface) < 0) return -1;
    if (iface != SC360SE_IFACE) return -1;

    out->vid  = (uint16_t)vid;
    out->pid  = (uint16_t)pid;
    out->link = link;
    snprintf(out->path, sizeof(out->path), "/dev/%s", hidraw_name);
    return 0;
}

int sc360se_open(struct sc360se_device *dev)
{
    memset(dev, 0, sizeof(*dev));
    dev->fd = -1;

    DIR *d = opendir("/sys/class/hidraw");
    if (!d) return -errno;

    struct dirent *de;
    int found = 0;
    while ((de = readdir(d))) {
        if (strncmp(de->d_name, "hidraw", 6) != 0) continue;
        if (match_node(de->d_name, dev) == 0) { found = 1; break; }
    }
    closedir(d);
    if (!found) return -ENODEV;

    dev->fd = open(dev->path, O_RDWR | O_CLOEXEC);
    if (dev->fd < 0) return -errno;
    return 0;
}

void sc360se_close(struct sc360se_device *dev)
{
    if (dev->fd >= 0) close(dev->fd);
    dev->fd = -1;
}

uint8_t sc360se_checksum(const uint8_t *frame)
{
    /* Verified empirically: sum of bytes [4..30] mod 256. */
    unsigned s = 0;
    for (int i = 4; i < SC360SE_FRAME_LEN - 1; i++) s += frame[i];
    return (uint8_t)(s & 0xff);
}

int sc360se_send(struct sc360se_device *dev, uint8_t *frame)
{
    /* Frame[31] is the checksum slot — stamp it before sending. */
    frame[SC360SE_FRAME_LEN - 1] = sc360se_checksum(frame);
    dump_frame("TX", frame);

    /* hidraw expects the report ID in byte[0]. The SC360SE uses report-id=0
     * (numbered reports disabled), so we prepend a zero byte and write 33. */
    uint8_t out[1 + SC360SE_FRAME_LEN];
    out[0] = 0x00;
    memcpy(out + 1, frame, SC360SE_FRAME_LEN);

    ssize_t n = write(dev->fd, out, sizeof(out));
    if (n < 0) {
        /* Some kernels accept the bare 32 bytes when no report id is used. */
        if (errno == EINVAL) {
            n = write(dev->fd, frame, SC360SE_FRAME_LEN);
            if (n == SC360SE_FRAME_LEN) return 0;
        }
        return -errno;
    }
    if (n != (ssize_t)sizeof(out)) return -EIO;
    return 0;
}

int sc360se_recv(struct sc360se_device *dev, uint8_t *frame, int timeout_ms)
{
    struct pollfd pf = { .fd = dev->fd, .events = POLLIN };
    int r = poll(&pf, 1, timeout_ms);
    if (r < 0) return -errno;
    if (r == 0) return -ETIMEDOUT;
    ssize_t n = read(dev->fd, frame, SC360SE_FRAME_LEN);
    if (n < 0) return -errno;
    if (n != SC360SE_FRAME_LEN) return -EIO;
    dump_frame("RX", frame);
    return 0;
}

int sc360se_try_recv(struct sc360se_device *dev, uint8_t *frame)
{
    return sc360se_recv(dev, frame, 0);
}

int sc360se_xfer(struct sc360se_device *dev, uint8_t *out, uint8_t *in)
{
    uint8_t stale[SC360SE_FRAME_LEN];
    for (int i = 0; i < 16; i++) {
        int d = sc360se_try_recv(dev, stale);
        if (d == -ETIMEDOUT) break;
        if (d < 0) return d;
        if (debug_enabled()) dump_frame("DRAIN", stale);
    }

    int r = sc360se_send(dev, out);
    if (r < 0) return r;
    if (!in) return 0;
    /* Reply is on EP 0x84 within ~1ms; ignore unrelated async events. */
    int saw_unrelated = 0;
    for (int tries = 0; tries < 8; tries++) {
        r = sc360se_recv(dev, in, 50);
        if (r == -ETIMEDOUT) continue;
        if (r < 0) return r;
        if (in[0] == out[0]) return 0;          /* matching reply */
        saw_unrelated = 1;
        if (debug_enabled()) dump_frame("IGNORE", in);
    }
    return saw_unrelated ? -EIO : -ETIMEDOUT;
}
