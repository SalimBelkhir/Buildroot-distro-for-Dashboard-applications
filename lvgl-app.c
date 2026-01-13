/*******************************************************************
 * main.c - LVGL simulator with integrated TCP server + predictions
 ******************************************************************/
#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "lvgl/lvgl.h"
#include "lvgl/demos/lv_demos.h"

#include "src/lib/driver_backends.h"
#include "src/lib/simulator_util.h"
#include "src/lib/simulator_settings.h"

#define PORT "8080"
#define BUF_SIZE 256

/* Shared data between network thread and UI */
typedef struct {
    float temperature;
    float humidity;
    float pred_temperature;
    float pred_humidity;
    bool new_data;
    pthread_mutex_t mutex;
} sensor_data_t;

static sensor_data_t sensor_data = {
    .temperature = 0.0,
    .humidity = 0.0,
    .pred_temperature = 0.0,
    .pred_humidity = 0.0,
    .new_data = false,
    .mutex = PTHREAD_MUTEX_INITIALIZER
};

/* UI Objects */
static lv_obj_t *temp_label = NULL;
static lv_obj_t *hum_label = NULL;
static lv_obj_t *pred_temp_label = NULL;
static lv_obj_t *pred_hum_label = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *time_label = NULL;

/* Internal functions */
static void configure_simulator(int argc, char ** argv);
static void print_lvgl_version(void);
static void print_usage(void);
static void create_dashboard(void);
static void *server_thread_func(void *arg);
static void update_ui_timer(lv_timer_t *timer);
static void update_time_timer(lv_timer_t *timer);

static char *selected_backend;
extern simulator_settings_t settings;

static float random_temperature() {
    return 15.0 + (rand() % 200) / 10.0;
}

static float random_humidity() {
    return 30.0 + (rand() % 500) / 10.0;
}

static float predict_temperature(float current) {
    float trend = (rand() % 20 - 10) / 10.0; // -1.0 to +1.0
    float noise = (rand() % 10) / 10.0;      // 0 to 1.0
    return current + trend + noise;
}

static float predict_humidity(float current) {
    float trend = (rand() % 30 - 15) / 10.0; // -1.5 to +1.5
    float noise = (rand() % 10) / 10.0;      // 0 to 1.0
    return current + trend + noise;
}

/**
 * @brief Server thread - accepts connections and sends random sensor data
 */
static void *server_thread_func(void *arg)
{
    struct sockaddr_storage claddr;
    struct addrinfo hints, *res, *rp;
    int sfd, cfd, optval = 1;
    socklen_t addrlen;
    char addrStr[128];
    char host[128], service[32];
    
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    
    if (getaddrinfo(NULL, PORT, &hints, &res) != 0) {
        perror("getaddrinfo");
        return NULL;
    }
    
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sfd == -1) continue;
        
        setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
        
        if (bind(sfd, rp->ai_addr, rp->ai_addrlen) == 0)
            break;
        
        close(sfd);
    }
    
    if (rp == NULL) {
        fprintf(stderr, "Server: Could not bind\n");
        freeaddrinfo(res);
        return NULL;
    }
    
    freeaddrinfo(res);
    
    if (listen(sfd, 5) == -1) {
        perror("listen");
        return NULL;
    }
    
    fprintf(stdout, "Server listening on port %s\n", PORT);
    
    while (1) {
        addrlen = sizeof(claddr);
        cfd = accept(sfd, (struct sockaddr *)&claddr, &addrlen);
        
        if (cfd == -1) {
            perror("accept");
            continue;
        }
        
        if (getnameinfo((struct sockaddr *)&claddr, addrlen,
                       host, sizeof(host), service, sizeof(service), 0) == 0) {
            snprintf(addrStr, sizeof(addrStr), "(%s,%s)", host, service);
        } else {
            snprintf(addrStr, sizeof(addrStr), "(UNKNOWN)");
        }
        
        fprintf(stdout, "Connection from %s\n", addrStr);
        
        for (int i = 0; i < 60; i++) {
            float temp = random_temperature();
            float hum = random_humidity();
            
            float pred_temp = predict_temperature(temp);
            float pred_hum = predict_humidity(hum);
            
            char data[BUF_SIZE];
            snprintf(data, BUF_SIZE, "%.1f,%.1f\n", temp, hum);
            
            if (write(cfd, data, strlen(data)) == -1) {
                perror("write");
                break;
            }
            
            pthread_mutex_lock(&sensor_data.mutex);
            sensor_data.temperature = temp;
            sensor_data.humidity = hum;
            sensor_data.pred_temperature = pred_temp;
            sensor_data.pred_humidity = pred_hum;
            sensor_data.new_data = true;
            pthread_mutex_unlock(&sensor_data.mutex);
            
            fprintf(stdout, "Sent: Temp=%.1f°C (Pred: %.1f°C), Humidity=%.1f%% (Pred: %.1f%%)\n", 
                    temp, pred_temp, hum, pred_hum);
            sleep(1);
        }
        
        close(cfd);
        fprintf(stdout, "Client disconnected\n");
    }
    
    return NULL;
}

/**
 * @brief LVGL timer callback to update UI with new sensor data
 */
static void update_ui_timer(lv_timer_t *timer)
{
    (void)timer;
    
    if (temp_label == NULL || hum_label == NULL)
        return;
    
    pthread_mutex_lock(&sensor_data.mutex);
    
    if (sensor_data.new_data) {
        char buf[64];
        
        snprintf(buf, sizeof(buf), "%.1f°C", sensor_data.temperature);
        lv_label_set_text(temp_label, buf);
        
        snprintf(buf, sizeof(buf), "%.1f%%", sensor_data.humidity);
        lv_label_set_text(hum_label, buf);
        
        snprintf(buf, sizeof(buf), "%.1f°C", sensor_data.pred_temperature);
        lv_label_set_text(pred_temp_label, buf);
        
        snprintf(buf, sizeof(buf), "%.1f%%", sensor_data.pred_humidity);
        lv_label_set_text(pred_hum_label, buf);
        
        lv_label_set_text(status_label, "● LIVE");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0x4CAF50), 0);
        
        sensor_data.new_data = false;
    }
    
    pthread_mutex_unlock(&sensor_data.mutex);
}

/**
 * @brief Update time display
 */
static void update_time_timer(lv_timer_t *timer)
{
    (void)timer;
    
    if (time_label == NULL)
        return;
    
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    
    char buf[32];
    strftime(buf, sizeof(buf), "%H:%M:%S", t);
    lv_label_set_text(time_label, buf);
}

/**
 * @brief Create a metric card
 */
static lv_obj_t *create_metric_card(lv_obj_t *parent, const char *title, 
                                     lv_color_t bg_color, lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 360, 140);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_style_bg_color(card, bg_color, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 15, 0);
    lv_obj_set_style_shadow_width(card, 20, 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_30, 0);
    
    lv_obj_t *title_label = lv_label_create(card);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(title_label, LV_OPA_70, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 15, 15);
    
    lv_obj_t *value_label = lv_label_create(card);
    lv_label_set_text(value_label, "--");
    lv_obj_set_style_text_font(value_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(value_label, lv_color_white(), 0);
    lv_obj_align(value_label, LV_ALIGN_CENTER, 0, 10);
    
    return value_label;
}

/**
 * @brief Create the dashboard UI
 */
static void create_dashboard(void)
{
    lv_obj_t *scr = lv_scr_act();
    
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_bg_grad_color(scr, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);
    
    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_set_size(header, LV_PCT(100), 80);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "🌡️ IoT SENSOR DASHBOARD");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 30, 0);
    
    time_label = lv_label_create(header);
    lv_label_set_text(time_label, "00:00:00");
    lv_obj_set_style_text_color(time_label, lv_color_hex(0x94A3B8), 0);
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_20, 0);
    lv_obj_align(time_label, LV_ALIGN_RIGHT_MID, -30, 0);
    
    status_label = lv_label_create(header);
    lv_label_set_text(status_label, "● WAITING");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xFB923C), 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, 0);
    lv_obj_align(status_label, LV_ALIGN_RIGHT_MID, -30, 25);
    
    lv_obj_t *current_label = lv_label_create(scr);
    lv_label_set_text(current_label, "CURRENT READINGS");
    lv_obj_set_style_text_color(current_label, lv_color_hex(0x64748B), 0);
    lv_obj_set_style_text_font(current_label, &lv_font_montserrat_14, 0);
    lv_obj_align(current_label, LV_ALIGN_TOP_LEFT, 30, 100);
    
    temp_label = create_metric_card(scr, "TEMPERATURE", 
                                     lv_color_hex(0xEF4444), 20, 130);
    
    hum_label = create_metric_card(scr, "HUMIDITY", 
                                    lv_color_hex(0x3B82F6), 400, 130);
    
    lv_obj_t *pred_label = lv_label_create(scr);
    lv_label_set_text(pred_label, "TOMORROW'S FORECAST");
    lv_obj_set_style_text_color(pred_label, lv_color_hex(0x64748B), 0);
    lv_obj_set_style_text_font(pred_label, &lv_font_montserrat_14, 0);
    lv_obj_align(pred_label, LV_ALIGN_TOP_LEFT, 30, 290);
    
    pred_temp_label = create_metric_card(scr, "PREDICTED TEMP", 
                                          lv_color_hex(0xF97316), 20, 320);
    
    pred_hum_label = create_metric_card(scr, "PREDICTED HUMIDITY", 
                                         lv_color_hex(0x06B6D4), 400, 320);
    
    lv_obj_t *footer = lv_label_create(scr);
    lv_label_set_text(footer, "Listening on port 8080 • Real-time IoT Gateway");
    lv_obj_set_style_text_color(footer, lv_color_hex(0x475569), 0);
    lv_obj_set_style_text_font(footer, &lv_font_montserrat_12, 0);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -10);
    
    lv_timer_create(update_ui_timer, 100, NULL);   
    lv_timer_create(update_time_timer, 1000, NULL); 
}

static void print_lvgl_version(void)
{
    fprintf(stdout, "%d.%d.%d-%s\n",
            LVGL_VERSION_MAJOR,
            LVGL_VERSION_MINOR,
            LVGL_VERSION_PATCH,
            LVGL_VERSION_INFO);
}

static void print_usage(void)
{
    fprintf(stdout, "\nlvglsim [-V] [-B] [-f] [-m] [-b backend_name] [-W window_width] [-H window_height]\n\n");
    fprintf(stdout, "-V print LVGL version\n");
    fprintf(stdout, "-B list supported backends\n");
    fprintf(stdout, "-f fullscreen\n");
    fprintf(stdout, "-m maximize\n");
}

static void configure_simulator(int argc, char ** argv)
{
    int opt = 0;

    selected_backend = NULL;
    driver_backends_register();

    const char *env_w = getenv("LV_SIM_WINDOW_WIDTH");
    const char *env_h = getenv("LV_SIM_WINDOW_HEIGHT");
    settings.window_width = atoi(env_w ? env_w : "800");
    settings.window_height = atoi(env_h ? env_h : "480");

    while ((opt = getopt(argc, argv, "b:fmW:H:BVh")) != -1) {
        switch (opt) {
            case 'h':
                print_usage();
                exit(EXIT_SUCCESS);
                break;
            case 'V':
                print_lvgl_version();
                exit(EXIT_SUCCESS);
                break;
            case 'B':
                driver_backends_print_supported();
                exit(EXIT_SUCCESS);
                break;
            case 'b':
                if (driver_backends_is_supported(optarg) == 0) {
                    die("error no such backend: %s\n", optarg);
                }
                selected_backend = strdup(optarg);
                break;
            case 'f':
                settings.fullscreen = true;
                break;
            case 'm':
                settings.maximize = true;
                break;
            case 'W':
                settings.window_width = atoi(optarg);
                break;
            case 'H':
                settings.window_height = atoi(optarg);
                break;
            case ':':
                print_usage();
                die("Option -%c requires an argument.\n", optopt);
                break;
            case '?':
                print_usage();
                die("Unknown option -%c.\n", optopt);
                break;
        }
    }
}

/**
 * @brief Main entry point
 */
int main(int argc, char **argv)
{
    srand(time(NULL));
    
    configure_simulator(argc, argv);
    
    lv_init();
    
    if (driver_backends_init_backend(selected_backend) == -1) {
        die("Failed to initialize display backend");
    }
    
    create_dashboard();
    
    pthread_t server_tid;
    if (pthread_create(&server_tid, NULL, server_thread_func, NULL) != 0) {
        die("Failed to create server thread");
    }
    pthread_detach(server_tid);
    
    fprintf(stdout, " Dashboard started with AI predictions!\n");
    fprintf(stdout, " Connect client to port 8080 to see live data.\n");
    
    driver_backends_run_loop();
    
    return 0;
}
