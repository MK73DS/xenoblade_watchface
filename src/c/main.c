#include <pebble.h>

// default locale, will be requested from the phone on init
static char s_locale[6] = "en_US";

// dimensions of the Pebble Time 2 screen
#define SCREEN_WIDTH 200
#define SCREEN_HEIGHT 228

/*** Main window ***/
static Window *s_main_window;


/*** Time display ***/
// Time layers, for each digit AB:CD
static BitmapLayer *s_time_layers[4];
// Xenoblade digits, they are 50x59
static GBitmap *s_digits_bitmap[10];
// Their GRect
// The width of the watch is 200px, and each digit is 50px wide, but we overlap a bit the two digits of each number
// We use the following config : [digit|-4px|digit|8px|digit|-4px|digit]
static const GRect s_digits_grects[4] = {
  GRect(0,   86 - 59/2, 50, 59),
  GRect(46,  86 - 59/2, 50, 59),
  GRect(104, 86 - 59/2, 50, 59),
  GRect(150, 86 - 59/2, 50, 59) 
};

// I don't know if there's a better way to iterate over all the RESOURCE_IDs so I make a table
static const int s_digits_resource_ids[10] = {
  RESOURCE_ID_DIGIT_0,
  RESOURCE_ID_DIGIT_1,
  RESOURCE_ID_DIGIT_2,
  RESOURCE_ID_DIGIT_3,
  RESOURCE_ID_DIGIT_4,
  RESOURCE_ID_DIGIT_5,
  RESOURCE_ID_DIGIT_6,
  RESOURCE_ID_DIGIT_7,
  RESOURCE_ID_DIGIT_8,
  RESOURCE_ID_DIGIT_9
};

/*** Arts text (heart rate, calories, ...) ***/
// Layers
enum ArtsLayers { 
  AL_STEPS,
  AL_CALORIES,
  AL_BATTERY,
  AL_HEART,
  AL_SLEEP,
  AL_DEEP_SLEEP,
  AL_COUNT // = the number of arts, since the first is assigned a value 0
};
static Layer* s_art_layers[AL_COUNT];

// Their GRects
static const GRect s_art_grects[AL_COUNT] = {
  GRect(   0, 196, 40, 18 ),
  GRect(  40, 196, 40, 18 ),
  GRect(  80, 203, 40, 18 ),
  GRect( 120, 196, 40, 18 ),
  GRect( 160, 196, 40, 18 ),
  GRect( 160, 210, 40, 18 )
};

// Texts
static char s_art_texts[6][AL_COUNT];

// Data structure for our own text layers
struct HealthLayerData {
  uint8_t idx;
  GColor text_color;
};

// Function to change the color of a health layer
static inline void health_layer_set_text_color(Layer * layer, const GColor color) {
  ((struct HealthLayerData *) layer_get_data(layer)) -> text_color = color;
}

/*** Calendar ***/
// Header bitmap
static GBitmap *s_header_bitmap;
static const GRect s_header_bitmap_draw_grect = GRect(10, 0, 180, 15);
// Our own text layer
static Layer *s_calendar_text_layer;
static const GRect s_calendar_text_layer_grect = GRect(0, 1, 200, 15);
static const GRect s_calendar_text_grect = GRect(60, -2, 80, 14);
static const GRect s_calendar_text_contour_grect = GRect(60, 1, 80, 14);
// Text buffer
static char s_calendar_text[11]; // XX/YY/ZZZZ + null terminated


/*** Fonts ***/
GFont s_health_font;
GFont s_calendar_font;



/*** Background images ***/
// Background bitmaps
static GBitmap *s_clouds_bitmap;
static GBitmap *s_ground_bitmap;
static GBitmap *s_arts_bitmap;

// Background size (better to not recalculate them each time)
static GSize s_clouds_size;
static GSize s_ground_size;
static GSize s_arts_size;

// Background number of pixels (same)
static size_t s_clouds_npixels;
static size_t s_ground_npixels;
static size_t s_arts_npixels;  


// The clouds will change color depending on the hour
// we record in which state the current clouds are and we also record their colors for the transition
// This allows us to not store another bitmap just for night clouds 
static bool s_current_clouds_day;
// Using a switch case should allow the compiler to optimize the search and replace process (according to the internet)
static inline uint8_t day_to_night(uint8_t color) {
  switch(color) {
    case GColorWhiteARGB8:
      return GColorPictonBlueARGB8;
    case GColorCelesteARGB8:
      return GColorBlueMoonARGB8;
    case GColorVividCeruleanARGB8:
      return GColorBlueARGB8;
    case GColorBlueMoonARGB8:
      return GColorDukeBlueARGB8;
    case GColorBlueARGB8:
      return GColorOxfordBlueARGB8;
    default:
      return color;
  }
}
static inline uint8_t night_to_day(uint8_t color) {
  switch(color) {
    case GColorPictonBlueARGB8:
      return GColorWhiteARGB8;
    case GColorBlueMoonARGB8:
      return GColorCelesteARGB8;
    case GColorBlueARGB8:
      return GColorVividCeruleanARGB8;
    case GColorDukeBlueARGB8:
      return GColorBlueMoonARGB8;
    case GColorOxfordBlueARGB8:
      return GColorBlueARGB8;
    default:
      return color;
  }
}

// How much the clouds are moved, incremented by one each tick_time
static int clouds_offset;




// Keeping in memory the battery charge state so we know if the handler got called for a percent change or not
static BatteryChargeState s_previous_charge_state;


// Function that updates the digits of the time
// if not forced, it only updates digits when they have changed
static void update_time_digits(const struct tm * tick_time, const bool force) {
  // s_time_layers contains 4 layers in the order AB:CD for the time

  // Always update minutes unit digit
  const int minute_digit = tick_time->tm_min%10;
  bitmap_layer_set_bitmap(s_time_layers[3], s_digits_bitmap[minute_digit]);

  // Only update minutes tens digit if of the form XX:X0
  if (force || minute_digit == 0) {
    const int minute_tens = tick_time->tm_min/10;
    bitmap_layer_set_bitmap(s_time_layers[2], s_digits_bitmap[minute_tens]);

    // Only update hour unit digit if of the form XX:00
    if (force || minute_tens == 0) {
      const int hour_digit = tick_time->tm_hour%10;
      bitmap_layer_set_bitmap(s_time_layers[1], s_digits_bitmap[hour_digit]);

      // Only update hour tens digit if of the form X0:00
      if (force || hour_digit == 0) {
	bitmap_layer_set_bitmap(s_time_layers[0], s_digits_bitmap[tick_time->tm_hour/10]);
      }
    }
  }
}

// Only update date when it's 00:00 or if we force it
static void update_calendar_text(const struct tm * tick_time, const bool force) {
  if (force || (tick_time->tm_min == 0 && tick_time->tm_hour == 0))
    strftime(s_calendar_text, sizeof(s_calendar_text), "%d/%m/%Y", tick_time);
}

static void seconds_to_HM_format(char* buffer, const int buffer_size, const HealthValue seconds) {
  if (seconds != 0) {
    const unsigned int H = (seconds / 3600) % 24;
    const unsigned int M = (seconds / 60) % 60;
    snprintf(buffer, buffer_size, "%u:%02u", H, M);
  } else {
    memcpy(buffer, "--:--", 6);
  }
}



static void update_health_metrics() {
  // Calories
  const HealthValue active_calories = health_service_sum_today(HealthMetricActiveKCalories);
  const HealthValue resting_calories = health_service_sum_today(HealthMetricRestingKCalories);
  snprintf(s_art_texts[AL_CALORIES], 6, "%li", active_calories + resting_calories);

  // Heart rate
  // When the last good known value is too old, it returns 0
  // We can get the last raw value, less accurate but better than nothing
  // Otherwise, we display --
  HealthValue bpm = health_service_peek_current_value(HealthMetricHeartRateBPM);
  GColor bpm_color = GColorWhite;
  if (bpm == 0) {
    bpm = health_service_peek_current_value(HealthMetricHeartRateRawBPM);
    bpm_color = GColorCeleste;
  }

  health_layer_set_text_color(s_art_layers[AL_HEART], bpm_color);

  if (bpm == 0) {
    memcpy(s_art_texts[AL_HEART], "--", 3);
  } else {
    snprintf(s_art_texts[AL_HEART], 6, "%li", bpm);
  }

  // Steps
  const HealthValue steps = health_service_sum_today(HealthMetricStepCount);
  snprintf(s_art_texts[AL_STEPS], 6, "%li", steps);

  // Sleep
  const HealthValue sleep = health_service_sum_today(HealthMetricSleepSeconds);
  seconds_to_HM_format(s_art_texts[AL_SLEEP], 6, sleep);

  // Deep sleep
  const HealthValue deepsleep = health_service_sum_today(HealthMetricSleepRestfulSeconds);
  seconds_to_HM_format(s_art_texts[AL_DEEP_SLEEP], 6, deepsleep);
}

static void update_battery_metrics() {
  // TODO do something when the battery is charging and/or plugged
  snprintf(s_art_texts[AL_BATTERY], 6, "%u%%", s_previous_charge_state.charge_percent);
}


// Changes the bitmap of the clouds to the day/night version depending on the hour
// TODO : get the actual sunrise and sunset times
static void update_clouds_day_night(const struct tm * tick_time) {
  // Check if it is day or night, and if we need to change anything
  const bool is_day = tick_time->tm_hour < 21 && tick_time->tm_hour > 6;

  // Do nothing if it is still the same time of day
  if (s_current_clouds_day == is_day)
    return;

  s_current_clouds_day = is_day;
  
  // Get the raw data
  uint8_t * clouds_data = gbitmap_get_data(s_clouds_bitmap);

  if (s_current_clouds_day) { // night->day transition
    for (size_t i=0; i<s_clouds_npixels; i++) {
      clouds_data[i] = night_to_day(clouds_data[i]);
    }
  } else {
    for (size_t i=0; i<s_clouds_npixels; i++) {
      clouds_data[i] = day_to_night(clouds_data[i]);
    }
  }
}

// Copies a bitmap in the framebuffer (at the top) while shifting it
// This can make cool effects
// We assume that source and destination have the same width!
// And they are not the same (memcpy has undefined behavior)
static void shift_copy_bitmap(const uint8_t* restrict source, uint8_t* restrict dest, const struct GSize* src_size, int amount) {
  amount %= src_size->w;

  uint8_t * restrict dest_row = dest;
  const uint8_t * restrict src_row = source;
  for (int i=0; i<src_size->h; i++) {
    memcpy(dest_row, src_row + amount, src_size->w - amount);
    memcpy(dest_row + src_size->w - amount, src_row, amount);
    
    // Update rows
    dest_row += src_size->w;
    src_row += src_size->w;
  }
}




//////////////////// Contour drawing around text (or other parts of the framebuffer) /////////////////
// It should be pretty efficient :)
//
// Useful macro, it copies in black non bg_color pixels from src and add a black pixel on the left and right of each one
static void expand_row(uint8_t * restrict row, const uint8_t * restrict src, const uint8_t color, const int w) {
  if (w <= 0) // Should not happen, but just to be sure
    return;

  for (int j=1; j<w-1; j++) {
    if (src[j] != color)
      continue;
    
    row[j-1] = GColorBlackARGB8;
    row[j]   = GColorBlackARGB8;
    row[j+1] = GColorBlackARGB8;
  }
  // Handle edge cases outside the loop for less branching
  // j = 0
  if (src[0] == color) {
    row[0] = GColorBlackARGB8;
    if (w > 1) row[1] = GColorBlackARGB8;
  }
  // j = w-1
  if (src[w-1] == color) {
    row[w-1] = GColorBlackARGB8;
    if (w > 1) row[w-2] = GColorBlackARGB8;
  }
}

// draws a black contour around all pixels chunks of color 'color' inside the framebuffer fb
// 'restrict' tells the compiler that no other pointer points to fb, and this optimizes a bit the writes to fb
static void contour_text(GRect rect, uint8_t * restrict fb, const uint8_t color) {
  // Get the position and size of the GRect in which to look for the icon
  const int x = rect.origin.x;
  const int y = rect.origin.y;
  int w = rect.size.w;
  int h = rect.size.h;

  // The rect can go outside the framebuffer, be careful not to allow it
  // crop if necessary
  if (x + w > SCREEN_WIDTH) {
    w = SCREEN_WIDTH - x;
  }
  if (y + h > SCREEN_HEIGHT) {
    h = SCREEN_HEIGHT - y;
  }
 

  // We need three scanlines
  // In fact, they could be of length w, but that would require the use of malloc which is slower (that what I was told, I don't know)
  // Each scanline will contain an elongated black border
  // They come from non bg_color pixels in the framebuffer
  uint8_t line0[SCREEN_WIDTH];
  uint8_t line1[SCREEN_WIDTH];
  uint8_t line2[SCREEN_WIDTH];

  // Pointer to these lines, we will rotate them in the loop
  uint8_t * restrict top_line = line0;
  uint8_t * restrict mid_line = line1;
  uint8_t * restrict bot_line = line2;

  // Initialize them with zeros
  memset(top_line, 0, w);
  memset(mid_line, 0, w);
  memset(bot_line, 0, w);

  // Pointer to the first useful byte, so that fb_offset[0] is the top left byte in the rectangle
  uint8_t * restrict fb_offset_row = fb + SCREEN_WIDTH*y + x;


  // Looping over all lines
  for (int i=0; i<h; i++) {
    // We can reuse the two last lines shadows if i>0
    if (i != 0) { // We only need to get the bottom line
      // Rotate lines
      uint8_t * tmp_line = top_line;
      top_line = mid_line; // top <- mid
      mid_line = bot_line; // mid <- bot
      bot_line = tmp_line; // bot <- top (tmp)
      // Clear the last line
      memset(bot_line, 0, w);
    } else { // We need to get the mid line and the bottom one (the top one is zeros, since outside of the rectangle) 
      // Middle line
      expand_row(mid_line, fb_offset_row, color, w);
    }
    // Bottom line
    if (i != h-1) {
      uint8_t * restrict fb_offset_next_row = fb_offset_row + SCREEN_WIDTH;
      expand_row(bot_line, fb_offset_next_row, color, w);
    }

    // We write the first w bytes of three_lines that are black into the framebuffer
    // At the same time, we also avoid writing over non bg_color pixels
    for (int j=0; j<w; j++) {
      if ((top_line[j] | mid_line[j] | bot_line[j]) == 0 || fb_offset_row[j] == color)
	continue;

      fb_offset_row[j] = GColorBlackARGB8;
    }
    // Going into next row
    fb_offset_row += SCREEN_WIDTH;
  }
}

/////////////////////// END /////////////////////

static void proc_draw_calendar(struct Layer * layer, GContext * ctx) {
  // Draw the header background first
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  graphics_draw_bitmap_in_rect(ctx, s_header_bitmap, s_header_bitmap_draw_grect);


  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, s_calendar_text, s_calendar_font, s_calendar_text_grect, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

  // Draw the contour of the text
  GBitmap * fb = graphics_capture_frame_buffer(ctx);
  uint8_t * fb_data = gbitmap_get_data(fb);
  contour_text(s_calendar_text_contour_grect, fb_data, GColorWhite.argb);
  graphics_release_frame_buffer(ctx, fb);
}


static void draw_health_metrics(struct Layer * layer, GContext * ctx) {
  const GRect bounds = layer_get_bounds(layer); // Does not contain positionnal info, but that's the one for drawing text
  const GRect frame = layer_get_frame(layer); // Contains positionnal info
  
  const GRect contour_rect = GRect(frame.origin.x, frame.origin.y + frame.size.h - 12, frame.size.w, 13); // Modified Rect for just the text that will be written

  const struct HealthLayerData * data = layer_get_data(layer);

  graphics_context_set_text_color(ctx, data->text_color);
  graphics_draw_text(ctx, s_art_texts[data->idx], s_health_font, bounds, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);


  // Get the framebuffer
  GBitmap * fb = graphics_capture_frame_buffer(ctx);

  uint8_t * fb_data = gbitmap_get_data(fb);

  contour_text(contour_rect, fb_data, data->text_color.argb);

  // Release the framebuffer
  graphics_release_frame_buffer(ctx, fb); 

}

static void background_draw_update_proc(struct Layer *layer, GContext *ctx) {
  // Get the framebuffer
  GBitmap *fb = graphics_capture_frame_buffer(ctx);

  uint8_t * restrict fb_data = gbitmap_get_data(fb);
  const uint8_t * restrict clouds_data = gbitmap_get_data(s_clouds_bitmap);
  const uint8_t * restrict ground_data = gbitmap_get_data(s_ground_bitmap);
  const uint8_t * restrict arts_data   = gbitmap_get_data(s_arts_bitmap);

  // Copy the data of the clouds directly into the framebuffer
  shift_copy_bitmap(clouds_data, fb_data, &s_clouds_size, clouds_offset);

  // Copy the data of the foreground, with transparency
  uint8_t * restrict fb_data_shifted = fb_data + 104*SCREEN_WIDTH;
  for (size_t i=0; i<s_ground_npixels; i++) {
    if (ground_data[i] != 0)
      fb_data_shifted[i] = ground_data[i];
  }

  // Copy the data of the arts directly into the framebuffer
  memcpy(fb_data + 168*SCREEN_WIDTH, arts_data, s_arts_npixels);

  // Release the framebuffer
  graphics_release_frame_buffer(ctx, fb);
}


// This runs once, the first time the window needs to render
// We use this opportunity to bake in some text into the background so that we don't have to render them each minute!
static void background_draw_one_shot(struct Layer *layer, GContext *ctx) {
  // Draw the background
  background_draw_update_proc(layer, ctx);

  // Render the UI texts depending on the locale
  char steps_text[6]; // It can't have more than 5 characters in our layout
  char bpm_text[4]; // In all languages, it's 3 characters long
  const char kcal_text[5] = "kcal"; // Doesn't change with locale
  // We get the language (eg. en_US -> en, fr_FR -> fr)
  // In order to do a switch statement, we convert them to an integer
  int lang = 0x100 * ((int) s_locale[0]) + (int) s_locale[1];
  switch (lang) {
    case 0x6672: // fr
      memcpy(steps_text, "pas", 4);
      memcpy(bpm_text, "bpm", 4);
      break;
    case 0x6573: // es
      memcpy(steps_text, "pasos", 6);
      memcpy(bpm_text, "lpm", 4);
      break;
    case 0x6974: // it
      memcpy(steps_text, "passi", 6);
      memcpy(bpm_text, "bpm", 4);
      break;
    case 0x6465: // de
      memcpy(steps_text, "schr.", 6);
      memcpy(bpm_text, "spm", 4);
      break;
    case 0x7074: // pt
      memcpy(steps_text, "pass.", 6);
      memcpy(bpm_text, "bpm", 4);
      break;
    default: // We default to english, in particular, chinese not supported
      memcpy(steps_text, "steps", 6);
      memcpy(bpm_text, "bpm", 4);
      break;
  }
  // Set the text color
  graphics_context_set_text_color(ctx, GColorWhite);

  // Set the rects in which to draw them, they're below the rects for the value of these metrics, and a bit shorter
  GRect steps_rect = s_art_grects[AL_STEPS];
  steps_rect.origin.y += 16;
  steps_rect.size.h = 16;
  GRect kcal_rect = s_art_grects[AL_CALORIES];
  kcal_rect.origin.y += 16;
  kcal_rect.size.h = 16;
  GRect bpm_rect = s_art_grects[AL_HEART];
  bpm_rect.origin.y += 16;
  bpm_rect.size.h = 16;

  // Draw the texts
  graphics_draw_text(ctx, steps_text, s_calendar_font, steps_rect, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL); // steps
  graphics_draw_text(ctx, kcal_text,  s_calendar_font, kcal_rect,  GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL); // kcal
  graphics_draw_text(ctx, bpm_text,   s_calendar_font, bpm_rect,   GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL); // bpm

  // Get the framebuffer
  GBitmap * fb = graphics_capture_frame_buffer(ctx);
  uint8_t * restrict fb_data = gbitmap_get_data(fb);

  // Draw the contour of the texts
  contour_text(steps_rect, fb_data, GColorWhite.argb);
  contour_text(kcal_rect,  fb_data, GColorWhite.argb);
  contour_text(bpm_rect,   fb_data, GColorWhite.argb);

  // Get the data of the arts bitmap, and offset it and the framebuffer to 16 lines before their end
  uint8_t * restrict arts_data_offset = gbitmap_get_data(s_arts_bitmap) + s_arts_size.w * (s_arts_size.h - 16);
  uint8_t * restrict fb_data_offset = fb_data + SCREEN_WIDTH * (SCREEN_HEIGHT - 16);

  // Bake the text into the background
  for (int i=0; i<16; i++) {
    memcpy(arts_data_offset, fb_data_offset, s_arts_size.w);
    fb_data_offset += SCREEN_WIDTH;
    arts_data_offset += s_arts_size.w;
  }

  // release the framebuffer
  graphics_release_frame_buffer(ctx, fb);
  
  // set the layer_update_proc to the main one, which does not draw the steps text each time now that it's baked in the background!
  layer_set_update_proc(window_get_root_layer(s_main_window), background_draw_update_proc);
}



// Will run every time a tick is sent (usually every minute)
static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_clouds_day_night(tick_time);
  update_time_digits(tick_time, false);   // false means it only updates if needed (when time changes)
  update_calendar_text(tick_time, false); //
  update_health_metrics();
  update_battery_metrics();
  
  // Increment clouds_offset while keeping it under SCREEN_WIDTH
  clouds_offset++;
  if (clouds_offset == SCREEN_WIDTH) {
    clouds_offset = 0;
  }
}


// Will run every time the battery state changed
// It also runs each time the battery charge changes, but I don't want
static void battery_handler(BatteryChargeState charge) {
  if (charge.is_plugged != s_previous_charge_state.is_plugged || charge.is_charging != s_previous_charge_state.is_charging) {
    tick_timer_service_subscribe(charge.is_plugged ? SECOND_UNIT : MINUTE_UNIT, tick_handler);
  }
  s_previous_charge_state = charge;
}




static void main_window_load(Window *window) {
  // Get information about the Window
  Layer *window_layer = window_get_root_layer(window);
  // GRect bounds = layer_get_bounds(window_layer);

  layer_set_update_proc(window_layer, background_draw_one_shot); // background_draw_update_proc);

  /******************************
   ********* BACKGROUND *********
   ******************************/

  s_current_clouds_day = true; // set to day by default, will be changed accordingly during initialization
  
  // Bitmaps
  s_clouds_bitmap = gbitmap_create_with_resource(RESOURCE_ID_BACKGROUND_CLOUDS); // 200x168
  s_ground_bitmap = gbitmap_create_with_resource(RESOURCE_ID_BACKGROUND_GROUND);     // 200x64
  s_arts_bitmap   = gbitmap_create_with_resource(RESOURCE_ID_BACKGROUND_ARTS);       // 200x60
  
  // Their size
  s_clouds_size = gbitmap_get_bounds(s_clouds_bitmap).size;
  s_ground_size = gbitmap_get_bounds(s_ground_bitmap).size;
  s_arts_size   = gbitmap_get_bounds(s_arts_bitmap).size;

  // Their number of pixels
  s_clouds_npixels = s_clouds_size.w * s_clouds_size.h;
  s_ground_npixels = s_ground_size.w * s_ground_size.h;
  s_arts_npixels   = s_arts_size.w   * s_arts_size.h;

  /******************************
   ******** DISPLAY TIME ********
   ******************************/

  // Load the Xenoblade digits
  for (int i=0; i<10; i++) {
    s_digits_bitmap[i] = gbitmap_create_with_resource(s_digits_resource_ids[i]);
  }

  // Create the time BitmapLayers
  for (int i=0; i<4; i++) {
    s_time_layers[i] = bitmap_layer_create(s_digits_grects[i]);
    // Set the compositing mode to allow transparency
    bitmap_layer_set_compositing_mode(s_time_layers[i], GCompOpSet);

    // Add the layer as child of the main window
    layer_add_child(window_layer, bitmap_layer_get_layer(s_time_layers[i]));
  }

  /******************************
   ***** CALENDAR (header)  *****
   ******************************/

  s_header_bitmap = gbitmap_create_with_resource(RESOURCE_ID_BACKGROUND_HEADER); // 180x15
  s_calendar_text_layer = layer_create(s_calendar_text_layer_grect);
  layer_set_update_proc(s_calendar_text_layer, proc_draw_calendar);
  layer_add_child(window_layer, s_calendar_text_layer);


  /******************************
   **** MONADO ARTS (health) ****
   ******************************/

  // We create the layers that will contain the texts of the health metrics
  // We do not use TextLayers because we are implementing our own text rendering for the black contour
  // We add a HealthLayerData to each layer, containing an id for identifying each layer (so we can use the same update_proc) and the text color
  for(uint8_t i=0; i<AL_COUNT; i++) { // Iterate over all art layers
    s_art_layers[i] = layer_create_with_data(s_art_grects[i], sizeof(struct HealthLayerData));

    // set the data
    struct HealthLayerData * data = layer_get_data(s_art_layers[i]);
    data->idx = i;
    data->text_color = i == AL_DEEP_SLEEP ? GColorRichBrilliantLavender : GColorWhite; // Deep sleep has a different color
    
    layer_set_update_proc(s_art_layers[i], draw_health_metrics);
    layer_add_child(window_layer, s_art_layers[i]);
  }


  // Initialize the displayed time, health and battery status
  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);
  update_time_digits(tick_time, true);
  update_clouds_day_night(tick_time);
  update_calendar_text(tick_time, true);
  update_health_metrics();
  update_battery_metrics();
}

static void main_window_unload(Window *window) {
  /*** Destroy all the bitmap layers ***/
  // Digits
  for (int i=0; i<4; i++) {
    bitmap_layer_destroy(s_time_layers[i]);
  }

  /*** Destroy all the bitmaps ***/
  // Backgrounds
  gbitmap_destroy(s_clouds_bitmap);
  gbitmap_destroy(s_arts_bitmap);
  gbitmap_destroy(s_ground_bitmap);

  // Digits
  for (int i=0; i<10; i++) {
    gbitmap_destroy(s_digits_bitmap[i]);
  }

  // Header
  gbitmap_destroy(s_header_bitmap);


  /*** Destroy all the layers ***/
  // Monado arts
  for (int i=0; i<AL_COUNT; i++) {
    layer_destroy(s_art_layers[i]);
  }
  // Calendar
  layer_destroy(s_calendar_text_layer);
}



static void init() {
  // Get the watch's locale
  memcpy(s_locale, i18n_get_system_locale(), 6);

  // Create the main window
  s_main_window = window_create();

  // Set the fonts
  s_health_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  s_calendar_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);

  // Setting the handlers
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload
  });

  // Get the current battery charge state before subscribing to the handler
  s_previous_charge_state = battery_state_service_peek();

  // Register with the TickTimerService and the BatteryStateService
  battery_state_service_subscribe(battery_handler);
  tick_timer_service_subscribe(s_previous_charge_state.is_plugged ? SECOND_UNIT : MINUTE_UNIT, tick_handler); // We update every seconds if plugged

  // Push the window (with no animation)
  window_stack_push(s_main_window, false);
}

static void deinit() {
  // Destroy the main window
  window_destroy(s_main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
