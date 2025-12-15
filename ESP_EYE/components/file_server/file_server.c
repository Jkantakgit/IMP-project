/* Simple HTTP file server - serves static files from SD card */

#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "recorder.h"

#define FILE_PATH_MAX 1024
#define SCRATCH_BUFSIZE 4096   // 4KB is enough for ~6.5KB assets, saves RAM

struct file_server_data {
    char base_path[128];
    char scratch[SCRATCH_BUFSIZE];
};

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
    
    /* Create pictures directory if it doesn't exist */
    snprintf(pictures_dir, sizeof(pictures_dir), "%s/pictures", server_data->base_path);
    if (strlen(server_data->base_path) + 10 >= FILE_PATH_MAX) {
        ESP_LOGE(TAG, "Path too long");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Path too long");
        return ESP_FAIL;
    }
    struct stat st;
    if (stat(pictures_dir, &st) == -1) {
        ESP_LOGI(TAG, "Creating pictures directory: %s", pictures_dir);
        if (mkdir(pictures_dir, 0775) != 0) {
            ESP_LOGE(TAG, "Failed to create pictures directory");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create directory");
            return ESP_FAIL;
        }
    }
    
    /* Generate filename using timestamp */
    static uint32_t photo_counter = 0;
    uint32_t timestamp = (uint32_t)(esp_timer_get_time() / 1000000);  // seconds since boot
    snprintf(filepath, sizeof(filepath), "%s/%lu_%lu.jpg", pictures_dir, timestamp, photo_counter++);
    
    /* Capture and save photo */
    ESP_LOGI(TAG, "Capturing photo to: %s", filepath);
    esp_err_t ret = recorder_capture_to_file(filepath);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to capture photo");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to capture photo");
        return ESP_FAIL;
    }
    
    /* Get file size */
    struct stat file_stat;
    long file_size = 0;
    if (stat(filepath, &file_stat) == 0) {
        file_size = file_stat.st_size;
    }
    
    /* Send JSON response */
    httpd_resp_set_type(req, "application/json");
    char json_response[512];
    snprintf(json_response, sizeof(json_response),
             "{\"status\":\"ok\",\"filename\":\"%lu_%lu.jpg\",\"path\":\"/photos/%lu_%lu.jpg\",\"size\":%ld}",
             timestamp, photo_counter - 1, timestamp, photo_counter - 1, file_size);
    httpd_resp_send(req, json_response, strlen(json_response));
    
    ESP_LOGI(TAG, "Photo saved successfully: %s (%ld bytes)", filepath, file_size);
    return ESP_OK;
}

static esp_err_t video_post_handler(httpd_req_t *req)
{
    struct file_server_data *server_data = (struct file_server_data *)req->user_ctx;
    char filepath[FILE_PATH_MAX * 2];
    char videos_dir[FILE_PATH_MAX];
    
    /* Create videos directory if it doesn't exist */
    snprintf(videos_dir, sizeof(videos_dir), "%s/videos", server_data->base_path);
    if (strlen(server_data->base_path) + 10 >= FILE_PATH_MAX) {
        ESP_LOGE(TAG, "Path too long");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Path too long");
        return ESP_FAIL;
    }
    struct stat st;
    if (stat(videos_dir, &st) == -1) {
        ESP_LOGI(TAG, "Creating videos directory: %s", videos_dir);
        if (mkdir(videos_dir, 0775) != 0) {
            ESP_LOGE(TAG, "Failed to create videos directory");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create directory");
            return ESP_FAIL;
        }
    }
    
    /* Generate filename using timestamp */
    static uint32_t video_counter = 0;
    uint32_t timestamp = (uint32_t)(esp_timer_get_time() / 1000000);  // seconds since boot
    snprintf(filepath, sizeof(filepath), "%s/%lu_%lu.mp4", videos_dir, timestamp, video_counter++);
    
    /* Start video recording with 30 second duration */
    ESP_LOGI(TAG, "Starting video recording to: %s", filepath);
    esp_err_t ret = recorder_start_video(filepath, 30000);  // 30 seconds in ms
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start video recording");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to start video recording");
        return ESP_FAIL;
    }
    
    /* Send JSON response */
    httpd_resp_set_type(req, "application/json");
    char json_response[512];
    snprintf(json_response, sizeof(json_response),
             "{\"status\":\"ok\",\"filename\":\"%lu_%lu.mp4\",\"path\":\"/videos/%lu_%lu.mp4\"}",
             timestamp, video_counter - 1, timestamp, video_counter - 1);
    httpd_resp_send(req, json_response, strlen(json_response));
    
    ESP_LOGI(TAG, "Video recording started: %s", filepath);
    return ESP_OK;
}

static esp_err_t list_directory_handler(httpd_req_t *req, const char *subdir)
{
    struct file_server_data *server_data = (struct file_server_data *)req->user_ctx;
    char dirpath[FILE_PATH_MAX];
    snprintf(dirpath, sizeof(dirpath), "%s/%s", server_data->base_path, subdir);

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

static esp_err_t photos_get_handler(httpd_req_t *req)
{
    ESP_LOGE(TAG, "Photos GET handler called");
    return list_directory_handler(req, "pictures");
}

static esp_err_t videos_get_handler(httpd_req_t *req)
{
    ESP_LOGE(TAG, "Videos GET handler called");
    return list_directory_handler(req, "videos");
}

static esp_err_t video_get_handler(httpd_req_t *req)
{
    struct file_server_data *server_data = (struct file_server_data *)req->user_ctx;
    char filepath[FILE_PATH_MAX];
    
    /* Extract video ID from URI: /video/{id} */
    const char *uri = req->uri;
    const char *video_id = uri + 7;  // Skip "/video/" prefix
    
    /* Build full file path */
    snprintf(filepath, sizeof(filepath), "%s/videos/%s", server_data->base_path, video_id);
    
    /* Check if file exists */
    struct stat file_stat;
    if (stat(filepath, &file_stat) == -1) {
        ESP_LOGE(TAG, "Video file not found: %s", filepath);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Video not found");
        return ESP_FAIL;
    }
    
    /* Open file */
    FILE *fd = fopen(filepath, "r");
    if (!fd) {
        ESP_LOGE(TAG, "Failed to open video: %s", filepath);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open video");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Serving video: %s (%ld bytes)", video_id, file_stat.st_size);
    httpd_resp_set_type(req, "video/mp4");
    
    /* Send file in chunks */
    char *chunk = server_data->scratch;
    size_t chunksize;
    do {
        chunksize = fread(chunk, 1, SCRATCH_BUFSIZE, fd);
        if (chunksize > 0) {
            if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK) {
                fclose(fd);
                ESP_LOGE(TAG, "Video send failed");
                httpd_resp_sendstr_chunk(req, NULL);
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send video");
                return ESP_FAIL;
            }
        }
    } while (chunksize != 0);
    
    fclose(fd);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t photo_get_handler(httpd_req_t *req)
{
    struct file_server_data *server_data = (struct file_server_data *)req->user_ctx;
    char filepath[FILE_PATH_MAX];
    
    /* Extract photo ID from URI: /photo/{id} */
    const char *uri = req->uri;
    const char *photo_id = uri + 7;  // Skip "/photo/" prefix
    
    /* Build full file path */
    snprintf(filepath, sizeof(filepath), "%s/pictures/%s", server_data->base_path, photo_id);
    
    /* Check if file exists */
    struct stat file_stat;
    if (stat(filepath, &file_stat) == -1) {
        ESP_LOGE(TAG, "Photo file not found: %s", filepath);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Photo not found");
        return ESP_FAIL;
    }
    
    /* Open file */
    FILE *fd = fopen(filepath, "r");
    if (!fd) {
        ESP_LOGE(TAG, "Failed to open photo: %s", filepath);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open photo");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Serving photo: %s (%ld bytes)", photo_id, file_stat.st_size);
    httpd_resp_set_type(req, "image/jpeg");
    
    /* Send file in chunks */
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

static esp_err_t photo_root_get_handler(httpd_req_t *req)
{
    /* Handle GET /photo without an id */
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_set_type(req, "text/plain");
    const char *msg = "Provide /photo/{filename.jpg} or use POST /photo to capture";
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
    snprintf(filepath, sizeof(filepath), "%s%s", server_data->base_path, uri);
    
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

esp_err_t example_start_file_server(const char *base_path)
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
    strlcpy(server_data->base_path, base_path, sizeof(server_data->base_path));

    /* Ensure media directories exist */
    ensure_subdir(base_path, "pictures");
    ensure_subdir(base_path, "videos");

    /* List all files in the base path for debugging */
    list_files_in_directory(base_path);

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.stack_size = 8192;   // Reduce stack to save RAM (small assets)
    config.lru_purge_enable = true;
    config.max_uri_handlers = 16;
    config.recv_wait_timeout = 20;
    config.send_wait_timeout = 20;

    ESP_LOGI(TAG, "Starting HTTP server on port %d", config.server_port);
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        free(server_data);
        return ESP_FAIL;
    }

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

    /* Video recording handler (POST to /video) */
    httpd_uri_t video_post = {
        .uri = "/video",
        .method = HTTP_POST,
        .handler = video_post_handler,
        .user_ctx = server_data
    };
    httpd_register_uri_handler(server, &video_post);

    /* Photos list handler */
    httpd_uri_t photos = {
        .uri = "/photos",
        .method = HTTP_GET,
        .handler = photos_get_handler,
        .user_ctx = server_data
    };
    httpd_register_uri_handler(server, &photos);

    /* Videos list handler */
    httpd_uri_t videos = {
        .uri = "/videos",
        .method = HTTP_GET,
        .handler = videos_get_handler,
        .user_ctx = server_data
    };
    httpd_register_uri_handler(server, &videos);

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

    /* Video download handler (GET /video/{id}) */
    httpd_uri_t video_get = {
        .uri = "/video/*",
        .method = HTTP_GET,
        .handler = video_get_handler,
        .user_ctx = server_data
    };
    httpd_register_uri_handler(server, &video_get);

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
