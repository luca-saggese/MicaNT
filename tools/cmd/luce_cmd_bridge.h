/*
 * Luce terminal to CMD core boundary.
 * The callback is process-local and keeps terminal rendering in the existing
 * Ring 3 frontend while the command interpreter owns parsing and execution.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t (__stdcall *LuceCmdOutputWriter)(
    void* stream, const wchar_t* text, uint32_t length);

int32_t LuceCmdInitialize(LuceCmdOutputWriter writer);
int32_t LuceCmdExecuteLine(wchar_t* line);
int32_t LuceCmdShouldExit(void);
void LuceCmdPrintPrompt(void);
void LuceCmdShutdown(void);
int32_t LuceCmdReadInput(wchar_t* buffer, uint32_t capacity, int32_t keyOnly);
uint16_t LuceCmdGetStreamAttribute(void* stream);
int32_t LuceCmdSetTerminalTitle(const wchar_t* title);

#ifdef __cplusplus
}
#endif
