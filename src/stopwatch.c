#include "pebble_os.h"
#include "pebble_app.h"
#include "pebble_fonts.h"


#define MY_UUID { 0xC2, 0x5A, 0x8D, 0x50, 0x10, 0x5F, 0x45, 0xBF, 0xB9, 0x92, 0xCF, 0xF9, 0x58, 0xA7, 0x93, 0xAD }
PBL_APP_INFO(MY_UUID,
             "Stopwatch", "Katharine Berry",
             1, 0, /* App version */
             DEFAULT_MENU_ICON,
             APP_INFO_STANDARD_APP);

Window window;
AppContextRef app;

// Main display
TextLayer big_time_layer;
TextLayer seconds_time_layer;
Layer line_layer;


// Lap time display
#define LAP_TIME_SIZE 4
char lap_times[LAP_TIME_SIZE][9] = {"00:00:00", "00:01:00", "00:02:00", "00:03:00"};
TextLayer lap_layers[LAP_TIME_SIZE]; // an extra temporary layer
int next_lap_layer = 0;
int last_lap_time = 0;

// The documentation claims this is defined, but it is not.
// Define it here for now.
#ifndef APP_TIMER_INVALID_HANDLE
    #define APP_TIMER_INVALID_HANDLE 0xDEADBEEF
#endif

// Actually keeping track of time
time_t start_time = 0;
time_t elapsed_time = 0;
bool started = false;
AppTimerHandle update_timer = APP_TIMER_INVALID_HANDLE;

#define TIMER_UPDATE 1
#define FONT_BIG_TIME RESOURCE_ID_FONT_DEJAVU_SANS_BOLD_SUBSET_32
#define FONT_SECONDS RESOURCE_ID_FONT_DEJAVU_SANS_SUBSET_20

void toggle_stopwatch_handler(ClickRecognizerRef recognizer, Window *window);
void config_provider(ClickConfig **config, Window *window);
void handle_init(AppContextRef ctx);
time_t time_seconds();
void stop_stopwatch();
void start_stopwatch();
void toggle_stopwatch_handler(ClickRecognizerRef recognizer, Window *window);
void reset_stopwatch_handler(ClickRecognizerRef recognizer, Window *window);
void itoa2(int num, char* buffer);
void update_stopwatch();
void handle_timer(AppContextRef ctx, AppTimerHandle handle, uint32_t cookie);
void pbl_main(void *params);
void draw_line(Layer *me, GContext* ctx);
void save_lap_time(int seconds);
void lap_time_handler(ClickRecognizerRef recognizer, Window *window);
void shift_lap_layer(PropertyAnimation* animation, Layer* layer, GRect* target, int distance_multiplier);

void handle_init(AppContextRef ctx) {
    app = ctx;

    window_init(&window, "Stopwatch");
    window_stack_push(&window, true /* Animated */);
    window_set_background_color(&window, GColorBlack);
    window_set_fullscreen(&window, false);

    resource_init_current_app(&APP_RESOURCES);

    // Arrange for user input.
    window_set_click_config_provider(&window, (ClickConfigProvider) config_provider);

    // Get our fonts
    GFont big_font = fonts_load_custom_font(resource_get_handle(FONT_BIG_TIME));
    GFont seconds_font = fonts_load_custom_font(resource_get_handle(FONT_SECONDS));
    GFont laps_font = seconds_font;

    // Root layer
    Layer *root_layer = window_get_root_layer(&window);

    // Set up the big timer.
    text_layer_init(&big_time_layer, GRect(0, 5, 105, 35));
    text_layer_set_background_color(&big_time_layer, GColorBlack);
    text_layer_set_font(&big_time_layer, big_font);
    text_layer_set_text_color(&big_time_layer, GColorWhite);
    text_layer_set_text(&big_time_layer, "00:00");
    text_layer_set_text_alignment(&big_time_layer, GTextAlignmentRight);
    layer_add_child(root_layer, &big_time_layer.layer);

    text_layer_init(&seconds_time_layer, GRect(107, 17, 33, 35));
    text_layer_set_background_color(&seconds_time_layer, GColorBlack);
    text_layer_set_font(&seconds_time_layer, seconds_font);
    text_layer_set_text_color(&seconds_time_layer, GColorWhite);
    text_layer_set_text(&seconds_time_layer, ":00");
    layer_add_child(root_layer, &seconds_time_layer.layer);

    // Draw our nice line.
    layer_init(&line_layer, GRect(0, 50, 144, 2));
    line_layer.update_proc = &draw_line;
    layer_add_child(root_layer, &line_layer);

    // Set up the lap time layers. These will be made visible later.
    for(int i = 0; i < LAP_TIME_SIZE; ++i) {
        text_layer_init(&lap_layers[i], GRect(-139, 57, 139, 30));
        text_layer_set_background_color(&lap_layers[i], GColorClear);
        text_layer_set_font(&lap_layers[i], laps_font);
        text_layer_set_text_color(&lap_layers[i], GColorWhite);
        text_layer_set_text(&lap_layers[i], lap_times[i]);
        layer_add_child(root_layer, &lap_layers[i].layer);
    }
}


void draw_line(Layer *me, GContext* ctx) {
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_draw_line(ctx, GPoint(0, 0), GPoint(140, 0));
    graphics_draw_line(ctx, GPoint(0, 1), GPoint(140, 1));
}

// Time since January 1st 2012 in some timezone, discounting leap years.
// There must be a better way to do this...
time_t time_seconds() {
    PblTm t;
    get_time(&t);
    time_t seconds = t.tm_sec;
    seconds += t.tm_min * 60; 
    seconds += t.tm_hour * 3600;
    seconds += t.tm_yday * 86400;
    seconds += (t.tm_year - 2012) * 31536000;
    return seconds;
}

void stop_stopwatch() {
    started = false;
    if(update_timer != APP_TIMER_INVALID_HANDLE) {
        if(app_timer_cancel_event(app, update_timer)) {
            update_timer = APP_TIMER_INVALID_HANDLE;
        }
    }
}

void start_stopwatch() {
    // Hack: set the start time to now minus the previously recorded time.
    // This lets us pause and unpause the timer.
    start_time = time_seconds() - elapsed_time;
    started = true;

    update_timer = app_timer_send_event(app, 1000, TIMER_UPDATE);
}

void toggle_stopwatch_handler(ClickRecognizerRef recognizer, Window *window) {
    if(started) {
        stop_stopwatch();
    } else {
        start_stopwatch();
    }
}

void reset_stopwatch_handler(ClickRecognizerRef recognizer, Window *window) {
    elapsed_time = 0;
    start_time = time_seconds();
    last_lap_time = 0;
    update_stopwatch();

    // Animate all the laps away.
    static PropertyAnimation animations[4];
    static GRect targets[4];
    for(int i = 0; i < LAP_TIME_SIZE; ++i) {
        shift_lap_layer(&animations[i], &lap_layers[i].layer, &targets[i], 4);
        animation_schedule(&animations[i].animation);
    }
    next_lap_layer = 0;
}

void lap_time_handler(ClickRecognizerRef recognizer, Window *window) {
    int t = elapsed_time - last_lap_time;
    last_lap_time = elapsed_time;
    save_lap_time(t);
}

// There must be some way of doing this besides writing our own...
void itoa2(int num, char* buffer) {
    const char digits[10] = "0123456789";
    if(num > 99) {
        buffer[0] = '9';
        buffer[1] = '9';
        return;
    } else if(num > 9) {
        buffer[0] = digits[num / 10];
    } else {
        buffer[0] = '0';
    }
    buffer[1] = digits[num % 10];
}

void update_stopwatch() {
    static char big_time[] = "00:00";
    static char seconds_time[] = ":00";
    if(started) {
        elapsed_time = time_seconds() - start_time;
    }

    // Now convert to hours/minutes/seconds.
    int seconds = elapsed_time % 60;
    int minutes = (elapsed_time / 60) % 60;
    int hours = elapsed_time / 3600;

    // We can't fit three digit hours, so stop timing here.
    if(hours > 99) {
        stop_stopwatch();
        return;
    }

    itoa2(hours, &big_time[0]);
    itoa2(minutes, &big_time[3]);
    itoa2(seconds, &seconds_time[1]);

    // Now draw the strings.
    text_layer_set_text(&big_time_layer, big_time);
    text_layer_set_text(&seconds_time_layer, seconds_time);
}

void shift_lap_layer(PropertyAnimation* animation, Layer* layer, GRect* target, int distance_multiplier) {
    GRect origin = layer_get_frame(layer);
    *target = origin;
    target->origin.y += target->size.h * distance_multiplier;
    property_animation_init_layer_frame(animation, layer, NULL, target);
    animation_set_duration(&animation->animation, 250);
    animation_set_curve(&animation->animation, AnimationCurveLinear);
}

void save_lap_time(int lap_time) {
    static PropertyAnimation animations[LAP_TIME_SIZE];
    static GRect targets[LAP_TIME_SIZE];

    // Shift them down visually (assuming they actually exist)
    for(int i = 0; i < LAP_TIME_SIZE; ++i) {
        if(i == next_lap_layer) continue; // This is handled separately.
        shift_lap_layer(&animations[i], &lap_layers[i].layer, &targets[i], 1);
        animation_schedule(&animations[i].animation);
    }

    // Once those are done we can slide our new lap time in.
    // First we need to generate a string.
    int seconds = lap_time % 60;
    int minutes = (lap_time / 60) % 60;
    int hours = lap_time / 3600;
    // Fix up our buffer
    itoa2(hours, &lap_times[next_lap_layer][0]);
    itoa2(minutes, &lap_times[next_lap_layer][3]);
    itoa2(seconds, &lap_times[next_lap_layer][6]);

    // Animate it
    static PropertyAnimation entry_animation;
    static GRect origin; origin = GRect(-139, 57, 139, 26);
    static GRect target; target = GRect(5, 57, 139, 26);
    property_animation_init_layer_frame(&entry_animation, &lap_layers[next_lap_layer].layer, &origin, &target);
    animation_set_curve(&entry_animation.animation, AnimationCurveEaseOut);
    animation_set_delay(&entry_animation.animation, 50);
    animation_schedule(&entry_animation.animation);
    next_lap_layer = (next_lap_layer + 1) % LAP_TIME_SIZE;

}

void handle_timer(AppContextRef ctx, AppTimerHandle handle, uint32_t cookie) {
    (void)handle;
    if(cookie == TIMER_UPDATE) {
        update_stopwatch();
        update_timer = app_timer_send_event(ctx, 1000, TIMER_UPDATE);
    }
}

void config_provider(ClickConfig **config, Window *window) {
    config[BUTTON_ID_SELECT]->click.handler = (ClickHandler)toggle_stopwatch_handler;
    config[BUTTON_ID_DOWN]->click.handler = (ClickHandler)reset_stopwatch_handler;
    config[BUTTON_ID_UP]->click.handler = (ClickHandler)lap_time_handler;
    (void)window;
}

void pbl_main(void *params) {
  PebbleAppHandlers handlers = {
    .init_handler = &handle_init,
    .timer_handler = &handle_timer
  };
  app_event_loop(params, &handlers);
}
