
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <time.h>
#include <errno.h>
#include <linux/input.h>

#include "lvgl.h"

extern "C" {
  #include "mem.h"
  #include "interp.h"
}


// defined in linux.c
extern bool useLVGL;
extern bool LVGL_initialized;
bool event_seen = false;


#define TFT_WIDTH 640
#define TFT_HEIGHT 480

/* ------------------- USER SETTINGS ------------------- */
#define FB_WIDTH           640
#define FB_HEIGHT          480
#define FB_STRIDE_BYTES    (FB_WIDTH * 3)       /* adjust if padded */
#define FB_BASE_PHYS       0x15a00000ULL        /* CHANGE THIS */
#define MOUSE_EVENT_DEV    "/dev/input/event0"  /* CHANGE THIS */
/* ----------------------------------------------------- */

static volatile bool g_running = true;

/* DDR framebuffer mapped as bytes: B,G,R per pixel */
static uint8_t *g_fb8 = NULL;
static void *g_map_base = NULL;
static size_t g_fb_map_bytes = 0;

/* LVGL draw buffer (we render XRGB8888 and convert to BGR888 in flush) */
static lv_color_t *g_draw_buf = NULL;

/* Mouse state */
static int g_mouse_fd = -1;
static int32_t g_mouse_x = FB_WIDTH / 2;
static int32_t g_mouse_y = FB_HEIGHT / 2;
static bool g_mouse_pressed = false;

/* --------- Tiny cursor image (A8 alpha-only) --------- */
static const uint8_t cursor_map_8x8[] = {
    255,0,0,0,0,0,0,0,
    255,255,0,0,0,0,0,0,
    255,255,255,0,0,0,0,0,
    255,255,255,255,0,0,0,0,
    255,255,255,255,255,0,0,0,
    255,255,255,255,255,255,0,0,
    255,255,255,255,255,255,255,0,
    255,255,255,255,255,255,255,255,
};

static const lv_image_dsc_t cursor_img_8x8 = {
    .header = {
        .cf = LV_COLOR_FORMAT_A8,
        .w = 8,
        .h = 8,
        .stride = 8,
    },
    .data_size = sizeof(cursor_map_8x8),
    .data = cursor_map_8x8,
};

/* --------- LVGL flush: XRGB8888 -> BGR888 --------- */
static void lvgl_flush_cb(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map)
{
    (void)disp;

    int32_t x1 = area->x1 < 0 ? 0 : area->x1;
    int32_t y1 = area->y1 < 0 ? 0 : area->y1;
    int32_t x2 = area->x2 >= FB_WIDTH  ? (FB_WIDTH - 1)  : area->x2;
    int32_t y2 = area->y2 >= FB_HEIGHT ? (FB_HEIGHT - 1) : area->y2;

    int32_t w = x2 - x1 + 1;
    int32_t h = y2 - y1 + 1;

    const uint32_t *src = (const uint32_t *)px_map; /* expected 0x00RRGGBB */

    for(int32_t row = 0; row < h; row++) {
        uint8_t *dst = g_fb8
            + (size_t)(y1 + row) * (size_t)FB_STRIDE_BYTES
            + (size_t)x1 * 3u;

        for(int32_t col = 0; col < w; col++) {
            uint32_t xrgb = src[(size_t)row * (size_t)w + (size_t)col];
            uint8_t r = (xrgb >> 16) & 0xFF;
            uint8_t g = (xrgb >>  8) & 0xFF;
            uint8_t b = (xrgb >>  0) & 0xFF;

            dst[(size_t)col * 3u + 0u] = b;
            dst[(size_t)col * 3u + 1u] = g;
            dst[(size_t)col * 3u + 2u] = r;
        }
    }

    lv_display_flush_ready(disp);
}

/* --------- 1ms tick thread --------- */
static void * tick_thread_fn(void *arg)
{
    (void)arg;
    struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000 };
    while(g_running) {
        nanosleep(&ts, NULL);
        lv_tick_inc(1);
    }
    return NULL;
}

/* --------- /dev/mem DDR mapping --------- */
static int map_ddr_fb(void)
{
    g_fb_map_bytes = (size_t)FB_STRIDE_BYTES * (size_t)FB_HEIGHT;

    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if(fd < 0) { perror("open(/dev/mem)"); return -1; }

    long page = sysconf(_SC_PAGESIZE);
    off_t phys = (off_t)FB_BASE_PHYS;
    off_t phys_page = phys & ~(off_t)(page - 1);
    off_t page_off  = phys - phys_page;

    g_map_base = mmap(NULL, g_fb_map_bytes + (size_t)page_off,
                      PROT_READ | PROT_WRITE, MAP_SHARED, fd, phys_page);
    close(fd);

    if(g_map_base == MAP_FAILED) { g_map_base = NULL; perror("mmap"); return -1; }

    g_fb8 = (uint8_t *)g_map_base + page_off;
    return 0;
}

/* --------- Mouse evdev --------- */
static int mouse_open(const char *devpath)
{
    g_mouse_fd = open(devpath, O_RDONLY | O_NONBLOCK);
    if(g_mouse_fd < 0) { perror("open mouse evdev"); return -1; }
    return 0;
}

static void mouse_read_cb(lv_indev_t * indev, lv_indev_data_t * data)
{
    (void)indev;

    struct input_event ev;
    while(1) {
        ssize_t n = read(g_mouse_fd, &ev, sizeof(ev));
        if(n == (ssize_t)sizeof(ev)) {
            if(ev.type == EV_REL) {
                if(ev.code == REL_X) g_mouse_x += ev.value;
                else if(ev.code == REL_Y) g_mouse_y += ev.value;
            } else if(ev.type == EV_KEY) {
                if(ev.code == BTN_LEFT) g_mouse_pressed = (ev.value != 0);
            }
        } else {
            if(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
            break;
        }
    }

    if(g_mouse_x < 0) g_mouse_x = 0;
    if(g_mouse_y < 0) g_mouse_y = 0;
    if(g_mouse_x >= FB_WIDTH)  g_mouse_x = FB_WIDTH - 1;
    if(g_mouse_y >= FB_HEIGHT) g_mouse_y = FB_HEIGHT - 1;

    data->point.x = g_mouse_x;
    data->point.y = g_mouse_y;
    data->state = g_mouse_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->continue_reading = 0;
}

// some includes for c++ maps etc.
#include <unordered_map>
#include <string>
#include <functional>
#include <vector>


template<typename T>
class ObjectRegistry {
public:
    void add(const std::string& name, T* obj) {
        registry[name] = obj;
    }

	

    T* get(const std::string& name) const {
		auto it = registry.find(name);
        return it != registry.end() ? it->second : nullptr;
	}

    bool remove(const std::string& name) {
        return registry.erase(name) > 0;
    }

    void printall() {
         for (const auto& pair : registry) {
			printf("name %s ",pair.first.c_str());
    	}
	}

    size_t size() const {
        return registry.size();
    }

	std::vector<std::string> getAllNames() const {
        std::vector<std::string> names;
        for (const auto& entry : registry) {
            names.push_back(entry.first);
        }
        return names;
    }

	std::string findNameFor(T* obj) const {
        for (const auto& pair : registry) {
            if (pair.second == obj) {
                return pair.first;
				//  char s[100];
				// sprintf(s,"find name %s ",pair.first.c_str());
				// outputString(s);
            }
        }
        return "";
    }

private:
    std::unordered_map<std::string, T*> registry;
};

template <>
lv_obj_t* ObjectRegistry<lv_obj_t>::get(const std::string& name) const {
    if (name == "lv_scr_act") {
        return lv_scr_act();
    } else {
        auto it = registry.find(name);
        return it != registry.end() ? it->second : nullptr;
    }
}


ObjectRegistry<lv_obj_t>  registry;

ObjectRegistry<lv_font_t> font_buffer;

ObjectRegistry<lv_style_t> style_registry;

ObjectRegistry<lv_chart_series_t> series_registry;

ObjectRegistry<char*> btnmap_registry;


void setup_lvgl() {
    if(map_ddr_fb() != 0) {
        fprintf(stderr, "Failed to map DDR framebuffer. Check sudo, FB_BASE_PHYS, FB_STRIDE_BYTES.\n");
        return ;
    }
    memset(g_fb8, 0x00, g_fb_map_bytes);

    lv_init();

    /* Draw buffer (partial rendering) */
    const uint32_t buf_lines = 40;
    g_draw_buf = (lv_color_t *)malloc((size_t)FB_WIDTH * buf_lines * sizeof(lv_color_t));
    if(!g_draw_buf) { fprintf(stderr, "draw buffer alloc failed\n"); return;  }

    lv_display_t * disp = lv_display_create(FB_WIDTH, FB_HEIGHT);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_flush_cb(disp, lvgl_flush_cb);
    lv_display_set_buffers(disp, g_draw_buf, NULL,
                           (size_t)FB_WIDTH * buf_lines * sizeof(lv_color_t),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    /* Tick thread */
    pthread_t tick_thread;
    if(pthread_create(&tick_thread, NULL, tick_thread_fn, NULL) != 0) {
        perror("pthread_create");
        return ;
    }

    /* Mouse indev + cursor */
    if(mouse_open(MOUSE_EVENT_DEV) == 0) {
        lv_indev_t * indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, mouse_read_cb);

        lv_obj_t * cursor = lv_image_create(lv_screen_active());
        lv_image_set_src(cursor, &cursor_img_8x8);
        lv_indev_set_cursor(indev, cursor);
    } else {
        fprintf(stderr, "Warning: could not open %s\n", MOUSE_EVENT_DEV);
    }
    LVGL_initialized = true;
    // store main screen object in object with name '!main_screen_default'
    // hide this object from user in get_all_objects
    registry.add("!main_screen_default",lv_scr_act() );
    useLVGL=false;
}


void set_lvgl(bool use_lvgl) {
	if (use_lvgl) {
		useLVGL=true;
		// refresh all objects
		lv_obj_invalidate(lv_scr_act());
	} else {
		useLVGL=false;
		// tftClear();
	}
}

// in lvgl 9 there is no LV_EVENT_NONE defined, so define it ourselves
#define LV_EVENT_NONE_CUSTOM (lv_event_code_t)(-1)
struct LastEventInfo {
    lv_event_t *event;
    lv_event_code_t code;
    lv_obj_t *target;
	std::string  name;
	uint32_t id;
};


static LastEventInfo last_event = {
    nullptr,
    LV_EVENT_NONE_CUSTOM,
    nullptr,
    std::string(),   // default empty string
    0                // id
};

// generic call back function for all events
void ui_log_event_cb(lv_event_t *e) {
    last_event.event = e;
    last_event.code = lv_event_get_code(e);
    last_event.target = (lv_obj_t *) lv_event_get_target(e);
	if (lv_obj_get_class(last_event.target) == &lv_buttonmatrix_class) {
		last_event.id = lv_buttonmatrix_get_selected_button(last_event.target);
	}
	last_event.name = registry.findNameFor( last_event.target);
		// char s[100];
		// sprintf(s,"Event %d on obj name %s id %d", last_event.code, last_event.name.c_str(),last_event.id);
		// outputString(s);
	// // send broadcast
	event_seen = true; // set to false in getevent
	
	char eventmessage[] = "LVGLevent";
	// send a broadcast with text: LVGLevent
	startReceiversOfBroadcast(eventmessage, 9);
	sendBroadcastToIDE(eventmessage, 9);
}

int ui_get_last_event(std::string& name_out) {
    name_out = last_event.name;
    return static_cast<int>(last_event.code);
}

const lv_font_t* get_font_from_scale(int scale_x) {
    switch(scale_x) {
        case 1: return &lv_font_montserrat_14;
//        case 2: return &lv_font_montserrat_24;
//        case 3: return &lv_font_montserrat_40;
//		case 4: return &lv_font_montserrat_48;
        default: return &lv_font_montserrat_14; // default fallback
    }
}

/*
void ui_add_font(char * obj_name, const char *path) {
	lv_obj_t* obj;
    // Create an img object
    
	if (!font_buffer.get(obj_name) ) {
		lv_font_t *obj = lv_binfont_create(path);
		// Set image source from file
		if (obj) {
			font_buffer.add(obj_name, obj);
		}
	}
}
*/
void ui_create_button_label(char * obj_name, int scale, const char * label_text, const char * parent_name) {
	lv_obj_t* parent = registry.get(parent_name);
	lv_obj_t* obj;
	if (!registry.get(obj_name) && parent) {
		if (lv_obj_get_class(parent) == &lv_list_class) {
			obj = lv_list_add_button(parent, NULL, label_text);
		} else {
			obj = lv_btn_create(parent);
			// sodb: check whether label is correctly removed when parent btn object is deleted
			lv_obj_t * label = lv_label_create(obj);
			lv_label_set_text(label, label_text);
			lv_obj_set_style_text_font(label, get_font_from_scale(scale), LV_PART_MAIN);
			lv_obj_center(label);
		}
		lv_obj_add_event_cb(obj, ui_log_event_cb, LV_EVENT_CLICKED, NULL);
		registry.add(obj_name, obj);
	}

}


void ui_create_button(char * obj_name, const char * parent) {
	if (!registry.get(obj_name) && registry.get(parent)) {
		lv_obj_t* obj = lv_btn_create(registry.get(parent));
		lv_obj_add_event_cb(obj, ui_log_event_cb, LV_EVENT_CLICKED, NULL);
		// sodb: check whether label is correctly removes when partent btn object is deleted
		registry.add(obj_name, obj);
	}

}

void ui_create_label(char * obj_name, int scale, const char * label_text, const char * parent) {
    if (!registry.get(obj_name) && registry.get(parent)) {
		lv_obj_t* label = lv_label_create(registry.get(parent));
		lv_label_set_text(label, label_text);
		lv_obj_set_style_text_font(label, get_font_from_scale(scale), LV_PART_MAIN);
		registry.add(obj_name, label);
	}
}


void ui_create_slider(char * obj_name, const char * parent) {
    if (!registry.get(obj_name) && registry.get(parent)) {
		lv_obj_t* obj = lv_slider_create(registry.get(parent));
		lv_obj_add_event_cb(obj, ui_log_event_cb,LV_EVENT_VALUE_CHANGED, NULL);
		lv_obj_add_event_cb(obj, ui_log_event_cb,LV_EVENT_LONG_PRESSED, NULL);
		lv_slider_set_range(obj, 0, 100);
		registry.add(obj_name, obj);
	}
}

void ui_create_arc(char * obj_name, const char * parent) {
    if (!registry.get(obj_name) && registry.get(parent)) {
		lv_obj_t* obj = lv_arc_create(registry.get(parent));
		lv_obj_add_event_cb(obj, ui_log_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
		lv_obj_add_event_cb(obj, ui_log_event_cb,LV_EVENT_LONG_PRESSED, NULL);
		// sodb solve unmovable arc on capacitive touch displays.
		lv_obj_add_flag(obj, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CHECKABLE ));
		registry.add(obj_name, obj);
	}
}


void ui_create_switch(char * obj_name, const char * parent) {
    if (!registry.get(obj_name) && registry.get(parent)) {
		lv_obj_t* obj = lv_switch_create(registry.get(parent));
		lv_obj_add_event_cb(obj, ui_log_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
		registry.add(obj_name, obj);
	}
}


void ui_create_led(char * obj_name, const char * parent) {
    if (!registry.get(obj_name) && registry.get(parent)) {
		lv_obj_t* obj = lv_led_create(registry.get(parent));
		// lv_obj_add_event_cb(obj, ui_log_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
		registry.add(obj_name, obj);
	}
}

void ui_create_bar(char * obj_name, const char * parent) {
    if (!registry.get(obj_name) && registry.get(parent)) {
		lv_obj_t* obj = lv_bar_create(registry.get(parent));
		// lv_obj_add_event_cb(obj, ui_log_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
		registry.add(obj_name, obj);
	}
}

void ui_create_tabview(char * obj_name, const char * parent) {
    if (!registry.get(obj_name) && registry.get(parent)) {
		lv_obj_t* obj = lv_tabview_create(registry.get(parent));
		// lv_obj_add_event_cb(obj, ui_log_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
		registry.add(obj_name, obj);
	}
}

void ui_create_tileview(char * obj_name, const char * parent) {
    if (!registry.get(obj_name) && registry.get(parent)) {
		lv_obj_t* obj = lv_tileview_create(registry.get(parent));
		// lv_obj_add_event_cb(obj, ui_log_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
		registry.add(obj_name, obj);
	}
}

void ui_create_screen(char * obj_name, const char * parent) {
    if (!registry.get(obj_name) && registry.get(parent)) {
		#if defined(LMSDISPLAY)
			#define _TFT_WIDTH TFT_HEIGHT
			#define _TFT_HEIGHT TFT_WIDTH
		#else
			#define _TFT_WIDTH TFT_WIDTH
			#define _TFT_HEIGHT TFT_HEIGHT
		#endif
		lv_obj_t* obj = lv_obj_create(0); // crete empty screen
		lv_obj_set_pos(obj, 0, 0);
		lv_obj_set_size(obj, _TFT_WIDTH, _TFT_HEIGHT);
		registry.add(obj_name, obj);
	}
}


void ui_create_roller(char * obj_name, const char * parent) {
    if (!registry.get(obj_name) && registry.get(parent)) {
		lv_obj_t* obj = lv_roller_create(registry.get(parent));
		lv_obj_add_event_cb(obj, ui_log_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
		lv_obj_add_event_cb(obj, ui_log_event_cb,LV_EVENT_LONG_PRESSED, NULL);
		registry.add(obj_name, obj);
	}
}

void ui_create_spinbox(char * obj_name, const char * parent) {
    if (!registry.get(obj_name) && registry.get(parent)) {
		lv_obj_t* obj = lv_spinbox_create(registry.get(parent));
		lv_obj_add_event_cb(obj, ui_log_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
		registry.add(obj_name, obj);
	}
}

void ui_create_spinner(char * obj_name, const char * parent) {
    if (!registry.get(obj_name) && registry.get(parent)) {
		lv_obj_t* obj = lv_spinner_create(registry.get(parent));
		registry.add(obj_name, obj);
	}
}

void ui_create_scale(char * obj_name, const char * parent) {
    if (!registry.get(obj_name) && registry.get(parent)) {
		lv_obj_t* obj = lv_scale_create(registry.get(parent));
		registry.add(obj_name, obj);
	}
}

void ui_create_keyboard(char * obj_name, const char * parent) {
    if (!registry.get(obj_name) && registry.get(parent)) {
		lv_obj_t* obj = lv_keyboard_create(registry.get(parent));
		lv_obj_add_event_cb(obj, ui_log_event_cb, LV_EVENT_READY, NULL);
		registry.add(obj_name, obj);
	}
}
void ui_create_textarea(char * obj_name, const char * parent) {
    if (!registry.get(obj_name) && registry.get(parent)) {
		lv_obj_t* obj = lv_textarea_create(registry.get(parent));
		registry.add(obj_name, obj);
	}
}

void ui_add_tab(char * obj_name, const char * parent) {
    if (!registry.get(obj_name) && registry.get(parent)) {
		lv_obj_t* obj = lv_tabview_add_tab(registry.get(parent),obj_name);
		lv_obj_add_event_cb(obj, ui_log_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
		registry.add(obj_name, obj);
	}
}

void ui_add_series(char * series, const char * chart, int color) {
    if (registry.get(chart) && !series_registry.get(series)) {
		lv_chart_series_t* obj = lv_chart_add_series(registry.get(chart), lv_color_hex(color),  LV_CHART_AXIS_PRIMARY_Y);
		//lv_obj_add_event_cb(obj, ui_log_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
		series_registry.add(series, obj);
	}
}

void ui_set_next_value(char * series, char * chart, int val) {
    if (registry.get(chart) && series_registry.get(series)) {
		lv_chart_set_next_value(registry.get(chart), series_registry.get(series), val);
	}
}

void ui_set_next_value2(char * series, char * chart, int val, int val2) {
    if (registry.get(chart) && series_registry.get(series)) {
		lv_chart_set_next_value2(registry.get(chart), series_registry.get(series), val, val2);
		//outputString("lv_chart_set_next_value2");
	}
}


void ui_add_chart(char * obj_name, const char * parent, char * chart_type, char * chart_update_mode) {
	if (!registry.get(obj_name))  {
		lv_chart_type_t chart_type_id = LV_CHART_TYPE_LINE;
		lv_chart_update_mode_t chart_update_mode_id = LV_CHART_UPDATE_MODE_SHIFT;
		if (strcmp(chart_type,"bar")==0) chart_type_id = LV_CHART_TYPE_BAR;
		else if (strcmp(chart_type,"scatter")==0) chart_type_id = LV_CHART_TYPE_SCATTER;

		if (strcmp(chart_update_mode,"circular")==0) chart_update_mode_id = LV_CHART_UPDATE_MODE_CIRCULAR;
		lv_obj_t* obj = lv_chart_create(registry.get(parent));
		lv_chart_set_type(obj, chart_type_id);
		lv_chart_set_update_mode(obj, chart_update_mode_id);

		registry.add(obj_name, obj);
	}
}

void ui_add_tile(char * obj_name, const char * parent, int col_id, int row_id, lv_dir_t dir ) {
    if (!registry.get(obj_name) && registry.get(parent)) {
		lv_obj_t* obj = lv_tileview_add_tile(registry.get(parent), col_id, row_id, dir);
		lv_obj_add_event_cb(obj, ui_log_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
		registry.add(obj_name, obj);
	}
}


void ui_create_list(char * obj_name, const char * parent) {
    if (!registry.get(obj_name) && registry.get(parent)) {
		lv_obj_t* obj = lv_list_create(registry.get(parent));
		// lv_obj_add_event_cb(obj, ui_log_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
		registry.add(obj_name, obj);
	}
	if (strcmp(parent,"lv_scr_act") !=0) {

	}
}

void ui_create_style(char * obj_name, const char * parent) {
    if (!style_registry.get(obj_name)) {
		lv_style_t* obj = new lv_style_t;
		lv_style_init(obj);
		// lv_obj_add_event_cb(obj, ui_log_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
		style_registry.add(obj_name, obj);
	}
}

void ui_set_parent(char * obj_name, const char * parent, int states, int parts){
	lv_obj_t* obj =  registry.get(obj_name);
	lv_obj_t* obj_parent =  registry.get(parent);
	if (obj && obj_parent) {
		if ( (lv_obj_get_class(obj) == &lv_keyboard_class) &&
			 (lv_obj_get_class(obj_parent) == &lv_textarea_class) ) {
			 	lv_keyboard_set_textarea(obj, obj_parent);
			} else
				lv_obj_set_parent(registry.get(obj_name), obj_parent);
	} else
	if (style_registry.get(obj_name) && obj_parent) {
		lv_style_t* style =  style_registry.get(obj_name);
		lv_obj_add_style(obj_parent, style, states + parts);
		//outputString("style added");

	}
}


void free_btnmap(char **btnmap) {
    if (!btnmap) return;
    for (size_t i = 0; btnmap[i] != NULL; i++) {
        free(btnmap[i]);
    }
    free(btnmap);
}

void ui_delete_obj(char * obj_name) {
    lv_obj_t* obj = registry.get(obj_name);
	lv_font_t* font = font_buffer.get(obj_name);
	lv_style_t* style = style_registry.get(obj_name);
	lv_chart_series_t* chart_series = series_registry.get(obj_name);
	char** btnmap = btnmap_registry.get(obj_name);
	// lv_chart_series_t* series = series_registry.get(obj_name);
	// not needed because lv_obj_del of chart already deletes all the series attached to the chart
    if (obj) {
		lv_obj_del(obj);
        registry.remove(obj_name);
    }
	if (font){
			lv_binfont_destroy(font);
			font_buffer.remove(obj_name);
	}
	if (style) {
		delete style;
		style_registry.remove(obj_name);
	}
	if (chart_series) {
		// lv_obj_del(obj); // slready deleted iwith parent chart
        series_registry.remove(obj_name);
	}
	if (btnmap) {
		free_btnmap(btnmap); // free structure of char** for btnmap
		btnmap_registry.remove(obj_name); // remove entry in btnmap_registry
	}
}

void ui_set_size(char * obj_name,  lv_coord_t w, lv_coord_t h ) {
	lv_obj_t* obj = registry.get(obj_name);
	if (obj) {
		lv_obj_set_size(obj,w,h);
	}
}

void ui_set_pos(char * obj_name,  uint16_t pos_x, uint16_t pos_y) {
	lv_obj_t* obj = registry.get(obj_name);
	if (obj!=nullptr) {
		lv_obj_set_pos(obj,pos_x,pos_y);
	}
}

void ui_set_scroll(char * obj_name,  lv_dir_t scroll_dir) {
	lv_obj_t* obj = registry.get(obj_name);
	if (obj!=nullptr) {
		if (lv_obj_get_class(obj) == &lv_tabview_class) {
			// when tabview, take container of tabs to controll scroll direction
			lv_obj_t *content = lv_tabview_get_content(obj);
			lv_obj_set_scroll_dir(content, scroll_dir);
		} else
			  lv_obj_set_scroll_dir(obj, scroll_dir);
		}
}


void ui_set_value(char * obj_name, int value) {
	lv_obj_t* obj = registry.get(obj_name);
	if (obj) {
		if (lv_obj_get_class(obj) == &lv_arc_class) {
			lv_arc_set_value(obj, value);
		} else
		if (lv_obj_get_class(obj) == &lv_slider_class) {
			lv_slider_set_value(obj, value, LV_ANIM_OFF);
		} else
		if (lv_obj_get_class(obj) == &lv_bar_class) {
			lv_bar_set_value(obj, value, LV_ANIM_OFF);
		} else
		if (lv_obj_get_class(obj) == &lv_spinbox_class) {
			lv_spinbox_set_value(obj, value);
		} else
		if (lv_obj_get_class(obj) == &lv_switch_class) {
			if (value==0) lv_obj_remove_state(obj, LV_STATE_CHECKED);
			else if (value&1) lv_obj_add_state(obj, LV_STATE_CHECKED);
			else if (value>1) lv_obj_add_state(obj, (lv_state_t)value);
			else if (value<0) lv_obj_remove_state(obj,(lv_state_t) -value);

		} else
		if (lv_obj_get_class(obj) == &lv_led_class) {
			if (value==0) lv_led_off(obj);
			else if (value&1) lv_led_on(obj);


		} else
		if (lv_obj_get_class(obj) == &lv_roller_class) {
			lv_roller_set_visible_row_count(obj,value);
		}
	}
}

void ui_set_text(char * obj_name, char * text, int scale) {
    lv_obj_t* obj = registry.get(obj_name);
	if (obj) {
		if (lv_obj_get_class(obj) == &lv_label_class) {
			lv_label_set_text(obj, text);
			lv_obj_set_style_text_font(obj, get_font_from_scale(scale), LV_PART_MAIN);
		} else
		if (lv_obj_get_class(obj) == &lv_button_class) {
			lv_obj_t *label = lv_obj_get_child(obj, 0);
			if (label) {
				lv_label_set_text(label, text);
				lv_obj_set_style_text_font(label, get_font_from_scale(scale), LV_PART_MAIN);
			}
		} else
		if (lv_obj_get_class(obj) == &lv_roller_class) {
			lv_roller_set_options(obj, text, LV_ROLLER_MODE_INFINITE);
			lv_obj_set_style_text_font(obj, get_font_from_scale(scale), LV_PART_MAIN | LV_STATE_DEFAULT | LV_STYLE_PROP_FLAG_INHERITABLE);
			lv_obj_set_style_text_font(obj, get_font_from_scale(scale), LV_PART_SELECTED|  LV_STATE_DEFAULT);

		}else
		if (lv_obj_get_class(obj) == &lv_textarea_class) {
			lv_textarea_set_text(obj, text);
			//lv_obj_set_style_text_font(obj, get_font_from_scale(scale), LV_PART_MAIN); // does not seem to work
		}
	}
}

void ui_set_text_font(char * obj_name, char * text, char * font_name) {
    lv_obj_t* obj = registry.get(obj_name);


	if (obj) {
		lv_font_t * font = font_buffer.get(font_name);
		if (lv_obj_get_class(obj) == &lv_label_class) {
			lv_label_set_text(obj, text);
			if (font) {
				lv_obj_set_style_text_font(obj, font, LV_PART_MAIN);
			}
		} else
		if (lv_obj_get_class(obj) == &lv_button_class) {
			lv_obj_t *label = lv_obj_get_child(obj, 0);
			if (label) {
				lv_label_set_text(label, text);
				if (font)
					lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
			}
		} else
		if (lv_obj_get_class(obj) == &lv_roller_class) {
			lv_roller_set_options(obj, text, LV_ROLLER_MODE_INFINITE);
		}
	}
}


void ui_set_attribute(char * obj_name, char * attribute_name, int to_val, int until_val){
	lv_obj_t* obj = registry.get(obj_name);
	if (obj) {
		if (strcmp(attribute_name,"flags")==0) lv_obj_add_flag(obj, (lv_obj_flag_t)to_val); else
		if (lv_obj_get_class(obj) == &lv_arc_class) {
			if (strcmp(attribute_name,"range")==0) lv_arc_set_range(obj, to_val, until_val);
			else if (strstr(attribute_name,"angles")) lv_arc_set_bg_angles(obj, to_val, until_val);
			else if (strstr(attribute_name,"rotation")) lv_arc_set_rotation(obj, to_val);
			else if (strstr(attribute_name,"line width")) {
				//outputString("line width");
				lv_obj_set_style_arc_width(obj,to_val,LV_PART_MAIN);
				lv_obj_set_style_arc_width(obj,to_val,LV_PART_INDICATOR);
			}
		} else
		if (lv_obj_get_class(obj) == &lv_slider_class) {
			if (strcmp(attribute_name,"range")==0) lv_slider_set_range(obj, to_val, until_val);
		} else
		if (lv_obj_get_class(obj) == &lv_bar_class) {
			if (strcmp(attribute_name,"range")==0) lv_bar_set_range(obj, to_val, until_val);
		} else
		if (lv_obj_get_class(obj) == &lv_spinner_class) {
			if (strcmp(attribute_name,"animation")==0) lv_spinner_set_anim_params(obj, to_val, until_val);
			else if (strstr(attribute_name,"line width")) {
				lv_obj_set_style_arc_width(obj,to_val,LV_PART_MAIN);
				lv_obj_set_style_arc_width(obj,to_val,LV_PART_INDICATOR);
			}
		} else
		if (lv_obj_get_class(obj) == &lv_spinbox_class) {
			if (strcmp(attribute_name,"range")==0) lv_spinbox_set_range(obj, to_val, until_val);
			if (strcmp(attribute_name,"digits")==0) lv_spinbox_set_digit_format(obj, to_val, until_val);
			if (strcmp(attribute_name,"increment")==0) lv_spinbox_increment(obj);
			if (strcmp(attribute_name,"decrement")==0) lv_spinbox_decrement(obj);
		} else
		if (lv_obj_get_class(obj) == &lv_led_class) {
			if (strcmp(attribute_name,"brightness")==0) {
				lv_led_set_brightness(obj,to_val );
			}
		} else
		if (lv_obj_get_class(obj) == &lv_chart_class) {
			if (strcmp(attribute_name,"points")==0) {
				lv_chart_set_point_count(obj,to_val );
				//outputString("lv_chart_set_point_count");
			}
			if (strcmp(attribute_name,"range")==0) {
				lv_chart_set_range(obj, LV_CHART_AXIS_PRIMARY_Y, to_val, until_val);
			}
		} else
		if (lv_obj_get_class(obj) == &lv_scale_class) {
			if (strcmp(attribute_name,"tick count")==0) lv_scale_set_total_tick_count(obj,to_val );
			if (strcmp(attribute_name,"major tick every")==0) lv_scale_set_major_tick_every(obj,to_val );
			if (strcmp(attribute_name,"length")==0) lv_obj_set_style_length(obj,to_val, until_val );
			if (strcmp(attribute_name,"range")==0) lv_scale_set_range(obj, to_val, until_val);
			if (strstr(attribute_name,"scale mode")) lv_scale_set_mode(obj, (lv_scale_mode_t) to_val);
			if (strstr(attribute_name,"angles")) lv_scale_set_angle_range(obj, to_val);
			if (strstr(attribute_name,"rotation")) lv_scale_set_rotation(obj, to_val);
			if (strcmp(attribute_name,"show labels")==0) {
				bool show_labels = (to_val==1);
				lv_scale_set_label_show(obj,show_labels);
			}
		} else
		if (lv_obj_get_class(obj) == &lv_buttonmatrix_class) {
			if (strcmp(attribute_name,"button ctrl")==0) {
				// first clear all states
				lv_btnmatrix_clear_btn_ctrl(obj, to_val, LV_BTNMATRIX_CTRL_HIDDEN);
				lv_btnmatrix_clear_btn_ctrl(obj, to_val, LV_BTNMATRIX_CTRL_DISABLED);
				lv_btnmatrix_clear_btn_ctrl(obj, to_val, LV_BTNMATRIX_CTRL_CHECKED);
				lv_btnmatrix_clear_btn_ctrl(obj, to_val, LV_BTNMATRIX_CTRL_CHECKABLE);
				lv_buttonmatrix_set_button_ctrl(obj,to_val, (lv_buttonmatrix_ctrl_t)until_val); // id, button_ctrl
			}
			if (strcmp(attribute_name,"width")==0) lv_buttonmatrix_set_button_width(obj,to_val, until_val); // id, width
		} else
		if (lv_obj_get_class(obj) == &lv_textarea_class) {
			if (strcmp(attribute_name,"focused")==0) lv_obj_add_state(obj, LV_STATE_FOCUSED);
		} else
		if ((lv_obj_get_class(obj) == &lv_button_class)  || (lv_obj_get_class(obj) == &lv_label_class)) {
		   if (strstr(attribute_name,"rotation")) lv_obj_set_style_transform_angle(obj, to_val, 0);
		}
	}
}


void ui_set_style(char * obj_name, char * style_name, int to_val){
	lv_style_t* obj = style_registry.get(obj_name);
	if (obj) {
		if (strcmp(style_name,"text font")==0) lv_style_set_text_font(obj,  get_font_from_scale(to_val));
			else if (strstr(style_name,"bg color")) lv_style_set_bg_color(obj, lv_color_hex(to_val));
			else if (strstr(style_name,"bg opa")) lv_style_set_bg_opa(obj, to_val);
			else if (strstr(style_name,"border width")) lv_style_set_border_width(obj, to_val);
    		else if (strstr(style_name,"border color")) lv_style_set_border_color(obj, lv_color_hex(to_val));
			else if (strstr(style_name,"radius")) lv_style_set_radius(obj, to_val);
			else if (strstr(style_name,"shadow width")) lv_style_set_shadow_width(obj, to_val);
			else if (strstr(style_name,"shadow offset x")) lv_style_set_shadow_offset_x(obj, to_val);
			else if (strstr(style_name,"shadow offset y")) lv_style_set_shadow_offset_y(obj, to_val);
			else if (strstr(style_name,"shadow opa")) lv_style_set_shadow_opa(obj, to_val);
			else if (strstr(style_name,"width")) lv_style_set_width(obj, to_val);
			else if (strstr(style_name,"line width")) lv_style_set_line_width(obj, to_val);
			else if (strstr(style_name,"line color")) lv_style_set_line_color(obj, lv_color_hex(to_val));
			else if (strstr(style_name,"text color")) lv_style_set_text_color(obj, lv_color_hex(to_val));




	}
}


struct ClassNameMap {
    const lv_obj_class_t *cls;
    const char *name;
} class_map[] = {
    { &lv_buttonmatrix_class, "buttonmatrix" },
    { &lv_label_class,       "label" },
    { &lv_button_class,         "button" },
    { &lv_obj_class,         "generic_obj" }, // base class
    { nullptr,               nullptr }
};

// Get a readable class name
const char *get_class_name(const lv_obj_class_t *cls) {
    for (int i = 0; class_map[i].cls; i++) {
        if (class_map[i].cls == cls) return class_map[i].name;
    }
    return "(unknown)";
}

// Print class hierarchy
void print_class_hierarchy(const lv_obj_t *obj) {
    if (!obj) return;

    const lv_obj_class_t *cls = lv_obj_get_class(obj);

    // char s[100];
	// sprintf(s,"class name - %s (%p)", get_class_name(cls), cls);
	// outputString(s);
}

void ui_set_color(char * obj_name, int color) {
	lv_obj_t* obj = registry.get(obj_name);
	if (obj) {
		if (lv_obj_get_class(obj) == &lv_label_class) {
			lv_obj_set_style_text_color(obj, lv_color_hex(color), LV_PART_MAIN);
		} else
		if  (lv_obj_get_class(obj) == &lv_led_class) {
			lv_led_set_color(obj,lv_color_hex(color));
		} else
		if  (lv_obj_get_class(obj) == &lv_switch_class) {
			lv_obj_set_style_bg_color(obj, lv_color_hex(color), LV_PART_MAIN  | LV_STATE_DEFAULT);
			lv_obj_set_style_bg_opa(obj, LV_OPA_COVER,LV_PART_MAIN |LV_STATE_DEFAULT);
	    } else
		if  ((lv_obj_get_class(obj) == &lv_arc_class) || (lv_obj_get_class(obj) == &lv_spinner_class)) {
			//outputString("change color arc or spinner");
			lv_obj_set_style_arc_color(obj, lv_color_hex(color), LV_PART_MAIN);
 		} else
			lv_obj_set_style_bg_color(obj, lv_color_hex(color), LV_PART_MAIN);

	}
}

void ui_set_color_2nd(char * obj_name, int color) {
	lv_obj_t* obj = registry.get(obj_name);
	if (obj) {
		if (lv_obj_get_class(obj) == &lv_button_class) {
			lv_obj_t *label = lv_obj_get_child(obj, 0);
			lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
		} else if ((lv_obj_get_class(obj) == &lv_arc_class)  || (lv_obj_get_class(obj) == &lv_spinner_class)){
			lv_obj_set_style_arc_color(obj, lv_color_hex(color), LV_PART_INDICATOR);
		} else if (lv_obj_get_class(obj) == &lv_switch_class) {
			lv_obj_set_style_bg_color(obj, lv_color_hex(color), LV_PART_INDICATOR|LV_STATE_CHECKED);
		    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_INDICATOR|LV_STATE_CHECKED);
		}
 		else
			lv_obj_set_style_bg_color(obj, lv_color_hex(color), LV_PART_INDICATOR);

	}
}

void ui_set_color_3rd(char * obj_name, int color) {
	lv_obj_t* obj = registry.get(obj_name);
	if (obj) {
		lv_obj_set_style_bg_color(obj, lv_color_hex(color), LV_PART_KNOB);
		if (lv_obj_get_class(obj) == &lv_switch_class) lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_KNOB);
	}
}

void printall() {
	registry.printall();

}


// helper code for object selection

typedef enum {
    CMD_UNKNOWN = -1,
    CMD_BUTTON,
	CMD_LABEL,
    CMD_ARC,
    CMD_SLIDER,
    CMD_LED,
    CMD_SWITCH,
	CMD_BAR,
	CMD_TABVIEW,
	CMD_TILEVIEW,
	CMD_LIST,
	CMD_ROLLER,
	CMD_SCREEN,
	CMD_STYLE,
	CMD_SPINBOX,
	CMD_SPINNER,
	CMD_SCALE,
	CMD_TEXTAREA,
	CMD_KEYBOARD,
    CMD_COUNT
} Command;


Command lookup_cmd(const char *s) {
    if (strcmp(s, "button") == 0)   return CMD_BUTTON;
	if (strcmp(s, "label") == 0)    return CMD_LABEL;
    if (strcmp(s, "arc") == 0)      return CMD_ARC;
    if (strcmp(s, "slider") == 0)   return CMD_SLIDER;
    if (strcmp(s, "led") == 0)      return CMD_LED;
    if (strcmp(s, "switch") == 0)   return CMD_SWITCH;
	if (strcmp(s, "bar") == 0)   	return CMD_BAR;
	if (strcmp(s, "tabview") == 0)  return CMD_TABVIEW;
	if (strcmp(s, "tileview") == 0) return CMD_TILEVIEW;
	if (strcmp(s, "list") == 0)     return CMD_LIST;
	if (strcmp(s, "roller") == 0)   return CMD_ROLLER;
	if (strcmp(s, "style") == 0)   return CMD_STYLE;
	if (strcmp(s, "spinbox") == 0)   return CMD_SPINBOX;
	if (strcmp(s, "spinner") == 0)   return CMD_SPINNER;
	if (strcmp(s, "screen") == 0)   return CMD_SCREEN;
	if (strcmp(s, "scale") == 0)   return CMD_SCALE;
	if (strcmp(s, "textarea") == 0)   return CMD_TEXTAREA;
	if (strcmp(s, "keyboard") == 0)   return CMD_KEYBOARD;
    return CMD_UNKNOWN;
}

/*
static OBJ primLVGLaddfont(int argCount, OBJ *args) {
	char* obj_name = obj2str(args[1]);
	char* filename = obj2str(args[0]);

	ui_add_font(obj_name,filename);
	return falseObj;
}
*/
/*
static OBJ primLVGLaddimg(int argCount, OBJ *args) {
	char* obj_name = obj2str(args[1]);
	char* filename = obj2str(args[0]);
	const char *parent;
	if (argCount > 2) {
		parent = obj2str(args[2]);
	} else {
		parent = "lv_scr_act";
	}

	ui_add_image(obj_name,filename,parent);
	return falseObj;
}
*/
// primLVGL function definitions
static OBJ primLVGLprintall(int argCount, OBJ *args) {
	printall();
	return falseObj;
}

static OBJ primLVGLgetallobjs(int argCount, OBJ *args) {
	int count = registry.size();
	count += font_buffer.size();
	count += style_registry.size();
	count += series_registry.size();
	count += btnmap_registry.size();
	OBJ result = newObj(ListType, count+1, zeroObj);
	FIELD(result, 0) = int2obj(count);
	int i=1;
	std::vector<std::string> names = registry.getAllNames();
	for (const auto& name : names) {
		FIELD(result, i)=newStringFromBytes(name.c_str(), name.length());
		i++;
	}
	names = font_buffer.getAllNames();
	for (const auto& name : names) {
		FIELD(result, i)=newStringFromBytes(name.c_str(), name.length());
		i++;
	}
	names = style_registry.getAllNames();
	for (const auto& name : names) {
		FIELD(result, i)=newStringFromBytes(name.c_str(), name.length());
		i++;
	}
	names = series_registry.getAllNames();
	for (const auto& name : names) {
		FIELD(result, i)=newStringFromBytes(name.c_str(), name.length());
		i++;
	}
	names = btnmap_registry.getAllNames();
	for (const auto& name : names) {
		FIELD(result, i)=newStringFromBytes(name.c_str(), name.length());
		i++;
	}
	return result;
}


static OBJ primLVGLgetallfonts(int argCount, OBJ *args) {
	std::vector<std::string> names = font_buffer.getAllNames();
	int count = font_buffer.size();
	OBJ result = newObj(ListType, count+1, zeroObj);
	FIELD(result, 0) = int2obj(count);
	int i=1;
	for (const auto& name : names) {
		FIELD(result, i)=newStringFromBytes(name.c_str(), name.length());
		i++;
	}
	return result;
}


static OBJ primLVGLgetallstyles(int argCount, OBJ *args) {
	std::vector<std::string> names = style_registry.getAllNames();
	int count = style_registry.size();
	OBJ result = newObj(ListType, count+1, zeroObj);
	FIELD(result, 0) = int2obj(count);
	int i=1;
	for (const auto& name : names) {
		FIELD(result, i)=newStringFromBytes(name.c_str(), name.length());
		i++;
	}
	return result;
}


static OBJ primLVGLgetallseries(int argCount, OBJ *args) {
	std::vector<std::string> names = series_registry.getAllNames();
	int count = series_registry.size();
	OBJ result = newObj(ListType, count+1, zeroObj);
	FIELD(result, 0) = int2obj(count);
	int i=1;
	for (const auto& name : names) {
		FIELD(result, i)=newStringFromBytes(name.c_str(), name.length());
		i++;
	}
	return result;
}

static OBJ primLVGLgetallbtnmaps(int argCount, OBJ *args) {
	std::vector<std::string> names = btnmap_registry.getAllNames();
	int count = btnmap_registry.size();
	OBJ result = newObj(ListType, count+1, zeroObj);
	FIELD(result, 0) = int2obj(count);
	int i=1;
	for (const auto& name : names) {
		FIELD(result, i)=newStringFromBytes(name.c_str(), name.length());
		i++;
	}
	return result;
}

static OBJ primLVGLaddBtn(int argCount, OBJ *args) {
	int scale = 1;
	char* obj_name = obj2str(args[0]);
	const char *label_text;
	if (argCount >1) {
		scale = obj2int(args[1]);
	} else scale=1;
	if (argCount >2) {
		OBJ value = args[2];
		if (IS_TYPE(value, StringType)) {
			label_text = obj2str(value);
		} else if (isInt(value)) {
   			char s[20];
   			sprintf(s, "%d", obj2int(value));
			label_text=s;
		} else
			label_text="";
	} else 	label_text = obj2str(args[0]);
	const char *parent;
	if (argCount > 3) {
		parent = obj2str(args[3]);
	} else {
		parent = "lv_scr_act";
	}
	ui_create_button_label(obj_name, scale, label_text, parent);
	return falseObj;
}

static OBJ primLVGLaddLabel(int argCount, OBJ *args) {
	int scale = 1;
	char* label_name = obj2str(args[0]);
	const char *label_text;
	if (argCount >1) {
		scale = obj2int(args[1]);
	} else scale =1;
	if (argCount >2) {
		OBJ value = args[2];
		if (IS_TYPE(value, StringType)) {
			label_text = obj2str(args[2]);
		} else if (isInt(value)) {
   			char s[20];
   			sprintf(s, "%d", obj2int(value));
			label_text=s;
		} else
			label_text="";
	} else 	label_text = obj2str(args[0]);
	const char *parent;
	if (argCount > 3) {
		parent = obj2str(args[3]);
	} else {
		parent = "lv_scr_act";
	}
	ui_create_label(label_name, scale, label_text, parent);
	return falseObj;
}

static OBJ primLVGLaddSlider(int argCount, OBJ *args) {
	char* obj_name = obj2str(args[0]);
	const char *parent;
	if (argCount > 1) {
		parent = obj2str(args[1]);
	} else {
		parent = "lv_scr_act";
	}
	ui_create_slider(obj_name, parent);
	return falseObj;
}

static OBJ primLVGLaddTab(int argCount, OBJ *args) {
	char* obj_name = obj2str(args[0]);
	char* parent = obj2str(args[1]);
	ui_add_tab(obj_name, parent);
	return falseObj;
}

static OBJ primLVGLaddTile(int argCount, OBJ *args) {
	char* obj_name = obj2str(args[0]);
	char* parent = obj2str(args[1]);
	int col_id = obj2int(args[2]);
	int row_id = obj2int(args[3]);
	uint8_t l = (trueObj == args[4]) ? 1:0;
	uint8_t r = (trueObj == args[5]) ? 1:0;
	uint8_t t = (trueObj == args[6]) ? 1:0;
	uint8_t b = (trueObj == args[7]) ? 1:0;
	lv_dir_t dir = (lv_dir_t)(l + (r<<1) + (t<<2) + (b<<3));
	ui_add_tile(obj_name, parent, col_id, row_id, dir);
	return falseObj;
}

static OBJ primLVGLaddSeries(int argCount, OBJ *args) {
	char* series = obj2str(args[0]);
	char* chart = obj2str(args[1]);
	int color = obj2int(args[2]);
	ui_add_series(series, chart, color);
	return falseObj;
}

static OBJ primLVGLaddchart(int argCount, OBJ *args) {
	char* obj_name = obj2str(args[0]);
	char* chart_type = obj2str(args[1]);
	char* chart_update_mode = obj2str(args[2]);
	const char* parent = "lv_scr_act";
	if (argCount > 3) {
		parent = obj2str(args[3]);
	}
	ui_add_chart(obj_name, parent, chart_type, chart_update_mode);
	return falseObj;
}

static OBJ primLVGLaddArc(int argCount, OBJ *args) {
	char* obj_name = obj2str(args[0]);
	const char *parent;
	if (argCount > 1) {
		parent = obj2str(args[1]);
	} else {
		parent = "lv_scr_act";
	}
	ui_create_arc(obj_name, parent);
	return falseObj;
}


void free_btnmap(char **btnmap, size_t count) {
    if (!btnmap) return;
    for (size_t i = 0; i <= count; i++) {
        free(btnmap[i]);  // free each string
    }
    free(btnmap);         // free the array of pointers
}




static OBJ primLVGLaddButtonMatrix(int argCount, OBJ *args) {
	int count;
	char* obj_name = obj2str(args[0]);
	const char *parent;
	OBJ obj = args[1];
	if (argCount > 2) {
		parent = obj2str(args[2]);
	} else {
		parent = "lv_scr_act";
	}

	if (IS_TYPE(obj, ListType)) {
		count = obj2int(FIELD(obj, 0));
		if (count >= WORDS(obj)) count = WORDS(obj) - 1;
		if (!registry.get(obj_name) && registry.get(parent)) {
			// alloc array of strings
			char** btnmap = (char **)malloc((count+1) * sizeof(char*));
			for (size_t i = 1; i < count+1; i++) {
				OBJ field =  FIELD(obj, i);
				char* string_n = obj2str(field);
				size_t len = strlen(string_n);
				//outputString(string_n);
				if (len==0) {
						btnmap[i-1] = (char *)malloc(2);
						strcpy(btnmap[i-1], "\n");
				} else {
					btnmap[i-1] = (char *)malloc(len + 1); // +1 for null terminator
					strcpy(btnmap[i-1], string_n);
				}
			}
			btnmap[count]=NULL; // end button map
			lv_obj_t* obj = lv_buttonmatrix_create(registry.get(parent));
			lv_buttonmatrix_set_map(obj, btnmap);
			lv_obj_add_event_cb(obj, ui_log_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
			registry.add(obj_name, obj);
			btnmap_registry.add(obj_name, btnmap); // store btnmap in registry
		}
	}

	return falseObj;
}



static OBJ primLVGLaddObject(int argCount, OBJ *args) {
	char* obj_type = obj2str(args[0]);
	char* obj_name = obj2str(args[1]);
	const char *parent;
	if (argCount > 2) {
		parent = obj2str(args[2]);
	} else {
		parent = "lv_scr_act";
	}
	//bool event = (argCount > 1) ? (trueObj == args[1]) : true;
	Command cmd = lookup_cmd(obj_type);
	switch (cmd) {
		case CMD_BUTTON:
			//outputString("Handle BUTTON");
			ui_create_button(obj_name, parent);
			break;
		case CMD_ARC:
			//outputString("Handle ARC");
			ui_create_arc(obj_name, parent);
			break;
		case CMD_SLIDER:
			//outputString("Handle SLIDER");
			ui_create_slider(obj_name, parent);
			break;
		case CMD_LED:
			//outputString("Handle LED");
			ui_create_led(obj_name, parent);
			break;
		case CMD_SWITCH:
			//outputString("Handle SWITCH");
			ui_create_switch(obj_name, parent);
			break;
		case CMD_BAR:
			//outputString("Handle BAR");
			ui_create_bar(obj_name, parent);
			break;
		case CMD_TABVIEW:
			//outputString("Handle TABVIEW");
			ui_create_tabview(obj_name, parent);
			break;
		case CMD_TILEVIEW:
			//outputString("Handle TILEVIEW");
			ui_create_tileview(obj_name, parent);
			break;
		case CMD_LIST:
			//outputString("Handle LIST");
			ui_create_list(obj_name, parent);
			break;
		case CMD_ROLLER:
			//outputString("Handle ROLLER");
			ui_create_roller(obj_name, parent);
			break;
		case CMD_SCREEN:
			//outputString("Handle SCREEN");
			ui_create_screen(obj_name, parent);
			break;
		case CMD_STYLE:
		 	//outputString("Handle STYLE");
		 	ui_create_style(obj_name, parent);
		 	break;
		case CMD_SPINBOX:
		 	//outputString("Handle SPINBOX");
		 	ui_create_spinbox(obj_name, parent);
		 	break;
		case CMD_SPINNER:
		 	//outputString("Handle SPINNER");
		 	ui_create_spinner(obj_name, parent);
		 	break;
		case CMD_SCALE:
		 	ui_create_scale(obj_name, parent);
		 	break;
		case CMD_TEXTAREA:
		 	ui_create_textarea(obj_name, parent);
		 	break;
		case CMD_KEYBOARD:
		 	ui_create_keyboard(obj_name, parent);
		 	break;
		default:
			outputString("Unknown command");;
	}
	return falseObj;
}

static OBJ primLVGLgetSymbol(int argCount, OBJ *args) {
    static const std::unordered_map<std::string, int> symbolMap = {
        {"bullet",        20042},
        {"audio",         61441},
        {"video",         61448},
        {"list",          61451},
        {"ok",            61452},
        {"close",         61453},
        {"power",         61457},
        {"settings",      61459},
        {"home",          61461},
        {"download",      61465},
        {"drive",         61468},
        {"refresh",       61473},
        {"mute",          61478},
        {"volume_mid",    61479},
        {"volume_max",    61480},
        {"image",         61502},
        {"tint",          61507},
        {"prev",          61512},
        {"play",          61515},
        {"pause",         61516},
        {"stop",          61517},
        {"next",          61521},
        {"eject",         61522},
        {"left",          61523},
        {"right",         61524},
        {"plus",          61543},
        {"minus",         61544},
        {"eye_open",      61550},
        {"eye_close",     61552},
        {"warning",       61553},
        {"shuffle",       61556},
        {"up",            61559},
        {"down",          61560},
        {"loop",          61561},
        {"directory",     61563},
        {"upload",        61587},
        {"call",          61589},
        {"cut",           61636},
        {"copy",          61637},
        {"save",          61639},
        {"bars",          61641},
        {"envelope",      61664},
        {"charge",        61671},
        {"paste",         61674},
        {"bell",          61683},
        {"keyboard",      61724},
        {"gps",           61732},
        {"file",          61787},
        {"wifi",          61931},
        {"battery_full",  62016},
        {"battery_3",     62017},
        {"battery_2",     62018},
        {"battery_1",     62019},
        {"battery_empty", 62020},
        {"usb",           62087},
        {"bluetooth",     62099},
        {"trash",         62189},
        {"edit",          62212},
        {"backspace",     62810},
        {"sd_card",       63426},
        {"new_line",      63650}
    };
	std::string symbol = obj2str(args[0]);
    auto it = symbolMap.find(symbol);
    if (it != symbolMap.end()) {
        return int2obj(it->second);
    }
    return falseObj; // not found
}

static OBJ primLVGLloadScreen(int argCount, OBJ *args) {
	char* obj_name = obj2str(args[0]);
	lv_obj_t* obj = registry.get(obj_name);
	//if (obj) lv_scr_load_anim(obj, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, false);
	if (obj) lv_scr_load(obj);
	return falseObj;
}

static OBJ primLVGLsetParent(int argCount, OBJ *args) {
	char* obj = obj2str(args[0]);
	char* parent = obj2str(args[1]);
	// states and parts are only used when applying a style to an object]
	int states = 0;
	int parts = 0;
	if (argCount >2) {
		states = obj2int(args[2]);
	}
	if (argCount >3) {
		parts = obj2int(args[3]);
	}
	ui_set_parent(obj, parent, states, parts);
	return falseObj;
}

#if defined(COCUBE) || defined(S3_ROTARY)
static OBJ primLVGLaddgroup(int argCount, OBJ *args) {
	char* obj_name = obj2str(args[0]);
	lv_obj_t* obj = registry.get(obj_name);
	lv_group_add_obj(group, obj);
	return falseObj;
}

static OBJ primLVGLgroupfocus(int argCount, OBJ *args) {
	int next_prev = obj2int(args[0]);
	if (next_prev==1)
		lv_group_focus_next(group);
	else if (next_prev==-1)
		lv_group_focus_prev(group);
	return falseObj;
}

static OBJ primLVGLgroupkey(int argCount, OBJ *args) {
	if (argCount==0) {
		lv_group_send_data(group, LV_KEY_ENTER);
		return falseObj;
	}
	int key = obj2int(args[0]);
	lv_group_send_data(group, key);
	return falseObj;
}


#endif

#if defined(S3_ROTARY)
static OBJ primLVGLencoder(int argCount, OBJ *args) {
	int val = mt8901_get_count();
	char s[100];
	sprintf(s,"Encoder count: %d",val);
	outputString(s);
	return int2obj(mt8901_get_count());
}
#endif
static OBJ primLVGLdelObj(int argCount, OBJ *args) {
	char* obj_name = obj2str(args[0]);
	ui_delete_obj(obj_name);
	return falseObj;
}

static OBJ primLVGLsetSize(int argCount, OBJ *args) {
	char* obj_name = obj2str(args[0]);
	int w = obj2int(args[1]);
	int h = obj2int(args[2]);
	ui_set_size(obj_name, w, h);
	return falseObj;
}

static OBJ primLVGLsetPos(int argCount, OBJ *args) {
	char* obj_name = obj2str(args[0]);
	int pos_x = obj2int(args[1]);
	int pos_y = obj2int(args[2]);
	ui_set_pos(obj_name, pos_x, pos_y);
	return falseObj;
}

static OBJ primLVGLsetScroll(int argCount, OBJ *args) {
	char* obj_name = obj2str(args[0]);
	char* direction = obj2str(args[1]);
	lv_dir_t scroll_dir = LV_DIR_NONE;
	if (strcmp(direction,"hor")==0) scroll_dir = LV_DIR_HOR;
	else if (strcmp(direction,"ver")==0) scroll_dir = LV_DIR_VER;
	else if (strcmp(direction,"all")==0) scroll_dir = LV_DIR_ALL;
	ui_set_scroll(obj_name, scroll_dir);
	return falseObj;
}




static OBJ primLVGLsetVal(int argCount, OBJ *args) {
	char* obj_name = obj2str(args[0]);
	lv_obj_t* obj = registry.get(obj_name);
	int value;
	if (obj) {
		if (lv_obj_get_class(obj) == &lv_led_class) {
			if (trueObj == args[1]) lv_led_on(obj);
			else lv_led_off(obj);
		} else if (lv_obj_get_class(obj) == &lv_switch_class) {
			if (IS_TYPE(args[1], BooleanType )) {
				if (args[1]==trueObj) value = 1; else value=0;
			}
		}
		else
		   value = obj2int(args[1]);
		if (lv_obj_get_class(obj) == &lv_arc_class) {
			lv_arc_set_value(obj, value);
		} else
		if (lv_obj_get_class(obj) == &lv_slider_class) {
			lv_slider_set_value(obj, value, LV_ANIM_OFF);
		} else
		if (lv_obj_get_class(obj) == &lv_bar_class) {
			lv_bar_set_value(obj, value, LV_ANIM_OFF);
		} else
		if (lv_obj_get_class(obj) == &lv_spinbox_class) {
			lv_spinbox_set_value(obj, value);
		} else
		if (lv_obj_get_class(obj) == &lv_switch_class) {
			if (value==0) lv_obj_remove_state(obj, LV_STATE_CHECKED);
			else if (value>0) lv_obj_add_state(obj, LV_STATE_CHECKED);
		}  else
		if (lv_obj_get_class(obj) == &lv_roller_class) {
			lv_roller_set_visible_row_count(obj,value);
		}
	}
	return falseObj;
}

static OBJ primLVGLsetnextvalue(int argCount, OBJ *args) {
	char* series = obj2str(args[0]);
	char *chart = obj2str(args[1]);
	int val = obj2int(args[2]);
	if (argCount > 3) {
		int val2 = obj2int(args[3]);
		ui_set_next_value2(series, chart, val, val2);
	} else {
		ui_set_next_value(series, chart, val);
	}
		return falseObj;
}


static OBJ primLVGLsetText(int argCount, OBJ *args) {
	int scale = 1;
	char* obj_name = obj2str(args[0]);
	char* obj_text;
	char *font_name;
	OBJ value = args[1];
	if (IS_TYPE(value, StringType)) {
		obj_text = obj2str(value);
	} else if (isInt(value)) {
		char s[20];
		sprintf(s, "%d", obj2int(value));
		obj_text = s;
	} else {
		char s[1];
		s[0]='\0';
		obj_text=s;
	}
	if (argCount >2) {
		if (IS_TYPE(args[2], StringType)) {
			font_name = obj2str(args[2]);
			ui_set_text_font(obj_name, obj_text, font_name);
		} else {
			scale = obj2int(args[2]);
			ui_set_text(obj_name, obj_text, scale);
		}
	} else
		ui_set_text(obj_name, obj_text, scale);

	return falseObj;
}

static OBJ primLVGLsetattribute(int argCount, OBJ *args) {
	char* attribute_name = obj2str(args[0]);
	char* obj_name = obj2str(args[1]);
	int to_val = obj2int(args[2]);
	int until_val=100;
	if (argCount >3) {
		until_val = obj2int(args[3]);
	}
	ui_set_attribute(obj_name, attribute_name, to_val, until_val);
	return falseObj;
}


static OBJ primLVGLsetstyle(int argCount, OBJ *args) {
	char* style_name = obj2str(args[0]);
	char* obj_name = obj2str(args[1]);
	int to_val = obj2int(args[2]);
	ui_set_style(obj_name, style_name, to_val);
	return falseObj;
}

static OBJ primLVGLgetVal(int argCount, OBJ *args) {
	char* obj_name = obj2str(args[0]);
	lv_obj_t* obj = registry.get(obj_name);
	if (obj) {
		if (lv_obj_get_class(obj) == &lv_arc_class) {
			return int2obj(lv_arc_get_value(obj));
 		} else
		if (lv_obj_get_class(obj) == &lv_slider_class) {
			return int2obj(lv_slider_get_value(obj));
		} else
		if (lv_obj_get_class(obj) == &lv_spinbox_class) {
			return int2obj(lv_spinbox_get_value(obj));
		} else
		if (lv_obj_get_class(obj) == &lv_textarea_class) {
			const char * text = lv_textarea_get_text(obj);
			return newStringFromBytes(text, strlen(text));
		} else
		if (lv_obj_get_class(obj) == &lv_switch_class) {
			return lv_obj_has_state(obj, LV_STATE_CHECKED)  ? trueObj : falseObj;
		} else
		if (lv_obj_get_class(obj) == &lv_roller_class) {
			char buf[100];
			lv_roller_get_selected_str(obj, buf, sizeof(buf));
			return  newStringFromBytes(buf, strlen(buf));
		} else
		if (lv_obj_check_type(obj, &lv_buttonmatrix_class)){
			// used when event to retuen the id of the btn
			int id = lv_buttonmatrix_get_selected_button(obj);
			int checked = 0;
			if (lv_buttonmatrix_has_button_ctrl(obj, id, LV_BTNMATRIX_CTRL_CHECKED)) checked = 512;
			return int2obj(id + checked);
		}
		else return falseObj;
	} else return falseObj;
}




static OBJ primLVGLsetColor(int argCount, OBJ *args) {
	char* obj_name = obj2str(args[0]);
	int color = obj2int(args[1]);
	ui_set_color(obj_name, color);
	if ( argCount > 2) {
		color = obj2int(args[2]);
		ui_set_color_2nd(obj_name, color);
	}
	if ( argCount > 3) {
		color = obj2int(args[3]);
		ui_set_color_3rd(obj_name, color);
	}

	return falseObj;
}


static OBJ primLVGLgetEvent(int argCount, OBJ *args) {
	std::string name;
	int code = ui_get_last_event(name);
	OBJ result = newStringFromBytes(name.c_str(), name.length());
	return result;
}

static OBJ primLVGLEvent(int argCount, OBJ *args) {
	bool check_event = event_seen;
	// char s[100];
	// sprintf(s,"event %d",event_seen);
	// outputString(s);
	event_seen = false; // wipe event for next event
	if (check_event) return trueObj; else return falseObj;
}


static OBJ primLVGLon(int argCount, OBJ *args) {
	set_lvgl(trueObj == args[0]);
	return falseObj;

}

/*
static OBJ primLVGLtick(int argCount, OBJ *args) {
	lvgl_tick();
	return falseObj;

}
*/
// dummy function for testing initialisation lvgl
static OBJ primLVGLinit(int argCount, OBJ *args) {
	//if (useTFT) // check whether TFT is active, only then setup LVGL
		setup_lvgl();
	return falseObj;

}


static PrimEntry entries[] = {
	{"LVGLon",primLVGLon},
//	{"LVGLbutton",primLVGLbutton},
//	{"LVGLtick",primLVGLtick},
//	{"LVGLstate",primLVGLstate},
	#if defined(LVGL_SNAPSHOT)
		{"LVGLsnapshot",primLVGLsnapshot},
	#endif
	{"LVGLaddbtn",primLVGLaddBtn},
	{"LVGLaddlabel",primLVGLaddLabel},
	{"LVGLaddslider",primLVGLaddSlider},
	{"LVGLaddarc",primLVGLaddArc},
	{"LVGLaddtab",primLVGLaddTab},
	{"LVGLaddseries",primLVGLaddSeries},
	{"LVGLaddchart",primLVGLaddchart},
	{"LVGLsetnextvalue",primLVGLsetnextvalue},
	{"LVGLaddtile",primLVGLaddTile},
	{"LVGLaddbuttonmatrix",primLVGLaddButtonMatrix},
	{"LVGLaddobj",primLVGLaddObject},
	{"LVGLdelobj",primLVGLdelObj},
	{"LVGLsetparent",primLVGLsetParent},
	{"LVGLsetpos",primLVGLsetPos},
	{"LVGLsetsize",primLVGLsetSize},
	{"LVGLsetval", primLVGLsetVal},
	{"LVGLsettext",primLVGLsetText},
	{"LVGLsetattribute",primLVGLsetattribute},
	{"LVGLsetstyle",primLVGLsetstyle},
	{"LVGLgetval", primLVGLgetVal},
	{"LVGLloadscreen",primLVGLloadScreen},
	{"LVGLevent",primLVGLEvent},
	{"LVGLgetevent",primLVGLgetEvent},
	{"LVGLsetcolor", primLVGLsetColor},
	{"LVGLgetallobjs", primLVGLgetallobjs},
	{"LVGLgetallfonts",primLVGLgetallfonts},
	{"LVGLgetallstyles",primLVGLgetallstyles},
	{"LVGLgetallseries",primLVGLgetallseries},
	{"LVGLgetallbtnmaps",primLVGLgetallbtnmaps},
	{"LVGLgetsymbol",primLVGLgetSymbol},
	{"LVGLinit", primLVGLinit},
//	{"LVGLaddimg", primLVGLaddimg},
// 	{"LVGLaddfont",primLVGLaddfont},
	{"LVGLsetscroll",primLVGLsetScroll},
//	{"LVGLpsram",primLVGLpsram},
	#if (defined(LMSDIAPLY) && defined(BREAKOUT))||defined(CYDROT)
		{"fliptouch",primfliptouch},
	#endif
	#if defined(COCUBE) || defined(S3_ROTARY)
		{"LVGLaddgroup",primLVGLaddgroup},
		{"LVGLgroupfocus",primLVGLgroupfocus},
		{"LVGLgroupkey",primLVGLgroupkey},
	#endif
	#if defined(S3_ROTARY)
	 {"LVGLencoder",primLVGLencoder},
	#endif
};


void addLVGLPrims() {
	addPrimitiveSet(LVGLPrims, "tft", sizeof(entries) / sizeof(PrimEntry), entries);
}
