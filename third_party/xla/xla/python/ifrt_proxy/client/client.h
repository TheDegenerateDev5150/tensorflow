/*
 * Copyright 2023 The OpenXLA Authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef XLA_PYTHON_IFRT_PROXY_CLIENT_CLIENT_H_
#define XLA_PYTHON_IFRT_PROXY_CLIENT_CLIENT_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "llvm/Support/ExtensibleRTTI.h"
#include "xla/pjrt/pjrt_layout.h"
#include "xla/python/ifrt/array.h"
#include "xla/python/ifrt/array_spec.h"
#include "xla/python/ifrt/attribute_map.h"
#include "xla/python/ifrt/client.h"
#include "xla/python/ifrt/compiler.h"
#include "xla/python/ifrt/device.h"
#include "xla/python/ifrt/device_list.h"
#include "xla/python/ifrt/dtype.h"
#include "xla/python/ifrt/future.h"
#include "xla/python/ifrt/memory.h"
#include "xla/python/ifrt/remap_plan.h"
#include "xla/python/ifrt/shape.h"
#include "xla/python/ifrt/sharding.h"
#include "xla/python/ifrt/topology.h"
#include "xla/python/ifrt/tuple.h"
#include "xla/python/ifrt/user_context.h"
#include "xla/python/ifrt/value.h"
#include "xla/python/ifrt_proxy/client/compiler.h"
#include "xla/python/ifrt_proxy/client/device.h"
#include "xla/python/ifrt_proxy/client/memory.h"
#include "xla/python/ifrt_proxy/client/rpc_helper.h"
#include "xla/python/ifrt_proxy/common/ifrt_service.pb.h"
#include "xla/tsl/concurrency/ref_count.h"

namespace xla {
namespace ifrt {
namespace proxy {

// Implementation of the xla::ifrt::Client interface.
class Client final : public llvm::RTTIExtends<Client, xla::ifrt::Client> {
 public:
  static absl::StatusOr<std::unique_ptr<Client>> Create(
      std::shared_ptr<RpcHelper> rpc_helper, InitResponse init_response);

  ~Client() override;

  absl::StatusOr<xla::ifrt::ArrayRef> MakeArrayFromHostBuffer(
      const void* data, xla::ifrt::DType dtype, xla::ifrt::Shape shape,
      std::optional<absl::Span<const int64_t>> byte_strides,
      xla::ifrt::ShardingRef sharding, HostBufferSemantics semantics,
      std::function<void()> on_done_with_host_buffer) override;
  absl::StatusOr<std::vector<xla::ifrt::ArrayRef>>
  MakeArraysFromHostBufferShards(
      absl::Span<MakeArraysFromHostBufferShardsSpec> specs,
      HostBufferSemantics semantics) override;
  absl::StatusOr<std::vector<xla::ifrt::ArrayRef>> MakeErrorArrays(
      const absl::Status& error,
      absl::Span<const ArraySpec> array_specs) override;
  absl::StatusOr<xla::ifrt::ArrayRef> AssembleArrayFromSingleDeviceArrays(
      DType dtype, Shape shape, ShardingRef sharding,
      absl::Span<xla::ifrt::ArrayRef> arrays,
      ArrayCopySemantics array_copy_semantics,
      SingleDeviceShardSemantics single_device_shard_semantics) override;

  absl::StatusOr<std::vector<ArrayRef>> CopyArrays(
      absl::Span<ArrayRef> arrays, std::optional<DeviceListRef> devices,
      std::optional<MemoryKind> memory_kind,
      ArrayCopySemantics semantics) override;

  absl::StatusOr<std::vector<xla::ifrt::ArrayRef>> RemapArrays(
      const RemapPlan& plan, absl::Span<xla::ifrt::ArrayRef> arrays,
      ArrayCopySemantics semantics) override;

  xla::ifrt::Future<> GetReadyFuture(
      absl::Span<const ValueRef> values) override;

  absl::StatusOr<tsl::RCReference<Tuple>> MakeTuple(
      absl::Span<ValueRef> values) override {
    return absl::UnimplementedError(
        "MakeTuple is not supported for the IFRT proxy client.");
  }

  absl::string_view runtime_type() const override { return runtime_type_; }
  absl::string_view platform_name() const override { return platform_name_; }
  absl::string_view platform_version() const override {
    return platform_version_;
  }
  PlatformId platform_id() const override { return platform_id_; }
  const AttributeMap& Attributes() const override { return attributes_; }
  int device_count() const override { return devices().size(); }
  int addressable_device_count() const override {
    return addressable_devices().size();
  }
  absl::Span<xla::ifrt::Device* const> devices() const override {
    return primary_device_ptrs_;
  }
  absl::Span<xla::ifrt::Device* const> addressable_devices() const override {
    return addressable_device_ptrs_;
  }
  int process_index() const override { return process_index_; }
  absl::Span<xla::ifrt::Device* const> GetAllDevices() const override;
  absl::StatusOr<DeviceAssignment> GetDefaultDeviceAssignment(
      int num_replicas, int num_partitions) const override;
  absl::StatusOr<xla::ifrt::Device*> LookupDevice(
      DeviceId device_id) const override;
  absl::StatusOr<xla::ifrt::Device*> LookupAddressableDevice(
      int local_hardware_id) const override {
    return absl::UnimplementedError(
        "LookupAddressableDevice is not supported for the IFRT proxy client.");
  }
  absl::StatusOr<xla::ifrt::DeviceListRef> MakeDeviceList(
      absl::Span<xla::ifrt::Device* const> devices) const override;
  xla::ifrt::Compiler* GetDefaultCompiler() override {
    return &default_compiler_;
  }
  absl::StatusOr<std::shared_ptr<xla::ifrt::Topology>> GetTopologyForDevices(
      const xla::ifrt::DeviceListRef& devices) const override {
    return absl::UnimplementedError(
        "GetTopologyForDevices is not supported for the IFRT proxy client.");
  }
  absl::StatusOr<std::shared_ptr<const xla::PjRtLayout>> GetDefaultPjRtLayout(
      xla::ifrt::DType dtype, absl::Span<const int64_t> dims,
      xla::ifrt::Device* device,
      xla::ifrt::MemoryKind memory_kind) const override;

  tsl::RCReference<xla::ifrt::UserContext> CreateUserContext() override {
    return tsl::RCReference<xla::ifrt::UserContext>();
  }

  // For llvm::RTTIExtends.
  static char ID;  // NOLINT

 private:
  struct LayoutKey {
    xla::ifrt::DType dtype;
    std::vector<int64_t> dims;
    xla::ifrt::MemoryKind memory_kind;
    std::string device_summary;

    bool operator==(const LayoutKey& other) const {
      return dtype == other.dtype && dims == other.dims &&
             memory_kind == other.memory_kind &&
             device_summary == other.device_summary;
    }

    template <typename H>
    friend H AbslHashValue(H h, const LayoutKey& key) {
      h = H::combine(std::move(h), key.dtype, key.memory_kind,
                     key.device_summary);
      h = H::combine_contiguous(std::move(h), key.dims.data(), key.dims.size());
      return h;
    }
  };

  Client(std::shared_ptr<RpcHelper> rpc_helper, uint64_t session_id,
         std::string platform_name, std::string platform_version,
         uint64_t platform_id, uint64_t process_index, std::string runtime_type,
         absl::flat_hash_map<int, std::unique_ptr<Device>> devices,
         std::vector<xla::ifrt::Device*> primary_device_ptrs,
         std::vector<xla::ifrt::Device*> addressable_device_ptrs,
         std::vector<xla::ifrt::Device*> all_device_ptrs,
         absl::flat_hash_map<int, std::unique_ptr<Memory>> memories,
         AttributeMap attributes);

  // rpc_helper_ will be referenced by various IFRT objects whose lifetime is
  // managed by the layer above the IFRT interface, so shared_ptr is
  // appropriate.
  const std::shared_ptr<RpcHelper> rpc_helper_;

  const std::string platform_name_;
  const std::string platform_version_;
  const uint64_t platform_id_;
  const uint64_t process_index_;
  const std::string runtime_type_;

  const AttributeMap attributes_;

  const absl::flat_hash_map<int, std::unique_ptr<Device>> devices_;
  const std::vector<xla::ifrt::Device*> primary_device_ptrs_;
  const std::vector<xla::ifrt::Device*> addressable_device_ptrs_;
  const std::vector<xla::ifrt::Device*> all_device_ptrs_;

  const absl::flat_hash_map<int, std::unique_ptr<Memory>> memories_;

  Compiler default_compiler_;

  mutable absl::Mutex mu_;
  mutable absl::flat_hash_map<LayoutKey, std::shared_ptr<const xla::PjRtLayout>>
      layout_cache_ ABSL_GUARDED_BY(mu_);
};

}  // namespace proxy
}  // namespace ifrt
}  // namespace xla

#endif  // XLA_PYTHON_IFRT_PROXY_CLIENT_CLIENT_H_
