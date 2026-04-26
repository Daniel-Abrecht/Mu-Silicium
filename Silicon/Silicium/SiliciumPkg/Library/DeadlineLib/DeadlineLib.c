#include <Library/DeadlineLib.h>
#include <Library/TimerLib.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>

static UINT64 timer_start;
static UINT64 timer_count;
static UINT64 timer_frequency;
static BOOLEAN timer_is_countdown;

RETURN_STATUS EFIAPI DeadlineLibInit(VOID){
  UINT64 start, end;
  timer_frequency = GetPerformanceCounterProperties(&start, &end);
  if(start < end){
    timer_start = start;
    timer_count = end - start;
  }else{
    timer_is_countdown = TRUE;
    timer_start = end;
    timer_count = start - end;
  }
  return EFI_SUCCESS;
}

EFI_STATUS Deadline_set(Deadline* t, UINT32 duration_ms){
  UINT64 duration = MultU64x32(timer_frequency, duration_ms) / 1000;
  if(duration > timer_count/2)
    return EFI_INVALID_PARAMETER;
  UINT64 now = GetPerformanceCounter() - timer_start;
  ASSERT(now <= timer_count);
  if(timer_is_countdown)
    now = timer_count - now;
  UINT64 end_time = now + duration;
  if(end_time > timer_count || end_time < now){
    // We want to calculate in mod timer_count, and end_time overflowed timer_count.
    // We subtract `timer_count+1`. If the integer itself overflowed, it'll underflow again.
    // Since end_time can't overflow timer_count more than once and timer_count is smaller then 2**64,
    // this will work out such that it seams as if we calculated in mod timer_count from the start.
    end_time -= timer_count+1;
  }
  t->end_time = end_time;
  return EFI_SUCCESS;
}

BOOLEAN Deadline_is_expired(Deadline* t){
  UINT64 now = GetPerformanceCounter() - timer_start;
  ASSERT(now <= timer_count);
  if(timer_is_countdown)
    now = timer_count - now;
  UINT64 remaining = t->end_time - now;
  if(remaining > timer_count){
    // remaining underflowed. We add `timer_count+1` so it'll overflow again. Since it can't underflow more than once,
    // this'll be as if we calculated in mod timer_count from the start.
    remaining += timer_count + 1;
  }
  // We don't accept `timeouts > timer_count/2`, so if remaining is bigger than that, the timeout must have expired.
  return remaining > timer_count/2;
}
