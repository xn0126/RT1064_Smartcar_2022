/*********************************************************************************************************************
 * COPYRIGHT NOTICE
 * Copyright (c) 2019,逐飞科技
 * All rights reserved.
 * 技术讨论QQ群：一群：179029047(已满)  二群：244861897
 *
 * 以下所有内容版权均属逐飞科技所有，未经允许不得用于商业用途，
 * 欢迎各位使用并传播本程序，修改内容时必须保留逐飞科技的版权声明。
 *
 * @file       		总钻风 RT CSI接口
 * @company	   		成都逐飞科技有限公司
 * @author     		逐飞科技(QQ3184284598)
 * @version    		查看doc内version文件 版本说明
 * @Software 		IAR 8.3 or MDK 5.28
 * @Taobao   		https://seekfree.taobao.com/
 * @date       		2019-02-18
 * @note		
					接线定义：
					------------------------------------ 
						模块管脚            单片机管脚
						SDA(51的RX)         查看SEEKFREE_MT9V03X_CSI.h文件中的MT9V03X_CSI_COF_UART_TX宏定义
						SCL(51的TX)         查看SEEKFREE_MT9V03X_CSI.h文件中的MT9V03X_CSI_COF_UART_RX宏定义
						场中断(VSY)         查看SEEKFREE_MT9V03X_CSI.h文件中的MT9V03X_CSI_VSYNC_PIN宏定义
						行中断(HREF)	    不需要使用
						像素中断(PCLK)      查看SEEKFREE_MT9V03X_CSI.h文件中的MT9V03X_CSI_PCLK_PIN宏定义
						数据口(D0-D7)		B31-B24 B31对应摄像头接口D0
					------------------------------------ 
	
					默认分辨率是            188*120
					默认FPS                 50帧
 ********************************************************************************************************************/


#include <rthw.h>
#include <rtthread.h>

#include "fsl_cache.h"
#include "fsl_common.h"
#include "fsl_gpio.h"
#include "fsl_csi.h"
#include "zf_systick.h"
#include "zf_pit.h"
#include "zf_camera.h"
#include "zf_gpio.h"
#include "zf_iomuxc.h"
#include "zf_csi.h"
#include "SEEKFREE_MT9V03X.h"
#include "SEEKFREE_MT9V03X_CSI.h"


//图像缓冲区  如果用户需要访问图像数据 最好通过mt9v03x_csi_image来访问数据，最好不要直接访问缓冲区
AT_SDRAM_SECTION_ALIGN(static uint8 mt9v03x_csi_buf[2][MT9V03X_CSI_H * MT9V03X_CSI_W], 64);


//用户访问图像数据直接访问这个指针变量就可以
//访问方式非常简单，可以直接使用下标的方式访问
//例如访问第10行 50列的点，mt9v03x_csi_image[10][50]就可以了
uint8 (*mt9v03x_csi_image)[MT9V03X_CSI_W];

static struct rt_semaphore mt9v03x_csi_sem;


uint8               csi_rx_buffer;
lpuart_transfer_t   csi_receivexfer;
lpuart_handle_t     csi_g_lpuartHandle;
//-------------------------------------------------------------------------------------------------------------------
//  @brief      MT9V03X摄像头串口中断函数
//  @param      NULL
//  @return     void					
//  @since      v1.0
//  Sample usage:	
//  @note       该函数在ISR文件 串口3中断程序被调用
//-------------------------------------------------------------------------------------------------------------------
void csi_mt9v03x_uart_callback(LPUART_Type *base, lpuart_handle_t *handle, status_t status, void *userData)
{
    if(kStatus_LPUART_RxIdle == status)
    {
        receive[receive_num] = csi_rx_buffer;
        receive_num++;
    
        if(1==receive_num && 0XA5!=receive[0])  receive_num = 0;
        if(3 == receive_num)
        {
            receive_num = 0;
            uart_receive_flag = 1;
        }
    }
    handle->rxDataSize = csi_receivexfer.dataSize;  //还原缓冲区长度
    handle->rxData = csi_receivexfer.data;          //还原缓冲区地址
}






uint8 mt9v03x_csi_finish_flag;  //图像采集完成的标志位    



void csi_isr(CSI_Type *base, csi_handle_t *handle, status_t status, void *userData)
{
    rt_sem_release(&mt9v03x_csi_sem);
}

uint8_t *mt9v03x_csi_image_take() {
    for (;;) {
        rt_sem_take(&mt9v03x_csi_sem, RT_WAITING_FOREVER);
        rt_base_t level = rt_hw_interrupt_disable();
        uint8_t res = csi_get_full_buffer(&csi_handle, &fullCameraBufferAddr);
        rt_hw_interrupt_enable(level);
        if (res) {
            L1CACHE_InvalidateDCacheByRange(fullCameraBufferAddr, MT9V03X_CSI_H * MT9V03X_CSI_W);
            return (uint8_t *)fullCameraBufferAddr;
        }
    }
}
void mt9v03x_csi_image_release() {
    L1CACHE_InvalidateDCacheByRange(fullCameraBufferAddr, MT9V03X_CSI_H * MT9V03X_CSI_W);
    rt_base_t level = rt_hw_interrupt_disable();
    csi_add_empty_buffer(&csi_handle, (uint8_t *)fullCameraBufferAddr);
    rt_hw_interrupt_enable(level);
}

//-------------------------------------------------------------------------------------------------------------------
//  @brief      MT9V03X摄像头初始化 使用CSI接口
//  @param      NULL
//  @return     void					
//  @since      v1.0
//  Sample usage:		使用CSI接口采集摄像头
//-------------------------------------------------------------------------------------------------------------------
void mt9v03x_csi_init(void)
{
    uart_init (MT9V03X_CSI_COF_UART, 9600,MT9V03X_CSI_COF_UART_TX,MT9V03X_CSI_COF_UART_RX);	//初始换串口 配置摄像头    
    uart_rx_irq(MT9V03X_CSI_COF_UART,1);
    //配置串口接收的缓冲区及缓冲区长度
    csi_receivexfer.dataSize = 1;
    csi_receivexfer.data = &csi_rx_buffer;
    
    uart_set_handle(MT9V03X_CSI_COF_UART, &csi_g_lpuartHandle, csi_mt9v03x_uart_callback, NULL, 0, csi_receivexfer.data, 1);

    //等待摄像头上电初始化成功 方式有两种：延时或者通过获取配置的方式 二选一
    //systick_delay_ms(1000);//延时方式
    get_config(MT9V03X_CSI_COF_UART,GET_CFG_CSI);//获取配置的方式
    
	uart_receive_flag = 0;
    set_config(MT9V03X_CSI_COF_UART,MT9V03X_CFG_CSI);
    //获取配置便于查看配置是否正确
    get_config(MT9V03X_CSI_COF_UART,GET_CFG_CSI);

    rt_sem_init(&mt9v03x_csi_sem, "mt9v03x_csi_sem", 0, RT_IPC_FLAG_FIFO);

    //CSI 采集初始化
    rt_base_t level = rt_hw_interrupt_disable();
    csi_init(MT9V03X_CSI_W, MT9V03X_CSI_H, &csi_handle, csi_isr, MT9V03X_CSI_VSYNC_PIN, MT9V03X_CSI_PCLK_PIN);
    csi_add_empty_buffer(&csi_handle, mt9v03x_csi_buf[0]);
    csi_add_empty_buffer(&csi_handle, mt9v03x_csi_buf[1]);
    csi_start(&csi_handle);
    rt_hw_interrupt_enable(level);
    // mt9v03x_csi_image = mt9v03x_csi_buf[0];//设置初值
    // EnableGlobalIRQ(0);
}




void csi_seekfree_sendimg_03x(UARTN_enum uartn, uint8 *image, uint16 width, uint16 height)
{
    uart_putchar(uartn,0x00);uart_putchar(uartn,0xff);uart_putchar(uartn,0x01);uart_putchar(uartn,0x01);//发送命令
    uart_putbuff(uartn, image, width*height);  //发送图像
    
}

