// Unit tests for the contention monitor: the per-device share hysteresis,
// how observations from several sources are merged, and how the other
// processes' share is worked out from the device's busy time and our own.
// Time is injected and the sources are fakes, so nothing depends on a GPU
// or a timer.

#include "gpu/contention/monitor.h"

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "gpu/constants.h"
#include "util/log.h"

#include <gtest/gtest.h>

namespace {

using gpu::DeviceLocator;
using gpu::contention::Monitor;
using gpu::contention::Observation;
using gpu::contention::ShareGate;
using gpu::contention::Source;
using Verdict = Observation::Verdict;

Observation Seen(Verdict verdict, int pid = 0, std::optional<double> device_busy = std::nullopt) {
  Observation o;
  o.verdict = verdict;
  o.pid = pid;
  o.device_busy = device_busy;
  return o;
}

class FakeSource final : public Source {
 public:
  explicit FakeSource(std::vector<Observation> *script) : script_(script) {}
  const char *Name() const override { return "fake"; }
  void Sample(const std::vector<DeviceLocator> &devices, std::vector<Observation> &out) override {
    for (size_t i = 0; i < devices.size() && i < script_->size(); ++i) {
      out[i] = (*script_)[i];
    }
  }

 private:
  std::vector<Observation> *script_;
};

class ContentionMonitorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    util::logger_internal::g_default_logger->SetLevel(util::Logger::kLevelOff);
  }
  void TearDown() override {
    util::logger_internal::g_default_logger->SetLevel(util::Logger::kLevelWarn);
  }
};

const TimePoint kT0 = TimePoint{} + std::chrono::hours(1);

TimePoint At(int ms) { return kT0 + std::chrono::milliseconds(ms); }

TEST_F(ContentionMonitorTest, GateLeavesTheLargestRecentShare) {
  constexpr int kHold = kGpuYieldResumeMS;
  ShareGate gate;
  EXPECT_EQ(gate.Update(std::nullopt, At(0)), std::nullopt);
  // Takes effect at once.
  EXPECT_EQ(gate.Update(40.0, At(1000)), 40.0);
  // A smaller share doesn't lower it until the larger one is kHold old.
  EXPECT_EQ(gate.Update(10.0, At(3000)), 40.0);
  EXPECT_EQ(gate.Update(std::nullopt, At(1000 + kHold - 1)), 40.0);
  EXPECT_EQ(gate.Update(std::nullopt, At(1000 + kHold)), 10.0);
  // A larger one raises it at once.
  EXPECT_EQ(gate.Update(70.0, At(4000)), 70.0);
  // Nobody for kHold after the last sighting: the device is ours again.
  EXPECT_EQ(gate.Update(std::nullopt, At(4000 + kHold - 1)), 70.0);
  EXPECT_EQ(gate.Update(std::nullopt, At(4000 + kHold)), std::nullopt);
}

using Change = std::tuple<size_t, std::optional<int>, int>;

TEST_F(ContentionMonitorTest, AnyBusySourceWinsAndChangesAreReported) {
  std::vector<Observation> nvml(2);
  std::vector<Observation> drm(2);
  std::vector<std::unique_ptr<Source>> sources;
  sources.push_back(std::make_unique<FakeSource>(&nvml));
  sources.push_back(std::make_unique<FakeSource>(&drm));
  std::vector<Change> changes;
  Monitor monitor({"gpu0", "gpu1"}, std::vector<DeviceLocator>(2), std::move(sources),
                  [&](size_t i, const std::string &, std::optional<int> others, int pid) {
                    changes.emplace_back(i, others, pid);
                  });

  nvml = {Seen(Verdict::kIdle), Seen(Verdict::kUnknown)};
  drm = {Seen(Verdict::kUnknown), Seen(Verdict::kIdle)};
  monitor.Poll(At(0));
  EXPECT_TRUE(changes.empty());

  // One source busy is enough, even if another says idle. Unmeasured, the
  // other process counts as taking the whole device.
  drm = {Seen(Verdict::kBusy, 42), Seen(Verdict::kIdle)};
  monitor.Poll(At(2000));
  ASSERT_EQ(changes.size(), 1u);
  EXPECT_EQ(changes[0], Change(0, 100, 42));

  // Still busy: no repeated report.
  monitor.Poll(At(4000));
  EXPECT_EQ(changes.size(), 1u);

  drm = {Seen(Verdict::kIdle), Seen(Verdict::kIdle)};
  monitor.Poll(At(4000 + kGpuYieldResumeMS - 1));
  EXPECT_EQ(changes.size(), 1u);
  monitor.Poll(At(4000 + kGpuYieldResumeMS));
  ASSERT_EQ(changes.size(), 2u);
  EXPECT_EQ(changes[1], Change(0, std::nullopt, 0));
}

TEST_F(ContentionMonitorTest, OthersTakeTheDevicesBusyTimeBeyondOurOwn) {
  std::vector<Observation> first(1);
  std::vector<Observation> second(1);
  std::vector<std::unique_ptr<Source>> sources;
  sources.push_back(std::make_unique<FakeSource>(&first));
  sources.push_back(std::make_unique<FakeSource>(&second));
  double own_ms = 0.0;
  std::vector<Change> changes;
  Monitor monitor(
      {"gpu0"}, std::vector<DeviceLocator>(1), std::move(sources),
      [&](size_t i, const std::string &, std::optional<int> others, int pid) {
        changes.emplace_back(i, others, pid);
      },
      [&](size_t) { return own_ms; });
  monitor.Poll(At(0));
  EXPECT_TRUE(changes.empty());

  // 2 s later the device was 70% busy, 40% of it ours: theirs is 30%. The
  // larger of two measurements counts.
  own_ms = 800.0;
  first = {Seen(Verdict::kBusy, 7, 55.0)};
  second = {Seen(Verdict::kUnknown, 0, 70.0)};
  monitor.Poll(At(2000));
  ASSERT_EQ(changes.size(), 1u);
  EXPECT_EQ(changes[0], Change(0, 30, 7));

  // They grow; we were squeezed to 10%.
  own_ms += 200.0;
  first = {Seen(Verdict::kBusy, 7, 90.0)};
  second = {Seen(Verdict::kUnknown)};
  monitor.Poll(At(4000));
  ASSERT_EQ(changes.size(), 2u);
  EXPECT_EQ(changes[1], Change(0, 80, 7));

  // Measured over a different window than ours, the device can look less
  // busy than we kept it: they take nothing then, not less than nothing.
  own_ms += 0.6 * kGpuYieldResumeMS;
  first = {Seen(Verdict::kBusy, 7, 50.0)};
  monitor.Poll(At(4000 + kGpuYieldResumeMS));
  ASSERT_EQ(changes.size(), 3u);
  EXPECT_EQ(changes[2], Change(0, 0, 7)) << "present, but taking nothing";
}

TEST_F(ContentionMonitorTest, UnknownDevicesNeverShare) {
  std::vector<Change> changes;
  Monitor monitor({"gpu0"}, std::vector<DeviceLocator>(1), {},
                  [&](size_t i, const std::string &, std::optional<int> others, int pid) {
                    changes.emplace_back(i, others, pid);
                  });
  monitor.Poll(At(0));
  monitor.Poll(At(2000));
  EXPECT_TRUE(changes.empty());
}

// Records what it was handed, then reports idle everywhere.
class RecordingSource final : public Source {
 public:
  const char *Name() const override { return "recording"; }
  void Sample(const std::vector<DeviceLocator> &, std::vector<Observation> &out) override {
    handed_ = out;
    for (auto &o : out) {
      if (o.verdict != Verdict::kBusy) {
        o.verdict = Verdict::kIdle;
      }
    }
  }
  std::vector<Observation> handed_;
};

TEST_F(ContentionMonitorTest, LaterSourcesAreHandedWhatIsAlreadyBusyAndMeasured) {
  std::vector<Observation> first = {Seen(Verdict::kBusy, 42, 60.0), Seen(Verdict::kBusy, 43),
                                    Seen(Verdict::kIdle)};
  auto recording = std::make_unique<RecordingSource>();
  RecordingSource *later = recording.get();
  std::vector<std::unique_ptr<Source>> sources;
  sources.push_back(std::make_unique<FakeSource>(&first));
  sources.push_back(std::move(recording));
  Monitor monitor({"gpu0", "gpu1", "gpu2"}, std::vector<DeviceLocator>(3), std::move(sources),
                  nullptr);
  monitor.Poll(At(0));
  ASSERT_EQ(later->handed_.size(), 3u);
  EXPECT_EQ(later->handed_[0].verdict, Verdict::kBusy);
  EXPECT_EQ(later->handed_[0].pid, 42);
  EXPECT_EQ(later->handed_[0].device_busy, 60.0);
  // Busy but unmeasured: a later source may still measure it.
  EXPECT_EQ(later->handed_[1].verdict, Verdict::kUnknown);
  EXPECT_EQ(later->handed_[2].verdict, Verdict::kUnknown);
}

TEST_F(ContentionMonitorTest, ThreadStopsPromptly) {
  std::vector<Observation> idle(1, Seen(Verdict::kIdle));
  std::vector<std::unique_ptr<Source>> sources;
  sources.push_back(std::make_unique<FakeSource>(&idle));
  Monitor monitor({"gpu0"}, std::vector<DeviceLocator>(1), std::move(sources), nullptr);
  monitor.Start();
  // Mid-wait, not before the thread got going.
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  const auto start = std::chrono::steady_clock::now();
  monitor.RequestStop();
  monitor.Join();
  // Woken at once, not at the end of the 2 s wait (or a 100 ms sleep step).
  EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(50));
}

}  // namespace
