#ifndef STDINC_H_3cb5a54f_fb1f_45b9_9235_3dfc7c485131
#define STDINC_H_3cb5a54f_fb1f_45b9_9235_3dfc7c485131

#include <chrono>
#include <mutex>

typedef int8_t      int8;
typedef int16_t     int16;
typedef int32_t     int32;
typedef int64_t     int64;
typedef uint8_t     byte;
typedef uint8_t     uint8;
typedef uint16_t    uint16;
typedef uint32_t    uint32;
typedef uint64_t    uint64;


namespace   clocks = std::chrono;
typedef clocks::steady_clock                    stdclock;
typedef clocks::steady_clock::time_point        timepoint;

typedef std::lock_guard<std::mutex>             scoped_lock;
typedef std::lock_guard<std::recursive_mutex>   rscoped_lock;


#ifndef  SAFE_DELETE
    #define SAFE_DELETE(p)  { if(p) { delete (p);     (p)=nullptr; } }
#endif

#ifndef  SAFE_DELETE_ARRAY
    #define SAFE_DELETE_ARRAY(p)  { if(p) { delete[] (p);     (p)=nullptr; } }
#endif

#ifndef  SAFE_RELEASE
    #define SAFE_RELEASE(p) { if(p) { (p)->Release(); (p)=NULL; } }
#endif

#ifndef STRLEN
    #define STRLEN(s)   ((s) ? strlen((s)) : 0)
#endif



// override these logging macros to suit your implementation
#define LOGD(format, ...) printf(format, ##__VA_ARGS__)



#endif  // STDINC_H_3cb5a54f_fb1f_45b9_9235_3dfc7c485131
