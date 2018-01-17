/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* APIs for getting a stack trace of the current thread. Derived from
 * Firefox's source/mozglue/misc/StackWalk.cpp. */

#pragma once

#include <stdint.h>

extern void* __libc_stack_end;

/**
 * The callback for MozStackWalk and MozStackWalkThread.
 *
 * @param aFrameNumber  The frame number (starts at 1, not 0).
 * @param aPC           The program counter value.
 * @param aSP           The best approximation possible of what the stack
 *                      pointer will be pointing to when the execution returns
 *                      to executing that at aPC. If no approximation can
 *                      be made it will be nullptr.
 * @param aClosure      Extra data passed in from MozStackWalk() or
 *                      MozStackWalkThread().
 */
typedef void
(*MozWalkStackCallback)(uint32_t aFrameNumber, void* aPC, void* aSP,
                        void* aClosure);

/**
 * Call aCallback for each stack frame on the current thread, from
 * the caller of MozStackWalk to main (or above).
 *
 * @param aCallback    Callback function, called once per frame.
 * @param aSkipFrames  Number of initial frames to skip.  0 means that
 *                     the first callback will be for the caller of
 *                     MozStackWalk.
 * @param aMaxFrames   Maximum number of frames to trace.  0 means no limit.
 * @param aClosure     Caller-supplied data passed through to aCallback.
 *
 * May skip some stack frames due to compiler optimizations or code
 * generation.
 */
void
MozStackWalk(MozWalkStackCallback aCallback, uint32_t aSkipFrames,
             uint32_t aMaxFrames, void* aClosure);

void
FramePointerStackWalk(MozWalkStackCallback aCallback, uint32_t aSkipFrames,
                      uint32_t aMaxFrames, void* aClosure, void** aBp,
                      void* aStackEnd)
{
  // Stack walking code courtesy Kipp's "leaky".

  int32_t skip = aSkipFrames;
  uint32_t numFrames = 0;
  while (aBp) {
    void** next = (void**)*aBp;
    // aBp may not be a frame pointer on i386 if code was compiled with
    // -fomit-frame-pointer, so do some sanity checks.
    // (aBp should be a frame pointer on ppc(64) but checking anyway may help
    // a little if the stack has been corrupted.)
    // We don't need to check against the begining of the stack because
    // we can assume that aBp > sp
    if (next <= aBp ||
        next > aStackEnd ||
        (uintptr_t(next) & 3)) {
      break;
    }
    void* pc = *(aBp + 1);
    aBp += 2;
    if (--skip < 0) {
      // Assume that the SP points to the BP of the function
      // it called. We can't know the exact location of the SP
      // but this should be sufficient for our use the SP
      // to order elements on the stack.
      numFrames++;
      (*aCallback)(numFrames, pc, aBp, aClosure);
      if (aMaxFrames != 0 && numFrames == aMaxFrames) {
        break;
      }
    }
    aBp = next;
  }
}

__attribute__((noinline))
void
MozStackWalk(MozWalkStackCallback aCallback, uint32_t aSkipFrames,
             uint32_t aMaxFrames, void* aClosure)
{
  // Get the frame pointer
  void** bp = (void**)__builtin_frame_address(0);

  void* stackEnd = __libc_stack_end;
  // for darwin: pthread_get_stackaddr_np(pthread_self());
  FramePointerStackWalk(aCallback, aSkipFrames, aMaxFrames, aClosure, bp,
                        stackEnd);
}
