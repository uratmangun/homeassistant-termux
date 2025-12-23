/*
 * Battery Charging Daemon for Termux
 * Controls Home Assistant smart power strip based on battery level
 * - Turn ON socket when battery < 30%
 * - Turn OFF socket when battery > 90%
 * 
 * Configuration is loaded from .env file in the same directory
 * 
 * Compiled with: clang -O2 -o battery_charger battery_charger.c -lcurl
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <curl/curl.h>
#include <ctype.h>

#define MAX_LINE 1024
#define MAX_TOKEN 512
#define MAX_URL 256

#define BATTERY_LOW 30
#define BATTERY_HIGH 90
#define CHECK_INTERVAL 60  /* seconds */

/* Configuration loaded from .env */
static char ha_host[MAX_URL] = "http://localhost:8123";
static char ha_token[MAX_TOKEN] = "";
static char entity_id[MAX_URL] = "switch.smart_power_strip_socket_4";

static volatile int running = 1;

/* Response buffer for HTTP requests */
struct response {
    char *data;
    size_t size;
};

/* Trim whitespace from string */
static char *trim(char *str) {
    char *end;
    
    /* Trim leading space */
    while (isspace((unsigned char)*str)) str++;
    
    if (*str == 0) return str;
    
    /* Trim trailing space */
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    
    end[1] = '\0';
    return str;
}

/* Parse .env file */
static int load_env(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        /* Try home directory */
        char home_path[MAX_LINE];
        const char *home = getenv("HOME");
        if (home) {
            snprintf(home_path, sizeof(home_path), "%s/.env", home);
            fp = fopen(home_path, "r");
        }
        if (!fp) {
            fprintf(stderr, "Warning: Could not open .env file\n");
            return -1;
        }
    }
    
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), fp)) {
        /* Skip comments and empty lines */
        char *trimmed = trim(line);
        if (*trimmed == '#' || *trimmed == '\0') continue;
        
        /* Find the = separator */
        char *eq = strchr(trimmed, '=');
        if (!eq) continue;
        
        *eq = '\0';
        char *key = trim(trimmed);
        char *value = trim(eq + 1);
        
        /* Remove surrounding quotes if present */
        size_t len = strlen(value);
        if (len >= 2 && ((value[0] == '"' && value[len-1] == '"') ||
                         (value[0] == '\'' && value[len-1] == '\''))) {
            value[len-1] = '\0';
            value++;
        }
        
        /* Set configuration values */
        if (strcmp(key, "HOMEASSISTANT_HOST") == 0) {
            snprintf(ha_host, sizeof(ha_host), "http://%s:8123", value);
        } else if (strcmp(key, "HOMEASSISTANT_TOKEN") == 0) {
            strncpy(ha_token, value, sizeof(ha_token) - 1);
            ha_token[sizeof(ha_token) - 1] = '\0';
        } else if (strcmp(key, "ENTITY_ID") == 0) {
            strncpy(entity_id, value, sizeof(entity_id) - 1);
            entity_id[sizeof(entity_id) - 1] = '\0';
        }
    }
    
    fclose(fp);
    return 0;
}

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct response *resp = (struct response *)userp;
    
    char *ptr = realloc(resp->data, resp->size + realsize + 1);
    if (!ptr) return 0;
    
    resp->data = ptr;
    memcpy(&(resp->data[resp->size]), contents, realsize);
    resp->size += realsize;
    resp->data[resp->size] = 0;
    
    return realsize;
}

static void signal_handler(int sig) {
    (void)sig;
    running = 0;
}

/* Get battery percentage from termux-battery-status */
static int get_battery_percentage(void) {
    FILE *fp = popen("termux-battery-status 2>/dev/null", "r");
    if (!fp) return -1;
    
    char buffer[512];
    int percentage = -1;
    
    while (fgets(buffer, sizeof(buffer), fp)) {
        char *p = strstr(buffer, "\"percentage\":");
        if (p) {
            percentage = atoi(p + 13);
            break;
        }
    }
    
    pclose(fp);
    return percentage;
}

/* Get current switch state from Home Assistant */
static int get_switch_state(CURL *curl, int *is_on) {
    struct response resp = {0};
    char url[512];
    struct curl_slist *headers = NULL;
    char auth_header[MAX_TOKEN + 32];
    
    snprintf(url, sizeof(url), "%s/api/states/%s", ha_host, entity_id);
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", ha_token);
    
    headers = curl_slist_append(headers, auth_header);
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    
    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    
    if (res != CURLE_OK) {
        free(resp.data);
        return -1;
    }
    
    *is_on = (resp.data && strstr(resp.data, "\"state\":\"on\"")) ? 1 : 0;
    free(resp.data);
    
    curl_easy_reset(curl);
    return 0;
}

/* Control the switch via Home Assistant API */
static int set_switch(CURL *curl, int turn_on) {
    struct response resp = {0};
    char url[512];
    char postdata[256];
    struct curl_slist *headers = NULL;
    char auth_header[MAX_TOKEN + 32];
    
    snprintf(url, sizeof(url), "%s/api/services/switch/%s", 
             ha_host, turn_on ? "turn_on" : "turn_off");
    snprintf(postdata, sizeof(postdata), "{\"entity_id\":\"%s\"}", entity_id);
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", ha_token);
    
    headers = curl_slist_append(headers, auth_header);
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postdata);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    
    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    free(resp.data);
    
    curl_easy_reset(curl);
    return (res == CURLE_OK) ? 0 : -1;
}

int main(int argc, char *argv[]) {
    /* Load configuration from .env file */
    const char *env_path = (argc > 1) ? argv[1] : ".env";
    load_env(env_path);
    
    /* Validate required configuration */
    if (strlen(ha_token) == 0) {
        fprintf(stderr, "Error: HOMEASSISTANT_TOKEN not set in .env file\n");
        fprintf(stderr, "Please create a .env file with:\n");
        fprintf(stderr, "  HOMEASSISTANT_HOST=localhost\n");
        fprintf(stderr, "  HOMEASSISTANT_TOKEN=your_token_here\n");
        return 1;
    }
    
    /* Set up signal handlers */
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);
    
    /* Initialize libcurl */
    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL *curl = curl_easy_init();
    
    if (!curl) {
        fprintf(stderr, "Failed to initialize curl\n");
        return 1;
    }
    
    printf("Battery Charger Daemon Started\n");
    printf("  Host: %s\n", ha_host);
    printf("  Entity: %s\n", entity_id);
    printf("  Low threshold: %d%%\n", BATTERY_LOW);
    printf("  High threshold: %d%%\n", BATTERY_HIGH);
    printf("  Check interval: %d seconds\n\n", CHECK_INTERVAL);
    fflush(stdout);
    
    while (running) {
        int battery = get_battery_percentage();
        int switch_on = 0;
        
        if (battery < 0) {
            fprintf(stderr, "Failed to get battery status\n");
            sleep(CHECK_INTERVAL);
            continue;
        }
        
        if (get_switch_state(curl, &switch_on) < 0) {
            fprintf(stderr, "Failed to get switch state\n");
            sleep(CHECK_INTERVAL);
            continue;
        }
        
        printf("Battery: %d%% | Switch: %s", battery, switch_on ? "ON" : "OFF");
        
        if (battery < BATTERY_LOW && !switch_on) {
            printf(" -> Turning ON (battery low)\n");
            if (set_switch(curl, 1) == 0) {
                printf("✓ Charger turned ON\n");
            } else {
                fprintf(stderr, "✗ Failed to turn on charger\n");
            }
        } else if (battery > BATTERY_HIGH && switch_on) {
            printf(" -> Turning OFF (battery full)\n");
            if (set_switch(curl, 0) == 0) {
                printf("✓ Charger turned OFF\n");
            } else {
                fprintf(stderr, "✗ Failed to turn off charger\n");
            }
        } else {
            printf(" -> No action needed\n");
        }
        
        fflush(stdout);
        sleep(CHECK_INTERVAL);
    }
    
    printf("\nShutting down...\n");
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    
    return 0;
}
