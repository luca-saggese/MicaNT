#ifdef _DEBUG_MEM

#define cmd_alloc(size) cmd_alloc_dbg(size, __FILE__, __LINE__)
#define cmd_realloc(ptr,size) cmd_realloc_dbg(ptr, size, __FILE__, __LINE__)
#define cmd_free(ptr) cmd_free_dbg(ptr, __FILE__, __LINE__)
#define cmd_checkbuffer(ptr) cmd_checkbuffer_dbg(ptr, __FILE__, __LINE__)
#define cmd_dup(str) cmd_dup_dbg(str, __FILE__, __LINE__)

void *
cmd_alloc_dbg(size_t size, const char *file, int line);

void *
cmd_realloc_dbg(void *ptr, size_t size, const char *file, int line);

void
cmd_free_dbg(void *ptr, const char *file, int line);

TCHAR *
cmd_dup_dbg(const TCHAR *str, const char *file, int line);

void
cmd_checkbuffer_dbg(void *ptr, const char *file, int line);

void
cmd_exit(int code);

#else

/* malloc returns void*, which C++ does not implicitly convert to the many
 * typed CMD allocations. Keep the legacy call sites while making the
 * conversion explicit and centralized for the C++23 port. */
struct CMD_ALLOCATION
{
    void* value;
    template<typename T>
    operator T*() const noexcept { return static_cast<T*>(value); }
};

inline CMD_ALLOCATION cmd_allocate(size_t size) noexcept
{
    return { malloc(size) };
}

#define cmd_alloc(size) cmd_allocate(size)
inline CMD_ALLOCATION cmd_reallocate(void* pointer, size_t size) noexcept
{
    return { realloc(pointer, size) };
}
#define cmd_realloc(ptr,size) cmd_reallocate(ptr, size)
#define cmd_free(ptr) free(ptr)
#define cmd_dup(str) _tcsdup(str)
#define cmd_checkbuffer(ptr)
#define cmd_exit(code) ExitProcess(code)

#endif
