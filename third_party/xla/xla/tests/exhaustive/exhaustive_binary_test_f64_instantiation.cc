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

#include "xla/tests/xla_test_backend_predicates.h"  // IWYU pragma: keep, exhaustive_binary_test_f64_instantiation.inc
#include "xla/tests/exhaustive/exhaustive_binary_test_definitions.h"  // IWYU pragma: keep, exhaustive_binary_test_f64_instantiation.inc
#include "xla/tests/exhaustive/exhaustive_op_test_utils.h"  // IWYU pragma: keep, exhaustive_binary_test_f64_instantiation.inc
#include "tsl/platform/test.h"  // IWYU pragma: keep, exhaustive_binary_test_f64_instantiation.inc

namespace xla {
namespace exhaustive_op_test {
namespace {

#include "xla/tests/exhaustive/exhaustive_binary_test_f64_instantiation.inc"

}  // namespace
}  // namespace exhaustive_op_test
}  // namespace xla
