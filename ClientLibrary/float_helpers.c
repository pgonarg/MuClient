/* Stub float conversion helpers required by .NET NativeAOT WorkstationGC on x86 with MSVC 14.32 */
#include <stdint.h>

float __ltof3(int64_t a)  { return (float)a; }
float __ultof3(uint64_t a){ return (float)a; }
