// Unit tests for the Linux contention sources (amdkfd and DRM fd scan)
// against a fake /proc + /sys tree in a temporary directory: symlinks
// stand in for the fd table and the sysfs device links, plain files for
// the queue / topology / maps contents.
//
// Linux-only: the sources are only built there. The target is registered
// everywhere and compiles to zero tests elsewhere.

#include "core/platform.h"

#if IS_LINUX

#  include <unistd.h>

#  include <filesystem>
#  include <fstream>
#  include <string>
#  include <vector>

#  include "gpu/contention/drm_source.h"
#  include "gpu/contention/kfd_source.h"
#  include "util/log.h"

#  include <gtest/gtest.h>

namespace {

namespace fs = std::filesystem;
using gpu::DeviceLocator;
using gpu::contention::DrmSource;
using gpu::contention::KfdSource;
using gpu::contention::Observation;
using Verdict = Observation::Verdict;

constexpr int kSelf = 4242;

class ContentionLinuxTest : public ::testing::Test {
 protected:
  void SetUp() override {
    util::logger_internal::g_default_logger->SetLevel(util::Logger::kLevelOff);
    root_ = fs::temp_directory_path() /
            ("loadmaster_contention_" + std::to_string(::getpid()) + "_" +
             ::testing::UnitTest::GetInstance()->current_test_info()->name());
    fs::remove_all(root_);
    fs::create_directories(root_);
  }
  void TearDown() override {
    fs::remove_all(root_);
    util::logger_internal::g_default_logger->SetLevel(util::Logger::kLevelWarn);
  }

  void File(const std::string &rel, const std::string &content) {
    const fs::path p = root_ / rel;
    fs::create_directories(p.parent_path());
    std::ofstream(p) << content;
  }
  void Link(const std::string &rel, const std::string &target) {
    const fs::path p = root_ / rel;
    fs::create_directories(p.parent_path());
    fs::create_symlink(target, p);
  }
  // A DRM node of the PCI device `pci` bound to `driver`.
  void DrmNode(const std::string &node, const std::string &pci, const std::string &driver) {
    const std::string dev = "sys/devices/pci0000:00/" + pci;
    if (!fs::exists(root_ / dev)) {
      fs::create_directories(root_ / dev);
      Link(dev + "/driver", "../../../bus/pci/drivers/" + driver);
    }
    Link("sys/class/drm/" + node + "/device", "../../../devices/pci0000:00/" + pci);
  }
  void KfdNode(int node, uint32_t gpu_id, const std::string &properties) {
    const std::string dir = "sys/class/kfd/kfd/topology/nodes/" + std::to_string(node);
    File(dir + "/gpu_id", std::to_string(gpu_id) + "\n");
    File(dir + "/properties", properties);
  }
  void KfdQueue(int pid, int queue, uint32_t gpu_id) {
    File("sys/class/kfd/kfd/proc/" + std::to_string(pid) + "/queues/" + std::to_string(queue) +
             "/gpuid",
         std::to_string(gpu_id));
  }

  static DeviceLocator At(const std::string &pci, bool kfd_client = false) {
    DeviceLocator loc;
    loc.pci_bus_id = pci;
    loc.kfd_client = kfd_client;
    return loc;
  }

  fs::path root_;
};

TEST_F(ContentionLinuxTest, DrmCountsProcessesWithAComputeRuntimeOnTheNode) {
  DrmNode("renderD128", "0000:03:00.0", "i915");
  DrmNode("card1", "0000:03:00.0", "i915");
  DrmNode("card1-HDMI-A-1", "0000:03:00.0", "i915");  // connector, ignored
  DrmNode("renderD129", "0000:04:00.0", "nvidia");    // left to NVML
  // A compositor: has the node open, no compute runtime.
  Link("proc/100/fd/7", "/dev/dri/card1");
  File("proc/100/maps", "... /usr/lib/dri/iris_dri.so\n");
  // Ourselves.
  Link("proc/" + std::to_string(kSelf) + "/fd/3", "/dev/dri/renderD128");
  File("proc/" + std::to_string(kSelf) + "/maps", "... /usr/lib/libze_intel_gpu.so.1\n");

  DrmSource source(root_.string(), kSelf);
  const std::vector<DeviceLocator> devices = {At("0000:03:00.0"), At("0000:04:00.0"), At("")};
  std::vector<Observation> out(devices.size());
  source.Sample(devices, out);
  EXPECT_EQ(out[0].verdict, Verdict::kIdle);
  EXPECT_EQ(out[1].verdict, Verdict::kUnknown) << "NVIDIA nodes are NVML's";
  EXPECT_EQ(out[2].verdict, Verdict::kUnknown);

  // An OpenCL job starts on the iGPU.
  Link("proc/200/fd/0", "/dev/null");
  Link("proc/200/fd/9", "/dev/dri/renderD128");
  File("proc/200/maps", "... /usr/lib/x86_64-linux-gnu/intel-opencl/libigdrcl.so\n");
  // A kernel without DRM usage stats: the runtime alone decides.
  File("proc/200/fdinfo/9", "pos:\t0\nflags:\t02100002\nmnt_id:\t25\n");
  out.assign(devices.size(), Observation{});
  source.Sample(devices, out);
  EXPECT_EQ(out[0].verdict, Verdict::kBusy);
  EXPECT_EQ(out[0].pid, 200);
}

// /proc/<pid>/fdinfo/<fd> of an i915 client `busy_ns` into its life.
std::string I915Fdinfo(int client_id, uint64_t busy_ns) {
  return "pos:\t0\nflags:\t02100002\ndrm-driver:\ti915\ndrm-client-id:\t" +
         std::to_string(client_id) + "\ndrm-engine-render:\t" + std::to_string(busy_ns) +
         " ns\ndrm-engine-capacity-video:\t2\n";
}

TEST_F(ContentionLinuxTest, DrmWantsEngineTimeWhereTheDriverReportsIt) {
  DrmNode("renderD128", "0000:03:00.0", "i915");
  // LibreOffice enumerated OpenCL platforms (every ICD got mapped) but
  // never runs anything on the GPU.
  Link("proc/300/fd/5", "/dev/dri/renderD128");
  File("proc/300/maps", "... /usr/lib/intel-opencl/libigdrcl.so\n");
  File("proc/300/fdinfo/5", I915Fdinfo(11, 1'000'000));
  // A real OpenCL job.
  Link("proc/400/fd/6", "/dev/dri/renderD128");
  Link("proc/400/fd/7", "/dev/dri/renderD128");  // dup() of fd 6: same client
  File("proc/400/maps", "... /usr/lib/intel-opencl/libigdrcl.so\n");
  File("proc/400/fdinfo/6", I915Fdinfo(12, 0));
  File("proc/400/fdinfo/7", I915Fdinfo(12, 0));

  DrmSource source(root_.string(), kSelf);
  const std::vector<DeviceLocator> devices = {At("0000:03:00.0")};
  std::vector<Observation> out(devices.size());
  // First sighting: both measured over a short window, both idle in it.
  source.Sample(devices, out);
  EXPECT_EQ(out[0].verdict, Verdict::kIdle);

  // The job runs; the other one stays put.
  File("proc/400/fdinfo/6", I915Fdinfo(12, 60'000'000'000));
  File("proc/400/fdinfo/7", I915Fdinfo(12, 60'000'000'000));
  out.assign(devices.size(), Observation{});
  source.Sample(devices, out);
  EXPECT_EQ(out[0].verdict, Verdict::kBusy);
  EXPECT_EQ(out[0].pid, 400);

  // Finished, process still around.
  out.assign(devices.size(), Observation{});
  source.Sample(devices, out);
  EXPECT_EQ(out[0].verdict, Verdict::kIdle);
}

TEST_F(ContentionLinuxTest, DrmIgnoresOtherVendorsRuntimes) {
  DrmNode("renderD128", "0000:00:02.0", "i915");
  // Firefox on a hybrid laptop: Intel node for rendering, libcuda mapped
  // by nvidia-vaapi-driver. No fdinfo: it must not get that far.
  Link("proc/500/fd/40", "/dev/dri/renderD128");
  File("proc/500/maps", "... /usr/lib/libcuda.so.1\n");
  DrmSource source(root_.string(), kSelf);
  std::vector<Observation> out(1);
  source.Sample({At("0000:00:02.0")}, out);
  EXPECT_EQ(out[0].verdict, Verdict::kIdle);
}

TEST_F(ContentionLinuxTest, DrmSkipsDevicesAlreadyBusy) {
  DrmNode("renderD128", "0000:03:00.0", "amdgpu");
  Link("proc/600/fd/3", "/dev/dri/renderD128");
  File("proc/600/maps", "... /usr/lib/libRusticlOpenCL.so.1\n");
  DrmSource source(root_.string(), kSelf);
  // amdkfd already saw pid 77 on it.
  std::vector<Observation> out = {{Verdict::kBusy, 77}};
  source.Sample({At("0000:03:00.0")}, out);
  EXPECT_EQ(out[0].verdict, Verdict::kBusy);
  EXPECT_EQ(out[0].pid, 77);
}

TEST_F(ContentionLinuxTest, KfdCountsQueuesPerGpu) {
  KfdNode(0, 0, "cpu_cores_count 16\n");
  KfdNode(1, 1111, "simd_count 120\nlocation_id 768\ndomain 0\n");   // 0000:03:00.0
  KfdNode(2, 2222, "simd_count 120\nlocation_id 1024\ndomain 0\n");  // 0000:04:00.0
  ASSERT_TRUE(KfdSource::Available(root_.string()));
  KfdQueue(kSelf, 0, 1111);
  KfdQueue(kSelf, 1, 2222);
  KfdQueue(300, 3, 2222);
  KfdQueue(300, 4, 2222);

  KfdSource source(root_.string(), kSelf);
  const std::vector<DeviceLocator> devices = {At("0000:03:00.0", true), At("0000:04:00.0", true),
                                              At("0000:05:00.0", true)};
  std::vector<Observation> out(devices.size());
  source.Sample(devices, out);
  EXPECT_EQ(out[0].verdict, Verdict::kIdle) << "only our own queue";
  EXPECT_EQ(out[1].verdict, Verdict::kBusy);
  EXPECT_EQ(out[1].pid, 300);
  EXPECT_EQ(out[2].verdict, Verdict::kUnknown) << "not an amdkfd GPU";
}

TEST_F(ContentionLinuxTest, KfdPartitionedGpuMatchesAnyOfItsNodes) {
  // Two partitions of one GPU: node ids OR'd into location_id's low bits.
  KfdNode(1, 1111, "location_id 768\ndomain 0\n");
  KfdNode(2, 2222, "location_id 769\ndomain 0\n");
  KfdQueue(kSelf, 0, 1111);
  KfdQueue(300, 1, 2222);
  KfdSource source(root_.string(), kSelf);
  std::vector<Observation> out(1);
  source.Sample({At("0000:03:00.0", true)}, out);
  EXPECT_EQ(out[0].verdict, Verdict::kBusy);
}

TEST_F(ContentionLinuxTest, KfdDiscountsItselfInsideAPidNamespace) {
  KfdNode(1, 1111, "location_id 768\ndomain 0\n");
  // amdkfd lists us under our host pid, which doesn't match getpid().
  KfdQueue(90001, 0, 1111);
  KfdSource source(root_.string(), kSelf);
  std::vector<Observation> out(1);
  source.Sample({At("0000:03:00.0", true)}, out);
  EXPECT_EQ(out[0].verdict, Verdict::kIdle);
  // A second process is still seen.
  KfdQueue(90002, 0, 1111);
  out.assign(1, Observation{});
  source.Sample({At("0000:03:00.0", true)}, out);
  EXPECT_EQ(out[0].verdict, Verdict::kBusy);
  // Not a kfd client ourselves (e.g. Mesa OpenCL): nothing to discount.
  out.assign(1, Observation{});
  source.Sample({At("0000:03:00.0", false)}, out);
  EXPECT_EQ(out[0].verdict, Verdict::kBusy);
}

}  // namespace

#endif  // IS_LINUX
