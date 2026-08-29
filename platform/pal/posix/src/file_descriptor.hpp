#pragma once

namespace volt::pal::posix::detail {

/// Owns a file descriptor and closes it exactly once.
///
/// Rule of five rather than rule of zero: the descriptor is a resource the
/// compiler cannot manage on its own. Copying is deleted because two owners
/// would close the same descriptor twice, and after a close the number can be
/// handed to an unrelated open, so a double close corrupts another object's
/// descriptor rather than merely failing.
class FileDescriptor final {
public:
  /// Constructs an empty holder.
  FileDescriptor() noexcept = default;

  /// Takes ownership of `descriptor`. A negative value means "none".
  explicit FileDescriptor(int descriptor) noexcept : descriptor_{descriptor} {}

  ~FileDescriptor() noexcept { reset(); }

  FileDescriptor(const FileDescriptor &) = delete;
  FileDescriptor &operator=(const FileDescriptor &) = delete;

  FileDescriptor(FileDescriptor &&other) noexcept : descriptor_{other.release()} {}

  FileDescriptor &operator=(FileDescriptor &&other) noexcept {
    if (this != &other) {
      reset();
      descriptor_ = other.release();
    }
    return *this;
  }

  /// Returns the descriptor without giving up ownership.
  [[nodiscard]] int get() const noexcept { return descriptor_; }

  /// Reports whether a descriptor is held.
  [[nodiscard]] bool valid() const noexcept { return descriptor_ >= 0; }

  /// Gives up ownership and returns the descriptor.
  [[nodiscard]] int release() noexcept {
    const int released = descriptor_;
    descriptor_ = kNoDescriptor;
    return released;
  }

  /// Closes the descriptor if one is held.
  void reset() noexcept;

private:
  // POSIX descriptors are non-negative, so -1 cannot collide with a real one.
  static constexpr int kNoDescriptor = -1;

  int descriptor_ = kNoDescriptor;
};

} // namespace volt::pal::posix::detail
