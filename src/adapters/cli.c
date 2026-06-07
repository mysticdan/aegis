#include <stdio.h>
#include <string.h>
#incldue "aegis/cli.h"
#include "aegis/command.h"

static void usage(void) {
    fprintf(stderr,
        "usage:\n"
        "  aegis run --task \"...\" [--workspace .] [--profile coding]\n"
        "  aegis replay --trace traces/latest.jsonl\n"
        "  aegis inspect --trace traces/latest.jsonl\n"
        "  aegis eval --suite evals/suites/coding_basic.json\n");
}

int aegis_cli_main(int argc, char **argv) {
    if (argc < 2) {
        usage();
        return 1;
    }


    AegisCommand cmd = aegis_command_from_string(argv[1]);
    if (cmd == AEGIS_CMD_RUN) {

    }

    if (cmd == AEGIS_CMD_REPLAY) {

    }

    if (cmd == AEGIS_CMD_INSPECT) {

    }

    if (cmd == AEGIS_CMD_EVAL) {

    }

    usage();
    return 1;
}