#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "aegis/str.h"
#include "aegis/tool_process.h"

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} ProcessBuffer;

static long monotonic_milliseconds(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }
    return now.tv_sec * 1000L + now.tv_nsec / 1000000L;
}

static int append_bytes(
    ProcessBuffer *buffer,
    const char *data,
    size_t length,
    size_t maximum
)
{
    size_t required;
    size_t capacity;
    char *resized;

    if (length > maximum - buffer->length) {
        return 0;
    }
    required = buffer->length + length + 1U;
    if (required > buffer->capacity) {
        capacity = buffer->capacity ? buffer->capacity : 4096U;
        while (capacity < required) {
            if (capacity > SIZE_MAX / 2U) {
                return 0;
            }
            capacity *= 2U;
        }
        resized = realloc(buffer->data, capacity);
        if (!resized) {
            return 0;
        }
        buffer->data = resized;
        buffer->capacity = capacity;
    }
    memcpy(buffer->data + buffer->length, data, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
    return 1;
}

static void apply_limits(const AegisConfig *config)
{
    struct rlimit limit;

    if (!config || !config->sandbox_enabled) {
        return;
    }
    limit.rlim_cur = limit.rlim_max = (rlim_t)config->cpu_time_seconds;
    (void)setrlimit(RLIMIT_CPU, &limit);
    limit.rlim_cur = limit.rlim_max = (rlim_t)config->memory_bytes;
    (void)setrlimit(RLIMIT_AS, &limit);
    limit.rlim_cur = limit.rlim_max = (rlim_t)config->file_size_bytes;
    (void)setrlimit(RLIMIT_FSIZE, &limit);
}

static int apply_environment_policy(const AegisConfig *config)
{
    char *names[AEGIS_CONFIG_MAX_TOOLS] = {0};
    char *values[AEGIS_CONFIG_MAX_TOOLS] = {0};
    size_t count = 0U;
    size_t index;
    int ok = 1;

    if (!config ||
        strcmp(config->shell_env_policy, "whitelist") != 0) {
        return 1;
    }
    for (index = 0U;
         index < config->shell_allowed_env.count;
         ++index) {
        const char *name = config->shell_allowed_env.items[index];
        const char *value = getenv(name);

        if (!value) {
            continue;
        }
        names[count] = strdup(name);
        values[count] = strdup(value);
        if (!names[count] || !values[count]) {
            ok = 0;
            break;
        }
        ++count;
    }
    if (ok && clearenv() != 0) {
        ok = 0;
    }
    for (index = 0U; ok && index < count; ++index) {
        if (setenv(names[index], values[index], 1) != 0) {
            ok = 0;
        }
    }
    if (ok && !getenv("PATH") &&
        setenv("PATH", "/usr/local/bin:/usr/bin:/bin", 1) != 0) {
        ok = 0;
    }
    for (index = 0U; index < count; ++index) {
        free(names[index]);
        free(values[index]);
    }
    return ok;
}

static int make_nonblocking(int descriptor)
{
    int flags = fcntl(descriptor, F_GETFL);

    return flags >= 0 && fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) == 0;
}

static int drain_descriptor(
    int descriptor,
    ProcessBuffer *buffer,
    size_t maximum,
    int *open
)
{
    char chunk[4096];
    ssize_t count;

    for (;;) {
        count = read(descriptor, chunk, sizeof(chunk));
        if (count > 0) {
            if (!append_bytes(buffer, chunk, (size_t)count, maximum)) {
                return 0;
            }
        } else if (count == 0) {
            close(descriptor);
            *open = 0;
            return 1;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 1;
        } else if (errno != EINTR) {
            close(descriptor);
            *open = 0;
            return 0;
        }
    }
}

AegisStatus aegis_tool_run_process(
    const AegisToolContext *context,
    const char *const argv[],
    const char *stdin_data,
    int timeout_ms,
    AegisToolResult *result
)
{
    int stdout_pipe[2];
    int stderr_pipe[2];
    int stdin_pipe[2];
    int has_stdin = stdin_data != NULL;
    pid_t child;
    ProcessBuffer output = {0};
    ProcessBuffer error = {0};
    int stdout_open = 1;
    int stderr_open = 1;
    int child_status = 0;
    int child_done = 0;
    long started;
    size_t maximum;

    if (!context || !context->config || !context->workspace_root ||
        !argv || !argv[0] || !result || timeout_ms <= 0) {
        return AEGIS_ERR_INVALID_ARGUMENT;
    }
    if (pipe(stdout_pipe) != 0) {
        return AEGIS_ERR_IO;
    }
    if (pipe(stderr_pipe) != 0) {
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        return AEGIS_ERR_IO;
    }
    if (has_stdin && pipe(stdin_pipe) != 0) {
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        return AEGIS_ERR_IO;
    }
    child = fork();
    if (child < 0) {
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        if (has_stdin) {
            close(stdin_pipe[0]);
            close(stdin_pipe[1]);
        }
        return AEGIS_ERR_IO;
    }
    if (child == 0) {
        (void)setpgid(0, 0);
        if (chdir(context->workspace_root) != 0) {
            _exit(126);
        }
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        if (has_stdin) {
            dup2(stdin_pipe[0], STDIN_FILENO);
        } else {
            int null_fd = open("/dev/null", O_RDONLY);
            if (null_fd >= 0) {
                dup2(null_fd, STDIN_FILENO);
                close(null_fd);
            }
        }
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        if (has_stdin) {
            close(stdin_pipe[0]);
            close(stdin_pipe[1]);
        }
        apply_limits(context->config);
        if (!apply_environment_policy(context->config)) {
            _exit(126);
        }
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }

    (void)setpgid(child, child);
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);
    if (has_stdin) {
        size_t length = strlen(stdin_data);
        size_t offset = 0U;
        close(stdin_pipe[0]);
        while (offset < length) {
            ssize_t written = write(
                stdin_pipe[1], stdin_data + offset, length - offset);
            if (written > 0) {
                offset += (size_t)written;
            } else if (written < 0 && errno != EINTR) {
                break;
            }
        }
        close(stdin_pipe[1]);
    }
    if (!make_nonblocking(stdout_pipe[0]) ||
        !make_nonblocking(stderr_pipe[0])) {
        kill(-child, SIGKILL);
        waitpid(child, NULL, 0);
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        return AEGIS_ERR_IO;
    }

    maximum = context->max_output_bytes
        ? context->max_output_bytes
        : 65536U;
    if (context->config->shell_max_output_bytes > 0 &&
        maximum > (size_t)context->config->shell_max_output_bytes) {
        maximum = (size_t)context->config->shell_max_output_bytes;
    }
    started = monotonic_milliseconds();
    while (!child_done || stdout_open || stderr_open) {
        struct pollfd descriptors[2];
        int poll_result;

        if (context->is_cancelled &&
            context->is_cancelled(context->adapter_userdata)) {
            kill(-child, SIGKILL);
            waitpid(child, &child_status, 0);
            if (stdout_open) {
                close(stdout_pipe[0]);
            }
            if (stderr_open) {
                close(stderr_pipe[0]);
            }
            free(output.data);
            free(error.data);
            result->exit_code = 130;
            result->duration_ms = monotonic_milliseconds() - started;
            aegis_tool_result_set_error(result, "process interrupted");
            return AEGIS_ERR_INTERRUPTED;
        }
        descriptors[0].fd = stdout_open ? stdout_pipe[0] : -1;
        descriptors[0].events = POLLIN | POLLHUP;
        descriptors[1].fd = stderr_open ? stderr_pipe[0] : -1;
        descriptors[1].events = POLLIN | POLLHUP;
        poll_result = poll(descriptors, 2, 50);
        if (poll_result < 0 && errno != EINTR) {
            kill(-child, SIGKILL);
            waitpid(child, NULL, 0);
            free(output.data);
            free(error.data);
            return AEGIS_ERR_IO;
        }
        if (stdout_open &&
            !drain_descriptor(
                stdout_pipe[0], &output, maximum, &stdout_open)) {
            kill(-child, SIGKILL);
            waitpid(child, NULL, 0);
            free(output.data);
            free(error.data);
            aegis_tool_result_set_error(result, "process output exceeds limit");
            return AEGIS_ERR_RUNTIME;
        }
        if (stderr_open &&
            !drain_descriptor(
                stderr_pipe[0], &error, maximum, &stderr_open)) {
            kill(-child, SIGKILL);
            waitpid(child, NULL, 0);
            free(output.data);
            free(error.data);
            aegis_tool_result_set_error(result, "process output exceeds limit");
            return AEGIS_ERR_RUNTIME;
        }
        if (!child_done) {
            pid_t waited = waitpid(child, &child_status, WNOHANG);
            child_done = waited == child;
        }
        if (!child_done &&
            monotonic_milliseconds() - started >= timeout_ms) {
            kill(-child, SIGKILL);
            waitpid(child, &child_status, 0);
            child_done = 1;
            result->exit_code = 124;
            result->duration_ms = monotonic_milliseconds() - started;
            result->stderr_data = aegis_strdup("process timed out");
            free(output.data);
            free(error.data);
            return AEGIS_ERR_RUNTIME;
        }
    }

    result->stdout_data = output.data ? output.data : aegis_strdup("");
    result->stderr_data = error.data ? error.data : aegis_strdup("");
    if (!result->stdout_data || !result->stderr_data) {
        free(result->stdout_data);
        free(result->stderr_data);
        memset(result, 0, sizeof(*result));
        return AEGIS_ERR_OOM;
    }
    result->duration_ms = monotonic_milliseconds() - started;
    result->output_bytes = output.length + error.length;
    if (WIFEXITED(child_status)) {
        result->exit_code = WEXITSTATUS(child_status);
    } else if (WIFSIGNALED(child_status)) {
        result->exit_code = 128 + WTERMSIG(child_status);
    } else {
        result->exit_code = 1;
    }
    result->ok = result->exit_code == 0;
    return result->ok ? AEGIS_OK : AEGIS_ERR_RUNTIME;
}

AegisStatus aegis_tool_run_shell_command(
    const AegisToolContext *context,
    const char *command,
    AegisToolResult *result
)
{
    const char *argv[] = {"/bin/sh", "-c", command, NULL};

    if (!command ||
        !aegis_command_policy_allows(context->config, command)) {
        aegis_tool_result_set_error(result, "command is blocked by policy");
        return AEGIS_ERR_POLICY_DENIED;
    }
    return aegis_tool_run_process(
        context,
        argv,
        NULL,
        context->config->shell_timeout_ms,
        result
    );
}
