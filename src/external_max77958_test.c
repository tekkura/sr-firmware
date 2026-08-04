#ifdef EXTERNAL_MAX77958_TEST

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "hardware/i2c.h"

#include "external_max77958_test.h"
#include "max77958.h"
#include "robot.h"
#include "rp2040_log.h"

#define EXT_MAX77958_ADDR 0x25
#define EXT_MAX77958_OPCODE_WRITE 0x21
#define EXT_MAX77958_OPCODE_READ 0x51
#define EXT_MAX77958_OPCODE_GPIO_READ 0x23
#define EXT_MAX77958_OPCODE_GPIO_WRITE 0x24
#define EXT_MAX77958_OPCODE_CC_CTRL1_READ 0x0B
#define EXT_MAX77958_OPCODE_CC_CTRL1_WRITE 0x0C
#define EXT_MAX77958_OPCODE_CUSTOMER_CONFIG_WRITE 0x56
#define EXT_MAX77958_STATUS_POLL_MS 4000
#define EXT_MAX77958_STATUS_INTERVAL_MS 250

static bool external_max77958_i2c1_read_register(uint8_t reg, uint8_t *dst, size_t len);
static bool external_max77958_i2c1_opcode_write(const uint8_t *command);
static bool external_max77958_i2c1_opcode_read(uint8_t *response, size_t len);
static bool external_max77958_i2c1_write_customer_config(void);
static bool external_max77958_i2c1_write_cc_ctrl1_src(void);
static bool external_max77958_i2c1_read_cc_ctrl1(void);
static bool external_max77958_i2c1_set_gpio(bool gpio4, bool gpio5);
static void external_max77958_i2c1_log_status(uint32_t elapsed_ms);

void external_max77958_i2c1_test(void)
{
    uint8_t id[2] = {0x00, 0x00};
    bool vbus_enabled = false;

    rp2040_log("EXT_MAX77958_I2C1_TEST: begin addr=0x%02x sda=%u scl=%u\n",
                EXT_MAX77958_ADDR, I2C_SDA1, I2C_SCL1);

    if (!external_max77958_i2c1_read_register(0x00, id, sizeof id)) {
        rp2040_log("EXT_MAX77958_I2C1_TEST: end\n");
        return;
    }

    rp2040_log("EXT_MAX77958_I2C1_TEST: DEVICE_ID=0x%02x DEVICE_REV=0x%02x %s\n",
                id[0], id[1], (id[0] == 0x58 && id[1] == 0x02) ? "PASS" : "FAIL");

    if (id[0] != 0x58 || id[1] != 0x02) {
        rp2040_log("EXT_MAX77958_I2C1_TEST: end\n");
        return;
    }

    external_max77958_i2c1_log_status(0);

    rp2040_log("EXT_MAX77958_I2C1_TEST: forcing GPIO4/GPIO5 low before source config\n");
    if (!external_max77958_i2c1_set_gpio(false, false)) {
        rp2040_log("EXT_MAX77958_I2C1_TEST: end\n");
        return;
    }
    sleep_ms(100);
    external_max77958_i2c1_log_status(100);

    if (!external_max77958_i2c1_write_customer_config()) {
        rp2040_log("EXT_MAX77958_I2C1_TEST: end\n");
        return;
    }
    sleep_ms(100);

    if (!external_max77958_i2c1_write_cc_ctrl1_src()) {
        rp2040_log("EXT_MAX77958_I2C1_TEST: end\n");
        return;
    }
    sleep_ms(100);

    external_max77958_i2c1_read_cc_ctrl1();

    for (uint32_t elapsed_ms = 0;
         elapsed_ms <= EXT_MAX77958_STATUS_POLL_MS;
         elapsed_ms += EXT_MAX77958_STATUS_INTERVAL_MS) {
        uint8_t cc_status0 = 0;

        external_max77958_i2c1_log_status(elapsed_ms);
        if (!vbus_enabled &&
            external_max77958_i2c1_read_register(0x0C, &cc_status0, 1) &&
            (cc_status0 & 0x07) == 0x02) {
            rp2040_log("EXT_MAX77958_I2C1_TEST: source attach detected, enabling GPIO4/GPIO5\n");
            if (external_max77958_i2c1_set_gpio(true, true)) {
                vbus_enabled = true;
            }
        }

        if (elapsed_ms == EXT_MAX77958_STATUS_POLL_MS) {
            break;
        }
        sleep_ms(EXT_MAX77958_STATUS_INTERVAL_MS);
    }

    rp2040_log("EXT_MAX77958_I2C1_TEST: vbus_enabled=%u\n", vbus_enabled ? 1 : 0);
    rp2040_log("EXT_MAX77958_I2C1_TEST: end\n");
}

static bool external_max77958_i2c1_read_register(uint8_t reg, uint8_t *dst, size_t len)
{
    int write_result = i2c_write_timeout_us(i2c1, EXT_MAX77958_ADDR, &reg, 1, true, I2C_TIMEOUT);
    if (write_result != 1) {
        rp2040_log("EXT_MAX77958_I2C1_TEST: FAIL reg=0x%02x write_result=%d\n", reg, write_result);
        return false;
    }

    int read_result = i2c_read_timeout_us(i2c1, EXT_MAX77958_ADDR, dst, len, false, I2C_TIMEOUT);
    if (read_result != (int)len) {
        rp2040_log("EXT_MAX77958_I2C1_TEST: FAIL reg=0x%02x read_result=%d expected=%u\n",
                    reg, read_result, (unsigned)len);
        return false;
    }

    return true;
}

static bool external_max77958_i2c1_opcode_write(const uint8_t *command)
{
    uint8_t write_end[] = {0x41, 0x00};
    int write_result = i2c_write_timeout_us(i2c1, EXT_MAX77958_ADDR, command, 33, false, I2C_TIMEOUT);
    if (write_result != 33) {
        rp2040_log("EXT_MAX77958_I2C1_TEST: FAIL opcode=0x%02x write_result=%d\n",
                    command[1], write_result);
        return false;
    }

    int end_result = i2c_write_timeout_us(i2c1, EXT_MAX77958_ADDR, write_end, sizeof write_end, false, I2C_TIMEOUT);
    if (end_result != (int)sizeof write_end) {
        rp2040_log("EXT_MAX77958_I2C1_TEST: FAIL opcode=0x%02x end_result=%d\n",
                    command[1], end_result);
        return false;
    }

    return true;
}

static bool external_max77958_i2c1_opcode_read(uint8_t *response, size_t len)
{
    uint8_t reg = EXT_MAX77958_OPCODE_READ;
    int write_result = i2c_write_timeout_us(i2c1, EXT_MAX77958_ADDR, &reg, 1, true, I2C_TIMEOUT);
    if (write_result != 1) {
        rp2040_log("EXT_MAX77958_I2C1_TEST: FAIL opcode read pointer write_result=%d\n", write_result);
        return false;
    }

    int read_result = i2c_read_timeout_us(i2c1, EXT_MAX77958_ADDR, response, len, false, I2C_TIMEOUT);
    if (read_result != (int)len) {
        rp2040_log("EXT_MAX77958_I2C1_TEST: FAIL opcode read_result=%d expected=%u\n",
                    read_result, (unsigned)len);
        return false;
    }

    return true;
}

static bool external_max77958_i2c1_write_customer_config(void)
{
    uint8_t command[33] = {0};
    max77958_customer_config_t config = {
        .dbg_src_enable = false,
        .dbg_snk_enable = false,
        .audio_acc_enable = false,
        .trysnk_enable = false,
        .typec_mode = TYPEC_MODE_SRC,
        .mem_update_customer = false,
        .moisture_enable = false
    };

    command[0] = EXT_MAX77958_OPCODE_WRITE;
    command[1] = EXT_MAX77958_OPCODE_CUSTOMER_CONFIG_WRITE;
    command[2] = max77958_build_customer_config_value(&config);
    command[3] = 0x6A;
    command[4] = 0x0B;
    command[5] = 0x60;
    command[6] = 0x68;
    command[7] = 0x00;
    command[8] = 0x64;
    command[9] = 0x00;
    command[10] = 0x96;
    command[11] = 0x00;
    command[21] = 0x69;
    command[22] = 0x69;
    command[23] = 0x35;
    command[24] = 0x28;

    rp2040_log("EXT_MAX77958_I2C1_TEST: writing source customer config=0x%02x\n", command[2]);
    return external_max77958_i2c1_opcode_write(command);
}

static bool external_max77958_i2c1_write_cc_ctrl1_src(void)
{
    uint8_t command[33] = {0};

    command[0] = EXT_MAX77958_OPCODE_WRITE;
    command[1] = EXT_MAX77958_OPCODE_CC_CTRL1_WRITE;
    command[2] = 0x82;

    rp2040_log("EXT_MAX77958_I2C1_TEST: writing CC_CTRL1=0x82\n");
    return external_max77958_i2c1_opcode_write(command);
}

static bool external_max77958_i2c1_read_cc_ctrl1(void)
{
    uint8_t command[33] = {0};
    uint8_t response[33] = {0};

    command[0] = EXT_MAX77958_OPCODE_WRITE;
    command[1] = EXT_MAX77958_OPCODE_CC_CTRL1_READ;
    if (!external_max77958_i2c1_opcode_write(command)) {
        return false;
    }

    sleep_ms(100);
    if (!external_max77958_i2c1_opcode_read(response, sizeof response)) {
        return false;
    }

    rp2040_log("EXT_MAX77958_I2C1_TEST: CC_CTRL1 response opcode=0x%02x value=0x%02x\n",
                response[0], response[1]);
    return response[0] == EXT_MAX77958_OPCODE_CC_CTRL1_READ;
}

static bool external_max77958_i2c1_set_gpio(bool gpio4, bool gpio5)
{
    uint8_t command[33] = {0};
    uint8_t gpio_val = (gpio5 ? 0x08 : 0x00) | 0x04 | (gpio4 ? 0x02 : 0x00) | 0x01;

    command[0] = EXT_MAX77958_OPCODE_WRITE;
    command[1] = EXT_MAX77958_OPCODE_GPIO_WRITE;
    command[2] = 0x00;
    command[3] = gpio_val;

    rp2040_log("EXT_MAX77958_I2C1_TEST: writing GPIO4=%u GPIO5=%u GPIO53=0x%02x\n",
                gpio4 ? 1 : 0, gpio5 ? 1 : 0, gpio_val);
    return external_max77958_i2c1_opcode_write(command);
}

static void external_max77958_i2c1_log_status(uint32_t elapsed_ms)
{
    uint8_t status[8] = {0};
    uint8_t gpio_command[33] = {0};
    uint8_t gpio_response[33] = {0};

    if (!external_max77958_i2c1_read_register(0x08, status, sizeof status)) {
        return;
    }

    uint8_t cc_status0 = status[4];
    uint8_t cc_status1 = status[5];
    uint8_t pd_status0 = status[6];
    uint8_t pd_status1 = status[7];
    rp2040_log("EXT_MAX77958_I2C1_TEST t=%lums USBC1=0x%02x SYS=0x%02x BC=0x%02x CC0=0x%02x pin=%u current=%u state=%u CC1=0x%02x wtr=%u detabrt=%u vsafeov=%u PD0=0x%02x PD1=0x%02x power=%u data=%u psrdy=%u\n",
                (unsigned long)elapsed_ms,
                status[0], status[1], status[2],
                cc_status0, (cc_status0 >> 6) & 0x03, (cc_status0 >> 4) & 0x03, cc_status0 & 0x07,
                cc_status1, (cc_status1 >> 1) & 0x01, (cc_status1 >> 2) & 0x01, (cc_status1 >> 3) & 0x01,
                pd_status0, pd_status1, (pd_status1 >> 6) & 0x01, (pd_status1 >> 7) & 0x01,
                (pd_status1 >> 4) & 0x01);

    gpio_command[0] = EXT_MAX77958_OPCODE_WRITE;
    gpio_command[1] = EXT_MAX77958_OPCODE_GPIO_READ;
    if (!external_max77958_i2c1_opcode_write(gpio_command)) {
        return;
    }

    sleep_ms(20);
    if (!external_max77958_i2c1_opcode_read(gpio_response, sizeof gpio_response)) {
        return;
    }

    rp2040_log("EXT_MAX77958_I2C1_TEST t=%lums GPIO opcode=0x%02x GPIO03=0x%02x GPIO47=0x%02x GPIO8=0x%02x\n",
                (unsigned long)elapsed_ms, gpio_response[0], gpio_response[1],
                gpio_response[2], gpio_response[3]);
}

#endif
