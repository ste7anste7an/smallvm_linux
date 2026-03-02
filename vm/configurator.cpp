#include "configurator.h"
#include <cstring>
#include <cstdlib>

namespace configurator {

static bool g_warn_unknown = false;
static bool g_debug = false;

void setWarnUnknownKeys(bool enable) { g_warn_unknown = enable; }
void setDebug(bool enable) { g_debug = enable; }

// ---------- Helpers ----------

static void trim(char* s) {
    if (!s) return;
    char* p = s;
    while (*p && isspace(*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n && isspace(s[n - 1])) s[--n] = '\0';
}

static bool parse_bool(const char* v) {
    return v &&
           (!strcasecmp(v, "true") ||
            !strcasecmp(v, "yes")  ||
            !strcasecmp(v, "on")   ||
            !strcmp(v, "1"));
}

static bool parse_gpio(const char* v, int& out) {
    if (!v) return false;

    if (!strncmp(v, "gpio_", 5)) {
        int p = atoi(v + 5);
        out = (p == 0) ? PIN_ZERO : p;
        return true;
    }

    out = atoi(v);
    return true;
}

static bool split_kv(char* line, char* key, size_t ksz, char* val, size_t vsz) {
    char* eq = strchr(line, '=');
    if (!eq) return false;
    *eq = '\0';
    strncpy(key, line, ksz);
    strncpy(val, eq + 1, vsz);
    key[ksz - 1] = val[vsz - 1] = '\0';
    trim(key);
    trim(val);
    return key[0];
}

// ---------- Table-driven mapping ----------

enum class Type : uint8_t { Int, Bool, Str, Pin };

struct Entry {
    const char* section;
    const char* key;
    Type type;
    size_t offset;
    size_t str_len;
};

#define DST(cfg, e) (reinterpret_cast<uint8_t*>(cfg) + (e).offset)

#define CFG_INT(sec,key,field) \
    { sec, key, Type::Int, offsetof(Config, field), 0 }

#define CFG_BOOL(sec,key,field) \
    { sec, key, Type::Bool, offsetof(Config, field), 0 }

#define CFG_STR(sec,key,field) \
    { sec, key, Type::Str, offsetof(Config, field), sizeof(((Config*)0)->field) }

#define CFG_PIN(sec,key,field) \
    { sec, key, Type::Pin, offsetof(Config, field), 0 }

// ---------- Config map ----------

static const Entry MAP[] = {

    // lcd
    CFG_STR ("lcd","controller", lcd.controller),
    CFG_INT ("lcd","spi",        lcd.spi),
    CFG_PIN ("lcd","mosi",       lcd.mosi),
    CFG_PIN ("lcd","miso",       lcd.miso),
    CFG_PIN ("lcd","sclk",       lcd.sclk),
    CFG_PIN ("lcd","cs",         lcd.cs),
    CFG_PIN ("lcd","dc",         lcd.dc),
    CFG_PIN ("lcd","rst",        lcd.rst),
    CFG_INT ("lcd","rotation",   lcd.rotation),
    CFG_BOOL ("lcd","colorBGR",      lcd.colorBGR),
    CFG_BOOL("lcd","invert",     lcd.invert),
    CFG_PIN ("lcd","backlight",  lcd.backlight),
    CFG_INT ("lcd","width",      lcd.width),
    CFG_INT ("lcd","height",     lcd.height),
    CFG_INT ("lcd","col_offset", lcd.col_offset),
    CFG_INT ("lcd","row_offset", lcd.row_offset),

    // lvgl
    CFG_INT ("lvgl","width",     lvgl.width),
    CFG_INT ("lvgl","height",    lvgl.height),

    // touch
    CFG_STR ("touch","controller", touch.controller),
    CFG_STR ("touch","interface",  touch.interface),
    CFG_INT ("touch","spi",        touch.spi),
    CFG_INT ("touch","i2c",        touch.i2c),
    CFG_PIN ("touch","irq",        touch.irq),
    CFG_PIN ("touch","miso",       touch.miso),
    CFG_PIN ("touch","mosi",       touch.mosi),
    CFG_PIN ("touch","sclk",       touch.sclk),
    CFG_PIN ("touch","cs",         touch.cs),
    CFG_INT ("touch","rotation",   touch.rotation),
    CFG_PIN ("touch","sda",        touch.sda),
    CFG_PIN ("touch","scl",        touch.scl),
    CFG_BOOL("touch","flip_x",     touch.flip_x),
    CFG_BOOL("touch","flip_y",     touch.flip_y),
    CFG_BOOL("touch","flip_x_y",   touch.flip_x_y),

    // other
    CFG_PIN ("other","sda",     other.sda),
    CFG_PIN ("other","scl",     other.scl),
    CFG_PIN ("other","rx_pin", other.rx_pin),
    CFG_PIN ("other","tx_pin", other.tx_pin),
};

// ---------- Apply ----------

static bool apply_kv(Config* cfg, const char* sec, const char* key, const char* val) {
    for (const auto& e : MAP) {
        if (!strcmp(e.section, sec) && !strcmp(e.key, key)) {

            uint8_t* dst = DST(cfg, e);

            switch (e.type) {
                case Type::Int:
                    *reinterpret_cast<int*>(dst) = atoi(val);
                    break;

                case Type::Bool:
                    *reinterpret_cast<bool*>(dst) = parse_bool(val);
                    break;

                case Type::Str:
                    strncpy((char*)dst, val, e.str_len);
                    ((char*)dst)[e.str_len - 1] = '\0';
                    break;

                case Type::Pin: {
                    int p;
                    if (parse_gpio(val, p)) {
                        *reinterpret_cast<int*>(dst) = p;
                    }
                    break;
                }
            }

            if (g_debug) {
                Serial.printf("CFG [%s] %s = %s\n", sec, key, val);
            }

            return true;
        }
    }
    return false;
}

// ---------- Loader ----------

bool loadConfig(Config* cfg) {
    File f = LittleFS.open("/config.txt", "r");
    if (!f) return false;

    char line[160], section[32] = "";

    while (f.available()) {
        size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
        line[n] = '\0';
        trim(line);

        if (!line[0] || line[0] == '#') continue;

        if (line[0] == '[') {
            sscanf(line, "[%31[^]]", section);
            trim(section);
            continue;
        }

        char k[64], v[96];
        if (!split_kv(line, k, sizeof(k), v, sizeof(v))) continue;

        if (!apply_kv(cfg, section, k, v) && g_warn_unknown) {
            Serial.printf("Unknown key [%s] %s\n", section, k);
        }
    }

    f.close();
    return true;
}

} // namespace configurator
