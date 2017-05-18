/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SourceBufferResource.h"

#include <algorithm>

#include "nsISeekableStream.h"
#include "nsISupports.h"
#include "mozilla/Logging.h"
#include "MediaData.h"

mozilla::LogModule* GetSourceBufferResourceLog()
{
  static mozilla::LazyLogModule sLogModule("SourceBufferResource");
  return sLogModule;
}

#define SBR_DEBUG(arg, ...) MOZ_LOG(GetSourceBufferResourceLog(), mozilla::LogLevel::Debug, ("SourceBufferResource(%p:%s)::%s: " arg, this, mType.OriginalString().Data(), __func__, ##__VA_ARGS__))
#define SBR_DEBUGV(arg, ...) MOZ_LOG(GetSourceBufferResourceLog(), mozilla::LogLevel::Verbose, ("SourceBufferResource(%p:%s)::%s: " arg, this, mType.OriginalString().Data(), __func__, ##__VA_ARGS__))

namespace mozilla {

nsresult
SourceBufferResource::Close()
{
  ReentrantMonitorAutoEnter mon(mMonitor);
  SBR_DEBUG("Close");
  //MOZ_ASSERT(!mClosed);
  mClosed = true;
  mon.NotifyAll();
  return NS_OK;
}

nsresult
SourceBufferResource::ReadAt(int64_t aOffset, char* aBuffer, uint32_t aCount, uint32_t* aBytes)
{
  SBR_DEBUG("ReadAt(aOffset=%lld, aBuffer=%p, aCount=%u, aBytes=%p)",
            aOffset, aBytes, aCount, aBytes);
  ReentrantMonitorAutoEnter mon(mMonitor);
  return ReadAtInternal(aOffset, aBuffer, aCount, aBytes, /* aMayBlock = */ true);
}

nsresult
SourceBufferResource::ReadAtInternal(int64_t aOffset, char* aBuffer, uint32_t aCount, uint32_t* aBytes,
                                     bool aMayBlock)
{
  mMonitor.AssertCurrentThreadIn();

  MOZ_ASSERT_IF(!aMayBlock, aBytes);

  if (mClosed ||
      aOffset < 0 ||
      uint64_t(aOffset) < mInputBuffer.GetOffset() ||
      aOffset > GetLength()) {
    return NS_ERROR_FAILURE;
  }

  while (aMayBlock &&
         !mEnded &&
         aOffset + aCount > GetLength()) {
    SBR_DEBUGV("waiting for data");
    mMonitor.Wait();
    // The callers of this function should have checked this, but it's
    // possible that we had an eviction while waiting on the monitor.
    if (uint64_t(aOffset) < mInputBuffer.GetOffset()) {
      return NS_ERROR_FAILURE;
    }
  }

  uint32_t available = GetLength() - aOffset;
  uint32_t count = std::min(aCount, available);

  // Keep the position of the last read to have Tell() approximately give us
  // the position we're up to in the stream.
  mOffset = aOffset + count;

  SBR_DEBUGV("offset=%llu GetLength()=%u available=%u count=%u mEnded=%d",
             aOffset, GetLength(), available, count, mEnded);
  if (available == 0) {
    SBR_DEBUGV("reached EOF");
    *aBytes = 0;
    return NS_OK;
  }

  mInputBuffer.CopyData(aOffset, count, aBuffer);
  *aBytes = count;

  return NS_OK;
}

nsresult
SourceBufferResource::ReadFromCache(char* aBuffer, int64_t aOffset, uint32_t aCount)
{
  SBR_DEBUG("ReadFromCache(aBuffer=%p, aOffset=%lld, aCount=%u)",
            aBuffer, aOffset, aCount);
  ReentrantMonitorAutoEnter mon(mMonitor);
  uint32_t bytesRead;
  nsresult rv = ReadAtInternal(aOffset, aBuffer, aCount, &bytesRead, /* aMayBlock = */ false);
  NS_ENSURE_SUCCESS(rv, rv);

  // ReadFromCache return failure if not all the data is cached.
  return bytesRead == aCount ? NS_OK : NS_ERROR_FAILURE;
}

uint32_t
SourceBufferResource::EvictData(uint64_t aPlaybackOffset, int64_t aThreshold,
                                ErrorResult& aRv)
{
  SBR_DEBUG("EvictData(aPlaybackOffset=%llu,"
            "aThreshold=%u)", aPlaybackOffset, aThreshold);
  ReentrantMonitorAutoEnter mon(mMonitor);
  uint32_t result = mInputBuffer.Evict(aPlaybackOffset, aThreshold, aRv);
  if (result > 0) {
    // Wake up any waiting threads in case a ReadInternal call
    // is now invalid.
    mon.NotifyAll();
  }
  return result;
}

void
SourceBufferResource::EvictBefore(uint64_t aOffset, ErrorResult& aRv)
{
  SBR_DEBUG("EvictBefore(aOffset=%llu)", aOffset);
  ReentrantMonitorAutoEnter mon(mMonitor);

  mInputBuffer.EvictBefore(aOffset, aRv);

  // Wake up any waiting threads in case a ReadInternal call
  // is now invalid.
  mon.NotifyAll();
}

uint32_t
SourceBufferResource::EvictAll()
{
  SBR_DEBUG("EvictAll()");
  ReentrantMonitorAutoEnter mon(mMonitor);
  return mInputBuffer.EvictAll();
}

void
SourceBufferResource::AppendData(MediaByteBuffer* aData)
{
  SBR_DEBUG("AppendData(aData=%p, aLength=%u)",
            aData->Elements(), aData->Length());
  ReentrantMonitorAutoEnter mon(mMonitor);
  mInputBuffer.AppendItem(aData);
  mEnded = false;
  mon.NotifyAll();
}

void
SourceBufferResource::Ended()
{
  SBR_DEBUG("");
  ReentrantMonitorAutoEnter mon(mMonitor);
  mEnded = true;
  mon.NotifyAll();
}

SourceBufferResource::~SourceBufferResource()
{
  SBR_DEBUG("");
}

SourceBufferResource::SourceBufferResource(const MediaContainerType& aType)
  : mType(aType)
  , mMonitor("mozilla::SourceBufferResource::mMonitor")
  , mOffset(0)
  , mClosed(false)
  , mEnded(false)
{
  SBR_DEBUG("");
}

#undef SBR_DEBUG
#undef SBR_DEBUGV
} // namespace mozilla
