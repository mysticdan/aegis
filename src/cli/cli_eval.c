#define _XOPEN_SOURCE 700

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cjson/cJSON.h>

#include "aegis/cli_command.h"
#include "aegis/message.h"
#include "aegis/response.h"
#include "aegis/runtime.h"
#include "aegis/session.h"
#include "aegis/state.h"
#include "aegis/tool_process.h"

static cJSON *read_suite(const char *path)
{
    FILE *file = fopen(path, "rb");
    long length;
    char *data;
    cJSON *root;

    if (!file) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0 || length > 16L * 1024L * 1024L) {
        fclose(file);
        return NULL;
    }
    data = malloc((size_t)length + 1U);
    if (!data) {
        fclose(file);
        return NULL;
    }
    if (length && fread(data, 1U, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    data[length] = '\0';
    root = cJSON_Parse(data);
    free(data);
    return root;
}

static int run_argv(const char *const argv[])
{
    pid_t child = fork();
    int status;

    if (child < 0) {
        return 0;
    }
    if (child == 0) {
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) {
            return 0;
        }
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static int prepare_workspace(
    const char *source,
    char *root,
    size_t root_size,
    char *workspace,
    size_t workspace_size
)
{
    char template[] = "/tmp/aegis-eval-XXXXXX";
    const char *copy_argv[] = {"cp", "-a", source, workspace, NULL};

    if (!mkdtemp(template) ||
        snprintf(root, root_size, "%s", template) < 0 ||
        strlen(template) >= root_size ||
        snprintf(workspace, workspace_size, "%s/workspace", template) < 0 ||
        strlen(template) + 11U >= workspace_size) {
        return 0;
    }
    return run_argv(copy_argv);
}

static void cleanup_workspace(const char *root)
{
    const char *argv[] = {"rm", "-rf", "--", root, NULL};
    (void)run_argv(argv);
}

static int run_success_command(
    const AegisConfig *config,
    const char *workspace,
    const char *command
)
{
    AegisToolContext context;
    AegisToolResult result;
    AegisStatus status;

    if (!command || !command[0]) {
        return 1;
    }
    aegis_tool_context_from_config(&context, config, workspace, 1);
    context.allow_shell = 1;
    aegis_tool_result_init(&result);
    status = aegis_tool_run_shell_command(&context, command, &result);
    aegis_tool_result_clear(&result);
    return status == AEGIS_OK;
}

int aegis_cli_cmd_eval(const CliOptions *options)
{
    CliEnvironment environment;
    cJSON *suite;
    cJSON *suite_id;
    cJSON *cases;
    cJSON *test_case;
    cJSON *results;
    char error[AEGIS_CLI_ERROR_MAX];
    int exit_code;
    int total = 0;
    int passed = 0;
    int interrupted = 0;

    if (!options->suite || options->positional_count != 0U) {
        return cli_error(
            options, AEGIS_CLI_EXIT_USAGE,
            "usage: aegis eval --suite <path>");
    }
    suite = read_suite(options->suite);
    suite_id = suite
        ? cJSON_GetObjectItemCaseSensitive(suite, "id")
        : NULL;
    cases = suite
        ? cJSON_GetObjectItemCaseSensitive(suite, "cases")
        : NULL;
    if (!cJSON_IsObject(suite) || !cJSON_IsString(suite_id) ||
        !cJSON_IsArray(cases)) {
        cJSON_Delete(suite);
        return cli_error(
            options, AEGIS_CLI_EXIT_EVAL, "invalid evaluation suite");
    }
    exit_code = cli_load_environment(
        options, &environment, error, sizeof(error));
    if (exit_code != AEGIS_CLI_EXIT_SUCCESS) {
        cJSON_Delete(suite);
        cli_environment_clear(&environment);
        return cli_error(options, exit_code, "%s", error);
    }
    exit_code = cli_confirm_agent_execution(
        options, &environment.config, 0);
    if (exit_code != AEGIS_CLI_EXIT_SUCCESS) {
        cJSON_Delete(suite);
        cli_environment_clear(&environment);
        return exit_code;
    }
    results = cJSON_CreateArray();
    cJSON_ArrayForEach(test_case, cases) {
        cJSON *id = cJSON_GetObjectItemCaseSensitive(test_case, "id");
        cJSON *task_value =
            cJSON_GetObjectItemCaseSensitive(test_case, "task");
        cJSON *workspace_value =
            cJSON_GetObjectItemCaseSensitive(test_case, "workspace");
        cJSON *expect =
            cJSON_GetObjectItemCaseSensitive(test_case, "expect_final_contains");
        cJSON *success =
            cJSON_GetObjectItemCaseSensitive(test_case, "success_command");
        char temporary_root[AEGIS_CONFIG_PATH_MAX * 2U] = "";
        char workspace[AEGIS_CONFIG_PATH_MAX * 2U];
        char session_id[AEGIS_SESSION_ID_MAX];
        AegisRuntime *runtime;
        AegisMessage message;
        AegisResponse response;
        AegisStatus status;
        int case_passed;
        cJSON *result;

        ++total;
        if (!cJSON_IsString(id) || !cJSON_IsString(task_value)) {
            case_passed = 0;
            goto case_result;
        }
        if (cJSON_IsString(workspace_value)) {
            const char *source = workspace_value->valuestring;
            char source_path[AEGIS_CONFIG_PATH_MAX * 2U];
            if (source[0] != '/' &&
                !cli_join_path(
                    source_path,
                    sizeof(source_path),
                    environment.workspace,
                    source)) {
                case_passed = 0;
                goto case_result;
            }
            if (!prepare_workspace(
                    source[0] == '/' ? source : source_path,
                    temporary_root,
                    sizeof(temporary_root),
                    workspace,
                    sizeof(workspace))) {
                case_passed = 0;
                goto case_result;
            }
        } else {
            snprintf(workspace, sizeof(workspace), "%s", environment.workspace);
        }
        if (aegis_session_id_make(
                "eval", session_id, sizeof(session_id)) != AEGIS_OK) {
            case_passed = 0;
            goto case_result;
        }
        memset(&message, 0, sizeof(message));
        message.channel = "eval";
        message.user_id = "eval";
        message.session_id = session_id;
        message.text = task_value->valuestring;
        message.workspace = workspace;
        message.profile = environment.config.active_profile.id;
        message.no_input = 1;
        message.is_cancelled = cli_interrupted;
        aegis_response_init(&response);
        runtime = aegis_runtime_new_with_config(&environment.config);
        status = runtime
            ? aegis_runtime_handle_message(runtime, &message, &response)
            : AEGIS_ERR_RUNTIME;
        aegis_runtime_free(runtime);
        interrupted = status == AEGIS_ERR_INTERRUPTED;
        case_passed = status == AEGIS_OK;
        if (case_passed && cJSON_IsString(expect)) {
            case_passed = response.text &&
                strstr(response.text, expect->valuestring) != NULL;
        }
        if (case_passed && cJSON_IsString(success)) {
            case_passed = run_success_command(
                &environment.config,
                workspace,
                success->valuestring
            );
        }
        aegis_response_free(&response);

case_result:
        if (case_passed) {
            ++passed;
        }
        result = cJSON_CreateObject();
        cJSON_AddStringToObject(
            result,
            "id",
            cJSON_IsString(id) ? id->valuestring : "invalid"
        );
        cJSON_AddBoolToObject(result, "passed", case_passed);
        cJSON_AddItemToArray(results, result);
        if (!options->json) {
            printf("%s %s\n",
                   case_passed ? "PASS" : "FAIL",
                   cJSON_IsString(id) ? id->valuestring : "invalid");
        }
        if (temporary_root[0]) {
            cleanup_workspace(temporary_root);
        }
        if (interrupted || (!case_passed && options->fail_fast)) {
            break;
        }
    }
    if (options->json) {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(
            root,
            "status",
            interrupted
                ? "interrupted"
                : passed == total ? "success" : "failed"
        );
        cJSON_AddStringToObject(root, "command", "eval");
        cJSON_AddStringToObject(root, "suite", suite_id->valuestring);
        cJSON_AddNumberToObject(root, "passed", passed);
        cJSON_AddNumberToObject(root, "total", total);
        cJSON_AddItemToObject(root, "results", results);
        cli_json_print(root);
        cJSON_Delete(root);
    } else {
        cJSON_Delete(results);
        printf("\nScore: %d/%d\n", passed, total);
    }
    cJSON_Delete(suite);
    cli_environment_clear(&environment);
    return interrupted
        ? AEGIS_CLI_EXIT_INTERRUPTED
        : passed == total
        ? AEGIS_CLI_EXIT_SUCCESS
        : AEGIS_CLI_EXIT_EVAL;
}
