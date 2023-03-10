#include "utils/FuncThread.hpp"
//
extern "C" {
#include "SEEKFREE_IPS114_SPI.h"
}
#include "devices.hpp"
#include "edge_detect/A4Detect.hpp"
//
#include "fieldParam.hpp"

namespace imgProc {
using apriltag::float_t;
namespace edge_detect {
static Beep beep;

static uint8_t id;

static decltype(target_coords_corr) coords;
static int cnt = 0;

static bool try_recv(SerialIO& uart) {
    uart.waitHeader();
    beep.set(true);
    if (!uart.getchar(id)) return false;
    if (!uart.getchar(id)) return false;
    target_coords_cnt = id / 2;
    if (target_coords_cnt > target_coords_maxn) return false;
    for (int i = 0; i < target_coords_cnt * 2; ++i)
        if (!uart.getData<float>(((float_t*)target_coords_corr)[i])) return false;
    beep.set(false);
    return true;
}

static void testA4ReceiveEntry() {
    static SerialIO::TxArr<float, target_coords_maxn * 2, true> a4_tx(32, "a4_tx");

    for (;;) {
        if (try_recv(uart3)) {
            draw_corr(coords, cnt, borderWidth, borderHeight, 0xffff);
            cnt = target_coords_cnt;
            rt_memcpy(coords[0], target_coords_corr[0], sizeof(target_coords_corr));
            draw_corr(coords, cnt, borderWidth, borderHeight);

            a4_tx.txFinished(-1);
            a4_tx.setArr(target_coords_corr[0], target_coords_cnt * 2);
            wireless.send(a4_tx);

            rt_kprintf("%d\r\n", target_coords_cnt);
            for (int i = 0; i < target_coords_cnt; ++i) {
                for (int j = 0; j < 2; ++j)
                    rt_kprintf("%d.%02d ", int(target_coords_corr[i][j]), int(target_coords_corr[i][j] * 100) % 100);
                rt_kprintf("\r\n");
            }
            rt_kprintf("\r\n");
        }
    }
}
}  // namespace edge_detect
}  // namespace imgProc

bool testA4ReceiveNode() { return FuncThread(imgProc::edge_detect::testA4ReceiveEntry, "testA4Receive", 4096, 0); }