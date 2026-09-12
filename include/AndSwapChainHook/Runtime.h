#pragma once
#include <stddef.h>
#include <stdint.h>

#if defined(__GNUC__)
#define ASC_API __attribute__((visibility("default")))
#else
#define ASC_API
#endif
#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct ASC_Session ASC_Session;
    typedef enum ASC_Error
    {
        ASC_OK,
        ASC_INVALID_ARGUMENT,
        ASC_INVALID_STATE,
        ASC_BUSY,
        ASC_MEMORY_ERROR,
        ASC_CAPACITY_EXCEEDED,
        ASC_INTERNAL_ERROR
    } ASC_Error;
    typedef enum ASC_State
    {
        ASC_CREATED,
        ASC_INITIALIZED,
        ASC_RUNNING,
        ASC_COMPLETED,
        ASC_FAILED,
        ASC_CANCELLED,
        ASC_STOPPED
    } ASC_State;
    typedef struct ASC_Status
    {
        ASC_Error error;
        ASC_State state;
        int memoryError;
        int systemError;
        size_t bytesRead;
        size_t matchCount;
    } ASC_Status;
    typedef struct ASC_ScanOptions
    {
        size_t byteBudget;
        size_t maxMatches;
        size_t chunkSize;
        uint32_t deadlineMilliseconds;
    } ASC_ScanOptions;

    // Create has no process inspection side effects. Destroy requires exclusive handle
    // ownership, cancels and joins the worker, and releases the process-memory reader.
    ASC_API ASC_Status ASC_Create(ASC_Session **session);
    ASC_API ASC_Status ASC_Initialize(ASC_Session *session);
    // Start copies pattern bytes before returning. The handle owns at most one worker.
    // The scan reads only the explicitly supplied range in this process.
    ASC_API ASC_Status ASC_Start(ASC_Session *session, uintptr_t address, size_t length,
                                 const unsigned char *pattern, size_t patternLength,
                                 ASC_ScanOptions options);
    ASC_API ASC_Status ASC_Cancel(ASC_Session *session);
    ASC_API ASC_Status ASC_Stop(ASC_Session *session);
    // Snapshot is a value copy. A null output array with capacity zero queries count.
    // A completed or failed snapshot remains available after Stop.
    ASC_API ASC_Status ASC_Snapshot(ASC_Session *session, uintptr_t *matches, size_t capacity);
    ASC_API void ASC_Destroy(ASC_Session *session);
#ifdef __cplusplus
}
#endif
