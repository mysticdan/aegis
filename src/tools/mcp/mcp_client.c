#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <cjson/cJSON.h>

#include "aegis/mcp.h"
#include "aegis/tool_http.h"
#include "aegis/tool_process.h"

static char *make_request(
    int id,
    const char *method,
    const char *params_json
)
{
    cJSON *request = cJSON_CreateObject();
    cJSON *params = params_json ? cJSON_Parse(params_json) : NULL;
    char *rendered;

    if (!request || (params_json && !params)) {
        cJSON_Delete(request);
        cJSON_Delete(params);
        return NULL;
    }
    cJSON_AddStringToObject(request, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(request, "id", id);
    cJSON_AddStringToObject(request, "method", method);
    if (params) {
        cJSON_AddItemToObject(request, "params", params);
    }
    rendered = cJSON_PrintUnformatted(request);
    cJSON_Delete(request);
    return rendered;
}

static long monotonic_milliseconds(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }
    return now.tv_sec * 1000L + now.tv_nsec / 1000000L;
}

static AegisStatus mcp_stdio_request(
    const AegisToolContext *context,
    const char *server,
    const char *input,
    AegisToolResult *result
)
{
    int input_pipe[2];
    int output_pipe[2];
    pid_t child;
    char *buffer;
    size_t used = 0U;
    size_t maximum;
    size_t input_length;
    size_t offset = 0U;
    long started;
    AegisStatus status = AEGIS_ERR_RUNTIME;

    if (pipe(input_pipe) != 0) {
        return AEGIS_ERR_IO;
    }
    if (pipe(output_pipe) != 0) {
        close(input_pipe[0]);
        close(input_pipe[1]);
        return AEGIS_ERR_IO;
    }
    child = fork();
    if (child < 0) {
        close(input_pipe[0]);
        close(input_pipe[1]);
        close(output_pipe[0]);
        close(output_pipe[1]);
        return AEGIS_ERR_IO;
    }
    if (child == 0) {
        (void)setpgid(0, 0);
        if (chdir(context->workspace_root) != 0 ||
            dup2(input_pipe[0], STDIN_FILENO) < 0 ||
            dup2(output_pipe[1], STDOUT_FILENO) < 0 ||
            dup2(output_pipe[1], STDERR_FILENO) < 0) {
            _exit(126);
        }
        close(input_pipe[0]);
        close(input_pipe[1]);
        close(output_pipe[0]);
        close(output_pipe[1]);
        execl("/bin/sh", "sh", "-c", server, (char *)NULL);
        _exit(127);
    }
    (void)setpgid(child, child);
    close(input_pipe[0]);
    close(output_pipe[1]);
    input_length = strlen(input);
    while (offset < input_length) {
        ssize_t written = write(
            input_pipe[1],
            input + offset,
            input_length - offset
        );
        if (written > 0) {
            offset += (size_t)written;
        } else if (written < 0 && errno != EINTR) {
            break;
        }
    }
    close(input_pipe[1]);

    maximum = context->max_output_bytes
        ? context->max_output_bytes
        : 65536U;
    buffer = malloc(maximum + 1U);
    if (!buffer) {
        kill(-child, SIGKILL);
        waitpid(child, NULL, 0);
        close(output_pipe[0]);
        return AEGIS_ERR_OOM;
    }
    started = monotonic_milliseconds();
    while (monotonic_milliseconds() - started <
           context->config->shell_timeout_ms) {
        struct pollfd descriptor = {
            .fd = output_pipe[0],
            .events = POLLIN | POLLHUP
        };
        int poll_result = poll(&descriptor, 1, 100);

        if (context->is_cancelled &&
            context->is_cancelled(context->adapter_userdata)) {
            status = AEGIS_ERR_INTERRUPTED;
            break;
        }
        if (poll_result < 0) {
            if (errno == EINTR) {
                continue;
            }
            status = AEGIS_ERR_IO;
            break;
        }
        if (descriptor.revents & (POLLIN | POLLHUP)) {
            ssize_t count = read(
                output_pipe[0],
                buffer + used,
                maximum - used
            );
            if (count > 0) {
                size_t line_start = 0U;
                size_t index;

                used += (size_t)count;
                buffer[used] = '\0';
                for (index = 0U; index < used; ++index) {
                    if (buffer[index] == '\n') {
                        cJSON *response;
                        cJSON *id;
                        char saved = buffer[index];

                        buffer[index] = '\0';
                        response = cJSON_Parse(buffer + line_start);
                        id = response
                            ? cJSON_GetObjectItemCaseSensitive(response, "id")
                            : NULL;
                        if (cJSON_IsNumber(id) &&
                            id->valuedouble == 2.0) {
                            cJSON *error =
                                cJSON_GetObjectItemCaseSensitive(
                                    response, "error");
                            status = aegis_tool_result_set_stdout(
                                result,
                                buffer + line_start
                            );
                            if (status == AEGIS_OK &&
                                cJSON_IsObject(error)) {
                                status = AEGIS_ERR_RUNTIME;
                                result->ok = 0;
                                result->exit_code = 1;
                            }
                            cJSON_Delete(response);
                            buffer[index] = saved;
                            goto done;
                        }
                        cJSON_Delete(response);
                        buffer[index] = saved;
                        line_start = index + 1U;
                    }
                }
                if (line_start > 0U) {
                    memmove(
                        buffer,
                        buffer + line_start,
                        used - line_start
                    );
                    used -= line_start;
                }
                if (used == maximum) {
                    aegis_tool_result_set_error(
                        result,
                        "MCP response exceeds output limit"
                    );
                    status = AEGIS_ERR_RUNTIME;
                    break;
                }
            } else if (count == 0) {
                status = AEGIS_ERR_IO;
                break;
            } else if (errno != EINTR) {
                status = AEGIS_ERR_IO;
                break;
            }
        }
    }
    if (status == AEGIS_ERR_RUNTIME && !result->error_message) {
        aegis_tool_result_set_error(result, "MCP request timed out");
    }

done:
    kill(-child, SIGTERM);
    if (waitpid(child, NULL, WNOHANG) == 0) {
        struct timespec delay = {.tv_sec = 0, .tv_nsec = 100000000L};
        nanosleep(&delay, NULL);
        if (waitpid(child, NULL, WNOHANG) == 0) {
            kill(-child, SIGKILL);
            waitpid(child, NULL, 0);
        }
    }
    close(output_pipe[0]);
    free(buffer);
    return status;
}

static AegisStatus normalize_mcp_http_result(AegisToolResult *result)
{
    const char *cursor;

    if (!result || !result->stdout_data) {
        return AEGIS_ERR_PARSE;
    }
    cursor = result->stdout_data;
    while (*cursor) {
        const char *line_end = strchr(cursor, '\n');
        size_t length = line_end
            ? (size_t)(line_end - cursor)
            : strlen(cursor);
        const char *payload = cursor;

        if (length > 5U && strncmp(cursor, "data:", 5U) == 0) {
            payload += 5U;
            while (*payload == ' ') {
                ++payload;
            }
            length -= (size_t)(payload - cursor);
        }
        if (*payload == '{') {
            char *line = malloc(length + 1U);
            cJSON *json;
            cJSON *id;

            if (!line) {
                return AEGIS_ERR_OOM;
            }
            memcpy(line, payload, length);
            line[length] = '\0';
            json = cJSON_Parse(line);
            id = json
                ? cJSON_GetObjectItemCaseSensitive(json, "id")
                : NULL;
            if (cJSON_IsNumber(id) && id->valuedouble == 2.0) {
                cJSON *error =
                    cJSON_GetObjectItemCaseSensitive(json, "error");
                free(result->stdout_data);
                result->stdout_data = line;
                result->output_bytes = length;
                result->ok = !cJSON_IsObject(error);
                result->exit_code = result->ok ? 0 : 1;
                cJSON_Delete(json);
                return result->ok ? AEGIS_OK : AEGIS_ERR_RUNTIME;
            }
            cJSON_Delete(json);
            free(line);
        }
        if (!line_end) {
            break;
        }
        cursor = line_end + 1U;
    }
    return AEGIS_ERR_PARSE;
}

AegisStatus aegis_mcp_request(
    const AegisToolContext *context,
    const char *server,
    const char *method,
    const char *params_json,
    AegisToolResult *result
)
{
    char *request;
    AegisStatus status;

    if (!context || !server || !server[0] || !method || !result) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    request = make_request(2, method, params_json);
    if (!request) {
        return AEGIS_ERR_PARSE;
    }
    if (strncmp(server, "http://", 7U) == 0 ||
        strncmp(server, "https://", 8U) == 0) {
        status = aegis_tool_http_request(
            context, "POST", server, request, result);
        if (status == AEGIS_OK) {
            status = normalize_mcp_http_result(result);
        }
    } else {
        static const char initialize[] =
            "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\","
            "\"params\":{\"protocolVersion\":\"2025-06-18\","
            "\"capabilities\":{},\"clientInfo\":{\"name\":\"aegis\","
            "\"version\":\"1.0.0\"}}}\n"
            "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\","
            "\"params\":{}}\n";
        size_t input_length =
            strlen(initialize) + strlen(request) + 2U;
        char *input;

        if (aegis_command_policy_is_blocked(server)) {
            cJSON_free(request);
            aegis_tool_result_set_error(result, "MCP command is blocked");
            return AEGIS_ERR_POLICY_DENIED;
        }
        input = malloc(input_length);
        if (!input) {
            cJSON_free(request);
            return AEGIS_ERR_OOM;
        }
        snprintf(input, input_length, "%s%s\n", initialize, request);
        status = mcp_stdio_request(context, server, input, result);
        free(input);
    }
    cJSON_free(request);
    return status;
}
