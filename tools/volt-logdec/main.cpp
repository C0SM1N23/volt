#include "volt/log/log_file_header.hpp"
#include "volt/log/record_reader.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iterator>
#include <map>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using volt::log::Argument;
using volt::log::DecodedFormat;
using volt::log::RecordReader;

/// Renders one decoded argument the way its type reads best.
[[nodiscard]] std::string render(const Argument &argument) {
  return std::visit(
      [](const auto &value) -> std::string {
        if constexpr (std::same_as<std::decay_t<decltype(value)>, std::string_view>) {
          return std::string{value};
        } else {
          return std::format("{}", value);
        }
      },
      argument);
}

/// Substitutes each `{}` in `format` with the next argument.
///
/// Written out rather than handed to std::format, because the format string
/// comes from a file rather than from source: the number of placeholders may
/// disagree with the number of arguments when a log was written by a different
/// build, and a decoder has to print what it has instead of refusing.
[[nodiscard]] std::string substitute(std::string_view format,
                                     const std::vector<std::string> &arguments) {
  std::string output;
  output.reserve(format.size());

  std::size_t next = 0;
  for (std::size_t index = 0; index < format.size(); ++index) {
    const bool placeholder =
        format[index] == '{' && index + 1 < format.size() && format[index + 1] == '}';
    if (!placeholder) {
      output.push_back(format[index]);
      continue;
    }
    output += next < arguments.size() ? arguments[next] : std::string{"<missing>"};
    next += 1;
    index += 1;
  }
  // Anything the format string had no room for is still shown, so a mismatched
  // build loses nothing.
  for (std::size_t extra = next; extra < arguments.size(); ++extra) {
    output += " " + arguments[extra];
  }
  return output;
}

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

/// Prints one record, returning how many bytes it occupied or zero on damage.
[[nodiscard]] std::size_t print_record(std::span<const std::byte> remaining,
                                       const std::map<std::uint64_t, DecodedFormat> &formats) {
  RecordReader reader{remaining};
  if (!reader.parse_header().has_value()) {
    return 0;
  }

  std::vector<std::string> arguments;
  arguments.reserve(reader.argument_count());
  for (std::size_t index = 0; index < reader.argument_count(); ++index) {
    const volt::core::expected<Argument> argument = reader.next_argument();
    if (!argument.has_value()) {
      return 0;
    }
    arguments.push_back(render(*argument));
  }

  const auto entry = formats.find(reader.format_id());
  const std::string_view format =
      entry == formats.end() ? std::string_view{"<unknown format {}>"} : entry->second.format;

  std::print("{:>16} {:<5} {:<13} {}\n", reader.timestamp_ns(), to_string(reader.level()),
             to_string(reader.module()), substitute(format, arguments));
  return reader.total_bytes();
}

/// Decodes one log file, returning the process exit status.
///
/// Separate from `main` so that `main` itself cannot let an exception escape:
/// the standard library allocates while reading the file and formatting, and a
/// tool should report that rather than terminate on it.
[[nodiscard]] int decode(std::span<char *> args) {
  if (args.size() != 2) {
    std::print("usage: volt-logdec <file.vlog>\n");
    return 2;
  }

  const std::vector<std::byte> content = read_file(std::string{args[1]});
  if (content.empty()) {
    std::print("cannot read {}\n", args[1]);
    return 1;
  }

  std::size_t offset = 0;
  const volt::core::expected<std::vector<DecodedFormat>> table =
      volt::log::parse_log_file_header(content, offset);
  if (!table.has_value()) {
    std::print("{} is not a VOLT log\n", args[1]);
    return 1;
  }

  std::map<std::uint64_t, DecodedFormat> formats;
  for (const DecodedFormat &entry : *table) {
    formats.emplace(entry.id, entry);
  }

  std::size_t decoded = 0;
  while (offset < content.size()) {
    const std::size_t used =
        print_record(std::span<const std::byte>{content}.subspan(offset), formats);
    if (used == 0) {
      // A log is often cut short by whatever stopped the process that wrote
      // it, so the tail being unreadable is expected rather than an error.
      std::print("-- {} bytes at the end could not be read\n", content.size() - offset);
      break;
    }
    offset += used;
    decoded += 1;
  }
  std::print("-- {} records, {} formats\n", decoded, formats.size());
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  // Catching everything is normally forbidden (AGENTS.md 2.10), and this is
  // the one shape where it is not: the top of a tool, where the alternative is
  // std::terminate and no message at all. The exit status is what a caller
  // reads, and the message is written with fputs rather than std::print
  // because a handler of last resort must not be able to fail in turn.
  try {
    return decode(std::span<char *>{argv, static_cast<std::size_t>(argc)});
  } catch (const std::exception &error) {
    static_cast<void>(std::fputs("volt-logdec failed: ", stderr));
    static_cast<void>(std::fputs(error.what(), stderr));
    static_cast<void>(std::fputs("\n", stderr));
    return 1;
  } catch (...) {
    static_cast<void>(std::fputs("volt-logdec failed for an unrecognised reason\n", stderr));
    return 1;
  }
}
