#pragma once

#include <driver/i2c_master.h>
#include <esp_timer.h>
#include <lvgl.h>

/**
 * @brief CHSC5432 电容触摸驱动 (芯微 CHSC6x 系列)
 *
 * 通过 I2C 总线轮询读取触摸坐标，直接注入 LVGL 输入系统。
 * 不使用 esp_lcd_touch 框架，避免为简单协议引入过度抽象。
 */
class Chsc5432Touch {
public:
    /**
     * @param i2c_bus  已初始化的 I2C 主总线句柄
     * @param width    显示宽度 (像素)
     * @param height   显示高度 (像素)
     */
    Chsc5432Touch(i2c_master_bus_handle_t i2c_bus, int width, int height);
    ~Chsc5432Touch();

private:
    static constexpr uint8_t  I2C_ADDR     = 0x2E;  /* CHSC5432 7位I2C地址 */
    static constexpr uint8_t  REG_STATUS   = 0x00;  /* 状态/数据寄存器 */
    static constexpr uint32_t POLL_MS      = 30;    /* 轮询间隔(毫秒) */
    static constexpr const char* TAG        = "CHSC5432";

    i2c_master_dev_handle_t dev_ = nullptr;
    lv_indev_t*             indev_ = nullptr;
    int                     width_, height_;
    bool                    touched_ = false;
    lv_point_t              point_ = {0, 0};
    esp_timer_handle_t      timer_ = nullptr;

    void InitI2c(i2c_master_bus_handle_t i2c_bus);
    void InitLvgl();
    void OnTimer();

    static void TimerCallback(void* arg);
    static void LvglReadCallback(lv_indev_t* indev, lv_indev_data_t* data);
};
