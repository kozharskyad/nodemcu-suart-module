// suart - software UART module

#include "lualib.h" /* for lua_* */
#include "lauxlib.h" /* for luaL_* */
#include "platform.h" /* for platform_gpio_* & PLATFORM_GPIO_* */
#include "module.h" /* for NODEMCU_MODULE() */
#include "c_types.h" /* for uint*_t */
#include "c_stdlib.h" /* for c_malloc() */
#include "c_string.h" /* for c_strcmp() */
#include "user_interface.h" /* for system_get_time() */

#define BUFFER_SIZE 128
#define WAIT { while((system_get_time()) < (st + (SUART_BIT_TIME * ii))); ii++; }
#define INTERRUPT_TYPE_IS_LEVEL(x) ((x) >= GPIO_PIN_INTR_LOLEVEL)

static uint8_t SUART_RX_PIN;
static uint8_t SUART_TX_PIN;
static uint32_t SUART_BAUD_RATE;
static uint16_t SUART_BUFF_SIZE;
static uint32_t SUART_BIT_TIME;

static uint8_t terminator = 0;
static uint8_t buffer[BUFFER_SIZE];
static uint8_t buffer_pos = 0;

typedef struct {
	uint32_t gpio_mux_name;
	uint8_t gpio_func;
} suart_reg_t;

suart_reg_t suart_reg[] =
{
	{ PERIPHS_IO_MUX_GPIO0_U, FUNC_GPIO0 },
	{ PERIPHS_IO_MUX_U0TXD_U, FUNC_GPIO1 },
	{ PERIPHS_IO_MUX_GPIO2_U, FUNC_GPIO2 },
	{ PERIPHS_IO_MUX_U0RXD_U, FUNC_GPIO3 },
	{ PERIPHS_IO_MUX_GPIO4_U, FUNC_GPIO4 },
	{ PERIPHS_IO_MUX_GPIO5_U, FUNC_GPIO5 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
	{ PERIPHS_IO_MUX_MTDI_U, FUNC_GPIO12 },
	{ PERIPHS_IO_MUX_MTCK_U, FUNC_GPIO13 },
	{ PERIPHS_IO_MUX_MTMS_U, FUNC_GPIO14 },
	{ PERIPHS_IO_MUX_MTDO_U, FUNC_GPIO15 },
};

static int suart_recv_cb_ref = LUA_NOREF;

static void suart_recv_cb(uint8_t *str)
{
    if(suart_recv_cb_ref == LUA_NOREF)
        return;

    lua_State *L = lua_getstate();
    lua_rawgeti(L, LUA_REGISTRYINDEX, suart_recv_cb_ref);
    lua_pushstring(L, str);
    lua_call(L, 1, 0);
}

static void rx_intr_handler(void *arg)
{
    uint32_t st = system_get_time();
    last_recv_time = st;
    uint8_t i;
    uint8_t ii = 1;
    platform_gpio_intr_init(SUART_RX_PIN, GPIO_PIN_INTR_DISABLE);
    
    uint8_t gpio_id = pin_num[SUART_RX_PIN];
    uint8_t lv = GPIO_INPUT_GET(GPIO_ID_PIN(gpio_id));
    uint32_t b = 0;

    if(lv == PLATFORM_GPIO_LOW) {
        os_delay_us(SUART_BIT_TIME / 3);
        st = system_get_time();

        for(i = 0; i < 8; i++) {
            WAIT;
            b >>= 1;

            if(GPIO_INPUT_GET(GPIO_ID_PIN(gpio_id)) == PLATFORM_GPIO_HIGH)
                b |= 0x80;
        }
    }
    
    if(b) {
        buffer[buffer_pos] = b;
        buffer_pos++;
        buffer[buffer_pos] = 0;
    }

    WAIT;
    
    platform_gpio_intr_init(SUART_RX_PIN, GPIO_PIN_INTR_LOLEVEL);

    if(buffer_pos == (BUFFER_SIZE - 1)) {
        suart_recv_cb(buffer);
        buffer_pos = 0;
        buffer[buffer_pos] = 0;
    }

    if(terminator && b == terminator) {
        buffer[buffer_pos] = 0;
        suart_recv_cb(buffer);
        buffer_pos = 0;
        buffer[buffer_pos] = 0;
    }
}

static void ICACHE_RAM_ATTR suart_setup(uint8_t rx_pin, uint8_t tx_pin, uint32_t baud_rate, uint16_t buff_size)
{
    SUART_RX_PIN = rx_pin;
    SUART_TX_PIN = tx_pin;
    SUART_BAUD_RATE = baud_rate;
    SUART_BUFF_SIZE = buff_size;
    SUART_BIT_TIME = 1000000 / SUART_BAUD_RATE;

    if(((100000000 / SUART_BAUD_RATE) - (100 * SUART_BIT_TIME)) > 50)
        SUART_BIT_TIME++;

    PIN_FUNC_SELECT(suart_reg[GPIO_ID_PIN(pin_num[SUART_RX_PIN])].gpio_mux_name, suart_reg[GPIO_ID_PIN(pin_num[SUART_RX_PIN])].gpio_func);
    ETS_GPIO_INTR_DISABLE();
    platform_gpio_mode(SUART_RX_PIN, PLATFORM_GPIO_INT, PLATFORM_GPIO_PULLUP);
    ETS_GPIO_INTR_ATTACH(rx_intr_handler, GPIO_ID_PIN(pin_num[SUART_TX_PIN]));
    gpio_pin_intr_state_set(GPIO_ID_PIN(pin_num[SUART_RX_PIN]), GPIO_PIN_INTR_LOLEVEL);
    ETS_GPIO_INTR_ENABLE();
    platform_gpio_mode(SUART_TX_PIN, PLATFORM_GPIO_OUTPUT, PLATFORM_GPIO_PULLUP);
    platform_gpio_write(SUART_TX_PIN, PLATFORM_GPIO_HIGH);
}

static int suart_l_setup(lua_State *L)
{
    uint8_t rx_pin = (uint8_t)luaL_checkinteger(L, 1);
    uint8_t tx_pin = (uint8_t)luaL_checkinteger(L, 2);
    uint32_t baud_rate = (uint32_t)luaL_optinteger(L, 3, 9600);
    uint16_t buff_size = (uint16_t)luaL_optinteger(L, 4, 128);
    suart_setup(rx_pin, tx_pin, baud_rate, buff_size);

    return 0;
}

static int suart_l_on(lua_State *L)
{
    char *type = (char*)luaL_checkstring(L, 1);
    uint8_t *terminator_str = (uint8_t*)luaL_checkstring(L, 2);
    terminator = terminator_str[0];
    bool func = false;

    if(lua_type(L, 3) == LUA_TFUNCTION || lua_type(L, 3) == LUA_TLIGHTFUNCTION) {
        func = true;
        lua_pushvalue(L, 3);
    }

    if(c_strcmp(type, "data") == 0) {
        if(suart_recv_cb_ref != LUA_NOREF) {
            luaL_unref(L, LUA_REGISTRYINDEX, suart_recv_cb_ref);
            suart_recv_cb_ref = LUA_NOREF;
        }

        if(func)
            suart_recv_cb_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }

    return 0;
}

static void ICACHE_RAM_ATTR suart_write(uint8_t b, bool intr)
{
    if(!intr)
        ets_intr_lock();

    uint8_t ii = 1;
    uint32_t st = system_get_time();

    platform_gpio_write(SUART_TX_PIN, PLATFORM_GPIO_LOW);
    WAIT;

    for(uint8_t i = 8; i > 0; --i) {
        if(b & 1)
            platform_gpio_write(SUART_TX_PIN, PLATFORM_GPIO_HIGH);
        else
            platform_gpio_write(SUART_TX_PIN, PLATFORM_GPIO_LOW);

        WAIT;
        b >>= 1;
    }

    WAIT;
    platform_gpio_write(SUART_TX_PIN, 1);
    WAIT;

    if(!intr)
        ets_intr_unlock();
}

static int suart_l_write(lua_State *L)
{
    uint8_t *str = (uint8_t*)luaL_checkstring(L, 1);
    suart_write(str[0], true);

    return 0;
}

static void ICACHE_RAM_ATTR suart_send(uint8_t *str)
{
    while(*str)
        suart_write(*str++, false);
}

static int suart_l_send(lua_State *L)
{
    uint8_t *str = (uint8_t*)luaL_checkstring(L, 1);
    suart_send(str);
    return 0;
}

static int suart_l_get_buffer(lua_State *L)
{
    lua_pushstring(L, buffer);
    return 1;
}

static const LUA_REG_TYPE suart_map[] = {
    { LSTRKEY("setup"), LFUNCVAL(suart_l_setup) },
    { LSTRKEY("on"), LFUNCVAL(suart_l_on) },
    { LSTRKEY("write"), LFUNCVAL(suart_l_write) },
    { LSTRKEY("send"), LFUNCVAL(suart_l_send) },
    { LSTRKEY("get_buffer"), LFUNCVAL(suart_l_get_buffer) },
    { LNILKEY, LNILVAL }
};

NODEMCU_MODULE(SUART, "suart", suart_map, NULL);
