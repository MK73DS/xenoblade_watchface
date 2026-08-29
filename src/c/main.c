#include <pebble.h>

////////////////////////////////////////////////////
/// STATIC VARIABLES
////////////////////////////////////////////////////

// default locale, will be requested from the phone on init
static char s_locale[6] = "en_US";

// dimensions of the Pebble Time 2 screen
// they are not const because they may vary (eg. timeline peek)
static int s_screen_w = 200;
static int s_screen_h = 228;
// however, the dimension of the framebuffer is constant
#define FB_WIDTH  200
#define FB_HEIGHT 228

/*** Main window ***/
static Window *s_main_window;


/*** Time display ***/
// Time layer
static Layer *s_time_layer;
// Xenoblade digits, they are 50x59
static GBitmap *s_digits_bitmap[10];
// An array to the current bitmaps used for time, each one for the digits AB:CD
static GBitmap *s_time_bitmaps[4];
// Their y position (top edge)
static int s_digits_y = 57; // 228/4

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

// Their GRects, we store them as a function since they depend on the screen height
static inline GRect s_art_grects(const int i) {
  switch (i) {
    case AL_STEPS:
      return GRect(   0, s_screen_h - 32, 40, 18 );
    case AL_CALORIES:
      return GRect(  40, s_screen_h - 32, 40, 18 );
    case AL_BATTERY:
      return GRect(  80, s_screen_h - 25, 40, 18 );
    case AL_HEART:
      return GRect( 120, s_screen_h - 32, 40, 18 );
    case AL_SLEEP:
      return GRect( 160, s_screen_h - 32, 40, 18 );
    case AL_DEEP_SLEEP:
      return GRect( 160, s_screen_h - 18, 40, 18 );
    default:
      return GRect(0,0,0,0); // Shouldn't happen
  }
}

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
static const GRect s_header_bitmap_draw_grect = GRect(10, 0, 180, 17);
// Our own text layer
static Layer *s_calendar_text_layer;
static const GRect s_calendar_text_layer_grect = GRect(0, 1, 200, 17);
static const GRect s_calendar_text_grect = GRect(30, -4, 140, 15);
static const GRect s_calendar_text_contour_grect = GRect(30, 2, 140, 16);
// Text buffer
// Format : "Sat 13 Sep 2026" + null terminated
// In some languages (like French), abbreviated names can take 4 characters
// That's why the buffer is of size 4+1+2+1+4+1+4+1 = 18
static char s_calendar_text[18]; // 

/*** Fonts ***/
GFont s_health_font;
GFont s_ui_font;
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
static int s_clouds_offset;
// crop vertically the clouds, useful when the screen gets vertically smaller (eg. Timeline peek)
static int s_clouds_shift = 0;


// Keeping in memory the battery charge state so we know if the handler got called for a percent change or not
static BatteryChargeState s_previous_charge_state;


//////////////////////////////////////////////////////////////
/// FUNCTIONS
//////////////////////////////////////////////////////////////

// Utility function, works like memcpy (got inspired by memcpy source code)
static inline void copy_with_transparency(void * restrict dest, const void *restrict src, size_t size) {
  char *d = dest;
  const char *s = src;
  while (size--) {
    if (*s & 0b11000000) *d = *s;
    s++; d++;
  }
}

// Contour drawing around text (or other parts of the framebuffer)
// It should be pretty efficient :)

// Useful helper, it copies in black pixels of color 'color' from src and add a black pixel on the left and right of each one
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
// Idea : 
//  - elongate each 'color' pixel from each row : OOXOO ---> OXXXO
//  - keep in memory 3 rows of elongated pixels and merge them
//  - this pads the content both horizontally and vertically by one pixel
static void contour_text(GRect rect, uint8_t * restrict fb, const uint8_t color) {
  // Get the position and size of the GRect in which we work in
  const int x = rect.origin.x;
  const int y = rect.origin.y;
  int w = rect.size.w;
  int h = rect.size.h;

  // The rect can go outside the framebuffer, be careful not to allow it
  // crop if necessary
  if (x + w > FB_WIDTH) {
    w = FB_WIDTH - x;
  }
  if (y + h > FB_HEIGHT) {
    h = FB_HEIGHT - y;
  }
 

  // We need three scanlines
  // In fact, they could be of length w, but that would require the use of malloc which is slower (that what I was told, I don't know)
  // Each scanline will contain an elongated black border
  // They come from non bg_color pixels in the framebuffer
  uint8_t line0[FB_WIDTH];
  uint8_t line1[FB_WIDTH];
  uint8_t line2[FB_WIDTH];

  // Pointer to these lines, we will rotate them in the loop
  uint8_t * restrict top_line = line0;
  uint8_t * restrict mid_line = line1;
  uint8_t * restrict bot_line = line2;

  // Initialize them with zeros
  memset(top_line, 0, w);
  memset(mid_line, 0, w);
  memset(bot_line, 0, w);

  // Pointer to the first useful byte, so that fb_offset[0] is the top left byte in the rectangle
  uint8_t * restrict fb_offset_row = fb + FB_WIDTH*y + x;


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
      uint8_t * restrict fb_offset_next_row = fb_offset_row + FB_WIDTH;
      expand_row(bot_line, fb_offset_next_row, color, w);
    }

    // We write the first w bytes of three_lines that are black into the framebuffer
    // At the same time, we also avoid writing over color pixels
    for (int j=0; j<w; j++) {
      if ((top_line[j] | mid_line[j] | bot_line[j]) == 0 || fb_offset_row[j] == color)
	continue;

      fb_offset_row[j] = GColorBlackARGB8;
    }
    // Going into next row
    fb_offset_row += FB_WIDTH;
  }
}


/////////////// TIME DISPLAY ////////////////

// Function that updates the digits of the time
static void update_time_digits(const struct tm * tick_time) {
  s_time_bitmaps[0] = s_digits_bitmap[tick_time->tm_hour/10];
  s_time_bitmaps[1] = s_digits_bitmap[tick_time->tm_hour%10];
  s_time_bitmaps[2] = s_digits_bitmap[tick_time->tm_min/10];
  s_time_bitmaps[3] = s_digits_bitmap[tick_time->tm_min%10];

  // If the time has been updated, we need to ask it to draw
  layer_mark_dirty(s_time_layer);
}

// Function that draws the time
static void draw_time(struct Layer * layer, GContext * ctx) {
  // Get the framebuffer
  GBitmap * fb = graphics_capture_frame_buffer(ctx);
  uint8_t * fb_data = gbitmap_get_data(fb);

  uint8_t * fb_data_row = fb_data + s_digits_y*FB_WIDTH;
  uint8_t * digit0_data_row = gbitmap_get_data(s_time_bitmaps[0]);
  uint8_t * digit1_data_row = gbitmap_get_data(s_time_bitmaps[1]);
  uint8_t * digit2_data_row = gbitmap_get_data(s_time_bitmaps[2]);
  uint8_t * digit3_data_row = gbitmap_get_data(s_time_bitmaps[3]);

  // The width of the watch is 200px, and each digit is 50px wide, but we overlap a bit the two digits of each number
  // We use the following config : [digit|-4px|digit|8px|digit|-4px|digit]
  for (int i=0; i<59; i++) { // 59 is the digit height
    copy_with_transparency(fb_data_row, digit0_data_row, 50);
    fb_data_row += 50 - 4;
    copy_with_transparency(fb_data_row, digit1_data_row, 50);
    fb_data_row += 50 + 8;
    copy_with_transparency(fb_data_row, digit2_data_row, 50);
    fb_data_row += 50 - 4;
    copy_with_transparency(fb_data_row, digit3_data_row, 50);
    fb_data_row += 50;
    digit0_data_row += 50;
    digit1_data_row += 50;
    digit2_data_row += 50;
    digit3_data_row += 50;
  }

  // Release the framebuffer
  graphics_release_frame_buffer(ctx, fb);
}


// Only update date when it's 00:00 or if we force it
static void update_calendar_text(const struct tm * tick_time, const bool force) {
  if (force || (tick_time->tm_min == 0 && tick_time->tm_hour == 0))
    strftime(s_calendar_text, sizeof(s_calendar_text), "%a %d %h %Y", tick_time);
}

// Draw the date and the header's background
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

///////////////// HEALTH METRICS & BATTERY /////////////////

// convers a number of seconds into HH:MM format (--:-- if 0 seconds)
static void seconds_to_HM_format(char* buffer, const int buffer_size, const HealthValue seconds) {
  if (seconds != 0) {
    const unsigned int H = (seconds / 3600) % 24;
    const unsigned int M = (seconds / 60) % 60;
    snprintf(buffer, buffer_size, "%u:%02u", H, M);
  } else {
    memcpy(buffer, "--:--", 6);
  }
}


// Update the texts for the health metrics
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

// Update the text for the battery metric
static void update_battery_metrics() {
  // TODO do something when the battery is charging and/or plugged?
  snprintf(s_art_texts[AL_BATTERY], 6, "%u%%", s_previous_charge_state.charge_percent);
}

// Draw the health metrics, including the battery
static void draw_health_metrics(struct Layer * layer, GContext * ctx) {
  const struct HealthLayerData * data = layer_get_data(layer);

  const GRect frame = layer_get_frame(layer);
  const GRect bounds = { .size = frame.size };
  
  const GRect contour_rect = GRect(frame.origin.x, frame.origin.y + frame.size.h - 12, frame.size.w, 13); // Modified Rect for just the text that will be written


  graphics_context_set_text_color(ctx, data->text_color);
  graphics_draw_text(ctx, s_art_texts[data->idx], s_health_font, bounds, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);


  // Get the framebuffer
  GBitmap * fb = graphics_capture_frame_buffer(ctx);

  uint8_t * fb_data = gbitmap_get_data(fb);

  contour_text(contour_rect, fb_data, data->text_color.argb);

  // Release the framebuffer
  graphics_release_frame_buffer(ctx, fb); 

}


////////////////////// BACKGROUND ////////////////////////

/*** Utility functions ***/

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
// We assume that dest is the framebuffer, and the source have the same width
// And they are not the same (memcpy has undefined behavior)
static void shift_copy_bitmap(uint8_t* restrict dest, const uint8_t* restrict source, const int src_h, int amount) {
  amount %= FB_WIDTH;

  uint8_t * restrict dest_row = dest;
  const uint8_t * restrict src_row = source;
  for (int i=0; i<src_h; i++) {
    memcpy(dest_row, src_row + amount, FB_WIDTH - amount);
    memcpy(dest_row + FB_WIDTH - amount, src_row, amount);
    
    // Update rows
    dest_row += FB_WIDTH;
    src_row += FB_WIDTH;
  }
}


/*** Drawing functions ***/

// Usual background draw function
static void background_draw_update_proc(struct Layer *layer, GContext *ctx) {
  // Get the framebuffer
  GBitmap *fb = graphics_capture_frame_buffer(ctx);

  uint8_t * restrict fb_data = gbitmap_get_data(fb);
  const uint8_t * restrict clouds_data = gbitmap_get_data(s_clouds_bitmap);
  const uint8_t * restrict ground_data = gbitmap_get_data(s_ground_bitmap);
  const uint8_t * restrict arts_data   = gbitmap_get_data(s_arts_bitmap);

  clouds_data += s_clouds_shift * FB_WIDTH; // vertically shift if the screen height is smaller, 0 when the sceen is full size
  // Copy the data of the clouds directly into the framebuffer
  shift_copy_bitmap(fb_data, clouds_data, s_clouds_size.h - s_clouds_shift, s_clouds_offset);

  // Copy the data of the foreground, with transparency
  // it is 200x64 and drawn 124 pixels above the bottom
  uint8_t * restrict fb_data_shifted = fb_data + (s_screen_h - 124)*FB_WIDTH;
  copy_with_transparency(fb_data_shifted, ground_data, s_ground_npixels);

  // Copy the data of the arts directly into the framebuffer
  // it is 200x60 and drawn 60 pixels above the bottom
  memcpy(fb_data + (s_screen_h - 60)*FB_WIDTH, arts_data, s_arts_npixels);

  // Pad by the grass color if the screen height is less than the framebuffer (otherwise can cause some visual glitches during the Timeline pop up animation)
  memset(fb_data + s_screen_h * FB_WIDTH, GColorDarkGreen.argb, (FB_HEIGHT - s_screen_h)*FB_WIDTH);

  // Release the framebuffer
  graphics_release_frame_buffer(ctx, fb);
}


// The first time we draw the background we also render the UI's text and bake it in the bitmap in memory, so that this text doesn't need to render anymore.
// This runs once, the first time the window needs to render
static void background_draw_one_shot(struct Layer *layer, GContext *ctx) {
  // Draw the background
  background_draw_update_proc(layer, ctx);

  // Render the UI texts depending on the locale
  // To be honest, I have no idea how to handle locale properly
  // Since this runs only once, it should be okay if it is not as optimized as it could be
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
  GRect steps_rect = s_art_grects(AL_STEPS);
  steps_rect.origin.y += 16;
  steps_rect.size.h = 16;
  GRect kcal_rect = s_art_grects(AL_CALORIES);
  kcal_rect.origin.y += 16;
  kcal_rect.size.h = 16;
  GRect bpm_rect = s_art_grects(AL_HEART);
  bpm_rect.origin.y += 16;
  bpm_rect.size.h = 16;

  // Draw the texts
  graphics_draw_text(ctx, steps_text, s_ui_font, steps_rect, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL); // steps
  graphics_draw_text(ctx, kcal_text,  s_ui_font, kcal_rect,  GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL); // kcal
  graphics_draw_text(ctx, bpm_text,   s_ui_font, bpm_rect,   GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL); // bpm

  // Get the framebuffer
  GBitmap * fb = graphics_capture_frame_buffer(ctx);
  uint8_t * restrict fb_data = gbitmap_get_data(fb);

  // Draw the contour of the texts
  contour_text(steps_rect, fb_data, GColorWhite.argb);
  contour_text(kcal_rect,  fb_data, GColorWhite.argb);
  contour_text(bpm_rect,   fb_data, GColorWhite.argb);

  // Get the data of the arts bitmap, and offset it and the framebuffer to 16 lines before their end
  uint8_t * restrict arts_data_offset = gbitmap_get_data(s_arts_bitmap) + s_arts_size.w * (s_arts_size.h - 16);
  uint8_t * restrict fb_data_offset = fb_data + FB_WIDTH * (s_screen_h - 16);

  // Bake the text into the background
  for (int i=0; i<16; i++) {
    memcpy(arts_data_offset, fb_data_offset, s_arts_size.w);
    fb_data_offset += FB_WIDTH;
    arts_data_offset += s_arts_size.w;
  }

  // release the framebuffer
  graphics_release_frame_buffer(ctx, fb);
  
  // set the layer_update_proc to the main one, which does not draw the steps text each time now that it's baked in the background!
  layer_set_update_proc(window_get_root_layer(s_main_window), background_draw_update_proc);
}




//////////////////// EVENT HANDLERS ////////////////////////

// Will run every time a tick is sent (usually every minute)
static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_clouds_day_night(tick_time);
  update_time_digits(tick_time);
  update_calendar_text(tick_time, false);
  update_health_metrics();
  update_battery_metrics();
  
  // Increment s_clouds_offset while keeping it under FB_WIDTH
  s_clouds_offset++;
  if (s_clouds_offset == FB_WIDTH) {
    s_clouds_offset = 0;
  }
}


// Will run every time the battery state changed
// We update the watch every second when charging, every minute when not
// The handler runs each time the battery charge changes, but I don't want to subscribe each time a percent change, so I keep in memory the previous charge state to see if it has changed or not
static void battery_handler(BatteryChargeState charge) {
  if (charge.is_plugged != s_previous_charge_state.is_plugged || charge.is_charging != s_previous_charge_state.is_charging) {
    tick_timer_service_subscribe(charge.is_plugged ? SECOND_UNIT : MINUTE_UNIT, tick_handler);
  }
  s_previous_charge_state = charge;
}



// Timeline peek handlers, we only need .change for now

// Before the animation starts
// static void prv_unobstructed_will_change(GRect final_screen_rect, void *ctx) {
//
// }

// During the animation 
static void prv_unobstructed_change(AnimationProgress progress, void *ctx) {
  // Update the new s_screen_h variable
  s_screen_h = layer_get_unobstructed_bounds(window_get_root_layer(s_main_window)).size.h;

  // Update the digits y position
  s_digits_y = (s_screen_h - 114)/2; // Empirically found, matches with the default position when s_screen_h = 228 (ie 228/4)
  
  // Update the clouds y shift
  s_clouds_shift = (228 - s_screen_h)/2; // Same, should be 0 when s_screen_h = 228

  // Update the health metrics bounds
  for (int i=0; i<AL_COUNT; i++) {
    //s_art_grects automatically computes the good grect for the screen height
    layer_set_frame(s_art_layers[i], s_art_grects(i)); 
  }
}

// After the animation
// static void prv_unobstructed_did_change(void *ctx) {
// 
// }



///////////////////// MAIN WINDOW LOAD /////////////////////////////

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
  s_ground_bitmap = gbitmap_create_with_resource(RESOURCE_ID_BACKGROUND_GROUND); // 200x64
  s_arts_bitmap   = gbitmap_create_with_resource(RESOURCE_ID_BACKGROUND_ARTS);   // 200x60
  
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
  // initialize the time bitmaps to 00:00
  for (int i=0; i<4; i++) {
    s_time_bitmaps[i] = s_digits_bitmap[0];
  }

  // Create the layer (the size doesn't matter, we will write to the framebuffer)
  s_time_layer = layer_create(GRect(0,0,FB_WIDTH,FB_HEIGHT)); 
  // Our own render function
  layer_set_update_proc(s_time_layer, draw_time);
  // Add it as a child to the window
  layer_add_child(window_layer, s_time_layer);

  /******************************
   ***** CALENDAR (header)  *****
   ******************************/

  s_header_bitmap = gbitmap_create_with_resource(RESOURCE_ID_BACKGROUND_HEADER); // 180x17
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
    s_art_layers[i] = layer_create_with_data(s_art_grects(i), sizeof(struct HealthLayerData));

    // set the data
    struct HealthLayerData * data = layer_get_data(s_art_layers[i]);
    data->idx = i;
    data->text_color = i == AL_DEEP_SLEEP ? GColorRichBrilliantLavender : GColorWhite; // Deep sleep has a different color
    
    layer_set_update_proc(s_art_layers[i], draw_health_metrics);
    layer_add_child(window_layer, s_art_layers[i]);
  }


  /****************************
   ****** Timeline peek *******
   ****************************/
  // We only need .change for now
  UnobstructedAreaHandlers handlers = {
//     .will_change = prv_unobstructed_will_change,
     .change = prv_unobstructed_change,
//     .did_change = prv_unobstructed_did_change
  };
 
  // Subscribe to the service that procs when the layout of the watch changes
  unobstructed_area_service_subscribe(handlers, NULL);



  /********************************
   **** Initialize the metrics ****
   ********************************/

  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);
  update_time_digits(tick_time);
  update_clouds_day_night(tick_time);
  update_calendar_text(tick_time, true); // true means "force", we usually don't update the text if it is not a new day (00:00)
  update_health_metrics();
  update_battery_metrics();
  // In case the Timeline is active at the start of the app, run once the .change handler
  prv_unobstructed_change(0, NULL);
}

static void main_window_unload(Window *window) {
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
  // Time
  layer_destroy(s_time_layer);
  // Monado arts
  for (int i=0; i<AL_COUNT; i++) {
    layer_destroy(s_art_layers[i]);
  }
  // Calendar
  layer_destroy(s_calendar_text_layer);
}


////////////////// INIT /////////////////////
// To be honest, I don't know what should be there or in the main_window_load, or if it makes a difference...
static void init() {
  // Get the watch's locale
  memcpy(s_locale, i18n_get_system_locale(), 6);
  setlocale(LC_ALL, s_locale);

  // Create the main window
  s_main_window = window_create();

  // Set the fonts
  s_health_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  s_ui_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  s_calendar_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);

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
