/* Copyright 2024 The OpenXLA Authors.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "xla/stream_executor/cuda/cuda_timer.h"

#include <cstdint>
#include <memory>
#include <optional>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "absl/status/status.h"
#include "absl/time/time.h"
#include "xla/stream_executor/cuda/cuda_platform_id.h"
#include "xla/stream_executor/device_memory.h"
#include "xla/stream_executor/gpu/gpu_test_kernels.h"
#include "xla/stream_executor/kernel.h"
#include "xla/stream_executor/launch_dim.h"
#include "xla/stream_executor/platform.h"
#include "xla/stream_executor/platform_manager.h"
#include "xla/stream_executor/stream.h"
#include "xla/tsl/platform/status_matchers.h"
#include "xla/tsl/platform/statusor.h"

namespace stream_executor::gpu {
namespace {
using ::testing::Gt;
using ::tsl::testing::IsOk;

class CudaTimerTest : public ::testing::TestWithParam<CudaTimer::TimerType> {
 public:
  void LaunchSomeKernel(StreamExecutor* executor, Stream* stream) {
    TF_ASSERT_OK_AND_ASSIGN(auto add, LoadAddI32TestKernel(executor));

    int64_t length = 4;
    int64_t byte_length = sizeof(int32_t) * length;

    // Prepare arguments: a=1, b=2, c=0
    DeviceMemory<int32_t> a = executor->AllocateArray<int32_t>(length, 0);
    DeviceMemory<int32_t> b = executor->AllocateArray<int32_t>(length, 0);
    DeviceMemory<int32_t> c = executor->AllocateArray<int32_t>(length, 0);

    ASSERT_THAT(stream->Memset32(&a, 1, byte_length), absl_testing::IsOk());
    ASSERT_THAT(stream->Memset32(&b, 2, byte_length), absl_testing::IsOk());
    ASSERT_THAT(stream->MemZero(&c, byte_length), absl_testing::IsOk());

    ASSERT_THAT(add.Launch(ThreadDim(), BlockDim(4), stream, a, b, c),
                absl_testing::IsOk());
  }

  StreamExecutor* executor_;
  std::unique_ptr<Stream> stream_;

 private:
  void SetUp() override {
    TF_ASSERT_OK_AND_ASSIGN(Platform * platform,
                            stream_executor::PlatformManager::PlatformWithId(
                                stream_executor::cuda::kCudaPlatformId));
    TF_ASSERT_OK_AND_ASSIGN(executor_, platform->ExecutorForDevice(0));
    TF_ASSERT_OK_AND_ASSIGN(stream_, executor_->CreateStream(std::nullopt));
  }
};

TEST_P(CudaTimerTest, Create) {
  TF_ASSERT_OK_AND_ASSIGN(
      CudaTimer timer, CudaTimer::Create(executor_, stream_.get(), GetParam()));

  // We don't really care what kernel we launch here as long as it takes a
  // non-zero amount of time.
  LaunchSomeKernel(executor_, stream_.get());

  TF_ASSERT_OK_AND_ASSIGN(absl::Duration timer_result,
                          timer.GetElapsedDuration());
  EXPECT_THAT(timer_result, Gt(absl::ZeroDuration()));
  EXPECT_THAT(timer.GetElapsedDuration(),
              absl_testing::StatusIs(absl::StatusCode::kFailedPrecondition));
}

INSTANTIATE_TEST_SUITE_P(CudaTimerTest, CudaTimerTest,
                         ::testing::Values(CudaTimer::TimerType::kEventBased,
                                           CudaTimer::TimerType::kDelayKernel));

}  // namespace
}  // namespace stream_executor::gpu
