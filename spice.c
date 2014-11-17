#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include "spice.h"
#include "globals.h"
#include "native.h"

pthread_t spice_worker;
pthread_t mainloop_worker;

void engine_mainloop_worker(void *data)
{
    spice_conn_data_t *conn_data = global_state.conn_data;
    int result;
    
    GLUE_DEBUG("InitializeLogging\n");
    SpiceGlibGlue_InitializeLogging(0);
    
    GLUE_DEBUG("engine_mainloop_worker\n");
    result = SpiceGlibGlue_Init();
    
    GLUE_DEBUG("engine_mainloop_worker: result=%d\n", result);
    
    GLUE_DEBUG("engine_spice_worker\n");
    result = SpiceGlibGlue_Connect(conn_data->host,
                                   conn_data->port, "",
                                   conn_data->wsport,
                                   conn_data->password, NULL, NULL, 0);
    
    GLUE_DEBUG("engine_spice_worker: result=%d\n", result);
}

void engine_spice_worker(void *data)
{
    spice_conn_data_t *conn_data = global_state.conn_data;
    int result;
    
    GLUE_DEBUG("engine_spice_worker\n");
    result = SpiceGlibGlue_Connect(conn_data->host,
                                   conn_data->port, "",
                                   conn_data->wsport,
                                   conn_data->password, NULL, NULL, 0);

    GLUE_DEBUG("engine_spice_worker: result=%d\n", result);
}

void engine_spice_set_connection_data(const char *host,
                                      const char *port,
                                      const char *wsport,
                                      const char *password)
{
    spice_conn_data_t *conn_data;
    
    if (global_state.conn_data != NULL) {
        if (global_state.conn_data->host != NULL) {
            free(global_state.conn_data->host);
        }
        if (global_state.conn_data->port != NULL) {
            free(global_state.conn_data->port);
        }
        if (global_state.conn_data->wsport != NULL) {
            free(global_state.conn_data->wsport);
        }
        if (global_state.conn_data->password != NULL) {
            free(global_state.conn_data->password);
        }
        conn_data = global_state.conn_data;
    } else {
        conn_data = global_state.conn_data = malloc(sizeof(spice_conn_data_t));
    }
    
    conn_data->host = malloc(strlen(host));
    strcpy(conn_data->host, host);
    
    conn_data->port = malloc(strlen(port));
    strcpy(conn_data->port, port);
    
    conn_data->wsport = malloc(strlen(wsport));
    strcpy(conn_data->wsport, wsport);
    
    conn_data->password = malloc(strlen(password));
    strcpy(conn_data->password, password);
}

int engine_spice_connect()
{
    int i;
    
    if (global_state.conn_state == DISCONNECTED) {
        pthread_create(&mainloop_worker, NULL, (void *) &engine_mainloop_worker, NULL);
        //pthread_create(&spice_worker, NULL, (void *) &engine_spice_worker, NULL);
        
        for (i = 0; i < 15; i++) {
            if (engine_spice_is_connected()) {
                global_state.display_state = CONNECTED;
                global_state.conn_state = CONNECTED;
                return 0;
            }
            sleep(1);
        }
        
        return -1;
    }
    
    return 0;
}

void engine_spice_disconnect()
{
    if (global_state.conn_state == CONNECTED) {
        /* Do this first, preventing screen updates */
        global_state.display_state = DISCONNECTED;
        SpiceGlibGlue_Disconnect();
//        global_state.guest_width = 0;
//        global_state.guest_height = 0;
        global_state.conn_state = DISCONNECTED;
        global_state.input_initialized = 0;
        native_connection_change(DISCONNECTED);
    }
}

int engine_spice_is_connected()
{
    return (int) SpiceGlibGlue_isConnected();
}

void engine_spice_request_resolution(int width, int height)
{
    SpiceGlibRecalcGeometry(0, 0, width, height);
    if (!global_state.change_resolution) {
        global_state.change_resolution = 1;
        native_resolution_change(1);
    }
}

void engine_spice_resolution_changed()
{
    if (global_state.change_resolution) {
        global_state.change_resolution = 0;
        native_resolution_change(0);
    }
}

int engine_spice_update_display(char *display_buffer, int *width, int *height)
{
    int invalidated = 0;
    int flags = 0;
    int update_result = 0;
    
    if (!global_state.input_initialized) {
        /* XXX - HACK! We send here a bogus input to ensure
         coroutines are properly coordinated. */
        SpiceGlibGlueMotionEvent(0, 0, global_state.button_mask);
        global_state.input_initialized = 1;
    }
    
    update_result = SpiceGlibGlueUpdateDisplayData(display_buffer, width, height);

    if (update_result == 0) {
        invalidated = 1;
    } else if (update_result == -2) {
        /* Guest resolution is too big for us. */
        //GLUE_DEBUG("Resolution too big.\n");
        return -2;
    } else {
        GLUE_DEBUG("Can't update screen.\n");
        if (global_state.conn_state == CONNECTED && !engine_spice_is_connected()) {
            global_state.conn_state = DISCONNECTED;
            global_state.display_state = DISCONNECTED;
            native_connection_change(DISCONNECTED);
        }
        return -1;
    }

    if (invalidated) {
        flags |= DISPLAY_INVALIDATE;
    }

    if (*width != 0 &&
        *height != 0 &&
        (*width != global_state.guest_width ||
         *height != global_state.guest_height)) {
        global_state.guest_width = *width;
        global_state.guest_height = *height;
        global_state.mouse_fix[0] = (double) global_state.guest_width / (double) global_state.width;
        global_state.mouse_fix[1] = (double) global_state.guest_height / (double) global_state.height;
        flags |= DISPLAY_CHANGE_RESOLUTION;
    }

    return flags;
}

void engine_spice_motion_event(int pos_x, int pos_y)
{
    SpiceGlibGlueMotionEvent(pos_x, pos_y, global_state.button_mask);
}

void engine_spice_button_event(int pos_x, int pos_y, int button, int down)
{
    if (button == 1 || button == 3) {
        if (down) {
            global_state.button_mask |= (1 << (button - 1));
        } else {
            global_state.button_mask &= ~(1 << (button - 1));
        }
    }
    SpiceGlibGlueButtonEvent(pos_x, pos_y, button, 0, down);
}

void engine_spice_keyboard_event(int keycode, int16_t down)
{
    SpiceGlibGlue_SpiceKeyEvent(down, keycode);
}