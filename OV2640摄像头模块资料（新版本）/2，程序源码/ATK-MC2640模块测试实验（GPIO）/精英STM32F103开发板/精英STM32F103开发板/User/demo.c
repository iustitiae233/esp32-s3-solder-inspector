/**
 ****************************************************************************************************
 * @file        demo.c
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2022-06-21
 * @brief       ATK-MC2640模块测试实验（GPIO）
 * @license     Copyright (c) 2020-2032, 广州市星翼电子科技有限公司
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:正点原子 STM32F103开发板
 * 在线视频:www.yuanzige.com
 * 技术论坛:www.openedv.com
 * 公司网址:www.alientek.com
 * 购买地址:openedv.taobao.com
 *
 ****************************************************************************************************
 */

#include "demo.h"
#include "./BSP/ATK_MC2640/atk_mc2640.h"
#include "./BSP/lcd/lcd.h"
#include "./BSP/led/led.h"
#include "./SYSTEM/usart/usart.h"
#include "./SYSTEM/delay/delay.h"
#include "./MALLOC/malloc.h"

/**
 * @brief       根据LCD的尺寸，设置ATK-MC2640模块合适的输出速率
 * @param       无
 * @retval      无
 */
void demo_set_outspeed_suit_lcd(void)
{
    if (lcddev.width == 240)
    {
        atk_mc2640_set_output_speed(1, 28);
    }
    else if (lcddev.width == 320)
    {
        atk_mc2640_set_output_speed(3, 15);
    }
    else
    {
        atk_mc2640_set_output_speed(15, 4);
    }
}

/**
 * @brief       复位LCD的写入位置至坐标为(0, 0)的像素点
 * @param       无
 * @retval      无
 */
static void demo_reset_lcd(void)
{
    lcd_scan_dir(R2L_U2D);
    lcd_set_cursor(0, 0);
    lcd_write_ram_prepare();
}

/**
 * @brief       例程演示入口函数
 * @param       无
 * @retval      无
 */
void demo_run(void)
{
    uint8_t ret;
    
    my_mem_init(SRAMIN);                                                    /* 初始化内部SRAM内存池 */
    ret  = atk_mc2640_init();                                               /* 初始化ATK-MC2640模块 */
    ret |= atk_mc2640_set_output_format(ATK_MC2640_OUTPUT_FORMAT_RGB565);   /* 输出图像格式 */
    ret |= atk_mc2640_set_output_size(lcddev.width, lcddev.height);         /* 输出图像分辨率 */
    if (ret != 0)
    {
        printf("ATK-MC2640 Init Failed!\r\n");
        while (1)
        {
            LED0_TOGGLE();
            delay_ms(200);
        }
    }
    
    demo_set_outspeed_suit_lcd();                                           /* 输出速率 */
    atk_mc2640_set_light_mode(ATK_MC2640_LIGHT_MODE_SUNNY);                 /* 设置灯光模式 */
    atk_mc2640_set_color_saturation(ATK_MC2640_COLOR_SATURATION_1);         /* 设置色彩饱和度 */
    atk_mc2640_set_brightness(ATK_MC2640_BRIGHTNESS_1);                     /* 设置亮度 */
    atk_mc2640_set_contrast(ATK_MC2640_CONTRAST_2);                         /* 设置对比度 */
    atk_mc2640_set_special_effect(ATK_MC2640_SPECIAL_EFFECT_NORMAL);        /* 设置特殊效果 */
    
    while (1)
    {
        /* 将获取到的图像数据，显示至LCD */
        atk_mc2640_get_frame((uint32_t)&LCD->LCD_RAM, ATK_MC2640_GET_TYPE_DTS_16B_NOINC, demo_reset_lcd);
    }
}
