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

#include "xla/stream_executor/cuda/compilation_provider.h"

#include <memory>
#include <string>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "xla/stream_executor/cuda/compilation_options.h"
#include "xla/stream_executor/cuda/compilation_provider_test.h"
#include "xla/stream_executor/cuda/driver_compilation_provider.h"
#include "xla/stream_executor/cuda/nvjitlink_compilation_provider.h"
#include "xla/stream_executor/cuda/nvjitlink_support.h"
#include "xla/stream_executor/cuda/nvptxcompiler_compilation_provider.h"
#include "xla/stream_executor/cuda/ptx_compiler_support.h"
#include "xla/stream_executor/cuda/subprocess_compilation.h"
#include "xla/stream_executor/cuda/subprocess_compilation_provider.h"
#include "xla/stream_executor/device_description.h"
#include "tsl/platform/env.h"
#include "tsl/platform/status_matchers.h"
#include "tsl/platform/statusor.h"
#include "tsl/platform/test.h"
#include "tsl/platform/threadpool.h"

namespace stream_executor::cuda {
using ::testing::_;
using ::testing::AnyOf;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Not;
using ::tsl::testing::IsOk;
using ::tsl::testing::IsOkAndHolds;
using ::tsl::testing::StatusIs;

void CompilationProviderTest::SetUp() {
#ifdef ABSL_HAVE_MEMORY_SANITIZER
  if (GetParam() == kNvJitLinkCompilationProviderName) {
    GTEST_SKIP() << "nvjitlink is a precompiled and not instrumented binary "
                    "library, so it's not compatible with MSAN.";
  }
  if (GetParam() == kNvptxcompilerCompilationProviderName) {
    GTEST_SKIP() << "nvptxcompiler is a precompiled and not instrumented "
                    "binary library, so it's not compatible with MSAN.";
  }
#endif

  if (GetParam() == kNvJitLinkCompilationProviderName &&
      !IsLibNvJitLinkSupported()) {
    GTEST_SKIP() << "nvjitlink is not supported in this build.";
  }
  if (GetParam() == kNvptxcompilerCompilationProviderName &&
      !IsLibNvPtxCompilerSupported()) {
    GTEST_SKIP() << "nvptxcompiler is not supported in this build.";
  }

  TF_ASSERT_OK_AND_ASSIGN(compilation_provider_,
                          CreateCompilationProvider(GetParam()));
}

absl::StatusOr<std::unique_ptr<CompilationProvider>>
CompilationProviderTest::CreateCompilationProvider(absl::string_view name) {
  if (name == kSubprocessCompilationProviderName) {
    TF_ASSIGN_OR_RETURN(auto ptxas,
                        FindCudaExecutable("ptxas", "/does/not/exist"));
    TF_ASSIGN_OR_RETURN(auto nvlink,
                        FindCudaExecutable("nvlink", "/does/not/exist"));
    return std::make_unique<SubprocessCompilationProvider>(ptxas, nvlink);
  }

  if (name == kNvJitLinkCompilationProviderName) {
    return std::make_unique<NvJitLinkCompilationProvider>();
  }

  if (name == kNvptxcompilerCompilationProviderName) {
    return std::make_unique<NvptxcompilerCompilationProvider>();
  }

  if (name == kDriverCompilationProviderName) {
    return std::make_unique<DriverCompilationProvider>();
  }

  return absl::NotFoundError(
      absl::StrCat("Unknown compilation provider: ", name));
}

TEST_P(CompilationProviderTest, NameIsNotEmpty) {
  EXPECT_THAT(compilation_provider()->name(), Not(IsEmpty()));
}

// Generated by the following command:
//
// echo "__device__ int magic() { return 42; }" |
//   nvcc -o - -rdc true --ptx --x cu -
//
constexpr const char kDependeePtx[] = R"(
.version 8.0
.target sm_52
.address_size 64

        // .globl       _Z5magicv

.visible .func  (.param .b32 func_retval0) _Z5magicv()
{
        .reg .b32       %r<2>;

        mov.u32         %r1, 42;
        st.param.b32    [func_retval0+0], %r1;
        ret;
})";

// Generated by the following command:
//
// echo "__device__ int magic(); __global__ void kernel(int* output) \
//   { *output = magic(); }" | nvcc -o - -rdc true --ptx --x cu -
//
constexpr const char kDependentPtx[] = R"(
.version 8.0
.target sm_52
.address_size 64

        // .globl       _Z6kernelPi
.extern .func  (.param .b32 func_retval0) _Z5magicv
()
;

.visible .entry _Z6kernelPi(
        .param .u64 _Z6kernelPi_param_0
)
// Insert .maxnreg directive here!
{
        .reg .b32       %r<2>;
        .reg .b64       %rd<3>;

        ld.param.u64    %rd1, [_Z6kernelPi_param_0];
        cvta.to.global.u64      %rd2, %rd1;
        { // callseq 0, 0
        .reg .b32 temp_param_reg;
        .param .b32 retval0;
        call.uni (retval0), 
        _Z5magicv, 
        (
        );
        ld.param.b32    %r1, [retval0+0];
        } // callseq 0
        st.global.u32   [%rd2], %r1;
        ret;
})";

// Generated by the following command:
//
// echo "__global__ void kernel(int* output) { *output = 42; }" |
//   nvcc -o - -rdc true --ptx --x cu -
//
constexpr const char kStandalonePtx[] = R"(
.version 8.0
.target sm_52
.address_size 64

        // .globl       _Z6kernelPi

.visible .entry _Z6kernelPi (
        .param .u64 _Z6kernelPi_param_0
)
{
        .reg .b32       %r<16>;
        .reg .b64       %rd<3>;


        ld.param.u64    %rd1, [_Z6kernelPi_param_0];
        cvta.to.global.u64      %rd2, %rd1;
        mov.u32         %r1, 42;
        st.global.u32   [%rd2], %r15;
        ret;

})";

constexpr stream_executor::CudaComputeCapability kDefaultComputeCapability{5,
                                                                           2};

TEST_P(CompilationProviderTest, CompileStandaloneModuleSucceeds) {
  CompilationOptions options;
  TF_ASSERT_OK_AND_ASSIGN(
      Assembly module, compilation_provider()->Compile(
                           kDefaultComputeCapability, kStandalonePtx, options));
  EXPECT_FALSE(module.cubin.empty());
}

TEST_P(CompilationProviderTest, CompileStandaloneRelocatableModuleSucceeds) {
  if (!compilation_provider()->SupportsCompileToRelocatableModule()) {
    GTEST_SKIP();
  }

  CompilationOptions options;
  TF_ASSERT_OK_AND_ASSIGN(
      RelocatableModule module,
      compilation_provider()->CompileToRelocatableModule(
          kDefaultComputeCapability, kStandalonePtx, options));
  EXPECT_FALSE(module.cubin.empty());
}

TEST_P(CompilationProviderTest,
       CompileToRelocatableModuleFailsWhenUnsupported) {
  if (compilation_provider()->SupportsCompileToRelocatableModule()) {
    GTEST_SKIP();
  }

  CompilationOptions options;
  EXPECT_THAT(compilation_provider()->CompileToRelocatableModule(
                  kDefaultComputeCapability, kStandalonePtx, options),
              absl_testing::StatusIs(absl::StatusCode::kUnavailable));
}

TEST_P(CompilationProviderTest, CompileAndLinkStandaloneModule) {
  if (!compilation_provider()->SupportsCompileAndLink()) {
    GTEST_SKIP() << "Compilation provider doesn't support CompileAndLink";
  }

  CompilationOptions options;
  TF_ASSERT_OK_AND_ASSIGN(
      Assembly assembly,
      compilation_provider()->CompileAndLink(kDefaultComputeCapability,
                                             {Ptx{kStandalonePtx}}, options));
  EXPECT_FALSE(assembly.cubin.empty());
}

TEST_P(CompilationProviderTest, CompileDependentRelocatableModuleSucceeds) {
  if (!compilation_provider()->SupportsCompileToRelocatableModule()) {
    GTEST_SKIP();
  }

  CompilationOptions options;
  TF_ASSERT_OK_AND_ASSIGN(
      RelocatableModule module,
      compilation_provider()->CompileToRelocatableModule(
          kDefaultComputeCapability, kDependentPtx, options));
  EXPECT_FALSE(module.cubin.empty());
}

TEST_P(CompilationProviderTest,
       CompileDependentModuleFailsWithUndefinedReferenceError) {
#ifdef ABSL_HAVE_THREAD_SANITIZER
  if (GetParam() == "nvjitlink") {
    GTEST_SKIP()
        << "nvjitlink fails with TSAN enabled due to some wrongly unlocked "
           "mutex. Note that this only happens when the compilation fails.";
  }
#endif

  CompilationOptions options;
  EXPECT_THAT(compilation_provider()->Compile(kDefaultComputeCapability,
                                              kDependentPtx, options),
              absl_testing::StatusIs(
                  _, AnyOf(HasSubstr("Undefined reference"),
                           HasSubstr("Unresolved extern function"))));
}

TEST_P(CompilationProviderTest,
       CompileAndLinkDependentModuleFailsWithUndefinedReferenceError) {
  if (!compilation_provider()->SupportsCompileAndLink()) {
    GTEST_SKIP() << "Compilation provider doesn't support CompileAndLink";
  }
#ifdef ABSL_HAVE_THREAD_SANITIZER
  if (GetParam() == "nvjitlink") {
    GTEST_SKIP()
        << "nvjitlink fails with TSAN enabled due to some wrongly unlocked "
           "mutex. Note that this only happens when the compilation fails.";
  }
#endif

  CompilationOptions options;
  EXPECT_THAT(compilation_provider()->CompileAndLink(
                  kDefaultComputeCapability, {Ptx{kDependentPtx}}, options),
              absl_testing::StatusIs(
                  _, AnyOf(HasSubstr("Undefined reference"),
                           HasSubstr("Unresolved extern function"))));
}

TEST_P(CompilationProviderTest, CompileAndLinkMultipleModulesSucceeds) {
  if (!compilation_provider()->SupportsCompileAndLink()) {
    GTEST_SKIP() << "Compilation provider doesn't support CompileAndLink";
  }

  CompilationOptions default_options;
  TF_ASSERT_OK_AND_ASSIGN(
      Assembly assembly,
      compilation_provider()->CompileAndLink(
          kDefaultComputeCapability, {Ptx{kDependentPtx}, Ptx{kDependeePtx}},
          default_options));
  EXPECT_FALSE(assembly.cubin.empty());
}

TEST_P(CompilationProviderTest, CompileAndLaterLinkMultipleModulesSucceeds) {
  if (!compilation_provider()->SupportsCompileToRelocatableModule()) {
    GTEST_SKIP()
        << "Compilation provider doesn't support CompileToRelocatableModule";
  }

  if (!compilation_provider()->SupportsCompileAndLink()) {
    GTEST_SKIP() << "Compilation provider doesn't support CompileAndLink";
  }

  CompilationOptions default_options;
  TF_ASSERT_OK_AND_ASSIGN(
      RelocatableModule module1,
      compilation_provider()->CompileToRelocatableModule(
          kDefaultComputeCapability, kDependentPtx, default_options));
  TF_ASSERT_OK_AND_ASSIGN(
      RelocatableModule module2,
      compilation_provider()->CompileToRelocatableModule(
          kDefaultComputeCapability, kDependeePtx, default_options));
  TF_ASSERT_OK_AND_ASSIGN(
      Assembly assembly,
      compilation_provider()->CompileAndLink(
          kDefaultComputeCapability, {std::move(module1), std::move(module2)},
          default_options));
  EXPECT_FALSE(assembly.cubin.empty());
}

TEST_P(CompilationProviderTest, CancelsOnRegSpill) {
  if (!compilation_provider()->SupportsCompileAndLink()) {
    GTEST_SKIP() << "Compilation provider doesn't support CompileAndLink";
  }
  if (GetParam() == kDriverCompilationProviderName) {
    GTEST_SKIP() << "Driver compilation doesn't support cancel_if_reg_spill";
  }

  std::string dependent_ptx = absl::StrReplaceAll(
      kDependentPtx, {{"// Insert .maxnreg directive here!", ".maxnreg 16"}});

  // We have to disable optimization here, otherwise PTXAS will optimize our
  // trivial register usages away and we don't spill as intended.
  CompilationOptions options;
  options.cancel_if_reg_spill = true;
  options.disable_optimizations = true;

  EXPECT_THAT(compilation_provider()->CompileAndLink(
                  kDefaultComputeCapability,
                  {Ptx{dependent_ptx}, Ptx{kDependeePtx}}, options),
              absl_testing::StatusIs(absl::StatusCode::kCancelled));

  // This is to make sure we didn't break the PTX and that's why it was failing
  // in the previous assertion.
  options.cancel_if_reg_spill = false;
  EXPECT_THAT(compilation_provider()->CompileAndLink(
                  kDefaultComputeCapability,
                  {Ptx{dependent_ptx}, Ptx{kDependeePtx}}, options),
              absl_testing::IsOk());
}

TEST_P(CompilationProviderTest,
       CompileFailsWhenInvalidArchitectureIsRequested) {
  CompilationOptions default_options;
  EXPECT_THAT(compilation_provider()->Compile(CudaComputeCapability{100, 0},
                                              kStandalonePtx, default_options),
              Not(absl_testing::IsOk()));
}

TEST_P(CompilationProviderTest,
       CompileToRelocatableModuleFailsWhenInvalidArchitectureIsRequested) {
  if (!compilation_provider()->SupportsCompileToRelocatableModule()) {
    GTEST_SKIP()
        << "Compilation provider doesn't support CompileToRelocatableModule";
  }

  CompilationOptions default_options;
  EXPECT_THAT(
      compilation_provider()->CompileToRelocatableModule(
          CudaComputeCapability{100, 0}, kStandalonePtx, default_options),
      Not(absl_testing::IsOk()));
}

TEST_P(CompilationProviderTest,
       CompileAndLinkFailsWhenInvalidArchitectureIsRequested) {
  if (!compilation_provider()->SupportsCompileAndLink()) {
    GTEST_SKIP() << "Compilation provider doesn't support CompileAndLink";
  }

  CompilationOptions default_options;
  EXPECT_THAT(compilation_provider()->CompileAndLink(
                  CudaComputeCapability{100, 0}, {Ptx{kStandalonePtx}},
                  default_options),
              Not(absl_testing::IsOk()));
}

TEST_P(CompilationProviderTest, ParallelCompileReturnsSameResult) {
  TF_ASSERT_OK_AND_ASSIGN(
      Assembly reference_assembly,
      compilation_provider()->Compile(kDefaultComputeCapability, kStandalonePtx,
                                      CompilationOptions()));

  // We spawn a hundred threads and schedule parallel calls to `Compile` on
  // them. This is not guaranteed to fail if something was broken, but since we
  // also run this test with thread sanitizer enabled, this should give us a
  // reliable signal whether the locking logic is bogus or not.
  tsl::thread::ThreadPool pool(tsl::Env::Default(), "test_pool", 100);

  for (int i = 0; i < pool.NumThreads(); ++i) {
    pool.Schedule([&]() {
      EXPECT_THAT(
          compilation_provider()->Compile(kDefaultComputeCapability,
                                          kStandalonePtx, CompilationOptions()),
          absl_testing::IsOkAndHolds(reference_assembly));
    });
  }
}

TEST_P(CompilationProviderTest,
       ParallelCompileToRelocatableModuleReturnsSameResult) {
  if (!compilation_provider()->SupportsCompileToRelocatableModule()) {
    GTEST_SKIP()
        << "Compilation provider doesn't support CompileToRelocatableModule";
  }

  TF_ASSERT_OK_AND_ASSIGN(
      RelocatableModule reference_module,
      compilation_provider()->CompileToRelocatableModule(
          kDefaultComputeCapability, kStandalonePtx, CompilationOptions()));

  // We spawn a hundred threads and schedule parallel calls to
  // `CompileToRelocatableModule` on them. This is not guaranteed to fail if
  // something was broken, but since we also run this test with thread sanitizer
  // enabled, this should give us a reliable signal whether the locking logic is
  // bogus or not.
  tsl::thread::ThreadPool pool(tsl::Env::Default(), "test_pool", 100);

  for (int i = 0; i < pool.NumThreads(); ++i) {
    pool.Schedule([&]() {
      EXPECT_THAT(
          compilation_provider()->CompileToRelocatableModule(
              kDefaultComputeCapability, kStandalonePtx, CompilationOptions()),
          absl_testing::IsOkAndHolds(reference_module));
    });
  }
}

TEST_P(CompilationProviderTest, ParallelCompileAndLinkReturnsSameResult) {
  if (!compilation_provider()->SupportsCompileAndLink()) {
    GTEST_SKIP() << "Compilation provider doesn't support CompileAndLink";
  }

  TF_ASSERT_OK_AND_ASSIGN(Assembly reference_assembly,
                          compilation_provider()->CompileAndLink(
                              kDefaultComputeCapability, {Ptx{kStandalonePtx}},
                              CompilationOptions()));

  // We spawn a hundred threads and schedule parallel calls to `CompileAndLink`
  // on them. This is not guaranteed to fail if something was broken, but since
  // we also run this test with thread sanitizer enabled, this should give us a
  // reliable signal whether the locking logic is bogus or not.
  tsl::thread::ThreadPool pool(tsl::Env::Default(), "test_pool", 100);

  for (int i = 0; i < pool.NumThreads(); ++i) {
    pool.Schedule([&]() {
      EXPECT_THAT(compilation_provider()->CompileAndLink(
                      kDefaultComputeCapability, {Ptx{kStandalonePtx}},
                      CompilationOptions()),
                  absl_testing::IsOkAndHolds(reference_assembly));
    });
  }
}

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(CompilationProviderTest);

}  // namespace stream_executor::cuda
