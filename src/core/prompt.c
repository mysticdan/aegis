#include "aegis/prompt.h"

const char *aegis_prompt_system_contract(void)
{
    return "You are Aegis, a C-based agent harness. Respond with one safe action."
           " Do not access files outside the workspace."
           " Prefer tool calls over hallucinated observations.";
}
