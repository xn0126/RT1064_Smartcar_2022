#include "utils/FuncThread.hpp"
//
extern "C" {
#include "SEEKFREE_IPS114_SPI.h"
#include "common.h"
#include "fsl_debug_console.h"
#include "zf_gpio.h"
#include "zf_usb_cdc.h"
}

#include "Camera.hpp"
#include "apriltag/visualization.hpp"
#include "devices.hpp"
#include "edge_detect/canny.hpp"
#include "edge_detect/conv.hpp"
#include "edge_detect/show_edge.hpp"

static void testCannyEntry() {
    using namespace imgProc::apriltag;
    using namespace imgProc::edge_detect;

    int32_t pre_time = rt_tick_get();

    for (;;) {
        bool visualize = slave_switch[2].get();  // 拨码开关决定是否进行可视化，因为可视化会消耗时间

        staticBuffer.reset();

        uint8_t* img = camera.snapshot();
        if (!visualize) show_grayscale(img);

        canny(img, 50, 100);  // 边缘检测

        if (visualize) show_edge(img);

        if (!slave_key[0].get()) {
            static uint8_t buf[4]{0x00, 0xff, 0x80, 0x7f};
            usb_cdc_send_buff(buf, 4);
            usb_cdc_send_buff(img, N * M);
        }

        camera.release();  // 释放图片

        int32_t cur_time = rt_tick_get();
        ips114_showint32(188, 0, cur_time - pre_time, 3);  // 显示耗时/ms
        pre_time = cur_time;
    }
}

bool testCannyNode() { return FuncThread(testCannyEntry, "testCanny", 4096, 2, 1000); }
