#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include <arpa/inet.h>
#include <curl/curl.h>
#include <netinet/in.h>

#include "aegis/tool_http.h"

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
    size_t maximum;
} HttpOutput;

static int http_transfer_cancelled(
    void *userdata,
    curl_off_t download_total,
    curl_off_t download_now,
    curl_off_t upload_total,
    curl_off_t upload_now
)
{
    const AegisToolContext *context = userdata;

    (void)download_total;
    (void)download_now;
    (void)upload_total;
    (void)upload_now;
    return context && context->is_cancelled &&
        context->is_cancelled(context->adapter_userdata);
}

static int list_contains(
    const AegisConfigStringList *list,
    const char *value
)
{
    size_t index;

    for (index = 0U; index < list->count; ++index) {
        if (strcmp(list->items[index], value) == 0) {
            return 1;
        }
    }
    return 0;
}

static int private_ipv4(uint32_t value)
{
    return (value & 0xFF000000U) == 0x00000000U ||
        (value & 0xFF000000U) == 0x0A000000U ||
        (value & 0xFFC00000U) == 0x64400000U ||
        (value & 0xFF000000U) == 0x7F000000U ||
        (value & 0xFFFF0000U) == 0xA9FE0000U ||
        (value & 0xFFF00000U) == 0xAC100000U ||
        (value & 0xFFFFFF00U) == 0xC0000000U ||
        (value & 0xFFFFFF00U) == 0xC0000200U ||
        (value & 0xFFFF0000U) == 0xC0A80000U ||
        (value & 0xFFFE0000U) == 0xC6120000U ||
        (value & 0xFFFFFF00U) == 0xC6336400U ||
        (value & 0xFFFFFF00U) == 0xCB007100U ||
        (value & 0xF0000000U) == 0xE0000000U ||
        (value & 0xF0000000U) == 0xF0000000U;
}

static int private_address(const struct sockaddr *address)
{
    if (address->sa_family == AF_INET) {
        const struct sockaddr_in *ipv4 =
            (const struct sockaddr_in *)address;
        uint32_t value = ntohl(ipv4->sin_addr.s_addr);

        return private_ipv4(value);
    }
    if (address->sa_family == AF_INET6) {
        const struct sockaddr_in6 *ipv6 =
            (const struct sockaddr_in6 *)address;
        const unsigned char *bytes = ipv6->sin6_addr.s6_addr;
        static const unsigned char loopback[16] = {
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1
        };

        if (IN6_IS_ADDR_V4MAPPED(&ipv6->sin6_addr)) {
            uint32_t mapped;

            memcpy(&mapped, bytes + 12U, sizeof(mapped));
            return private_ipv4(ntohl(mapped));
        }
        return memcmp(bytes, loopback, sizeof(loopback)) == 0 ||
            IN6_IS_ADDR_UNSPECIFIED(&ipv6->sin6_addr) ||
            IN6_IS_ADDR_LINKLOCAL(&ipv6->sin6_addr) ||
            (bytes[0] & 0xFEU) == 0xFCU ||
            (bytes[0] == 0xFEU && (bytes[1] & 0xC0U) == 0xC0U) ||
            (bytes[0] == 0x20U && bytes[1] == 0x01U &&
             bytes[2] == 0x0DU && bytes[3] == 0xB8U) ||
            IN6_IS_ADDR_MULTICAST(&ipv6->sin6_addr);
    }
    return 1;
}

static int literal_host_is_private(const char *host)
{
    struct in_addr ipv4;
    struct in6_addr ipv6;
    char unbracketed[INET6_ADDRSTRLEN + 1U];
    const char *candidate = host;
    size_t length;

    if (!host) {
        return 0;
    }
    length = strlen(host);
    if (length >= 2U && host[0] == '[' && host[length - 1U] == ']') {
        if (length - 2U >= sizeof(unbracketed)) {
            return 1;
        }
        memcpy(unbracketed, host + 1U, length - 2U);
        unbracketed[length - 2U] = '\0';
        candidate = unbracketed;
    }
    if (inet_pton(AF_INET, candidate, &ipv4) == 1) {
        return private_ipv4(ntohl(ipv4.s_addr));
    }
    if (inet_pton(AF_INET6, candidate, &ipv6) == 1) {
        struct sockaddr_in6 address = {
            .sin6_family = AF_INET6,
            .sin6_addr = ipv6
        };

        return private_address((const struct sockaddr *)&address);
    }
    return 0;
}

static int url_allowed(const AegisConfig *config, const char *url)
{
    CURLU *parsed = curl_url();
    char *scheme = NULL;
    char *host = NULL;
    int allowed;

    if (!parsed ||
        curl_url_set(parsed, CURLUPART_URL, url, 0U) != CURLUE_OK ||
        curl_url_get(parsed, CURLUPART_SCHEME, &scheme, 0U) != CURLUE_OK ||
        curl_url_get(parsed, CURLUPART_HOST, &host, 0U) != CURLUE_OK) {
        curl_free(scheme);
        curl_free(host);
        curl_url_cleanup(parsed);
        return 0;
    }
    allowed = list_contains(&config->http_allowed_schemes, scheme) &&
        host[0] != '\0' &&
        (config->http_allow_private_networks ||
         (strcmp(host, "localhost") != 0 &&
          strcmp(host, "localhost.localdomain") != 0 &&
          !literal_host_is_private(host)));
    curl_free(scheme);
    curl_free(host);
    curl_url_cleanup(parsed);
    return allowed;
}

static curl_socket_t open_http_socket(
    void *userdata,
    curlsocktype purpose,
    struct curl_sockaddr *address
)
{
    const AegisConfig *config = userdata;

    (void)purpose;
    if (!config || (!config->http_allow_private_networks &&
                    private_address(&address->addr))) {
        return CURL_SOCKET_BAD;
    }
    return socket(
        address->family,
        address->socktype,
        address->protocol
    );
}

static size_t receive_http(
    char *data,
    size_t size,
    size_t count,
    void *userdata
)
{
    HttpOutput *output = userdata;
    size_t bytes = size * count;
    size_t required;
    size_t capacity;
    char *resized;

    if ((size && bytes / size != count) ||
        bytes > output->maximum - output->length) {
        return 0U;
    }
    required = output->length + bytes + 1U;
    if (required > output->capacity) {
        capacity = output->capacity ? output->capacity : 4096U;
        while (capacity < required) {
            if (capacity > SIZE_MAX / 2U) {
                return 0U;
            }
            capacity *= 2U;
        }
        resized = realloc(output->data, capacity);
        if (!resized) {
            return 0U;
        }
        output->data = resized;
        output->capacity = capacity;
    }
    memcpy(output->data + output->length, data, bytes);
    output->length += bytes;
    output->data[output->length] = '\0';
    return bytes;
}

AegisStatus aegis_tool_http_request(
    const AegisToolContext *context,
    const char *method,
    const char *url,
    const char *body,
    AegisToolResult *result
)
{
    CURL *curl;
    CURLcode curl_status;
    struct curl_slist *headers = NULL;
    HttpOutput output = {0};
    long status_code = 0;

    if (!context || !context->config || !method || !url || !result) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    if (context->is_cancelled &&
        context->is_cancelled(context->adapter_userdata)) {
        return AEGIS_ERR_INTERRUPTED;
    }
    if (!context->allow_network || !context->config->http_enabled) {
        aegis_tool_result_set_error(result, "network access is disabled");
        return AEGIS_ERR_POLICY_DENIED;
    }
    if (!url_allowed(context->config, url)) {
        aegis_tool_result_set_error(result, "URL is blocked by HTTP policy");
        return AEGIS_ERR_POLICY_DENIED;
    }
    output.maximum = (size_t)context->config->http_max_response_bytes;
    curl = curl_easy_init();
    if (!curl) {
        return AEGIS_ERR_RUNTIME;
    }
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive_http);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &output);
    curl_easy_setopt(
        curl, CURLOPT_TIMEOUT_MS, (long)context->config->http_timeout_ms);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_OPENSOCKETFUNCTION, open_http_socket);
    curl_easy_setopt(curl, CURLOPT_OPENSOCKETDATA, context->config);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, http_transfer_cancelled);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, context);
    if (strcmp(method, "POST") == 0) {
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(
            headers,
            "Accept: application/json, text/event-stream"
        );
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body ? body : "");
    }
    curl_status = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (curl_status != CURLE_OK) {
        free(output.data);
        if (curl_status == CURLE_ABORTED_BY_CALLBACK &&
            context->is_cancelled &&
            context->is_cancelled(context->adapter_userdata)) {
            return AEGIS_ERR_INTERRUPTED;
        }
        aegis_tool_result_set_error(result, curl_easy_strerror(curl_status));
        return AEGIS_ERR_IO;
    }
    result->stdout_data = output.data
        ? output.data
        : calloc(1U, 1U);
    if (!result->stdout_data) {
        return AEGIS_ERR_OOM;
    }
    result->output_bytes = output.length;
    result->exit_code = status_code >= 200 && status_code < 300
        ? 0
        : (int)status_code;
    result->ok = result->exit_code == 0;
    return result->ok ? AEGIS_OK : AEGIS_ERR_RUNTIME;
}
