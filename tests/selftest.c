/* Self-test: build the same frames the Linux driver would send and
 * compare against actual byte sequences captured from the Windows
 * driver. Anything that differs is a bug in protocol.c. */
#include "sc360se.h"
#include <stdio.h>
#include <string.h>

extern uint8_t sc360se_checksum(const uint8_t *frame);

static int pass = 0, fail = 0;

static void check(const char *label, const uint8_t *got, const char *expected_hex)
{
    uint8_t exp[SC360SE_FRAME_LEN];
    for (int i = 0; i < SC360SE_FRAME_LEN; i++) {
        unsigned b;
        sscanf(expected_hex + i*2, "%2x", &b);
        exp[i] = (uint8_t)b;
    }
    if (memcmp(got, exp, SC360SE_FRAME_LEN) == 0) {
        printf("PASS: %s\n", label);
        pass++;
    } else {
        printf("FAIL: %s\n", label);
        printf("  got: ");
        for (int i = 0; i < SC360SE_FRAME_LEN; i++) printf("%02x", got[i]);
        printf("\n  exp: %s\n", expected_hex);
        fail++;
    }
}

int main(void)
{
    uint8_t f[SC360SE_FRAME_LEN];

    /* polling 1000 Hz */
    memset(f, 0, sizeof(f));
    f[0]=0x02; f[2]=0x01; f[3]=0x01; f[4]=0x01;
    f[31] = sc360se_checksum(f);
    check("polling 1000 Hz",
          f, "0200010101000000000000000000000000000000000000000000000000000001");

    /* polling 500 Hz */
    memset(f, 0, sizeof(f));
    f[0]=0x02; f[2]=0x01; f[3]=0x01; f[4]=0x02;
    f[31] = sc360se_checksum(f);
    check("polling 500 Hz",
          f, "0200010102000000000000000000000000000000000000000000000000000002");

    /* sleep 90 s */
    memset(f, 0, sizeof(f));
    f[0]=0x07; f[2]=0x01; f[3]=0x04; f[4]=0x5a; f[7]=0x08;
    f[31] = sc360se_checksum(f);
    check("sleep 90 s",
          f, "070001045a000008000000000000000000000000000000000000000000000062");

    /* DPI 6-stage 800/1600/2400/3200/5300/6000, active=1 */
    memset(f, 0, sizeof(f));
    f[0]=0x03; f[2]=0x01; f[3]=0x25; f[4]=(1<<4)|5;
    uint16_t cpis[6] = {800, 1600, 2400, 3200, 5300, 6000};
    for (int i = 0; i < 6; i++) {
        uint16_t v = cpis[i] / 100;
        f[5+i*4+0] = v & 0xff; f[5+i*4+1] = v >> 8;
        f[5+i*4+2] = v & 0xff; f[5+i*4+3] = v >> 8;
    }
    f[31] = sc360se_checksum(f);
    check("DPI 6-stage (5 enabled, active=1)",
          f, "030001251508000800100010001800180020002000350035003c003c00000097");

    /* buttons default — captured from wireshark_catch1.pdml frame #30109 */
    memset(f, 0, sizeof(f));
    f[0]=0x09; f[2]=0x01; f[3]=0x0f;
    uint8_t btn_def[6][3] = {
        {0x10, 0x01, 0x00},
        {0x10, 0x02, 0x00},
        {0x10, 0x04, 0x00},
        {0x10, 0x08, 0x00},
        {0x10, 0x10, 0x00},
        {0x40, 0x01, 0x00},
    };
    for (int i = 0; i < 6; i++) memcpy(&f[4+i*3], btn_def[i], 3);
    f[31] = sc360se_checksum(f);
    check("buttons all default",
          f, "0900010f100100100200100400100800101000400100000000000000000000b0");

    /* front side button (slot 4) → Ctrl+C — from 侧键改为Ctrl+C.txt */
    memset(f, 0, sizeof(f));
    f[0]=0x09; f[2]=0x01; f[3]=0x0f;
    uint8_t btn_cc[6][3] = {
        {0x10, 0x01, 0x00}, {0x10, 0x02, 0x00},
        {0x10, 0x04, 0x00}, {0x10, 0x08, 0x00},
        {0x70, 0x01, 0x06},                          /* Ctrl + 'C' (HID 0x06) */
        {0x40, 0x01, 0x00},
    };
    for (int i = 0; i < 6; i++) memcpy(&f[4+i*3], btn_cc[i], 3);
    f[31] = sc360se_checksum(f);
    check("front-side → Ctrl+C",
          f, "0900010f10010010020010040010080070010640010000000000000000000007");

    /* slot 4 → DPI+ — from DPI循环和DPI+以及DPI-.txt */
    memset(f, 0, sizeof(f));
    f[0]=0x09; f[2]=0x01; f[3]=0x0f;
    uint8_t btn_dpip[6][3] = {
        {0x10, 0x01, 0x00}, {0x10, 0x02, 0x00},
        {0x10, 0x04, 0x00}, {0x10, 0x08, 0x00},
        {0x40, 0x02, 0x00},
        {0x40, 0x01, 0x00},
    };
    for (int i = 0; i < 6; i++) memcpy(&f[4+i*3], btn_dpip[i], 3);
    f[31] = sc360se_checksum(f);
    check("front-side → DPI+",
          f, "0900010f100100100200100400100800400200400100000000000000000000d2");

    /* slot 4 → disable — from 侧键改为禁用.txt */
    memset(f, 0, sizeof(f));
    f[0]=0x09; f[2]=0x01; f[3]=0x0f;
    uint8_t btn_dis[6][3] = {
        {0x10, 0x01, 0x00}, {0x10, 0x02, 0x00},
        {0x10, 0x04, 0x00}, {0x10, 0x08, 0x00},
        {0x60, 0x00, 0x00},
        {0x40, 0x01, 0x00},
    };
    for (int i = 0; i < 6; i++) memcpy(&f[4+i*3], btn_dis[i], 3);
    f[31] = sc360se_checksum(f);
    check("front-side → disable",
          f, "0900010f100100100200100400100800600000400100000000000000000000f0");

    /* slot 2 (wheel) → RMB — from 滚轮键改为右键.txt */
    memset(f, 0, sizeof(f));
    f[0]=0x09; f[2]=0x01; f[3]=0x0f;
    uint8_t btn_wr[6][3] = {
        {0x10, 0x01, 0x00}, {0x10, 0x02, 0x00},
        {0x10, 0x02, 0x00},                          /* wheel → RMB */
        {0x10, 0x01, 0x00},                          /* (rear side now LMB) */
        {0x60, 0x00, 0x00},                          /* (front disabled)   */
        {0x40, 0x01, 0x00},
    };
    for (int i = 0; i < 6; i++) memcpy(&f[4+i*3], btn_wr[i], 3);
    f[31] = sc360se_checksum(f);
    check("wheel → RMB (along with prior changes)",
          f, "0900010f100100100200100200100100600000400100000000000000000000e7");

    /* Profile 2 (FPS, 5000/4000/3000/2000/1000, active=4)
     *   from 配置切换.pcapng frame #2645 */
    memset(f, 0, sizeof(f));
    f[0]=0x03; f[2]=0x01; f[3]=0x25; f[4]=(4<<4)|5;
    uint16_t fps[6] = {5000, 4000, 3000, 2000, 1000, 6000};
    for (int i = 0; i < 6; i++) {
        uint16_t v = fps[i] / 100;
        f[5+i*4+0] = v & 0xff; f[5+i*4+1] = v >> 8;
        f[5+i*4+2] = v & 0xff; f[5+i*4+3] = v >> 8;
    }
    f[31] = sc360se_checksum(f);
    check("Profile 2 (FPS) DPI",
          f, "030001254532003200280028001e001e00140014000a000a003c003c000000e9");

    /* Profile 3 (Office, 1000/2000/3000, active=2)
     *   from 配置切换.pcapng frame #4763 */
    memset(f, 0, sizeof(f));
    f[0]=0x03; f[2]=0x01; f[3]=0x25; f[4]=(2<<4)|3;
    uint16_t off[6] = {1000, 2000, 3000, 3200, 5000, 6000};
    for (int i = 0; i < 6; i++) {
        uint16_t v = off[i] / 100;
        f[5+i*4+0] = v & 0xff; f[5+i*4+1] = v >> 8;
        f[5+i*4+2] = v & 0xff; f[5+i*4+3] = v >> 8;
    }
    f[31] = sc360se_checksum(f);
    check("Profile 3 (Office) DPI",
          f, "03000125230a000a00140014001e001e0020002000320032003c003c000000b7");

    /* Static DPI light restore — verified live after host-side DPI writes */
    memset(f, 0, sizeof(f));
    f[0]=0x06; f[2]=0x01; f[3]=0x05; f[4]=0x02;
    f[31] = sc360se_checksum(f);
    check("static DPI light (op 0x06)",
          f, "0600010502000000000000000000000000000000000000000000000000000002");

    /* Factory reset — from DPI配置.pcapng frame #9955 / #33257 */
    memset(f, 0, sizeof(f));
    f[0]=0x0f; f[2]=0x01; f[3]=0x01; f[4]=0xff;
    f[31] = sc360se_checksum(f);
    check("factory reset",
          f, "0f000101ff0000000000000000000000000000000000000000000000000000ff");

    /* DPI colors — factory rainbow, all flags off
     * Write format: [R][G][B]×6 at [4..21] + [flag]×6 at [22..27]
     * Verified against 连接+切换DPI.pcapng frame #19233 (0x14 reply). */
    memset(f, 0, sizeof(f));
    f[0]=0x04; f[2]=0x01; f[3]=0x12;
    /* R  G  B */
    f[4]=0xff; f[5]=0x00; f[6]=0x00;   /* stage 0: red    */
    f[7]=0x00; f[8]=0xff; f[9]=0x00;   /* stage 1: green  */
    f[10]=0x00;f[11]=0x00;f[12]=0xff;  /* stage 2: blue   */
    f[13]=0xff;f[14]=0x00;f[15]=0xff;  /* stage 3: magenta*/
    f[16]=0xff;f[17]=0xff;f[18]=0x00;  /* stage 4: yellow */
    f[19]=0x00;f[20]=0xff;f[21]=0xff;  /* stage 5: cyan   */
    /* flags [22..27] are all 0x00 (factory default) */
    f[31] = sc360se_checksum(f);
    check("DPI colors rainbow (all flags off)",
          f, "04000112ff000000ff000000ffff00ffffff0000ffff000000000000000000f7");

    /* --- event decoding (连接+切换DPI.pcapng) --- */

    struct sc360se_event evt;

    /* Battery: 0xc0 01 5c 00... → 92% */
    memset(f, 0, sizeof(f));
    f[0] = 0xc0; f[1] = 0x01; f[2] = 0x5c;
    if (sc360se_decode_event(f, &evt) == 0 &&
        evt.type == SC360SE_EVT_BATTERY && evt.battery_pct == 92)
        { printf("PASS: decode battery 92%%\n"); pass++; }
    else
        { printf("FAIL: decode battery 92%%\n"); fail++; }

    /* DPI button: 0xc2 26 18... → stage 2/6, 2400 cpi */
    memset(f, 0, sizeof(f));
    f[0] = 0xc2; f[1] = 0x26; f[2] = 0x18; f[6] = 0x01;
    if (sc360se_decode_event(f, &evt) == 0 &&
        evt.type == SC360SE_EVT_DPI &&
        evt.dpi_active == 2 && evt.dpi_count == 6 && evt.dpi_cpi == 2400)
        { printf("PASS: decode DPI stage 2 → 2400\n"); pass++; }
    else
        { printf("FAIL: decode DPI stage 2 → 2400\n"); fail++; }

    /* DPI button: 0xc2 56 3c... → stage 5/6, 6000 cpi */
    memset(f, 0, sizeof(f));
    f[0] = 0xc2; f[1] = 0x56; f[2] = 0x3c; f[6] = 0x01;
    if (sc360se_decode_event(f, &evt) == 0 &&
        evt.type == SC360SE_EVT_DPI &&
        evt.dpi_active == 5 && evt.dpi_count == 6 && evt.dpi_cpi == 6000)
        { printf("PASS: decode DPI stage 5 → 6000\n"); pass++; }
    else
        { printf("FAIL: decode DPI stage 5 → 6000\n"); fail++; }

    printf("\n%d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
