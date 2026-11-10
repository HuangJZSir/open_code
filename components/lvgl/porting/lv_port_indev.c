/**
 * @file lv_port_indev_templ.c
 *
 */

 /*Copy this file as "lv_port_indev.c" and set this value to "1" to enable content*/
#if 1

/*********************
 *      INCLUDES
 *********************/
#include "lv_port_indev.h"
#include "../../lvgl.h"

#include "driver/gpio.h"
#include "esp_attr.h"
#include "bsp_board.h"
#include "bsp_i2c.h"
#include <stdio.h>

/*********************
 *      DEFINES
 *********************/
static const char *TAG = "lv_port_indev";

/* Touch controller polling.
 *
 * The panel's INT line (GPIO_TOUCH_INT) tells us when the controller has new
 * data, so in the ideal case the I2C bus is only touched on an edge. Two guarded
 * fallback polls keep finger tracking and - more importantly - the release event
 * correct if an edge is missed, and they are what makes a tap deterministic when
 * the INT line is not wired or not pulled up:
 *   - while pressed   : poll every TOUCH_POLL_ACTIVE_MS
 *   - while released  : poll every TOUCH_POLL_IDLE_MS
 *
 * Both intervals are 10 ms, i.e. every LVGL indev read (lv_conf.h:
 * LV_INDEV_DEF_READ_PERIOD 10) talks to the controller. That matters because a
 * tap is only visible to LVGL as press-then-release: if the release is sampled
 * 30 ms late and the next tap starts inside that window, LVGL still sees one
 * continuous press and the second tap is swallowed - the "I have to tap twice"
 * symptom. One 5-byte I2C transaction is ~0.15 ms, so 100 Hz costs well under
 * 2 % CPU and buys deterministic taps. */
#define TOUCH_POLL_ACTIVE_MS 10u
#define TOUCH_POLL_IDLE_MS   10u

/* lv_tick is 1 ms and LVGL reads the pointer every 10 ms (lv_conf.h:
 * LV_INDEV_DEF_READ_PERIOD). Without this tolerance a read that lands 1-2 ms
 * early would be skipped, doubling the effective sampling interval and merging
 * two fast taps into one continuous press. */
#define TOUCH_POLL_TOLERANCE_MS 2u

static volatile bool touch_irq_pending;
static volatile uint32_t touch_irq_count;      /* edges seen on the INT line */
static uint32_t touch_fallback_polls;          /* reads done without an interrupt pending */
static bool touch_int_warning_logged;
static uint32_t touch_next_poll_ms;
static lv_indev_state_t touch_last_state = LV_INDEV_STATE_REL;

/* Application hooks. Registered from main.c (see lv_port_indev.h for why they
 * are not weak symbols). */
static lv_port_indev_tap_cb_t tap_cb;
static lv_port_indev_activity_cb_t activity_cb;

void lv_port_indev_set_tap_cb(lv_port_indev_tap_cb_t cb)
{
    tap_cb = cb;
}

void lv_port_indev_set_activity_cb(lv_port_indev_activity_cb_t cb)
{
    activity_cb = cb;
}


esp_err_t ctpfalg=-1;


/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void touchpad_init(void);
static void touchpad_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data);
static bool touchpad_is_pressed(void);
static void touchpad_get_xy(lv_coord_t * x, lv_coord_t * y);

static void mouse_init(void);
static void mouse_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data);
static bool mouse_is_pressed(void);
static void mouse_get_xy(lv_coord_t * x, lv_coord_t * y);

static void keypad_init(void);
static void keypad_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data);
static uint32_t keypad_get_key(void);

static void encoder_init(void);
static void encoder_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data);
static void encoder_handler(void);

static void button_init(void);
static void button_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data);
static int8_t button_get_pressed_id(void);
static bool button_is_pressed(uint8_t id);

/**********************
 *  STATIC VARIABLES
 **********************/
lv_indev_t * indev_touchpad;
lv_indev_t * indev_mouse;
lv_indev_t * indev_keypad;
lv_indev_t * indev_encoder;
lv_indev_t * indev_button;

static int32_t encoder_diff;
static lv_indev_state_t encoder_state;

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_port_indev_init(void)
{
    /**
     * Here you will find example implementation of input devices supported by LittelvGL:
     *  - Touchpad
     *  - Mouse (with cursor support)
     *  - Keypad (supports GUI usage only with key)
     *  - Encoder (supports GUI usage only with: left, right, push)
     *  - Button (external buttons to press points on the screen)
     *
     *  The `..._read()` function are only examples.
     *  You should shape them according to your hardware
     */

    static lv_indev_drv_t indev_drv;

    /*------------------
     * Touchpad
     * -----------------*/

    /*Initialize your touchpad if you have*/
    touchpad_init();

    /*Register a touchpad input device*/
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touchpad_read;
    indev_touchpad = lv_indev_drv_register(&indev_drv);
    
#if 0
    /*------------------
     * Button
     * -----------------*/

    /*Initialize your button if you have*/
    button_init();

    /*Register a button input device*/
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_BUTTON;
    indev_drv.read_cb = button_read;
    indev_button = lv_indev_drv_register(&indev_drv);

    /*Assign buttons to points on the screen*/
    static const lv_point_t btn_points[2] = {
            {10, 10},   /*Button 0 -> x:10; y:10*/
            {40, 100},  /*Button 1 -> x:40; y:100*/
    };
    lv_indev_set_button_points(indev_button, btn_points);

    /*------------------
     * Mouse
     * -----------------*/

    /*Initialize your mouse if you have*/
    mouse_init();

    /*Register a mouse input device*/
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = mouse_read;
    indev_mouse = lv_indev_drv_register(&indev_drv);

    /*Set cursor. For simplicity set a HOME symbol now.*/
    lv_obj_t * mouse_cursor = lv_img_create(lv_scr_act());
    lv_img_set_src(mouse_cursor, LV_SYMBOL_HOME);
    lv_indev_set_cursor(indev_mouse, mouse_cursor);

    /*------------------
     * Keypad
     * -----------------*/

    /*Initialize your keypad or keyboard if you have*/
    keypad_init();

    /*Register a keypad input device*/
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_KEYPAD;
    indev_drv.read_cb = keypad_read;
    indev_keypad = lv_indev_drv_register(&indev_drv);

    /*Later you should create group(s) with `lv_group_t * group = lv_group_create()`,
     *add objects to the group with `lv_group_add_obj(group, obj)`
     *and assign this input device to group to navigate in it:
     *`lv_indev_set_group(indev_keypad, group);`*/

    /*------------------
     * Encoder
     * -----------------*/

    /*Initialize your encoder if you have*/
    encoder_init();

    /*Register a encoder input device*/
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_ENCODER;
    indev_drv.read_cb = encoder_read;
    indev_encoder = lv_indev_drv_register(&indev_drv);

    /*Later you should create group(s) with `lv_group_t * group = lv_group_create()`,
     *add objects to the group with `lv_group_add_obj(group, obj)`
     *and assign this input device to group to navigate in it:
     *`lv_indev_set_group(indev_encoder, group);`*/
#endif
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/*------------------
 * Touchpad
 * -----------------*/

/* GPIO ISR: the controller pulled its INT line, new data is waiting. */
static void IRAM_ATTR touch_int_isr(void *arg)
{
	(void)arg;
	touch_irq_pending = true;
	touch_irq_count++;
}

/* Configure the touch INT GPIO and hook up its interrupt. */
static void touch_interrupt_init(void)
{
#ifdef GPIO_TOUCH_INT
	const int int_pin = (int)GPIO_TOUCH_INT;
	gpio_config_t io_conf = {
		.pin_bit_mask = 0,
		.mode = GPIO_MODE_INPUT,
		.pull_up_en = GPIO_PULLUP_ENABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.intr_type = GPIO_INTR_ANYEDGE,
	};
	esp_err_t err;

	if (int_pin < 0) {
		ESP_LOGW(TAG, "touch INT not wired, touch stays in polling mode");
		return;
	}

	io_conf.pin_bit_mask = 1ULL << int_pin;

	if (gpio_config(&io_conf) != ESP_OK) {
		ESP_LOGW(TAG, "touch INT gpio config failed, staying in polling mode");
		return;
	}

	err = gpio_install_isr_service(0);
	if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
		ESP_LOGW(TAG, "ISR service unavailable (%d), staying in polling mode", (int)err);
		return;
	}

	if (gpio_isr_handler_add((gpio_num_t)int_pin, touch_int_isr, NULL) != ESP_OK) {
		ESP_LOGW(TAG, "touch INT handler add failed, staying in polling mode");
		return;
	}

	touch_irq_pending = true;   /* force one read at start-up */
	ESP_LOGI(TAG, "touch in INTERRUPT mode: INT=GPIO%d (idle level=%d, internal pull-up)",
	         int_pin, gpio_get_level((gpio_num_t)int_pin));
#else
	ESP_LOGW(TAG, "no touch INT pin defined, touch stays in polling mode");
#endif
}

/*Initialize your touchpad*/
static void touchpad_init(void)
{
	uint8_t addrs[16];
	uint8_t found;

	if(gt911_init() == ESP_OK)
	{
		ctpfalg=GT911;
		ESP_LOGI(TAG, "touch controller: GT911");
	}
	else if(CST3240_init() == ESP_OK)
	{
		ctpfalg=CST3240;
		ESP_LOGI(TAG, "touch controller: CST3240");
	}
	else if(FT62XX_init() == ESP_OK)
	{
		ctpfalg=FT62XX;
		ESP_LOGI(TAG, "touch controller: FT62XX");
	}
	else
	{
		ESP_LOGE(TAG, "no touch controller detected - touch is disabled");
		found = bsp_i2c_scan(addrs, (uint8_t)sizeof(addrs));
		if(found > 0)
		{
			char line[96];
			int off = 0;
			uint8_t n;

			line[0] = 0;
			for(n = 0; n < found && off < (int)sizeof(line) - 5; n++)
			{
				off += snprintf(line + off, sizeof(line) - off, "0x%02X ", addrs[n]);
			}
			ESP_LOGE(TAG, "I2C devices on the bus: %s", line);
		}
		else
		{
			ESP_LOGE(TAG, "I2C bus is empty - check SDA/SCL and the touch connector");
		}
	}

	touch_interrupt_init();
}

/*Will be called by the library to read the touchpad*/
static void touchpad_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data)
{
    static lv_coord_t last_x = 0;
    static lv_coord_t last_y = 0;
    uint32_t now = lv_tick_get();
    bool should_poll;

    (void)indev_drv;

    /* Interrupt driven: only talk to the controller when its INT line fired or
     * when the fallback poll timer elapsed. */
    should_poll = touch_irq_pending ||
                  ((int32_t)(now - touch_next_poll_ms) >= -(int32_t)TOUCH_POLL_TOLERANCE_MS);

    if(should_poll) {
        if (!touch_irq_pending) {
            touch_fallback_polls++;
            if (!touch_int_warning_logged && touch_irq_count == 0 && touch_fallback_polls > 100) {
                touch_int_warning_logged = true;
                ESP_LOGW(TAG, "touch INT never fired - check the INT pull-up (4.7k-10k to 3V3) "
                              "and the wiring; falling back to periodic polling");
            }
        }
        touch_irq_pending = false;

        if(touchpad_is_pressed()) {
            bool pressed_now = (touch_last_state == LV_INDEV_STATE_REL);

            if (activity_cb != NULL) {
                activity_cb();
            }
            touchpad_get_xy(&last_x, &last_y);
            touch_last_state = LV_INDEV_STATE_PR;
            touch_next_poll_ms = now + TOUCH_POLL_ACTIVE_MS;

            /* Finger just went down: report the point so the application can
             * decide whether it landed on a control (and only then beep). */
            if(pressed_now) {
                if (tap_cb != NULL) {
                    tap_cb(last_x, last_y);
                }
            }
        } else {
            touch_last_state = LV_INDEV_STATE_REL;
            touch_next_poll_ms = now + TOUCH_POLL_IDLE_MS;
        }
    }

    data->state = touch_last_state;
    data->point.x = last_x;
    data->point.y = last_y;
}

/*Return true is the touchpad is pressed*/
static bool touchpad_is_pressed(void)
{
	uint8_t temp = 0;

	if(ctpfalg==GT911)
	{
	    gt911_read_bytes(GT_GSTID_REG, 1, &temp);

		//////]ESP_LOGI(TAG, "temp=0x%X",temp);
		 
	    if (temp & 0x80) {
	        if(temp & 0x0F){
	            return true;            
	        }
	        temp = 0;
	        gt911_write_bytes(GT_GSTID_REG, 1, &temp);
	    }
	    return false;
	}
	else if(ctpfalg==FT62XX)
	{
	    FT62XX_read_bytes(FT_REG_NUM_FINGER, 1, &temp);
	    if ((temp&0XF)&&((temp&0XF)<6)) {
	        if(temp&0XF){
	            return true;            
	        }
	    }
	    return false;
	}
	else if(ctpfalg==CST3240)
	{
		
		CST3240_read_bytes(CST_GSTID_REG, 1, &temp);
		//ESP_LOGI(TAG, "temp=0x%X",temp);
		
		if((temp&0XF)&&((temp&0XF)<6)) {
			if(temp&0XF){
				return true;			
			}
		}
		return false;
	}
	return false;

}

/*Get the x and y coordinates if the touchpad is pressed*/
static void touchpad_get_xy(lv_coord_t * x, lv_coord_t * y)
{
	if(ctpfalg==GT911)
	{
	    uint8_t data[4];

	    gt911_read_bytes(GT_TP1_REG, 4, data);
	    *x = ((data[1] & 0x0f) << 8) + data[0];
	    *y = ((data[3] & 0x0f) << 8) + data[2];
	    //ESP_LOGI(TAG, "x=%d, y=%d",*x, *y);

	    data[0] = 0;
	    gt911_write_bytes(GT_GSTID_REG, 1, data);
	}
	else if(ctpfalg==FT62XX)
	{

	    uint8_t buf[4];
		uint16_t ctpx[5]; 		 
		uint16_t xtpy[5];

		//璇诲彇XY鍧愭爣鍊?
		FT62XX_read_bytes(0x03,1,&buf[0]);
		xtpy[0] = (int16_t)(buf[0] & 0x0F)<<8;
		FT62XX_read_bytes(0x04,1,&buf[1]);
		xtpy[0]|= (int16_t)buf[1];
		
		FT62XX_read_bytes(0x05,1,&buf[2]);
		ctpx[0] = (int16_t)(buf[2] & 0x0F)<<8;
		FT62XX_read_bytes(0x06,1,&buf[3]);
		ctpx[0]|= (int16_t)buf[3];
			
	    *y = ctpx[0];
	    *x = xtpy[0];
				
	    //ESP_LOGI(TAG, "x=0x%X, y=%d",*x, *y);
	}
	else if(ctpfalg==CST3240)
	{
	    static uint8_t data[4];

	    CST3240_read_bytes(CST_TP1_REG, 4, data);
		if(1)	//绔栧睆
		{
			*x=((uint16_t)data[0]<<4)+(((uint16_t)data[2]>>4)&0x0F);
			*y=(((uint16_t)data[1]<<4)+((uint16_t)data[2]&0x0f));
		}else //妯睆
		{
			*y=((uint16_t)data[0]<<4)+(((uint16_t)data[2]>>4)&0x0F);
			*x=(((uint16_t)data[3]<<8)+data[2]);
		} 

		//ESP_LOGI(TAG, "x=%d, y=%d",*x, *y);
		
		data[0] = 0;
	    CST3240_write_bytes(CST_GSTID_REG,1,data);//娓呮爣蹇?	
	}
}

/*------------------
 * Mouse
 * -----------------*/

/*Initialize your mouse*/
static void mouse_init(void)
{
    /*Your code comes here*/
}

/*Will be called by the library to read the mouse*/
static void mouse_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data)
{
    /*Get the current x and y coordinates*/
    mouse_get_xy(&data->point.x, &data->point.y);

    /*Get whether the mouse button is pressed or released*/
    if(mouse_is_pressed()) {
        data->state = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

/*Return true is the mouse button is pressed*/
static bool mouse_is_pressed(void)
{
    /*Your code comes here*/

    return false;
}

/*Get the x and y coordinates if the mouse is pressed*/
static void mouse_get_xy(lv_coord_t * x, lv_coord_t * y)
{
    /*Your code comes here*/

    (*x) = 0;
    (*y) = 0;
}

/*------------------
 * Keypad
 * -----------------*/

/*Initialize your keypad*/
static void keypad_init(void)
{
    /*Your code comes here*/
}

/*Will be called by the library to read the mouse*/
static void keypad_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data)
{
    static uint32_t last_key = 0;

    /*Get the current x and y coordinates*/
    mouse_get_xy(&data->point.x, &data->point.y);

    /*Get whether the a key is pressed and save the pressed key*/
    uint32_t act_key = keypad_get_key();
    if(act_key != 0) {
        data->state = LV_INDEV_STATE_PR;

        /*Translate the keys to LVGL control characters according to your key definitions*/
        switch(act_key) {
        case 1:
            act_key = LV_KEY_NEXT;
            break;
        case 2:
            act_key = LV_KEY_PREV;
            break;
        case 3:
            act_key = LV_KEY_LEFT;
            break;
        case 4:
            act_key = LV_KEY_RIGHT;
            break;
        case 5:
            act_key = LV_KEY_ENTER;
            break;
        }

        last_key = act_key;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }

    data->key = last_key;
}

/*Get the currently being pressed key.  0 if no key is pressed*/
static uint32_t keypad_get_key(void)
{
    /*Your code comes here*/

    return 0;
}

/*------------------
 * Encoder
 * -----------------*/

/*Initialize your keypad*/
static void encoder_init(void)
{
    /*Your code comes here*/
}

/*Will be called by the library to read the encoder*/
static void encoder_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data)
{

    data->enc_diff = encoder_diff;
    data->state = encoder_state;
}

/*Call this function in an interrupt to process encoder events (turn, press)*/
static void encoder_handler(void)
{
    /*Your code comes here*/

    encoder_diff += 0;
    encoder_state = LV_INDEV_STATE_REL;
}

/*------------------
 * Button
 * -----------------*/

/*Initialize your buttons*/
static void button_init(void)
{
    ESP_LOGI(TAG, "init key gpio");
    // gpio_pad_select_gpio(GPIO_KEY);
    // gpio_set_direction(GPIO_KEY, GPIO_MODE_INPUT);
    // gpio_set_pull_mode(GPIO_KEY, GPIO_PULLUP_ONLY);
}

/*Will be called by the library to read the button*/
static void button_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data)
{
    static uint8_t last_btn = 0;

    /*Get the pressed button's ID*/
    int8_t btn_act = button_get_pressed_id();

    if(btn_act >= 0) {
        data->state = LV_INDEV_STATE_PR;
        last_btn = btn_act;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }

    /*Save the last pressed button's ID*/
    data->btn_id = last_btn;

}

/*Get ID  (0, 1, 2 ..) of the pressed button*/
static int8_t button_get_pressed_id(void)
{
    uint8_t i;

    /*Check to buttons see which is being pressed (assume there are 2 buttons)*/
    for(i = 0; i < 2; i++) {
        /*Return the pressed button's ID*/
        if(button_is_pressed(i)) {
            return i;
        }
    }

    /*No button pressed*/
    return -1;
}

/*Test if `id` button is pressed or not*/
static bool button_is_pressed(uint8_t id)
{
    // if((0 == id) && (0 == gpio_get_level(GPIO_KEY)))  return true;

    return false;
}

#else /*Enable this file at the top*/

/*This dummy typedef exists purely to silence -Wpedantic.*/
typedef int keep_pedantic_happy;
#endif
