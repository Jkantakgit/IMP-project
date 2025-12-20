/* Simple HTTP file server - serves static files from SD card */

#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include <sys/time.h>
#include <ctype.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "recorder.h"
#include "esp_camera.h"

#define FILE_PATH_MAX 1024
#define SCRATCH_BUFSIZE 16384   // 16KB chunk for faster downloads; PSRAM available

struct file_server_data {
    char static_base[128];
    char media_base[128];
    char scratch[SCRATCH_BUFSIZE];
};

/* Time offset (ms) to translate external epoch time to device relative time.
   current_time_ms() = esp_timer_get_time()/1000 + g_time_offset_ms */
static int64_t g_time_offset_ms = 0;

/* Accept capture commands only if requested capture time is within this window
    (milliseconds) of the device's synced time. Prevents accepting stale/late
    triggers from remote PIR device. */
#ifndef CAPTURE_ACCEPT_WINDOW_MS
#define CAPTURE_ACCEPT_WINDOW_MS 5000
#endif

static const char *TAG = "file_server";

static esp_err_t ensure_subdir(const char *base_path, const char *subdir)
{
    char dirpath[FILE_PATH_MAX];
    int needed = snprintf(dirpath, sizeof(dirpath), "%s/%s", base_path, subdir);
    if (needed < 0 || needed >= (int)sizeof(dirpath)) {
        ESP_LOGE(TAG, "Path too long for %s", subdir);
        return ESP_FAIL;
    }
    struct stat st;
    if (stat(dirpath, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return ESP_OK;
        }
        ESP_LOGE(TAG, "Path exists but is not dir: %s", dirpath);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Creating directory: %s", dirpath);
    if (mkdir(dirpath, 0775) != 0) {
        ESP_LOGE(TAG, "Failed to create directory: %s", dirpath);
        return ESP_FAIL;
    }
    return ESP_OK;
}


static esp_err_t time_post_handler(httpd_req_t *req)
{
    int content_len = req->content_len;
    if (content_len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing body");
        return ESP_FAIL;
    }
    char *buf = malloc(content_len + 1);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }
    int r = httpd_req_recv(req, buf, content_len);
    if (r <= 0) {
        free(buf);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad body");
        return ESP_FAIL;
    }
    buf[r] = '\0';
    unsigned long long ts = 0;
    // Accept JSON like {"time_ms":12345} or plaintext like "time:12345"
    if (buf[0] == '{') {
        char *digit = buf;
        while (*digit && !isdigit((unsigned char)*digit)) digit++;
        if (!*digit) {
            free(buf);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing time field");
            return ESP_FAIL;
        }
        ts = strtoull(digit, NULL, 10);
    } else {
        char *p = strstr(buf, "time:");
        if (!p) p = strstr(buf, "timestamp:");
        if (!p) {
            free(buf);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing time field");
            return ESP_FAIL;
        }
        p += strchr(p, ':') ? 1 : 0; // move past ':'
        ts = strtoull(p, NULL, 10);
    }
    uint64_t now_ms = (uint64_t)(esp_timer_get_time() / 1000);
    g_time_offset_ms = (int64_t)ts - (int64_t)now_ms;
    ESP_LOGI(TAG, "Time sync set: remote=%llu now=%llu offset=%lld", ts, (unsigned long long)now_ms, (long long)g_time_offset_ms);
    // Also set the system clock so libc time functions (localtime, strftime)
    // reflect the synced real time.
    struct timeval tv;
    tv.tv_sec = (time_t)(ts / 1000ULL);
    tv.tv_usec = (suseconds_t)((ts % 1000ULL) * 1000ULL);
    if (settimeofday(&tv, NULL) == 0) {
        ESP_LOGI(TAG, "System time set to %llu (s)", (unsigned long long)tv.tv_sec);
    } else {
        ESP_LOGW(TAG, "settimeofday failed");
    }
    free(buf);
    httpd_resp_set_type(req, "application/json");
    char resp[128];
    snprintf(resp, sizeof(resp), "{\"status\":\"ok\",\"offset_ms\":%lld}", (long long)g_time_offset_ms);
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_sendstr(req, resp);
    return ESP_OK;
}

/* GET /time - return device's current synced epoch ms as JSON */
static esp_err_t time_get_handler(httpd_req_t *req){
    // Get current time with offset
    uint64_t now_ms_rel = (uint64_t)(esp_timer_get_time() / 1000);
    uint64_t ts_now = now_ms_rel + (uint64_t)g_time_offset_ms;
    // Send response
    char resp[64];
    snprintf(resp, sizeof(resp), "{\"time_ms\":%llu}", (unsigned long long)ts_now);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}


static void list_files_in_directory(const char *path)
{
    DIR *dir = opendir(path);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open directory: %s", path);
        return;
    }

    ESP_LOGI(TAG, "=== Files in %s ===", path);
    struct dirent *entry;
    int file_count = 0;
    while ((entry = readdir(dir)) != NULL) {
        struct stat entry_stat;
        char filepath[512];
        snprintf(filepath, sizeof(filepath), "%s/%s", path, entry->d_name);
        
        if (stat(filepath, &entry_stat) == 0) {
            const char *type = (entry->d_type == DT_DIR) ? "DIR " : "FILE";
            ESP_LOGI(TAG, "  %s: %s (%ld bytes)", type, entry->d_name, entry_stat.st_size);
            file_count++;
        } else {
            ESP_LOGW(TAG, "  ??: %s (stat failed)", entry->d_name);
        }
    }
    closedir(dir);
    ESP_LOGI(TAG, "=== Total: %d items ===", file_count);
}

static esp_err_t favicon_get_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t picture_post_handler(httpd_req_t *req)
{
    struct file_server_data *server_data = (struct file_server_data *)req->user_ctx;

    char filepath[FILE_PATH_MAX * 2];
    char pictures_dir[FILE_PATH_MAX];

    /* Ensure pictures directory exists under media base */
    if (ensure_subdir(server_data->media_base, "pictures") != ESP_OK) {
        ESP_LOGE(TAG, "Failed to ensure pictures directory under media base %s", server_data->media_base);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create directory");
        return ESP_FAIL;
    }
    snprintf(pictures_dir, sizeof(pictures_dir), "%s/pictures", server_data->media_base);
    
    /* Require a body containing `capture:<epoch_ms>`; reject other requests.
       This enforces that remote triggers provide a timestamp that we can
       validate against the device clock (safety window). */
    int content_len = req->content_len;
    if (content_len <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"status\":\"rejected\",\"reason\":\"missing_body\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    char *body = malloc(content_len + 1);
    if (!body) {
        ESP_LOGE(TAG, "OOM reading request body");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }
    int r = httpd_req_recv(req, body, content_len);
    if (r <= 0) {
        free(body);
        ESP_LOGE(TAG, "Failed to read request body");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad request body");
        return ESP_FAIL;
    }
    body[r] = '\0';
    body[r] = '\0';

    /* Expect plaintext body containing `capture:<epoch_ms>` */

    char *p = strstr(body, "capture:");
    if (!p) {
        free(body);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"status\":\"rejected\",\"reason\":\"missing_capture_time\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    p += strlen("capture:");
    unsigned long long capture_time = strtoull(p, NULL, 10);

    uint64_t now_ms_rel = (uint64_t)(esp_timer_get_time() / 1000);
    uint64_t ts_now = now_ms_rel + (uint64_t)g_time_offset_ms;
    int64_t diff = (int64_t)capture_time - (int64_t)ts_now;
    if (llabs(diff) > (int64_t)CAPTURE_ACCEPT_WINDOW_MS) {
        ESP_LOGW(TAG, "Rejected capture; requested %llu now %llu diff %lld ms > window %d ms",
                 (unsigned long long)capture_time, (unsigned long long)ts_now, (long long)diff, CAPTURE_ACCEPT_WINDOW_MS);
        free(body);
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_set_type(req, "application/json");
        char resp[128];
        snprintf(resp, sizeof(resp), "{\"status\":\"rejected\",\"reason\":\"outside_window\",\"now\":%llu,\"requested\":%llu}", (unsigned long long)ts_now, (unsigned long long)capture_time);
        httpd_resp_send(req, resp, strlen(resp));
        return ESP_OK;
    }

    /* Within window — enqueue an async capture where filename uses the
       requested capture_time to make it deterministic for the caller.
       Use local calendar date/time (YYYY-MM-DD_HH:MM:SS) as requested. */
    time_t sec = (time_t)(capture_time / 1000ULL);
    struct tm tm;
    localtime_r(&sec, &tm);
    /* filename: YYYY-MM-DDxHH_MM_SS.jpg (x and _ used to avoid ':' on FAT) */
    snprintf(filepath, sizeof(filepath), "%s/%04d-%02d-%02dx%02d_%02d_%02d.jpg",
             pictures_dir,
             tm.tm_year + 1900,
             tm.tm_mon + 1,
             tm.tm_mday,
             tm.tm_hour,
             tm.tm_min,
             tm.tm_sec);

    free(body);
    char *task_filepath = strdup(filepath);
    if (!task_filepath) {
        ESP_LOGE(TAG, "Failed to allocate memory for filepath task");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Server OOM");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Accepted capture within window, enqueuing: %s", task_filepath);
    extern esp_err_t recorder_enqueue_capture(char *filepath);
    if (recorder_enqueue_capture(task_filepath) != ESP_OK) {
        free(task_filepath);
        ESP_LOGE(TAG, "Failed to enqueue capture");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to start capture");
        return ESP_FAIL;
    }

    httpd_resp_set_status(req, "200 OK");
    httpd_resp_set_type(req, "application/json");
    const char *nameptr = strrchr(filepath, '/') ? strrchr(filepath, '/') + 1 : filepath;
    size_t name_len = strlen(nameptr);
    /* allocate exact buffer to avoid compile-time truncation warnings */
    size_t buf_len = 32 + name_len + 8;
    char *okresp = malloc(buf_len);
    if (!okresp) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }
    snprintf(okresp, buf_len, "{\"status\":\"accepted\",\"path\":\"/photos/%s\"}", nameptr);
    httpd_resp_send(req, okresp, strlen(okresp));
    free(okresp);
    return ESP_OK;
}


static esp_err_t list_directory_handler(httpd_req_t *req, const char *subdir)
{
    struct file_server_data *server_data = (struct file_server_data *)req->user_ctx;
    char dirpath[FILE_PATH_MAX];
    snprintf(dirpath, sizeof(dirpath), "%s/%s", server_data->media_base, subdir);


    DIR *dir = opendir(dirpath);
    if (!dir) {
        ESP_LOGW(TAG, "Directory not found: %s", dirpath);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"files\":[]}", strlen("{\"files\":[]}"));
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr_chunk(req, "{\"files\":[");

    struct dirent *entry;
    int first = 1;
    int sent = 0;
    while ((entry = readdir(dir)) != NULL) {
        struct stat file_stat;
        char filepath[FILE_PATH_MAX * 2];
        snprintf(filepath, sizeof(filepath), "%s/%s", dirpath, entry->d_name);

        // FATFS doesn't reliably set d_type; use stat to decide
        if (stat(filepath, &file_stat) == 0 && S_ISREG(file_stat.st_mode)) {
            if (!first) {
                httpd_resp_sendstr_chunk(req, ",");
            }
            char file_json[512];
            snprintf(file_json, sizeof(file_json),
                     "{\"name\":\"%s\",\"size\":%ld}",
                     entry->d_name, file_stat.st_size);
            httpd_resp_sendstr_chunk(req, file_json);
            first = 0;
            sent++;
            if (sent <= 4) {
                ESP_LOGI(TAG, "%s file: %s (%ld bytes)", subdir, entry->d_name, file_stat.st_size);
            }
        }
    }
    closedir(dir);
    
    httpd_resp_sendstr_chunk(req, "]}");
    ESP_LOGI(TAG, "%s response count=%d", subdir, sent);

    /* Terminate chunked response */
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

static esp_err_t photo_get_handler(httpd_req_t *req)
{
    struct file_server_data *server_data = (struct file_server_data *)req->user_ctx;
    const char *uri = req->uri;
    const char *id = uri;
    const char *prefix = "/photo/";
    if (strncmp(uri, prefix, strlen(prefix)) == 0) {
        id = uri + strlen(prefix);
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad request");
        return ESP_FAIL;
    }

    char filepath[FILE_PATH_MAX];
    snprintf(filepath, sizeof(filepath), "%s/pictures/%s", server_data->media_base, id);

    struct stat file_stat;
    if (stat(filepath, &file_stat) == -1) {
        ESP_LOGE(TAG, "Photo file not found: %s", filepath);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Photo not found");
        return ESP_FAIL;
    }

    FILE *fd = fopen(filepath, "r");
    if (!fd) {
        ESP_LOGE(TAG, "Failed to open photo: %s", filepath);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open photo");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Serving photo: %s (%ld bytes)", id, file_stat.st_size);
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment");
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=3600");

    char *chunk = server_data->scratch;
    size_t chunksize;
    do {
        chunksize = fread(chunk, 1, SCRATCH_BUFSIZE, fd);
        if (chunksize > 0) {
            if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK) {
                fclose(fd);
                ESP_LOGE(TAG, "Photo send failed");
                httpd_resp_sendstr_chunk(req, NULL);
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send photo");
                return ESP_FAIL;
            }
        }
    } while (chunksize != 0);

    fclose(fd);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t photos_get_handler(httpd_req_t *req)
{
    return list_directory_handler(req, "pictures");
}

/* Simple informative handler for GET /photo (root) */
static esp_err_t photo_root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    const char *msg = "{\"usage\":\"GET /photo/{id} to download, POST /photo to capture\"}";
    httpd_resp_send(req, msg, strlen(msg));
    return ESP_OK;
}

/* Stub handler for /video when HTTP streaming is not provided by httpd.
   We serve MJPEG via standalone TCP streamer on port 8081; inform clients. */
static esp_err_t mjpeg_stream_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_set_type(req, "text/plain");
    const char *msg = "MJPEG stream served on TCP port 8081";
    httpd_resp_send(req, msg, strlen(msg));
    return ESP_OK;
}



#define IS_FILE_EXT(filename, ext) \
    (strcasecmp(&filename[strlen(filename) - sizeof(ext) + 1], ext) == 0)

static esp_err_t set_content_type_from_file(httpd_req_t *req, const char *filename)
{
    if (IS_FILE_EXT(filename, ".html")) {
        return httpd_resp_set_type(req, "text/html");
    } else if (IS_FILE_EXT(filename, ".css")) {
        return httpd_resp_set_type(req, "text/css");
    } else if (IS_FILE_EXT(filename, ".js")) {
        return httpd_resp_set_type(req, "application/javascript");
    } else if (IS_FILE_EXT(filename, ".jpeg") || IS_FILE_EXT(filename, ".jpg")) {
        return httpd_resp_set_type(req, "image/jpeg");
    } else if (IS_FILE_EXT(filename, ".png")) {
        return httpd_resp_set_type(req, "image/png");
    } else if (IS_FILE_EXT(filename, ".ico")) {
        return httpd_resp_set_type(req, "image/x-icon");
    }
    return httpd_resp_set_type(req, "text/plain");
}

static esp_err_t file_get_handler(httpd_req_t *req)
{
    char filepath[FILE_PATH_MAX];
    struct file_server_data *server_data = (struct file_server_data *)req->user_ctx;
    
    /* Map "/" to "/index.html" */
    const char *uri = req->uri;
    if (strcmp(uri, "/") == 0) {
        uri = "/index.html";
    }
    
    /* Build full file path */
    snprintf(filepath, sizeof(filepath), "%s%s", server_data->static_base, uri);
    
    
    /* Check if file exists */
    struct stat file_stat;
    if (stat(filepath, &file_stat) == -1) {
        ESP_LOGE(TAG, "File not found: %s", filepath);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }
    
    /* Open file */
    FILE *fd = fopen(filepath, "r");
    if (!fd) {
        ESP_LOGE(TAG, "Failed to open file: %s", filepath);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open file");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Serving file: %s (%ld bytes)", uri, file_stat.st_size);
    set_content_type_from_file(req, uri);
    
    /* Send file in chunks */
    char *chunk = server_data->scratch;
    size_t chunksize;
    do {
        chunksize = fread(chunk, 1, SCRATCH_BUFSIZE, fd);
        if (chunksize > 0) {
            if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK) {
                fclose(fd);
                ESP_LOGE(TAG, "File send failed for %s", uri);
                /* Don't try to send error - socket is already broken */
                return ESP_FAIL;
            }
            /* Small delay to prevent overwhelming WiFi */
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    } while (chunksize != 0);
    
    fclose(fd);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

esp_err_t example_start_file_server(const char *static_base_path, const char *photos_base_path)
{
    static struct file_server_data *server_data = NULL;

    if (server_data) {
        ESP_LOGE(TAG, "File server already started");
        return ESP_ERR_INVALID_STATE;
    }

    server_data = calloc(1, sizeof(struct file_server_data));
    if (!server_data) {
        ESP_LOGE(TAG, "Failed to allocate memory");
        return ESP_ERR_NO_MEM;
    }
    strlcpy(server_data->static_base, static_base_path, sizeof(server_data->static_base));

    /* Use provided media base (photos_base_path) for storing pictures/videos */
    if (photos_base_path && strlen(photos_base_path) > 0) {
        strlcpy(server_data->media_base, photos_base_path, sizeof(server_data->media_base));
    } else {
        /* default to SD card mount point if none provided */
        strlcpy(server_data->media_base, "/data", sizeof(server_data->media_base));
    }

    ESP_LOGI(TAG, "Media base set to: %s", server_data->media_base);

    /* Ensure media directories exist */
    ensure_subdir(server_data->media_base, "pictures");

    /* List all files in the media base path for debugging */
    list_files_in_directory(server_data->media_base);

    httpd_handle_t server = NULL;
     httpd_config_t config = HTTPD_DEFAULT_CONFIG();
     config.uri_match_fn = httpd_uri_match_wildcard;
     /* Increase stack for streaming and raise server task priority so MJPEG
         streaming is less likely to block other handlers. */
     config.stack_size = 16384;   // larger stack for MJPEG streaming
     config.task_priority = 5;    // moderate priority
     config.lru_purge_enable = true;
     config.max_uri_handlers = 16;
     config.recv_wait_timeout = 20;
     config.send_wait_timeout = 20;
     config.max_open_sockets = 5;

    ESP_LOGI(TAG, "Starting HTTP server on port %d", config.server_port);
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        free(server_data);
        return ESP_FAIL;
    }

    /* Start standalone MJPEG TCP streamer (port 8081) so streaming cannot
       block the main HTTP server handlers. */
    extern void mjpeg_tcp_server_start(void);
    mjpeg_tcp_server_start();

    /* Favicon handler (no-op) - register before wildcard */
    httpd_uri_t favicon = {
        .uri = "/favicon.ico",
        .method = HTTP_GET,
        .handler = favicon_get_handler,
        .user_ctx = server_data
    };
    httpd_register_uri_handler(server, &favicon);

    /* Picture capture handler (POST to /photo) */
    httpd_uri_t photo_post = {
        .uri = "/photo",
        .method = HTTP_POST,
        .handler = picture_post_handler,
        .user_ctx = server_data
    };
    httpd_register_uri_handler(server, &photo_post);

    /* Photos list handler */
    httpd_uri_t photos = {
        .uri = "/photos",
        .method = HTTP_GET,
        .handler = photos_get_handler,
        .user_ctx = server_data
    };
    httpd_register_uri_handler(server, &photos);

    /* Time sync handler (POST /time) */
    httpd_uri_t time_post = {
        .uri = "/time",
        .method = HTTP_POST,
        .handler = time_post_handler,
        .user_ctx = server_data
    };
    httpd_register_uri_handler(server, &time_post);

    /* Time query handler (GET /time) - return device time for clients */
    httpd_uri_t time_get = {
        .uri = "/time",
        .method = HTTP_GET,
        .handler = time_get_handler,
        .user_ctx = server_data
    };
    httpd_register_uri_handler(server, &time_get);

    /* (encryption removed) */

    /* Photo download handler (GET /photo/{id}) */
    httpd_uri_t photo_get = {
        .uri = "/photo/*",
        .method = HTTP_GET,
        .handler = photo_get_handler,
        .user_ctx = server_data
    };
    httpd_register_uri_handler(server, &photo_get);

    /* Photo root handler (GET /photo) to guide clients */
    httpd_uri_t photo_root_get = {
        .uri = "/photo",
        .method = HTTP_GET,
        .handler = photo_root_get_handler,
        .user_ctx = server_data
    };
    httpd_register_uri_handler(server, &photo_root_get);

    /* MJPEG stream handler (real-time) */
    httpd_uri_t mjpeg = {
        .uri = "/video",
        .method = HTTP_GET,
        .handler = mjpeg_stream_handler,
        .user_ctx = server_data
    };
    httpd_register_uri_handler(server, &mjpeg);

    /* Root handler */
    httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = file_get_handler,
        .user_ctx = server_data
    };
    httpd_register_uri_handler(server, &root);

    /* Wildcard handler for all files - MUST BE LAST */
    httpd_uri_t file_handler = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = file_get_handler,
        .user_ctx = server_data
    };
    httpd_register_uri_handler(server, &file_handler);

    ESP_LOGI(TAG, "File server started successfully");
    return ESP_OK;
}