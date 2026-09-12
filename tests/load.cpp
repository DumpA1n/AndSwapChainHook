#include <cstdlib>
#include <dirent.h>
#include <dlfcn.h>
#include <iostream>
#include <jni.h>
#include <signal.h>
#include <string>
#include <sys/prctl.h>

#define CHECK(value)                                                                               \
    do                                                                                             \
    {                                                                                              \
        if (!(value))                                                                              \
        {                                                                                          \
            std::cerr << __LINE__ << ": " #value "\n";                                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)
static int Count(const char *path)
{
    DIR *directory = opendir(path);
    if (!directory)
        return -1;
    int count = 0;
    while (auto *entry = readdir(directory))
        if (entry->d_name[0] != '.')
            ++count;
    closedir(directory);
    return count;
}
int main(int argc, char **argv)
{
    CHECK(argc == 2);
    const int threads = Count("/proc/self/task");
    const int descriptors = Count("/proc/self/fd");
    const int files = Count(".");
    const int dumpability = prctl(PR_GET_DUMPABLE);
    CHECK(threads >= 1 && descriptors >= 0 && files >= 0 && dumpability >= 0);
    const int signals[] = {SIGSEGV, SIGBUS, SIGABRT, SIGILL, SIGFPE};
    struct sigaction before[5]{};
    for (int i = 0; i < 5; ++i)
        CHECK(sigaction(signals[i], nullptr, &before[i]) == 0);
    for (int cycle = 0; cycle < 50; ++cycle)
    {
        void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
        if (!library)
            std::cerr << dlerror() << '\n';
        CHECK(library);
        auto load = reinterpret_cast<jint (*)(JavaVM *, void *)>(dlsym(library, "JNI_OnLoad"));
        CHECK(load && load(nullptr, reinterpret_cast<void *>(1337)) == JNI_VERSION_1_6);
        const int currentThreads = Count("/proc/self/task"),
                  currentDescriptors = Count("/proc/self/fd"), currentFiles = Count(".");
        if (currentThreads != threads || currentDescriptors != descriptors || currentFiles != files)
            std::cerr << "cycle=" << cycle << " threads=" << threads << "->" << currentThreads
                      << " descriptors=" << descriptors << "->" << currentDescriptors
                      << " directory_entries=" << files << "->" << currentFiles << '\n';
        CHECK(currentThreads == threads && currentDescriptors == descriptors &&
              currentFiles == files);
        CHECK(prctl(PR_GET_DUMPABLE) == dumpability);
        for (int i = 0; i < 5; ++i)
        {
            struct sigaction after{};
            CHECK(sigaction(signals[i], nullptr, &after) == 0);
            CHECK(after.sa_sigaction == before[i].sa_sigaction &&
                  after.sa_flags == before[i].sa_flags);
        }
        CHECK(dlclose(library) == 0);
    }
    std::cout << "E01: 50 inert dlopen/JNI_OnLoad/dlclose cycles; threads, descriptors, directory "
                 "entries, dumpability and signal handlers unchanged\n";
}
