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
#include "recorder.h"

#define FILE_PATH_MAX 1024
#define SCRATCH_BUFSIZE 4096

struct file_server_data {
    char base_path[128];
    char scratch[SCRATCH_BUFSIZE];
};

static const char *TAG = "file_server";

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
    char filepath[FILE_PATH_MAX];
    char pictures_dir[FILE_PATH_MAX];
    
    /* Create pictures directory if it doesn't exist */
    snprintf(pictures_dir, sizeof(pictures_dir), "%s/pictures", server_data->base_path);
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
             "{\"status\":\"ok\",\"filename\":\"%lu_%lu.jpg\",\"path\":\"/pictures/%lu_%lu.jpg\",\"size\":%ld}",
             timestamp, photo_counter - 1, timestamp, photo_counter - 1, file_size);
    httpd_resp_send(req, json_response, strlen(json_response));
    
    ESP_LOGI(TAG, "Photo saved successfully: %s (%ld bytes)", filepath, file_size);
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
                ESP_LOGE(TAG, "File send failed");
                httpd_resp_sendstr_chunk(req, NULL);
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
                return ESP_FAIL;
            }
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

    /* List all files in the base path for debugging */
    list_files_in_directory(base_path);

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.stack_size = 8192;  // Increase from default 4096 to handle file operations

    ESP_LOGI(TAG, "Starting HTTP server on port %d", config.server_port);
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        free(server_data);
        return ESP_FAIL;
    }

    /* Root handler */
    httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = file_get_handler,
        .user_ctx = server_data
    };
    httpd_register_uri_handler(server, &root);

    /* Wildcard handler for all files */
    httpd_uri_t file_handler = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = file_get_handler,
        .user_ctx = server_data
    };
    httpd_register_uri_handler(server, &file_handler);

    /* Favicon handler (no-op) */
    httpd_uri_t favicon = {
        .uri = "/favicon.ico",
        .method = HTTP_GET,
        .handler = favicon_get_handler,
        .user_ctx = server_data
    };
    httpd_register_uri_handler(server, &favicon);

    /* Picture capture handler */
    httpd_uri_t picture = {
        .uri = "/picture",
        .method = HTTP_POST,
        .handler = picture_post_handler,
        .user_ctx = server_data
    };
    httpd_register_uri_handler(server, &picture);

    ESP_LOGI(TAG, "File server started successfully");
    return ESP_OK;
}
