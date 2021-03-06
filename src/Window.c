#include <pebble.h>
#include "Window.h"

struct objects *myWindows;

static void MyWindowDestructor(void *vptr) {
  MyWindow *mw = (MyWindow *)vptr;
  if (mw->myTextLayers) {
    freeObjects(mw->myTextLayers);
    mw->myTextLayers = NULL;
  }
  
  if (mw->appTimer) {
    app_timer_cancel(mw->appTimer);
    mw->appTimer = NULL;
  }
  
  if (mw->w) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "About to call window_destroy. mw=%p w=%p", mw, mw->w);
    window_stack_remove(mw->w, false);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Window removed from stack.");
    window_destroy(mw->w);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Window Destroyed.");
    mw->w = NULL;
  }
  
  free(mw);
}

static MyWindow *findMyWindow(Window *w) {
  MyWindow *mw = (MyWindow *)window_get_user_data(w);
  return mw;
}

static void window_load(Window *w) {
  MyWindow *mw = findMyWindow(w);
  unsigned update_freq = (unsigned) -1;
  
  for (int i = 0; i < mw->myTextLayers->count; ++i) {
    MyTextLayer *mtl = (MyTextLayer *)mw->myTextLayers->objects[i];
    if (mtl != NULL) {
      unsigned f = myTextLayerLoad(mw, mtl);
      if (f < update_freq) {
        update_freq = f;
      }
    }
  }
  if (update_freq != (unsigned) -1) {
    windowRescheduleTimer(mw, update_freq);
  }
}

static void timer_callback(void *vptr) {
  MyWindow *mw = (MyWindow *)vptr;
  window_load(mw->w);
}


void windowRescheduleTimer(MyWindow *mw, uint32_t update_freq) {
  if (update_freq != (unsigned) -1) {
    time_t sec;
    uint16_t ms;
    time_ms(&sec, &ms);
    uint32_t now = ms + sec * 1000;
    now = now % ms;
    now -= ms;
    // now is now the delay to the next time the timer should go off.
    if (now < MIN_REFRESH_DELAY) {
      now = MIN_REFRESH_DELAY;
    }
    if (mw->appTimer == NULL) {
      mw->appTimer = app_timer_register(now, timer_callback, mw);
    } else {
      app_timer_reschedule(mw->appTimer, now);
    }
  }
}

static void window_unload(Window *w) {
  MyWindow *mw = findMyWindow(w);
  for (int i = 0; i < mw->myTextLayers->count; ++i) {
    MyTextLayer *mtl = (MyTextLayer *)mw->myTextLayers->objects[i];
    if (mtl != NULL) {
      myTextLayerUnload(mw, mtl);
    }
  }
}

int clearWindow(MyWindow *mw, DictionaryIterator *rdi) {
  freeObjects(mw->myTextLayers);
  mw->myTextLayers = NULL;
  return 0;
}

int resetWindows(DictionaryIterator *rdi) {
  MyWindow *mw;
  int rh;
  objects *tmpWindows;
  APP_LOG(APP_LOG_LEVEL_DEBUG, "resetWindows");
  
  // Doesn't currently work once windows exist.
  if (myWindows != NULL) {
    return 0;
  }
  
  tmpWindows = myWindows;
  myWindows = NULL;
  
  APP_LOG(APP_LOG_LEVEL_DEBUG, "About to create windows structure.");
  myWindows = createObjects(MyWindowDestructor);
  
  APP_LOG(APP_LOG_LEVEL_DEBUG, "objects created. ");
  // Need to create a window to keep the app from
  // exiting, so we might as well make it available.
  rh = allocWindow(NULL);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "root window handle = %d", rh);
  if (rh != 0) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Root window handle %d != 0", rh);
  }
  mw = getWindowByHandle(rh);
  if (mw == NULL) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Root window null");
  }
  
  pushWindow(mw, rdi);
  
  if (tmpWindows) {
    freeObjects(tmpWindows);
  }

  return 0;
}

void deinit_windows() {
  if (myWindows) {
    freeObjects(myWindows);
    myWindows = NULL;
  }
}

int allocWindow(DictionaryIterator *rdi) {
    Tuple *t;
  
    MyWindow *mw = malloc(sizeof(MyWindow));
    if (mw == NULL) {
      return -ENOMEM;
    }
    mw->myTextLayers = createObjects(myTextLayerDestructor);
    if (mw == NULL) {
      free(mw);
      return -ENOMEM;
    }
  
    for (int i = 0;i < NUM_BUTTONS; ++i) {
      mw->button_config[i] = 0;
    }
  
    mw->id = 0;
    mw->w = window_create();
    mw->appTimer = NULL;

  if (mw->w == NULL) {
      freeObjects(mw->myTextLayers);
      free(mw);
      return -ENOMEM;
    }  

    window_set_user_data(mw->w, mw);
  
    // Set handlers to manage the elements inside the Window
    window_set_window_handlers(mw->w, (WindowHandlers) {
      .load = window_load,
      .unload = window_unload
    });
  
    if (rdi != NULL) {
      t = dict_find(rdi, KEY_ID);
      if (t != NULL && t->type == TUPLE_UINT ) {
        mw->id = tuple_get_uint32(t);
      }
    } else {
      mw->id = ROOT_WINDOW_ID;
    }
    return allocObjects(myWindows, mw);
}

uint32_t timestamp() {
  time_t t;
  uint16_t ms;
  time_ms(&t, &ms);
  return (t << 16) | ms;
}

void onClick(ClickRecognizerRef recognizer, void *context) {
  uint8_t count =  click_number_of_clicks_counted(recognizer);
  uint8_t button = click_recognizer_get_button_id(recognizer);
  bool repeating = click_recognizer_is_repeating(recognizer);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "onClick count=%d button=%d repeating=%d", count, button, repeating);
  DictionaryIterator *di = begin_message(STATUS_OK, timestamp());
  dict_write_uint32(di, KEY_CLICK, CLICK_DATA(repeating, count, button));
  send_message(di);
}

void onLongClickDown(ClickRecognizerRef recognizer, void *context) {
  uint8_t count =  click_number_of_clicks_counted(recognizer);
  uint8_t button = click_recognizer_get_button_id(recognizer);
  bool repeating = click_recognizer_is_repeating(recognizer);
  DictionaryIterator *di = begin_message(STATUS_OK, timestamp());
  dict_write_uint32(di, KEY_CLICK, LONG_CLICK_DATA(LONG_CLICK_DOWN, repeating, count, button));
  send_message(di);
}

void onLongClickUp(ClickRecognizerRef recognizer, void *context) {
  uint8_t count =  click_number_of_clicks_counted(recognizer);
  uint8_t button = click_recognizer_get_button_id(recognizer);
  bool repeating = click_recognizer_is_repeating(recognizer);
  DictionaryIterator *di = begin_message(STATUS_OK, timestamp());
  dict_write_uint32(di, KEY_CLICK, LONG_CLICK_DATA(LONG_CLICK_UP, repeating, count, button));
  send_message(di);
}

void click_config_provider(void *context) {
  MyWindow *mw = (MyWindow *)context;
  for (int i = 0; i < NUM_BUTTONS; ++i) {
    if (mw->button_config[i] & BUTTON_WANT_SINGLE_CLICK) {
      window_single_click_subscribe(i, onClick);
    }
    if (mw->button_config[i] & BUTTON_WANT_REPEATED_MASK) {
      window_single_repeating_click_subscribe(i, BUTTON_REPEAT_DELAY(mw->button_config[i]), onClick);
    }
    
    if (mw->button_config[i] & BUTTON_WANT_MULTI_MASK) {
      window_multi_click_subscribe(i, 2, BUTTON_MULTI_MAX(mw->button_config[i]), 0, true, onClick);
    }
    
    if (mw->button_config[i] & BUTTON_LONG_CLICK_MASK) {
      window_long_click_subscribe(i, BUTTON_LONG_CLICK_DELAY(mw->button_config[i]), onLongClickDown,
                                  onLongClickUp);
    } 
  }
}

int pushWindow(MyWindow *mw, DictionaryIterator *rdi) {
  // Show the Window on the watch, with animated=true
  APP_LOG(APP_LOG_LEVEL_DEBUG, "pushWindow mw=%p window=%p", mw, mw == NULL ? NULL : mw->w);
  window_stack_push(mw->w, true);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Pushed window.");
  return 0;
}

int requestClicks(MyWindow *mw, DictionaryIterator *rdi) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "in requestClicks");
    for (int i = 0; i < NUM_BUTTONS; ++i) {
      Tuple *t =  dict_find(rdi, KEY_BUTTON_0 + i);
      if (t == NULL || t->type != TUPLE_UINT || t->length != 4) {
        continue;
      }
      mw->button_config[i] = tuple_get_uint32(t);
      APP_LOG(APP_LOG_LEVEL_DEBUG, "inRequest button=%d config=%08X", i, (unsigned int)mw->button_config[i]);
    }
    window_set_click_config_provider_with_context(mw->w, click_config_provider, mw);
    return 0;
}

MyWindow *getWindowByHandle(int id) {
  if (id < 0 || myWindows == NULL || myWindows->count <= id) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Failed to find window at %d, count = %d, myWindows = %p",
           id, myWindows == NULL ? -1 : myWindows->count, myWindows);
    return NULL;
  }
  
  return (MyWindow *)(myWindows->objects[id]);
}


int getWindowByID(DictionaryIterator *rdi) {
  uint32_t id;
  Tuple *t = dict_find(rdi, KEY_ID);
  if (t == NULL) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Looking for window without id");
    return -ENOWINDOW;
  }
  
  id = tuple_get_uint32(t);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Looking for window %d", (int)id);
  if (id == 0 || myWindows == NULL) {
    return -ENOWINDOW;
  }
  
  for (int i = 0; i < myWindows->count; ++i) {
    MyWindow *mw = (MyWindow *)(myWindows->objects[i]);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Looking at window %p id = %d",
           mw, mw == NULL ? 0 :(int)(mw->id));
    if (mw != NULL && mw->id == id) {
        return i;    
    }
  }
  return -ENOWINDOW;
 }