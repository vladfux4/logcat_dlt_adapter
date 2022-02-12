#include <dlt.h>

#include <iostream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <vector>

class ContextIdEncoder {
 public:
  std::string getContextId(const std::string& name) {
    std::string input = filterName(name);
    if (input.empty()) {
      input = "Z";
    }

    std::transform(input.begin(), input.end(), input.begin(),
                   [](unsigned char c) { return std::toupper(c); });

    auto ctx_id = encodeContextId(input);
    registeredContextIds.emplace(ctx_id);

    return ctx_id;
  }

 private:
  bool isUnique(std::string ctx_id) {
    return (registeredContextIds.find(ctx_id) == registeredContextIds.end());
  }

  std::vector<int> intToOtherBase(std::size_t value, std::size_t base) {
    std::vector<int> extraCode;

    std::size_t counter = value;

    if (counter == 0) {
      extraCode.push_back(0);
    }

    while (counter != 0) {
      std::size_t mod = counter % base;
      std::size_t div = counter / base;

      if (div != 0) {
        extraCode.push_back(mod);

        counter = div;
      } else {
        extraCode.push_back(mod);
        counter = 0;
      }
    }

    std::reverse(extraCode.begin(), extraCode.end());
    return extraCode;
  }

  std::string intToLetter(std::size_t value) {
    const auto begin = static_cast<int>('A');
    const auto end = static_cast<int>('Z');
    const auto base = end - begin + 1;

    std::stringstream stream;
    auto ints = intToOtherBase(value, base);
    for (const auto value : ints) {
      stream << static_cast<char>(begin + value);
    }

    return stream.str();
  }

  std::string filterName(const std::string& line) {
    std::stringstream result;

    for (std::size_t i = 0; i < line.size(); ++i) {
      if ((line[i] >= 'a' && line[i] <= 'z') ||
          (line[i] >= 'A' && line[i] <= 'Z') ||
          (line[i] >= '0' && line[i] <= '9')) {
        result << line[i];
      }
    }

    return result.str();
  }

  std::string checkAndEncode(const std::string& name,
                             const std::size_t counter) {
    if (isUnique(name)) {
      return name;
    } else {
      return encodeContextId(name, counter + 1);
    }
  }

  std::string modifyName(const std::string& name, const std::size_t counter) {
    auto extra_code = intToLetter(counter);

    std::string new_name;
    if ((name.size() + extra_code.size()) <= kLength) {
      new_name = name + extra_code;
    } else {
      if (extra_code.size() > kLength) {
        throw std::runtime_error("Out of CTX ID range");
      }

      int name_new_length = name.size() - extra_code.size();
      if (name_new_length < 0) {
        throw std::runtime_error("CTX name length < 0");
      }

      new_name = std::string(name.begin(), name.begin() + name_new_length) +
                 extra_code;
    }

    return checkAndEncode(new_name, counter);
  }

  std::string encodeContextId(const std::string& name,
                              const std::size_t counter = 0) {
    if (counter != 0) {
      return modifyName(name, counter);
    }

    std::string new_name;
    if (name.size() <= kLength) {
      new_name = name;
    } else {
      const int interval = name.size() / kLength;
      for (std::size_t i = 0; i < kLength; i++) {
        new_name.push_back(name[i * interval]);
      }
    }

    return checkAndEncode(new_name, getCounter(new_name));
  }

  std::size_t getCounter(const std::string& name) {
    std::size_t str_hash = std::hash<std::string>{}(name);
    return (str_hash % 9) + 1;
  }

  std::set<std::string> registeredContextIds;
  const std::size_t kLength = 4;
};

class Context {
 public:
  Context(const std::string& name, const std::string& context_id) {
    DLT_REGISTER_CONTEXT(ctx_, context_id.c_str(), name.c_str());
  }

  ~Context() { DLT_UNREGISTER_CONTEXT(ctx_); }

  void log(DltLogLevelType logLevel, const std::string& input) {
    DLT_LOG(ctx_, logLevel, DLT_STRING(input.c_str()));
  }

 private:
  DltContext ctx_;
};

std::unique_ptr<Context> g_ctx;

struct Metadata {
  const std::size_t kKeyMinCount = 6;

  const std::size_t kTimestampKey = 1;
  const std::size_t kContextKey = 4;

  const std::size_t kContextMinLength = 3;

  const std::string openToken = "[";
  const std::string closeToken = "]";

  struct LogContext {
    DltLogLevelType logLevel;
    std::string name;
  };

  std::vector<std::string> values;

  Metadata() { values.reserve(kKeyMinCount); }

  bool isValid() {
    return (values.size() >= kKeyMinCount) && (values.front() == openToken) &&
           (values.back() == closeToken) &&
           (values.at(kContextKey).size() >= kContextMinLength);
  }

  float getTimestamp() {
    try {
      return std::stof(values[kTimestampKey]);
    } catch (...) {
      return -1;
    }
  }

  LogContext getLogContext() {
    LogContext result = {DLT_LOG_FATAL, "Broken Context"};

    std::string raw_msg = values[kContextKey];
    std::string raw_level = std::string(raw_msg.begin(), (raw_msg.begin() + 1));
    auto logLiteral = logLevelLiterals.find(raw_level);
    if (logLiteral == logLevelLiterals.end()) {
      return result;
    }

    std::string name = std::string(raw_msg.begin() + 2, raw_msg.end());
    if (values.size() > kKeyMinCount) {
      for (auto it = (values.begin() + kContextKey + 1);
           it < (values.end() - 1); it++) {
        name += *it;
      }
    }

    result.logLevel = logLiteral->second;
    result.name = name;

    return result;
  }

 private:
  std::map<std::string, DltLogLevelType> logLevelLiterals = {
      {"F", DLT_LOG_FATAL}, {"E", DLT_LOG_ERROR}, {"W", DLT_LOG_WARN},
      {"I", DLT_LOG_INFO},  {"D", DLT_LOG_DEBUG}, {"V", DLT_LOG_VERBOSE}};
};

class ParsingContext {
 public:
  ParsingContext() {}

  enum class Step { METADATA, MESSAGE };

  void reset() {
    step_ = Step::METADATA;
    metadata_.reset();
    message_.reset();
  }

  void step() { step_ = Step::MESSAGE; }

  void setMetadata(const Metadata& metadata) { metadata_.emplace(metadata); }

  void setMessage(const std::string& message) { message_.emplace(message); }

  bool isCompleted() { return (metadata_ && message_); }

  Step currentStep() { return step_; }

  std::string message() { return *message_; }
  Metadata metadata() { return *metadata_; }

 private:
  Step step_ = Step::METADATA;
  std::optional<Metadata> metadata_;
  std::optional<std::string> message_;
};

std::optional<Metadata> parseMetadata(const std::string& lineInput) {
  Metadata metadata;

  std::regex rgx("\\s+");
  std::sregex_token_iterator iter(lineInput.begin(), lineInput.end(), rgx, -1);
  std::sregex_token_iterator end;
  for (; iter != end; ++iter) {
    metadata.values.push_back(*iter);
  }

  if (metadata.isValid()) {
    return metadata;
  } else {
    return {};
  }
}

int main() {
  DLT_REGISTER_APP("LDA", "Logcat DLT Adapter");
  g_ctx = std::make_unique<Context>("Logcat DLT Adapter", "LDA");

  std::string lineInput;
  ParsingContext parsingContext;
  ContextIdEncoder id_encoder;
  std::map<std::string, std::unique_ptr<Context>> dlt_contexts;

  while (getline(std::cin, lineInput)) {
    // logcat --format="monotonic long" | LD_LIBRARY_PATH=.
    // ./dlt-android-converter

    // [ 6252.287 443: 530 E/WifiVendorHal ]

    g_ctx->log(DLT_LOG_VERBOSE, lineInput);

    if (lineInput.size() == 0) {  // for case of corrupted input
      g_ctx->log(DLT_LOG_VERBOSE, "Null line");
      parsingContext.reset();
    } else if (lineInput ==
               "\n") {  // logcat send epmty lines between log messages
      g_ctx->log(DLT_LOG_VERBOSE, "Next line");
      parsingContext.reset();
    } else {
      g_ctx->log(DLT_LOG_VERBOSE, "Valid input line");

      if (parsingContext.currentStep() == ParsingContext::Step::METADATA) {
        g_ctx->log(DLT_LOG_VERBOSE, "ParsingContext::Step::METADATA");
        auto metadata = parseMetadata(lineInput);
        if (metadata) {
          parsingContext.setMetadata(*metadata);
          parsingContext.step();
        } else {
          g_ctx->log(DLT_LOG_WARN, "Corrupted metadata: " + lineInput);
          parsingContext.reset();
        }
      } else if (parsingContext.currentStep() ==
                 ParsingContext::Step::MESSAGE) {
        g_ctx->log(DLT_LOG_VERBOSE, "DEBUG: ParsingContext::Step::MESSAGE");

        parsingContext.setMessage(lineInput);

        if (parsingContext.isCompleted()) {
          auto metadata = parsingContext.metadata();
          auto logContext = metadata.getLogContext();

          // Here we get dlt context and send message in it.
          auto context_it = dlt_contexts.find(logContext.name);
          if (context_it == dlt_contexts.end()) {
            auto ctx_id = id_encoder.getContextId(logContext.name);
            auto new_it = dlt_contexts.emplace(
                logContext.name,
                std::make_unique<Context>(logContext.name, ctx_id));
            context_it = new_it.first;

            g_ctx->log(DLT_LOG_INFO, "Create new DLT Context. " + ctx_id +
                                         " - " + logContext.name);
          }

          context_it->second->log(logContext.logLevel,
                                  parsingContext.message());

        } else {
          g_ctx->log(DLT_LOG_WARN, "Corrupted parsing context");
        }

        parsingContext.reset();
      } else {
        throw std::runtime_error("Unsupported parsing context step");
      }
    }
  }

  dlt_contexts.clear();

  DLT_UNREGISTER_APP();
  return 0;
}
