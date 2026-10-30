// Unit tests for gpu/contention/parse.h: the string formats the contention
// sources read from vendor APIs, sysfs / procfs, Windows performance
// counters and the macOS IORegistry. Pure functions, so every format is
// checked on every host.

#include "gpu/contention/parse.h"

#include <gtest/gtest.h>

namespace {

using namespace gpu::contention;

TEST(ContentionParse, CanonicalPciBusIdAcceptsTheVendorSpellings) {
  EXPECT_EQ(CanonicalPciBusId("0000:03:00.0"), "0000:03:00.0");      // sysfs, HIP
  EXPECT_EQ(CanonicalPciBusId("00000000:0A:00.0"), "0000:0a:00.0");  // NVML
  EXPECT_EQ(CanonicalPciBusId("0000:0A:1F.7"), "0000:0a:1f.7");      // CUDA, upper case
  EXPECT_EQ(CanonicalPciBusId("03:00.1"), "0000:03:00.1");           // no domain
  EXPECT_EQ(CanonicalPciBusId("10000:c1:00.0"), "");                 // domain > 16 bits
}

TEST(ContentionParse, CanonicalPciBusIdRejectsOtherNames) {
  EXPECT_EQ(CanonicalPciBusId(""), "");
  EXPECT_EQ(CanonicalPciBusId("fec00000.v3d"), "");  // ARM SoC platform device
  EXPECT_EQ(CanonicalPciBusId("0000:03:00"), "");
  EXPECT_EQ(CanonicalPciBusId("0000:03:00.0 "), "");
  EXPECT_EQ(CanonicalPciBusId("0000:03:20.0"), "");  // device is 5 bits
  EXPECT_EQ(CanonicalPciBusId("0000:03:00.8"), "");  // function is 3 bits
  EXPECT_EQ(CanonicalPciBusId("0:0:0:00.0"), "");
}

TEST(ContentionParse, KfdTopology) {
  const char *properties =
      "cpu_cores_count 0\n"
      "simd_count 192\n"
      "location_id 768\n"
      "domain 1\n";
  EXPECT_EQ(KfdProperty(properties, "location_id"), 768u);
  EXPECT_EQ(KfdProperty(properties, "domain"), 1u);
  EXPECT_EQ(KfdProperty(properties, "count"), std::nullopt) << "must match whole keys";
  EXPECT_EQ(KfdProperty(properties, "gpu_id"), std::nullopt);
  // location_id = bus << 8 | device << 3 | function.
  EXPECT_EQ(KfdPciBusId(1, 768), "0001:03:00.0");
  EXPECT_EQ(KfdPciBusId(0, (0xc1 << 8) | (2 << 3) | 1), "0000:c1:02.1");
}

TEST(ContentionParse, MapsComputeRuntime) {
  EXPECT_TRUE(MapsComputeRuntime(
      "7f0000000000-7f0000001000 r-xp 00000000 08:01 123 /usr/lib/x86_64-linux-gnu/"
      "libze_intel_gpu.so.1.3.29735\n",
      "i915"));
  EXPECT_TRUE(MapsComputeRuntime("... /opt/rocm/lib/libamdhip64.so.6.2.60200\n", "amdgpu"));
  EXPECT_TRUE(MapsComputeRuntime("... /usr/lib/libRusticlOpenCL.so.1\n", "v3d"));
  // Loaders and graphics drivers alone don't count.
  EXPECT_FALSE(
      MapsComputeRuntime("... /usr/lib/libOpenCL.so.1.0.0\n"
                         "... /usr/lib/libze_loader.so.1\n"
                         "... /usr/lib/dri/radeonsi_dri.so\n"
                         "... /usr/lib/libvulkan_intel.so\n",
                         "amdgpu"));
}

TEST(ContentionParse, MapsComputeRuntimeMatchesTheDriver) {
  // nvidia-vaapi-driver maps libcuda into Firefox, which also has the
  // Intel node open on a hybrid laptop: not compute on the Intel GPU.
  EXPECT_FALSE(MapsComputeRuntime("... /usr/lib/libcuda.so.1\n", "i915"));
  EXPECT_FALSE(MapsComputeRuntime("... /usr/lib/intel-opencl/libigdrcl.so\n", "amdgpu"));
  EXPECT_FALSE(MapsComputeRuntime("... /opt/rocm/lib/libhsa-runtime64.so.1\n", "xe"));
  EXPECT_TRUE(MapsComputeRuntime("... /usr/lib/intel-opencl/libigdrcl.so\n", "xe"));
}

TEST(ContentionParse, DrmFdinfoEngineTime) {
  // i915: engine capacities are counts, not times.
  const auto i915 = ParseDrmFdinfo(
      "pos:\t0\nflags:\t02100002\n"
      "drm-driver:\ti915\n"
      "drm-client-id:\t7\n"
      "drm-engine-render:\t9288864723 ns\n"
      "drm-engine-copy:\t2035071108 ns\n"
      "drm-engine-video:\t0 ns\n"
      "drm-engine-capacity-video:\t2\n");
  ASSERT_TRUE(i915.has_value());
  EXPECT_EQ(i915->client_id, 7u);
  EXPECT_TRUE(i915->has_engine_ns);
  EXPECT_EQ(i915->engine_ns, 9288864723u + 2035071108u);
  EXPECT_TRUE(i915->cycles.empty());

  DrmFdinfo later = *i915;
  later.engine_ns += 50'000'000;  // 50 ms busy
  const auto fraction = DrmBusyFraction(*i915, later, 1e9);
  ASSERT_TRUE(fraction.has_value());
  EXPECT_DOUBLE_EQ(*fraction, 0.05);
  EXPECT_EQ(DrmBusyFraction(*i915, later, 0.0), std::nullopt);

  // Not a DRM fd at all.
  EXPECT_EQ(ParseDrmFdinfo("pos:\t0\nflags:\t02\nmnt_id:\t25\n"), std::nullopt);
}

TEST(ContentionParse, DrmFdinfoXeCycles) {
  const auto before = ParseDrmFdinfo(
      "drm-driver:\txe\n"
      "drm-client-id:\t31\n"
      "drm-cycles-rcs:\t1000\n"
      "drm-total-cycles-rcs:\t100000\n"
      "drm-cycles-ccs:\t0\n"
      "drm-total-cycles-ccs:\t100000\n"
      "drm-cycles-bcs:\t5\n");  // no total: left out
  ASSERT_TRUE(before.has_value());
  EXPECT_FALSE(before->has_engine_ns);
  EXPECT_EQ(before->cycles.size(), 2u);
  const auto after = ParseDrmFdinfo(
      "drm-driver:\txe\n"
      "drm-client-id:\t31\n"
      "drm-cycles-rcs:\t1000\n"
      "drm-total-cycles-rcs:\t200000\n"
      "drm-cycles-ccs:\t30000\n"
      "drm-total-cycles-ccs:\t200000\n");
  ASSERT_TRUE(after.has_value());
  const auto fraction = DrmBusyFraction(*before, *after, 1e9);
  ASSERT_TRUE(fraction.has_value());
  EXPECT_DOUBLE_EQ(*fraction, 0.3);  // ccs 30%, rcs idle

  // A driver without usage stats.
  const auto bare = ParseDrmFdinfo("drm-driver:\tnouveau\ndrm-client-id:\t3\n");
  ASSERT_TRUE(bare.has_value());
  EXPECT_EQ(DrmBusyFraction(*bare, *bare, 1e9), std::nullopt);
}

TEST(ContentionParse, GpuEngineInstance) {
  const auto inst =
      ParseGpuEngineInstance("pid_1234_luid_0x00000001_0x0000D1B2_phys_0_eng_3_engtype_Compute_0");
  ASSERT_TRUE(inst.has_value());
  EXPECT_EQ(inst->pid, 1234);
  EXPECT_EQ(inst->luid, (uint64_t{1} << 32) | 0xD1B2);  // HighPart first
  EXPECT_EQ(inst->engine_type, "Compute_0");
  // PowerShell lower-cases instance names.
  const auto lower =
      ParseGpuEngineInstance("pid_7_luid_0x00000000_0x0000d1b2_phys_0_eng_0_engtype_3d");
  ASSERT_TRUE(lower.has_value());
  EXPECT_EQ(lower->luid, 0xD1B2u);
  EXPECT_EQ(ParseGpuEngineInstance("_Total"), std::nullopt);
  EXPECT_EQ(ParseGpuEngineInstance("pid_1_luid_0x0_0x1_phys_0_eng_0"), std::nullopt);
}

TEST(ContentionParse, ComputeEngineTypes) {
  EXPECT_TRUE(IsComputeEngineType("Compute"));
  EXPECT_TRUE(IsComputeEngineType("Compute_1"));
  EXPECT_TRUE(IsComputeEngineType("compute_0"));
  EXPECT_TRUE(IsComputeEngineType("Cuda"));
  EXPECT_FALSE(IsComputeEngineType("3D"));
  EXPECT_FALSE(IsComputeEngineType("Copy"));
  EXPECT_FALSE(IsComputeEngineType("VideoDecode"));
}

TEST(ContentionParse, UserClientCreator) {
  const auto c = ParseUserClientCreator("pid 631, WindowServer");
  ASSERT_TRUE(c.has_value());
  EXPECT_EQ(c->pid, 631);
  EXPECT_EQ(c->name, "WindowServer");
  EXPECT_EQ(ParseUserClientCreator("pid 12,python3.12")->name, "python3.12");
  EXPECT_EQ(ParseUserClientCreator("kernel_task"), std::nullopt);
  EXPECT_EQ(ParseUserClientCreator("pid x, a"), std::nullopt);
}

}  // namespace
