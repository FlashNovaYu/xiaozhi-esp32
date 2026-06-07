#include "chsc5432_touch.h"
#include <esp_log.h>
#include <cstring>

Chsc5432Touch::Chsc5432Touch(i2c_master_bus_handle_t i2c_bus, int width, int height)
    : width_(width), height_(height)
{
    InitI2c(i2c_bus);
    InitLvgl();

    esp_timer_create_args_t args = {
        .callback = &TimerCallback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "chsc5432_poll",
    };
    ESP_ERROR_CHECK(esp_timer_create(&args, &timer_));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer_, POLL_MS * 1000));

    ESP_LOGI(TAG, "CHSC5432 触摸驱动已启动, 轮询间隔=%lums", POLL_MS);
}

Chsc5432Touch::~Chsc5432Touch() {
    if (timer_) {
        esp_timer_stop(timer_);
        esp_timer_delete(timer_);
    }
    if (dev_) i2c_master_bus_rm_device(dev_);
    if (indev_) lv_indev_delete(indev_);
}

void Chsc5432Touch::InitI2c(i2c_master_bus_handle_t i2c_bus) {
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = I2C_ADDR,
        .scl_speed_hz = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &dev_cfg, &dev_));
}

void Chsc5432Touch::InitLvgl() {
    indev_ = lv_indev_create();
    lv_indev_set_type(indev_, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev_, LvglReadCallback);
    lv_indev_set_user_data(indev_, this);
}

void Chsc5432Touch::OnTimer() {
    /* CHSC5432: 从寄存器 0x00 读取 6 字节
     * [0]     = 触摸点数 (0/1)
     * [1]     = 状态/手势
     * [2]     = X 坐标低字节
     * [3]     = X 坐标高字节 (0=单字节模式)
     * [4]     = Y 坐标低字节
     * [5]     = Y 坐标高字节 (0=单字节模式)
     *
     * 坐标范围: 0~4095 (12位) 映射到屏幕像素
     */
    uint8_t buf[6] = {0};
    esp_err_t ret = i2c_master_transmit_receive(dev_, &REG_STATUS, 1, buf, sizeof(buf), 100);
    if (ret != ESP_OK) {
        return; /* I2C 读取失败, 静默跳过 */
    }

    uint8_t touch_count = buf[0];

    if (touch_count == 0) {
        touched_ = false;
        return;
    }

    /* 尝试双字节坐标: 若高字节非零则使用双字节, 否则用单字节+线性缩放 */
    uint16_t raw_x, raw_y;
    if (buf[3] != 0 || buf[5] != 0) {
        raw_x = ((uint16_t)buf[3] << 8) | buf[2];
        raw_y = ((uint16_t)buf[5] << 8) | buf[4];
    } else {
        raw_x = buf[2];
        raw_y = buf[4];
        /* 单字节 0~255 映射到屏幕宽度, 屏幕 <256px 时直接使用 */
        if (width_ > 255) raw_x = (uint16_t)buf[2] * width_ / 255;
        if (height_ > 255) raw_y = (uint16_t)buf[4] * height_ / 255;
    }

    /* 映射到屏幕坐标系 */
    point_.x = (lv_coord_t)((uint32_t)raw_x * width_ / 4096);
    point_.y = (lv_coord_t)((uint32_t)raw_y * height_ / 4096);

    /* 边界钳制 */
    if (point_.x < 0) point_.x = 0;
    if (point_.x >= width_) point_.x = width_ - 1;
    if (point_.y < 0) point_.y = 0;
    if (point_.y >= height_) point_.y = height_ - 1;

    touched_ = true;
}

/* -- 静态回调 ------------------------------------------------------------- */

void Chsc5432Touch::TimerCallback(void* arg) {
    static_cast<Chsc5432Touch*>(arg)->OnTimer();
}

void Chsc5432Touch::LvglReadCallback(lv_indev_t* indev, lv_indev_data_t* data) {
    auto* self = static_cast<Chsc5432Touch*>(lv_indev_get_user_data(indev));
    if (self->touched_) {
        data->point = self->point_;
        data->state = LV_INDEV_STATE_PRESSED;
        self->touched_ = false; /* 一次性报告, 避免重复 */
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}
