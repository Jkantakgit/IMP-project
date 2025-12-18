/* Lightweight MJPEG TCP streamer running on its own FreeRTOS task
 * Streams multipart/x-mixed-replace MJPEG on port 8081.
 */

#include <string.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_camera.h"

static const char *TAG = "mjpeg_tcp";
#define MJPEG_PORT 8081
#define BACKLOG 1
#define FRAME_DELAY_MS 100

static void mjpeg_client_handler(int client_sock)
{
    const char *hdr = "HTTP/1.0 200 OK\r\n"
                      "Server: esp32-mjpeg\r\n"
                      "Cache-Control: no-cache\r\n"
                      "Pragma: no-cache\r\n"
                      "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n";

    if (send(client_sock, hdr, strlen(hdr), 0) < 0) {
        ESP_LOGW(TAG, "Failed to send header: errno=%d", errno);
        close(client_sock);
        return;
    }

    while (true) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        char part_hdr[128];
        int hlen = snprintf(part_hdr, sizeof(part_hdr),
                            "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                            (unsigned)fb->len);

        if (send(client_sock, part_hdr, hlen, 0) < 0) {
            ESP_LOGI(TAG, "client disconnected (header)");
            esp_camera_fb_return(fb);
            break;
        }

        ssize_t sent = 0;
        const char *buf = (const char *)fb->buf;
        size_t to_send = fb->len;
        while (to_send > 0) {
            ssize_t s = send(client_sock, buf + sent, to_send, 0);
            if (s < 0) {
                ESP_LOGI(TAG, "client disconnected (body): errno=%d", errno);
                break;
            }
            sent += s;
            to_send -= s;
        }

        if (to_send > 0) {
            esp_camera_fb_return(fb);
            break;
        }

        if (send(client_sock, "\r\n", 2, 0) < 0) {
            ESP_LOGI(TAG, "client disconnected (terminator)");
            esp_camera_fb_return(fb);
            break;
        }

        esp_camera_fb_return(fb);
        vTaskDelay(pdMS_TO_TICKS(FRAME_DELAY_MS));
    }

    close(client_sock);
}

static void mjpeg_tcp_server_task(void *arg)
{
    (void)arg;
    int listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Failed to create socket: errno=%d", errno);
        vTaskDelete(NULL);
        return;
    }

    int reuse = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(MJPEG_PORT);

    if (bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "Socket bind failed: errno=%d", errno);
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }

    if (listen(listen_sock, BACKLOG) < 0) {
        ESP_LOGE(TAG, "Socket listen failed: errno=%d", errno);
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "MJPEG TCP server listening on port %d", MJPEG_PORT);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_sock = accept(listen_sock, (struct sockaddr *)&client_addr, &addr_len);
        if (client_sock < 0) {
            ESP_LOGW(TAG, "Accept failed: errno=%d", errno);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        ESP_LOGI(TAG, "Client connected: %s:%d", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        mjpeg_client_handler(client_sock);
        ESP_LOGI(TAG, "Client handler finished");
    }

    close(listen_sock);
    vTaskDelete(NULL);
}

void mjpeg_tcp_server_start(void)
{
    /* Create the TCP server task on its own stack and priority */
    if (xTaskCreate(mjpeg_tcp_server_task, "mjpeg_tcp", 12 * 1024, NULL, tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create MJPEG TCP server task");
    }
}
