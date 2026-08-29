#include "volt/trace/perfetto_export.hpp"
#include "volt/trace/trace_file.hpp"

#include <cstddef>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iterator>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

[[nodiscard]] std::vector<std::byte> read_file(const std::string &path) {
  std::ifstream input{path, std::ios::binary};
  if (!input) {
    return {};
  }
  const std::vector<char> raw{std::istreambuf_iterator<char>{input},
                              std::istreambuf_iterator<char>{}};
  std::vector<std::byte> content;
  content.reserve(raw.size());
  for (const char character : raw) {
    content.push_back(static_cast<std::byte>(character));
  }
  return content;
}

[[nodiscard]] bool write_file(const std::string &path, std::string_view text) {
  std::ofstream output{path, std::ios::binary};
  if (!output) {
    return false;
  }
  output.write(text.data(), static_cast<std::streamsize>(text.size()));
  return output.good();
}

void print_usage() {
  std::print("usage: volt-trace export --perfetto <capture.vtrace> -o <trace.json>\n");
}

/// Converts a capture into a trace `ui.perfetto.dev` opens.
[[nodiscard]] int run(std::span<char *> args) {
  constexpr std::size_t kExpectedArguments = 6;
  if (args.size() != kExpectedArguments || std::string_view{args[1]} != "export" ||
      std::string_view{args[2]} != "--perfetto" || std::string_view{args[4]} != "-o") {
    print_usage();
    return 2;
  }

  const std::string input_path{args[3]};
  const std::string output_path{args[5]};

  const std::vector<std::byte> content = read_file(input_path);
  if (content.empty()) {
    std::print("cannot read {}\n", input_path);
    return 1;
  }

  const volt::core::expected<volt::trace::StoredCapture> stored =
      volt::trace::read_capture(content);
  if (!stored.has_value()) {
    std::print("{} is not a VOLT capture\n", input_path);
    return 1;
  }

  const std::string json = volt::trace::to_chrome_trace(stored->capture, stored->cycle_clock);
  if (!write_file(output_path, json)) {
    std::print("cannot write {}\n", output_path);
    return 1;
  }

  std::print("{} events, {} threads, {} dropped -> {}\n", stored->capture.records.size(),
             stored->capture.thread_names.size(), stored->capture.dropped, output_path);
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  // Catching everything is normally forbidden (AGENTS.md 2.10), and this is
  // the one shape where it is not: the top of a tool, where the alternative is
  // std::terminate and no message. The message goes out through fputs because
  // a handler of last resort must not be able to fail in turn.
  try {
    return run(std::span<char *>{argv, static_cast<std::size_t>(argc)});
  } catch (const std::exception &error) {
    static_cast<void>(std::fputs("volt-trace failed: ", stderr));
    static_cast<void>(std::fputs(error.what(), stderr));
    static_cast<void>(std::fputs("\n", stderr));
    return 1;
  } catch (...) {
    static_cast<void>(std::fputs("volt-trace failed for an unrecognised reason\n", stderr));
    return 1;
  }
}
