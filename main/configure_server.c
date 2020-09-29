#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <dirent.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs.h"
#include "esp_spiffs.h"
#include "esp_http_server.h"
#include "esp_httpd_priv.h"

#include "Wed_Control_Light_main.h"

#define TAG "configure_server"

#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + CONFIG_SPIFFS_OBJ_NAME_LEN)

#define IS_FILE_EXT(filename, ext) \
    (strcasecmp(&filename[strlen(filename) - sizeof(ext) + 1], ext) == 0)

#define MAXI_TRIGGER_KEY_LENGTH 50

#define TRIGGER_KEY_END 12064 //'/ '

/* 单个文件的最大大小。确保这个
 * 值与 upload_script.html 中设置的值相同 */
#define MAX_FILE_SIZE   (10*1024) // 10 KB
#define MAX_FILE_SIZE_STR "10KB"

/* 暂存缓冲区大小 */
#define SCRATCH_BUFSIZE  8192

static const char macro_extension[] = ".macro";
static const char enabled_macro_extension[] = ".build";

struct file_server_data {
    /* 文件存储的基本路径 */
    char base_path[ESP_VFS_PATH_MAX + 1];
    /* 在文件传输期间临时存储临时缓冲区 */
    char scratch[SCRATCH_BUFSIZE];
};

/* 处理程序，将 /index.html 的传入GET请求重定向到 /
 * 可以通过上传具有相同名称的文件来覆盖 */
static esp_err_t index_html_get_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "307 Temporary Redirect");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);  // 响应主体可以为空
    return ESP_OK;
}

/* 处理程序以嵌入在Flash中的图标文件作为响应。
 * 浏览器希望在URI /favicon.ico 上获取GET网站图标。
 * 可以通过上传具有相同名称的文件来覆盖 */
static esp_err_t favicon_get_handler(httpd_req_t *req)
{
    extern const unsigned char favicon_ico_start[] asm("_binary_favicon_ico_start");
    extern const unsigned char favicon_ico_end[]   asm("_binary_favicon_ico_end");
    const size_t favicon_ico_size = (favicon_ico_end - favicon_ico_start);
    httpd_resp_set_type(req, "image/x-icon");
    httpd_resp_send(req, (const char *)favicon_ico_start, favicon_ico_size);
    return ESP_OK;
}

/* 发送包含运行时生成的html的HTTP响应，其中包括
 * 请求路径下所有文件和文件夹的列表。
 * 在SPIFFS的情况下，如果path为任意，则返回空列表
 * 除'/'以外的字符串，因为SPIFFS不支持目录 */
static esp_err_t http_resp_dir_html(httpd_req_t *req, const char *dirpath)
{
    char entrypath[FILE_PATH_MAX];
    char entrysize[16];
    const char *entrytype;

    struct dirent *entry;
    struct stat entry_stat;

    DIR *dir = opendir(dirpath);
    const size_t dirpath_len = strlen(dirpath);

    /* 检索文件存储的基本路径以构造完整路径 */
    strlcpy(entrypath, dirpath, sizeof(entrypath));

    if (!dir) {
        ESP_LOGE(TAG, "Failed to stat dir : %s", dirpath);
        /* 回应404未找到 */
        //httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Directory does not exist");
        return ESP_FAIL;
    }

    /* Send HTML file header */
    httpd_resp_sendstr_chunk(req, "<!DOCTYPE html><html><body>");

    /* Get handle to embedded file upload script */
    extern const unsigned char upload_script_start[] asm("_binary_upload_script_html_start");
    extern const unsigned char upload_script_end[]   asm("_binary_upload_script_html_end");
    const size_t upload_script_size = (upload_script_end - upload_script_start);

    /* Add file upload form and script which on execution sends a POST request to /upload */
    httpd_resp_send_chunk(req, (const char *)upload_script_start, upload_script_size);

    /* Send file-list table definition and column labels */
    httpd_resp_sendstr_chunk(req,
                             "<table class=\"fixed\" border=\"1\">"
                             "<col width=\"800px\" /><col width=\"300px\" /><col width=\"300px\" /><col width=\"100px\" /><col width=\"100px\" /><col width=\"100px\" />\n"
                             "<thead><tr><th>Name</th><th>Type</th><th>Size (Bytes)</th><th>Delete</th><th>Status</th><th>Send</th></tr></thead>\n"
                             "<tbody>");

    /* 遍历所有文件/文件夹并获取其名称和大小 */
    while ((entry = readdir(dir)) != NULL) {
        if (IS_FILE_EXT(entry->d_name, enabled_macro_extension)) {
            continue;
        }
        entrytype = (entry->d_type == DT_DIR ? "directory" : "file");

        strlcpy(entrypath + dirpath_len, entry->d_name, sizeof(entrypath) - dirpath_len);
        if (stat(entrypath, &entry_stat) == -1) {
            ESP_LOGE(TAG, "Failed to stat %s : %s", entrytype, entry->d_name);
            continue;
        }
        sprintf(entrysize, "%ld", entry_stat.st_size);
        ESP_LOGI(TAG, "Found %s : %s (%s bytes)", entrytype, entry->d_name, entrysize);

        /* Send chunk of HTML file containing table entries with file name and size */
        httpd_resp_sendstr_chunk(req, "<tr><td><a href=\"");
        httpd_resp_sendstr_chunk(req, req->uri);
        httpd_resp_sendstr_chunk(req, entry->d_name);
        if (entry->d_type == DT_DIR) {
            httpd_resp_sendstr_chunk(req, "/");
        }
        httpd_resp_sendstr_chunk(req, "\">");
        httpd_resp_sendstr_chunk(req, entry->d_name);
        httpd_resp_sendstr_chunk(req, "</a></td><td>");
        httpd_resp_sendstr_chunk(req, entrytype);
        httpd_resp_sendstr_chunk(req, "</td><td>");
        httpd_resp_sendstr_chunk(req, entrysize);
        httpd_resp_sendstr_chunk(req, "</td><td>");
        httpd_resp_sendstr_chunk(req, "<form method=\"post\" action=\"/delete");
        httpd_resp_sendstr_chunk(req, req->uri);
        httpd_resp_sendstr_chunk(req, entry->d_name);
        httpd_resp_sendstr_chunk(req, "\"><button type=\"submit\">Delete</button></form>");

        if (IS_FILE_EXT(entry->d_name, macro_extension)) {

            struct stat file_stat;
            char buildpath[FILE_PATH_MAX] = "/spiffs/";
            for (int i = 0; i < FILE_PATH_MAX; i++) {
                if (entry->d_name[i] == 0) {
                    if (i > FILE_PATH_MAX - 7) {
                        ESP_LOGE(TAG, "Filename is too long");
                        /* Respond with 500 Internal Server Error */
                        //httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
                        return ESP_FAIL;
                    }
                    memcpy(&buildpath[8], &entry->d_name[0], i);
                    memcpy(&buildpath[i + 8], enabled_macro_extension, sizeof(enabled_macro_extension));
                    break;
                }
            }
            if (stat(buildpath, &file_stat) == -1) {
                httpd_resp_sendstr_chunk(req, "</td><td>");
                httpd_resp_sendstr_chunk(req, "<form method=\"post\" action=\"/enable");
                httpd_resp_sendstr_chunk(req, req->uri);
                httpd_resp_sendstr_chunk(req, entry->d_name);
                httpd_resp_sendstr_chunk(req, "\"><button type=\"submit\">Disable</button></form>");
            } else {
                httpd_resp_sendstr_chunk(req, "</td><td>");
                httpd_resp_sendstr_chunk(req, "<form method=\"post\" action=\"/delete");
                httpd_resp_sendstr_chunk(req, req->uri);
                httpd_resp_sendstr_chunk(req, entry->d_name);
                httpd_resp_sendstr_chunk(req, enabled_macro_extension);
                httpd_resp_sendstr_chunk(req, "\"><button type=\"submit\">Enable</button></form>");
                httpd_resp_sendstr_chunk(req, "</td><td>");
                httpd_resp_sendstr_chunk(req, "<form method=\"post\" action=\"/send");
                httpd_resp_sendstr_chunk(req, req->uri);
                httpd_resp_sendstr_chunk(req, entry->d_name);
                httpd_resp_sendstr_chunk(req, enabled_macro_extension);
                httpd_resp_sendstr_chunk(req, "\"><button type=\"submit\">SendMacro</button></form>");
            }
        }
        httpd_resp_sendstr_chunk(req, "</td></tr>\n");
    }
    closedir(dir);

    /* Finish the file list table */
    httpd_resp_sendstr_chunk(req, "</tbody></table>");

    /* Send remaining chunk of HTML file to complete it */
    httpd_resp_sendstr_chunk(req, "</body></html>");

    /* Send empty chunk to signal HTTP response completion */
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

/* 根据文件扩展名设置HTTP响应内容类型 */
static esp_err_t set_content_type_from_file(httpd_req_t *req, const char *filename)
{
    if (IS_FILE_EXT(filename, ".pdf")) {
        return httpd_resp_set_type(req, "application/pdf");
    } else if (IS_FILE_EXT(filename, ".html")) {
        return httpd_resp_set_type(req, "text/html");
    } else if (IS_FILE_EXT(filename, ".jpeg")) {
        return httpd_resp_set_type(req, "image/jpeg");
    } else if (IS_FILE_EXT(filename, ".ico")) {
        return httpd_resp_set_type(req, "image/x-icon");
    }
    /* This is a limited set only */
    /* For any other type always set as plain text */
    return httpd_resp_set_type(req, "text/plain");
}

/* 将完整路径复制到目标缓冲区并返回
 * 指向路径的指针（跳过前面的基本路径） */
static const char *get_path_from_uri(char *dest, const char *base_path, const char *uri, size_t destsize)
{
    const size_t base_pathlen = strlen(base_path);
    size_t pathlen = strlen(uri);

    const char *quest = strchr(uri, '?');
    if (quest) {
        pathlen = MIN(pathlen, quest - uri);
    }
    const char *hash = strchr(uri, '#');
    if (hash) {
        pathlen = MIN(pathlen, hash - uri);
    }

    if (base_pathlen + pathlen + 1 > destsize) {
        /* Full path string won't fit into destination buffer */
        return NULL;
    }

    /* Construct full path (base + path) */
    strcpy(dest, base_path);
    strlcpy(dest + base_pathlen, uri, pathlen + 1);

    /* Return pointer to path, skipping the base */
    return dest + base_pathlen;
}

/* Handler to download a file kept on the server */
static esp_err_t download_get_handler(httpd_req_t *req)
{
    char filepath[FILE_PATH_MAX];
    FILE *fd = NULL;
    struct stat file_stat;

    const char *filename = get_path_from_uri(filepath, ((struct file_server_data *)req->user_ctx)->base_path,
                           req->uri, sizeof(filepath));
    if (!filename) {
        ESP_LOGE(TAG, "Filename is too long");
        /* Respond with 500 Internal Server Error */
        //httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
        return ESP_FAIL;
    }

    /* If name has trailing '/', respond with directory contents */
    if (filename[strlen(filename) - 1] == '/') {
        return http_resp_dir_html(req, filepath);
    }

    if (stat(filepath, &file_stat) == -1) {
        /* If file not present on SPIFFS check if URI
         * corresponds to one of the hardcoded paths */
        if (strcmp(filename, "/index.html") == 0) {
            return index_html_get_handler(req);
        } else if (strcmp(filename, "/favicon.ico") == 0) {
            return favicon_get_handler(req);
        }
        ESP_LOGE(TAG, "Failed to stat file : %s", filepath);
        /* Respond with 404 Not Found */
        //httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File does not exist");
        return ESP_FAIL;
    }

    fd = fopen(filepath, "r");
    if (!fd) {
        ESP_LOGE(TAG, "Failed to read existing file : %s", filepath);
        /* Respond with 500 Internal Server Error */
        //httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Sending file : %s (%ld bytes)...", filename, file_stat.st_size);
    set_content_type_from_file(req, filename);

    /* Retrieve the pointer to scratch buffer for temporary storage */
    char *chunk = ((struct file_server_data *)req->user_ctx)->scratch;
    size_t chunksize;
    do {
        /* Read file in chunks into the scratch buffer */
        chunksize = fread(chunk, 1, SCRATCH_BUFSIZE, fd);

        if (chunksize > 0) {
            /* Send the buffer contents as HTTP response chunk */
            if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK) {
                fclose(fd);
                ESP_LOGE(TAG, "File sending failed!");
                /* Abort sending file */
                httpd_resp_sendstr_chunk(req, NULL);
                /* Respond with 500 Internal Server Error */
                //httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
                return ESP_FAIL;
            }
        }

        /* Keep looping till the whole file is sent */
    } while (chunksize != 0);

    /* Close file after sending complete */
    fclose(fd);
    ESP_LOGI(TAG, "File sending complete");

    /* Respond with an empty chunk to signal HTTP response completion */
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/* Handler to upload a file onto the server */
static esp_err_t upload_post_handler(httpd_req_t *req)
{
    char filepath[FILE_PATH_MAX];
    FILE *fd = NULL;
    struct stat file_stat;

    /* Skip leading "/upload" from URI to get filename */
    /* Note sizeof() counts NULL termination hence the -1 */
    const char *filename = get_path_from_uri(filepath, ((struct file_server_data *)req->user_ctx)->base_path,
                           req->uri + sizeof("/upload") - 1, sizeof(filepath));
    if (!filename) {
        ESP_LOGE(TAG, "Filename is too long");
        /* Respond with 500 Internal Server Error */
        //httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
        return ESP_FAIL;
    }

    /* Filename cannot have a trailing '/' */
    if (filename[strlen(filename) - 1] == '/') {
        ESP_LOGE(TAG, "Invalid filename : %s", filename);
        //httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Invalid filename");
        return ESP_FAIL;
    }

    if (stat(filepath, &file_stat) == 0) {
        ESP_LOGE(TAG, "File already exists : %s", filepath);
        /* Respond with 400 Bad Request */
        //httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File already exists");
        return ESP_FAIL;
    }

    /* File cannot be larger than a limit */
    if (req->content_len > MAX_FILE_SIZE) {
        ESP_LOGE(TAG, "File too large : %d bytes", req->content_len);
        /* Respond with 400 Bad Request */
        //httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            // "File size must be less than "
                            // MAX_FILE_SIZE_STR "!");
        /* Return failure to close underlying connection else the
         * incoming file content will keep the socket busy */
        return ESP_FAIL;
    }

    fd = fopen(filepath, "w");
    if (!fd) {
        ESP_LOGE(TAG, "Failed to create file : %s", filepath);
        /* Respond with 500 Internal Server Error */
        //httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create file");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Receiving file : %s...", filename);

    /* 检索指向暂存缓冲区的指针以进行临时存储 */
    char *buf = ((struct file_server_data *)req->user_ctx)->scratch;
    int received;

    /* Content length of the request gives
     * the size of the file being uploaded */
    int remaining = req->content_len;

    while (remaining > 0) {

        ESP_LOGI(TAG, "Remaining size : %d", remaining);
        /* Receive the file part by part into a buffer */
        if ((received = httpd_req_recv(req, buf, MIN(remaining, SCRATCH_BUFSIZE))) <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                /* Retry if timeout occurred */
                continue;
            }

            /* In case of unrecoverable error,
             * close and delete the unfinished file*/
            fclose(fd);
            unlink(filepath);

            ESP_LOGE(TAG, "File reception failed!");
            /* Respond with 500 Internal Server Error */
            //httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive file");
            return ESP_FAIL;
        }

        /* Write buffer content to file on storage */
        if (received && (received != fwrite(buf, 1, received, fd))) {
            /* Couldn't write everything to file!
             * Storage may be full? */
            fclose(fd);
            unlink(filepath);

            ESP_LOGE(TAG, "File write failed!");
            /* Respond with 500 Internal Server Error */
            //httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to write file to storage");
            return ESP_FAIL;
        }

        /* Keep track of remaining size of
         * the file left to be uploaded */
        remaining -= received;
    }

    /* Close file upon upload completion */
    fclose(fd);
    ESP_LOGI(TAG, "File reception complete");

    /* Redirect onto root to see the updated file list */
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_sendstr(req, "File uploaded successfully");
    return ESP_OK;
}

/* Handler to delete a file from the server */
static esp_err_t delete_post_handler(httpd_req_t *req)
{
    char filepath[FILE_PATH_MAX];
    struct stat file_stat;

    /* Skip leading "/delete" from URI to get filename */
    /* Note sizeof() counts NULL termination hence the -1 */
    const char *filename = get_path_from_uri(filepath, ((struct file_server_data *)req->user_ctx)->base_path,
                           req->uri + sizeof("/delete") - 1, sizeof(filepath));
    if (!filename) {
        ESP_LOGE(TAG, "Filename is too long");
        /* Respond with 500 Internal Server Error */
        //httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
        return ESP_FAIL;
    }

    /* Filename cannot have a trailing '/' */
    if (filename[strlen(filename) - 1] == '/') {
        ESP_LOGE(TAG, "Invalid filename : %s", filename);
        //httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Invalid filename");
        return ESP_FAIL;
    }

    if (stat(filepath, &file_stat) == -1) {
        ESP_LOGE(TAG, "File does not exist : %s", filename);
        /* Respond with 400 Bad Request */
        //httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File does not exist");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Deleting file : %s", filename);
    /* Delete file */
    unlink(filepath);

    /* Redirect onto root to see the updated file list */
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_sendstr(req, "File deleted successfully");
    return ESP_OK;
}

// static esp_err_t enable_macro_handler(httpd_req_t *req)
// {
//     char filepath[FILE_PATH_MAX];
//     FILE *fd = NULL;
//     struct stat file_stat;

//     /* Skip leading "/enable" from URI to get filename */
//     /* Note sizeof() counts NULL termination hence the -1 */
//     const char *filename = get_path_from_uri(filepath, ((struct file_server_data *)req->user_ctx)->base_path,
//                            req->uri + sizeof("/enable") - 1, sizeof(filepath));

//     if (stat(filepath, &file_stat) == -1) {
//         ESP_LOGE(TAG, "unknown error");
//         //httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "unknown error");
//         return ESP_FAIL;
//     }

//     /*创建文件缓冲区，补充 NULL 表示字符串终止*/
//     uint8_t file_buffer[file_stat.st_size + 1];
//     memset(file_buffer, 0, file_stat.st_size + 1);
//     /*创建触发键缓冲区*/
//     uint8_t trigger_buffer[MAXI_TRIGGER_KEY_LENGTH];
//     memset(trigger_buffer, 0, MAXI_TRIGGER_KEY_LENGTH);

//     fd = fopen(filepath, "r");
//     if (!fd) {
//         ESP_LOGE(TAG, "Failed to read existing file : %s", filepath);
//         /* Respond with 500 Internal Server Error */
//         //httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
//         return ESP_FAIL;
//     }

//     if (file_stat.st_size != fread(file_buffer, 1, file_stat.st_size, fd)) {
//         ESP_LOGE(TAG, "File error!");
//         //httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "File error!");
//         fclose(fd);
//         return ESP_FAIL;
//     }

//     fclose(fd);

//     int i = 0;
//     for (; i < file_stat.st_size; i++) {
//         if ((file_buffer[i] << 8) + file_buffer[i + 1] == TRIGGER_KEY_END) {
//             trigger_buffer[i] = file_buffer[i];
//             break;
//         }
//         trigger_buffer[i] = file_buffer[i];
//     }

//     for (int i = 0; i < FILE_PATH_MAX; i++) {
//         if (filepath[i] == 0) {
//             memcpy(&filepath[i], enabled_macro_extension, sizeof(enabled_macro_extension));
//             break;
//         }
//     }

//     fd = fopen(filepath, "w");
//     if (!fd) {
//         ESP_LOGE(TAG, "Failed to create file : %s", filepath);
//         /* Respond with 500 Internal Server Error */
//         //httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create file");
//         return ESP_FAIL;
//     }

//     keyboard_macro_handle(trigger_buffer, i + 1, fd);
//     keyboard_macro_handle(&file_buffer[i + 2], sizeof(file_buffer) - i - 2, fd);

//     fclose(fd);
//     ESP_LOGI(TAG, "Macro enable :%s", filename);

//     httpd_resp_set_status(req, "303 See Other");
//     httpd_resp_set_hdr(req, "Location", "/");
//     httpd_resp_sendstr(req, "Macro enable successfully");
//     return ESP_OK;
// }

// static esp_err_t send_macro_handler(httpd_req_t *req)
// {
//     char filepath[FILE_PATH_MAX];
//     FILE *fd = NULL;
//     struct stat file_stat;

//     /* Skip leading "/send" from URI to get filename */
//     /* Note sizeof() counts NULL termination hence the -1 */
//     const char *filename = get_path_from_uri(filepath, ((struct file_server_data *)req->user_ctx)->base_path,
//                            req->uri + sizeof("/send") - 1, sizeof(filepath));
//     if (!filename) {
//         ESP_LOGE(TAG, "Filename is too long");
//         //httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
//         return ESP_FAIL;
//     }

//     /* Filename cannot have a trailing '/' */
//     if (filename[strlen(filename) - 1] == '/') {
//         ESP_LOGE(TAG, "Invalid filename : %s", filename);
//         //httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Invalid filename");
//         return ESP_FAIL;
//     }

//     if (stat(filepath, &file_stat) == -1) {
//         ESP_LOGE(TAG, "unknown error");
//         //httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "unknown error");
//         return ESP_FAIL;
//     }

//     fd = fopen(filepath, "r");
//     if (!fd) {
//         ESP_LOGE(TAG, "Failed to open file : %s", filepath);
//         //httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open file");
//         return ESP_FAIL;
//     }

//     ESP_LOGI(TAG, "Send macro : %s...", filename);

//     uint8_t key_value[HID_KEYBOARD_IN_RPT_LEN];

//     /*不发送触发键*/
//     if (HID_KEYBOARD_IN_RPT_LEN != fread(key_value, 1, HID_KEYBOARD_IN_RPT_LEN, fd)) {
//         ESP_LOGE(TAG, "File error!");
//         //httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "File error!");
//         fclose(fd);
//         return ESP_FAIL;
//     }
//     int remaining = file_stat.st_size - HID_KEYBOARD_IN_RPT_LEN;

//     while (remaining > 0) {
//         ESP_LOGD(TAG, "Remaining size : %d", remaining);

//         if (HID_KEYBOARD_IN_RPT_LEN != fread(key_value, 1, HID_KEYBOARD_IN_RPT_LEN, fd)) {
//             ESP_LOGE(TAG, "File error!");
//             //httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "File error!");
//             fclose(fd);
//             return ESP_FAIL;
//         }

//         //post_item(key_value);

//         remaining -= HID_KEYBOARD_IN_RPT_LEN;
//     }

//     fclose(fd);
//     ESP_LOGI(TAG, "Macro sended successfully");

//     httpd_resp_set_status(req, "303 See Other");
//     httpd_resp_set_hdr(req, "Location", "/");
//     httpd_resp_sendstr(req, "Macro sended successfully");
//     return ESP_OK;
// }

/* Function to start the file server */
esp_err_t configure_server()
{
    char base_path[] = "/spiffs";
    static struct file_server_data *server_data = NULL;

    if (server_data) {
        ESP_LOGE(TAG, "File server already started");
        return ESP_ERR_INVALID_STATE;
    }

    /* Allocate memory for server data */
    server_data = calloc(1, sizeof(struct file_server_data));
    if (!server_data) {
        ESP_LOGE(TAG, "Failed to allocate memory for server data");
        return ESP_ERR_NO_MEM;
    }
    strlcpy(server_data->base_path, base_path,
            sizeof(server_data->base_path));

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    /* Use the URI wildcard matching function in order to
     * allow the same handler to respond to multiple different
     * target URIs which match the wildcard scheme */
    config.uri_match_fn = httpd_uri_match_wildcard;

    ESP_LOGI(TAG, "Starting HTTP Server");
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start file server!");
        return ESP_FAIL;
    }

    /* URI handler for getting uploaded files */
    httpd_uri_t file_download = {
        .uri = "/*",  // Match all URIs of type /path/to/file
        .method = HTTP_GET,
        .handler = download_get_handler,
        .user_ctx = server_data    // Pass server data as context
    };
    httpd_register_uri_handler(server, &file_download);

    /* URI handler for uploading files to server */
    httpd_uri_t file_upload = {
        .uri = "/upload/*",   // Match all URIs of type /upload/path/to/file
        .method = HTTP_POST,
        .handler = upload_post_handler,
        .user_ctx = server_data    // Pass server data as context
    };
    httpd_register_uri_handler(server, &file_upload);

    /* URI handler for deleting files from server */
    httpd_uri_t file_delete = {
        .uri = "/delete/*",   // Match all URIs of type /delete/path/to/file
        .method = HTTP_POST,
        .handler = delete_post_handler,
        .user_ctx = server_data    // Pass server data as context
    };
    httpd_register_uri_handler(server, &file_delete);

    // httpd_uri_t enable_macro = {
    //     .uri = "/enable/*",
    //     .method = HTTP_POST,
    //     .handler = enable_macro_handler,
    //     .user_ctx = server_data
    // };
    // httpd_register_uri_handler(server, &enable_macro);

    // httpd_uri_t send_macro = {
    //     .uri = "/send/*",
    //     .method = HTTP_POST,
    //     .handler = send_macro_handler,
    //     .user_ctx = server_data
    // };
    // httpd_register_uri_handler(server, &send_macro);

    return ESP_OK;
}
