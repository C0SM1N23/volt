#include "volt/ipc/control_channel.hpp"

#include "volt/pal/posix/posix_platform.hpp"
#include "volt/pal/sim/sim_platform.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <format>
#include <memory>
#include <string>
#include <vector>

namespace volt::ipc {
namespace {

struct PosixCase {
  static std::unique_ptr<pal::IPlatform> platform() {
    return std::make_unique<pal::posix::PosixPlatform>();
  }
};

struct SimCase {
  static std::unique_ptr<pal::IPlatform> platform() {
    return std::make_unique<pal::sim::SimPlatform>(pal::sim::SimConfig{.seed = 0xC0117701ULL});
  }
};

template <typename Case> class ControlChannelTest : public ::testing::Test {
protected:
  void SetUp() override { platform_ = Case::platform(); }

  [[nodiscard]] std::string channel_path(std::string_view stem) {
    return std::format("/tmp/volt-ctl-{}-{}.sock", platform_->current_process_id(), stem);
  }

  [[nodiscard]] std::span<const std::byte> as_bytes(std::string_view text) {
    return std::as_bytes(std::span{text.data(), text.size()});
  }

  std::unique_ptr<pal::IPlatform> platform_;
};

using Cases = ::testing::Types<PosixCase, SimCase>;
TYPED_TEST_SUITE(ControlChannelTest, Cases);

TYPED_TEST(ControlChannelTest, FramesSurviveTheStream) {
  core::expected<ControlServer> server =
      ControlServer::listen(*this->platform_, this->channel_path("frames"));
  ASSERT_TRUE(server.has_value());
  core::expected<ControlConnection> client =
      connect_control(*this->platform_, this->channel_path("frames"));
  ASSERT_TRUE(client.has_value());
  core::expected<ControlConnection> serving = server->accept();
  ASSERT_TRUE(serving.has_value());

  // Two frames sent back to back must come out as two frames, not one blob:
  // the length prefix is what restores the boundaries a stream erases.
  ASSERT_TRUE(client->send(this->as_bytes("set-level")).has_value());
  ASSERT_TRUE(client->send(this->as_bytes("debug")).has_value());

  std::array<std::byte, 64> frame{};
  const core::expected<std::size_t> first = serving->receive(frame);
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(*first, 9U);
  const core::expected<std::size_t> second = serving->receive(frame);
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(*second, 5U);
  EXPECT_EQ(std::memcmp(frame.data(), "debug", 5U), 0);

  // And the answer travels the other way.
  ASSERT_TRUE(serving->send(this->as_bytes("ok")).has_value());
  const core::expected<std::size_t> answer = client->receive(frame);
  ASSERT_TRUE(answer.has_value());
  EXPECT_EQ(*answer, 2U);
}

TYPED_TEST(ControlChannelTest, ServerIdentityIsThisProcess) {
  core::expected<ControlServer> server =
      ControlServer::listen(*this->platform_, this->channel_path("identity"));
  ASSERT_TRUE(server.has_value());
  EXPECT_EQ(server->identity().process_id, this->platform_->current_process_id());
}

TYPED_TEST(ControlChannelTest, AcceptedPeerCarriesKernelCredentials) {
  core::expected<ControlServer> server =
      ControlServer::listen(*this->platform_, this->channel_path("creds"));
  ASSERT_TRUE(server.has_value());
  core::expected<ControlConnection> client =
      connect_control(*this->platform_, this->channel_path("creds"));
  ASSERT_TRUE(client.has_value());
  core::expected<ControlConnection> serving = server->accept();
  ASSERT_TRUE(serving.has_value());

  EXPECT_EQ(serving->peer().process_id, this->platform_->current_process_id());
  EXPECT_EQ(serving->peer().user_id, server->identity().user_id);
}

TYPED_TEST(ControlChannelTest, RefusesAFrameBeyondTheProtocolBound) {
  core::expected<ControlServer> server =
      ControlServer::listen(*this->platform_, this->channel_path("oversized"));
  ASSERT_TRUE(server.has_value());
  core::expected<ControlConnection> client =
      connect_control(*this->platform_, this->channel_path("oversized"));
  ASSERT_TRUE(client.has_value());

  const std::vector<std::byte> huge(kMaxControlFrameBytes + 1U);
  const core::expected<void> sent = client->send(huge);
  ASSERT_FALSE(sent.has_value());
  EXPECT_EQ(sent.error(), core::ErrorCode::kInternalBufferTooSmall);
}

TYPED_TEST(ControlChannelTest, RefusesAFrameLargerThanTheCallersBuffer) {
  core::expected<ControlServer> server =
      ControlServer::listen(*this->platform_, this->channel_path("small-buffer"));
  ASSERT_TRUE(server.has_value());
  core::expected<ControlConnection> client =
      connect_control(*this->platform_, this->channel_path("small-buffer"));
  ASSERT_TRUE(client.has_value());
  core::expected<ControlConnection> serving = server->accept();
  ASSERT_TRUE(serving.has_value());

  ASSERT_TRUE(client->send(this->as_bytes("twelve bytes")).has_value());
  std::array<std::byte, 4> tiny{};
  const core::expected<std::size_t> received = serving->receive(tiny);
  ASSERT_FALSE(received.has_value());
  EXPECT_EQ(received.error(), core::ErrorCode::kInternalBufferTooSmall);
}

TYPED_TEST(ControlChannelTest, PeerClosingBetweenFramesEndsTheConversation) {
  core::expected<ControlServer> server =
      ControlServer::listen(*this->platform_, this->channel_path("closing"));
  ASSERT_TRUE(server.has_value());
  core::expected<ControlConnection> serving = [&] {
    core::expected<ControlConnection> client =
        connect_control(*this->platform_, this->channel_path("closing"));
    EXPECT_TRUE(client.has_value());
    core::expected<ControlConnection> accepted = server->accept();
    // The client dies here, cleanly, between frames.
    return accepted;
  }();
  ASSERT_TRUE(serving.has_value());

  std::array<std::byte, 16> frame{};
  const core::expected<std::size_t> received = serving->receive(frame);
  ASSERT_FALSE(received.has_value());
  EXPECT_EQ(received.error(), core::ErrorCode::kExternalNotConnected);
}

} // namespace
} // namespace volt::ipc
