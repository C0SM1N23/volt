#include "volt/ipc/publisher.hpp"
#include "volt/ipc/subscriber.hpp"
#include "volt/ipc/topic.hpp"

#include "volt/pal/posix/posix_platform.hpp"
#include "volt/pal/sim/sim_platform.hpp"

#include <gtest/gtest.h>

#include <format>
#include <memory>
#include <string>
#include <utility>

namespace volt::ipc {
namespace {

/// The payload from SPEC 10.2, byte for byte.
struct WheelSpeeds {
  float fl = 0.0F;
  float fr = 0.0F;
  float rl = 0.0F;
  float rr = 0.0F;
};

/// A different shape, for the mismatch test.
struct Imu {
  double yaw_rate = 0.0;
  double pitch_rate = 0.0;
  double roll_rate = 0.0;
};

constexpr TopicConfig kSmallTopic{.slot_count = 8, .history_depth = 4, .max_subscribers = 2};

struct PosixCase {
  static std::unique_ptr<pal::IPlatform> platform() {
    return std::make_unique<pal::posix::PosixPlatform>();
  }
};

struct SimCase {
  static std::unique_ptr<pal::IPlatform> platform() {
    return std::make_unique<pal::sim::SimPlatform>(pal::sim::SimConfig{.seed = 0x1BC0DE5EULL});
  }
};

template <typename Case> class TopicApiTest : public ::testing::Test {
protected:
  void SetUp() override { platform_ = Case::platform(); }

  /// Names carry the process id so parallel test runs on one machine cannot
  /// collide inside the kernel's shared-memory namespace.
  [[nodiscard]] std::string topic_name(std::string_view stem) {
    return std::format("volt-ipc-{}-{}", platform_->current_process_id(), stem);
  }

  std::unique_ptr<pal::IPlatform> platform_;
};

using Cases = ::testing::Types<PosixCase, SimCase>;
TYPED_TEST_SUITE(TopicApiTest, Cases);

TYPED_TEST(TopicApiTest, RoundTripsATypedMessage) {
  core::expected<Topic<WheelSpeeds>> topic =
      Topic<WheelSpeeds>::create(*this->platform_, this->topic_name("round"), kSmallTopic);
  ASSERT_TRUE(topic.has_value());
  core::expected<Publisher<WheelSpeeds>> publisher = topic->publisher();
  core::expected<Subscriber<WheelSpeeds>> subscriber = topic->subscriber();
  ASSERT_TRUE(publisher.has_value());
  ASSERT_TRUE(subscriber.has_value());

  core::expected<Loan<WheelSpeeds>> loan = publisher->loan();
  ASSERT_TRUE(loan.has_value());
  (*loan)->fl = 12.4F;
  (*loan)->fr = 12.5F;
  EXPECT_EQ(publisher->publish(std::move(*loan)), 1U);

  core::expected<Sample<WheelSpeeds>> sample = subscriber->take();
  ASSERT_TRUE(sample.has_value());
  EXPECT_FLOAT_EQ((*sample)->fl, 12.4F);
  EXPECT_FLOAT_EQ((*sample)->fr, 12.5F);
}

TYPED_TEST(TopicApiTest, SecondMappingReadsWhatTheFirstWrote) {
  const std::string name = this->topic_name("two-mappings");
  core::expected<Topic<WheelSpeeds>> created =
      Topic<WheelSpeeds>::create(*this->platform_, name, kSmallTopic);
  ASSERT_TRUE(created.has_value());
  // A second, independent mapping of the same segment: on the POSIX backend
  // this lands at a different address, which is exactly what the index-based
  // layout must survive.
  core::expected<Topic<WheelSpeeds>> opened = Topic<WheelSpeeds>::open(*this->platform_, name);
  ASSERT_TRUE(opened.has_value());

  core::expected<Publisher<WheelSpeeds>> publisher = created->publisher();
  core::expected<Subscriber<WheelSpeeds>> subscriber = opened->subscriber();
  ASSERT_TRUE(publisher.has_value());
  ASSERT_TRUE(subscriber.has_value());

  core::expected<Loan<WheelSpeeds>> loan = publisher->loan();
  ASSERT_TRUE(loan.has_value());
  (*loan)->rl = 3.5F;
  publisher->publish(std::move(*loan));

  core::expected<Sample<WheelSpeeds>> sample = subscriber->take();
  ASSERT_TRUE(sample.has_value());
  EXPECT_FLOAT_EQ((*sample)->rl, 3.5F);
}

TYPED_TEST(TopicApiTest, OpenRefusesTheWrongPayloadType) {
  const std::string name = this->topic_name("mismatch");
  core::expected<Topic<WheelSpeeds>> created =
      Topic<WheelSpeeds>::create(*this->platform_, name, kSmallTopic);
  ASSERT_TRUE(created.has_value());

  const core::expected<Topic<Imu>> wrong = Topic<Imu>::open(*this->platform_, name);
  ASSERT_FALSE(wrong.has_value());
  EXPECT_EQ(wrong.error(), core::ErrorCode::kConfigInvalidValue);
}

TYPED_TEST(TopicApiTest, OpeningAMissingTopicReportsAnError) {
  const core::expected<Topic<WheelSpeeds>> opened =
      Topic<WheelSpeeds>::open(*this->platform_, this->topic_name("never-created"));
  ASSERT_FALSE(opened.has_value());
  EXPECT_EQ(opened.error(), core::ErrorCode::kResourceUnavailable);
}

TYPED_TEST(TopicApiTest, AnAbandonedLoanReturnsItsSlot) {
  core::expected<Topic<WheelSpeeds>> topic =
      Topic<WheelSpeeds>::create(*this->platform_, this->topic_name("abandon"), kSmallTopic);
  ASSERT_TRUE(topic.has_value());
  core::expected<Publisher<WheelSpeeds>> publisher = topic->publisher();
  ASSERT_TRUE(publisher.has_value());

  {
    core::expected<Loan<WheelSpeeds>> loan = publisher->loan();
    ASSERT_TRUE(loan.has_value());
    EXPECT_EQ(topic->available_slots(), kSmallTopic.slot_count - 1U);
    // The loan dies here, unpublished: an error path must not leak capacity.
  }
  EXPECT_EQ(topic->available_slots(), kSmallTopic.slot_count);
}

TYPED_TEST(TopicApiTest, SeatsComeBackWhenEndpointsDie) {
  core::expected<Topic<WheelSpeeds>> topic =
      Topic<WheelSpeeds>::create(*this->platform_, this->topic_name("seats"), kSmallTopic);
  ASSERT_TRUE(topic.has_value());

  {
    core::expected<Publisher<WheelSpeeds>> publisher = topic->publisher();
    ASSERT_TRUE(publisher.has_value());
    EXPECT_FALSE(topic->publisher().has_value()) << "the seat is exclusive while held";
  }
  EXPECT_TRUE(topic->publisher().has_value()) << "the destructor freed the seat";

  {
    core::expected<Subscriber<WheelSpeeds>> first = topic->subscriber();
    core::expected<Subscriber<WheelSpeeds>> second = topic->subscriber();
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(topic->subscriber_count(), 2U);
  }
  EXPECT_EQ(topic->subscriber_count(), 0U);
}

TYPED_TEST(TopicApiTest, LaggardLosesOldestAndKnowsIt) {
  core::expected<Topic<WheelSpeeds>> topic =
      Topic<WheelSpeeds>::create(*this->platform_, this->topic_name("laggard"), kSmallTopic);
  ASSERT_TRUE(topic.has_value());
  core::expected<Publisher<WheelSpeeds>> publisher = topic->publisher();
  core::expected<Subscriber<WheelSpeeds>> subscriber = topic->subscriber();
  ASSERT_TRUE(publisher.has_value());
  ASSERT_TRUE(subscriber.has_value());

  for (int message = 0; message < 6; ++message) {
    core::expected<Loan<WheelSpeeds>> loan = publisher->loan();
    ASSERT_TRUE(loan.has_value());
    (*loan)->fl = static_cast<float>(message);
    publisher->publish(std::move(*loan));
  }

  // history_depth is 4 and six were published while the consumer slept:
  // the two oldest are gone, counted, and the newest four arrive in order.
  EXPECT_EQ(subscriber->dropped(), 2U);
  EXPECT_EQ(subscriber->pending(), 4U);
  for (int message = 2; message < 6; ++message) {
    core::expected<Sample<WheelSpeeds>> sample = subscriber->take();
    ASSERT_TRUE(sample.has_value());
    EXPECT_FLOAT_EQ((*sample)->fl, static_cast<float>(message));
  }
}

TYPED_TEST(TopicApiTest, EndpointsSurviveBeingMoved) {
  core::expected<Topic<WheelSpeeds>> topic =
      Topic<WheelSpeeds>::create(*this->platform_, this->topic_name("moves"), kSmallTopic);
  ASSERT_TRUE(topic.has_value());
  core::expected<Publisher<WheelSpeeds>> publisher = topic->publisher();
  core::expected<Subscriber<WheelSpeeds>> subscriber = topic->subscriber();
  ASSERT_TRUE(publisher.has_value());
  ASSERT_TRUE(subscriber.has_value());

  Publisher<WheelSpeeds> moved_publisher{std::move(*publisher)};
  Subscriber<WheelSpeeds> moved_subscriber{std::move(*subscriber)};

  core::expected<Loan<WheelSpeeds>> loan = moved_publisher.loan();
  ASSERT_TRUE(loan.has_value());
  Loan<WheelSpeeds> moved_loan{std::move(*loan)};
  moved_loan->rr = 9.0F;
  moved_publisher.publish(std::move(moved_loan));

  core::expected<Sample<WheelSpeeds>> sample = moved_subscriber.take();
  ASSERT_TRUE(sample.has_value());
  Sample<WheelSpeeds> moved_sample{std::move(*sample)};
  EXPECT_FLOAT_EQ(moved_sample->rr, 9.0F);
}

} // namespace
} // namespace volt::ipc
