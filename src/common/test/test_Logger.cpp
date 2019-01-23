#include <iostream>
#include <sstream>
#include <algorithm>
#include <string>
#include <vector>

#include <boost/regex.hpp>

#include "common/Logger.hpp"
#include "test_utility/unittest_main_function.hpp"

using namespace sarus;

TEST_GROUP(LoggerTestGroup) {
};

class LoggerChecker {
public:
    LoggerChecker& log(common::logType logLevel, const std::string& message) {
        auto& logger = common::Logger::getInstance();
        logger.log(message, "subsystem", logLevel, stdoutStream, stderrStream);
        return *this;
    }

    LoggerChecker& expectGeneralMessageInStdout(const std::string& message) {
        auto pattern = ".*^" + message + "\n.*";
        expectedPatternInStdout += pattern;
        return *this;
    }

    LoggerChecker& expectMessageInStdout(const std::string& logLevel, const std::string& message) {
        return expectMessage(logLevel, message, expectedPatternInStdout);
    }

    LoggerChecker& expectMessageInStderr(const std::string& logLevel, const std::string& message) {
        return expectMessage(logLevel, message, expectedPatternInStderr);
    }

    ~LoggerChecker() {
        check(stdoutStream, expectedPatternInStdout);
        check(stderrStream, expectedPatternInStderr);
    }

private:
    LoggerChecker& expectMessage(const std::string& logLevel, const std::string& message, std::string& expectedPattern) {
        auto messagePattern = "\\[.*\\..*\\] \\[.*\\] \\[subsystem\\] \\[" + logLevel + "\\] " + message + "\n";
        expectedPattern += messagePattern;
        return *this;
    }

    void check(const std::ostringstream& stream, const std::string& expectedPattern) const {
        auto regex = boost::regex(expectedPattern);
        boost::cmatch matches;
        CHECK(boost::regex_match(stream.str().c_str(), matches, regex));
    }

private:
    std::ostringstream stdoutStream;
    std::ostringstream stderrStream;

    std::string expectedPatternInStdout;
    std::string expectedPatternInStderr;
};

TEST(LoggerTestGroup, logger) {
    const std::string generalMessage = "GENERAL message";
    const std::string debugMessage = "DEBUG message";
    const std::string infoMessage = "INFO message";
    const std::string warnMessage = "WARN message";
    const std::string errorMessage = "ERROR message";

    // DEBUG level
    common::Logger::getInstance().setLevel(common::logType::DEBUG);
    LoggerChecker{}
        .log(common::logType::GENERAL, generalMessage)
        .log(common::logType::DEBUG, debugMessage)
        .log(common::logType::INFO, infoMessage)
        .log(common::logType::WARN, warnMessage)
        .log(common::logType::ERROR, errorMessage)
        .expectGeneralMessageInStdout(generalMessage)
        .expectMessageInStdout("DEBUG", debugMessage)
        .expectMessageInStdout("INFO", infoMessage)
        .expectMessageInStderr("WARN", warnMessage)
        .expectMessageInStderr("ERROR", errorMessage);

    // INFO level
    common::Logger::getInstance().setLevel(common::logType::INFO);
    LoggerChecker{}
        .log(common::logType::GENERAL, generalMessage)
        .log(common::logType::DEBUG, debugMessage)
        .log(common::logType::INFO, infoMessage)
        .log(common::logType::WARN, warnMessage)
        .log(common::logType::ERROR, errorMessage)
        .expectGeneralMessageInStdout(generalMessage)
        .expectMessageInStdout("INFO", infoMessage)
        .expectMessageInStderr("WARN", warnMessage)
        .expectMessageInStderr("ERROR", errorMessage);

    // WARN level
    common::Logger::getInstance().setLevel(common::logType::WARN);
    LoggerChecker{}
        .log(common::logType::GENERAL, generalMessage)
        .log(common::logType::DEBUG, debugMessage)
        .log(common::logType::INFO, infoMessage)
        .log(common::logType::WARN, warnMessage)
        .log(common::logType::ERROR, errorMessage)
        .expectGeneralMessageInStdout(generalMessage)
        .expectMessageInStderr("WARN", warnMessage)
        .expectMessageInStderr("ERROR", errorMessage);

    // ERROR level
    common::Logger::getInstance().setLevel(common::logType::ERROR);
    LoggerChecker{}
        .log(common::logType::GENERAL, generalMessage)
        .log(common::logType::DEBUG, debugMessage)
        .log(common::logType::INFO, infoMessage)
        .log(common::logType::WARN, warnMessage)
        .log(common::logType::ERROR, errorMessage)
        .expectGeneralMessageInStdout(generalMessage)
        .expectMessageInStderr("ERROR", errorMessage);
}

SARUS_UNITTEST_MAIN_FUNCTION();