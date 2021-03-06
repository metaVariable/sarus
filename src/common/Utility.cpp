/*
 * Sarus
 *
 * Copyright (c) 2018-2019, ETH Zurich. All rights reserved.
 *
 * Please, refer to the LICENSE file in the root directory.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include "Utility.hpp"

#include <fstream>
#include <array>
#include <memory>
#include <string>
#include <stdexcept>
#include <iterator>
#include <random>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>

#include <wordexp.h>
#include <termios.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <pwd.h>
#include <limits.h>

#include <rapidjson/istreamwrapper.h>
#include <rapidjson/ostreamwrapper.h>
#include <rapidjson/filereadstream.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/error/en.h>
#include <rapidjson/writer.h>

#include "common/Config.hpp"
#include "common/Error.hpp"

/**
 * Utility functions
 */

namespace sarus {
namespace common {

std::unordered_map<std::string, std::string> parseEnvironmentVariables(char** env) {
    auto map = std::unordered_map<std::string, std::string>{};

    for(size_t i=0; env[i] != nullptr; ++i) {
        std::string key, value;
        std::tie(key, value) = parseEnvironmentVariable(env[i]);
        map[key] = value;
    }

    return map;
}

std::tuple<std::string, std::string> parseEnvironmentVariable(const std::string& variable) {
    auto keyEnd = std::find(variable.cbegin(), variable.cend(), '=');
    if(keyEnd == variable.cend()) {
        auto message = boost::format("Failed to parse environment variable \"%s\". Expected symbol '='.")
            % variable;
        SARUS_THROW_ERROR(message.str());
    }

    auto key = std::string(variable.cbegin(), keyEnd);
    auto value = std::string(keyEnd+1, variable.cend());

    return std::tuple<std::string, std::string>{key, value};
}

std::string getEnvironmentVariable(const std::string& key) {
    char* p;
    if((p = getenv(key.c_str())) == nullptr) {
        auto message = boost::format("Environment doesn't contain variable with key %s") % key;
        SARUS_THROW_ERROR(message.str());
    }
    return p;
}

void setEnvironmentVariable(const std::string& variable) {
    auto* p = strdup(variable.c_str());
    if(putenv(p) != 0) {
        free(p);
        auto message = boost::format("Failed to set environment variable %s: %s")
            % variable % strerror(errno);
        SARUS_THROW_ERROR(message.str());
    }
}

std::string removeWhitespaces(const std::string& s) {
    auto result = s;
    auto newEnd = std::remove_if(result.begin(), result.end(), iswspace);
    result.erase(newEnd, result.end());
    return result;
}

std::string replaceString(std::string &buf, const std::string& from, const std::string& to)
{
    std::string::size_type pos = buf.find(from);
    while(pos != std::string::npos){
        buf.replace(pos, from.size(), to);
        pos = buf.find(from, pos + to.size());
    }
    return buf;
}

std::string eraseFirstAndLastDoubleQuote(const std::string& s) {
    if(s.size() < 2 || *s.cbegin() != '"' || *s.crbegin() != '"') {
        auto message = boost::format(   "Failed to remove first and last double quotes"
                                        " in string \"%s\". The string doesn't"
                                        " contain such double quotes.") % s;
        SARUS_THROW_ERROR(message.str());
    }
    return std::string( s.cbegin() + 1,
                        s.cbegin() + s.size() - 1);
}

std::string executeCommand(const std::string& command) {
    auto commandWithRedirection = command + " 2>&1"; // stderr-to-stdout redirection necessary because popen only reads stdout

    FILE* pipe = popen(commandWithRedirection.c_str(), "r");
    if(!pipe) {
        auto message = boost::format("Failed to execute command \"%s\". Call to popen() failed (%s)")
            % commandWithRedirection % strerror(errno);
        SARUS_THROW_ERROR(message.str());
    }

    char buffer[1024];
    std::string commandOutput;
    while(!feof(pipe)) {
        if(fgets(buffer, sizeof(buffer), pipe)) {
            commandOutput += buffer;
        }
        else if(!feof(pipe)) {
            auto message = boost::format("Failed to execute command \"%s\". Call to fgets() failed.")
                % commandWithRedirection;
            SARUS_THROW_ERROR(message.str());
        }
    }

    auto status = pclose(pipe);
    if(status == -1) {
        auto message = boost::format("Failed to execute command \"%s\". Call to pclose() failed (%s)")
            % commandWithRedirection % strerror(errno);
        SARUS_THROW_ERROR(message.str());
    }
    else if(!WIFEXITED(status)) {
        auto message = boost::format(   "Failed to execute command \"%s\"."
                                        " Process terminated abnormally. Process' output:\n\n%s")
                                        % commandWithRedirection % commandOutput;
        SARUS_THROW_ERROR(message.str());
    }
    else if(WEXITSTATUS(status) != 0) {
        auto message = boost::format(   "Failed to execute command \"%s\"."
                                        " Process terminated with status %d. Process' output:\n\n%s")
                                        % commandWithRedirection % WEXITSTATUS(status) % commandOutput;
        SARUS_THROW_ERROR(message.str());
    }

    return commandOutput;
}

void forkExecWait(const common::CLIArguments& args, const boost::optional<boost::filesystem::path>& chrootJail) {
    logMessage(boost::format("Executing %s") % args, common::logType::DEBUG);

    // fork and execute
    auto pid = fork();
    if(pid == -1) {
        auto message = boost::format("Failed to fork to execute subprocess %s: %s")
            % args % strerror(errno);
        SARUS_THROW_ERROR(message.str());
    }

    bool isChild = pid == 0;
    if(isChild) {
        if(chrootJail) {
            if(chroot(chrootJail->c_str()) != 0) {
                auto message = boost::format("Failed to chroot to %s: %s")
                    % *chrootJail % strerror(errno);
                SARUS_THROW_ERROR(message.str());
            }
        }
        execvp(args.argv()[0], args.argv());
        auto message = boost::format("Failed to exevp subprocess %s: %s") % args % strerror(errno);
        SARUS_THROW_ERROR(message.str());
    }
    else {
        int status;
        do {
            if(waitpid(pid, &status, 0) == -1) {
                auto message = boost::format("Failed to waitpid subprocess %s: %s")
                    % args % strerror(errno);
                SARUS_THROW_ERROR(message.str());
            }
        } while(!WIFEXITED(status) && !WIFSIGNALED(status));

        if(!WIFEXITED(status)) {
            auto message = boost::format("Subprocess %s terminated abnormally")
                % args;
            SARUS_THROW_ERROR(message.str());
        }
        else if(WEXITSTATUS(status) != 0) {
            auto message = boost::format("Subprocess %s exited with error status %s")
                % args % WEXITSTATUS(status);
            SARUS_THROW_ERROR(message.str());
        }
    }

    logMessage(boost::format("Successfully executed %s") % args, common::logType::DEBUG);
}

void SetStdinEcho(bool flag)
{
    struct termios tty;
    tcgetattr(STDIN_FILENO, &tty);
    if( !flag ) {
        tty.c_lflag &= ~ECHO;
    }
    else {
        tty.c_lflag |= ECHO;
    }

    (void) tcsetattr(STDIN_FILENO, TCSANOW, &tty);
}

std::string getHostname() {
    char hostname[HOST_NAME_MAX];
    if(gethostname(hostname, HOST_NAME_MAX) != 0) {
        auto message = boost::format("failed to retrieve hostname (%s)") % strerror(errno);
        SARUS_THROW_ERROR(message.str());
    }
    hostname[HOST_NAME_MAX-1] = '\0';
    return hostname;
}

std::string getUsername(uid_t uid) {
    struct passwd *result;
    if((result = getpwuid(uid)) == nullptr) {
        auto message = boost::format("failed to retrieve username with getpwuid(%s): %s") % uid % strerror(errno);
        SARUS_THROW_ERROR(message.str());
    }
    return result->pw_name;
}

size_t getFileSize(const boost::filesystem::path& filename) {
    struct stat st;
    if(stat(filename.string().c_str(), &st) != 0) {
        auto message = boost::format("Failed to retrieve size of file %s. Stat failed: %s")
            % filename % strerror(errno);
        SARUS_THROW_ERROR(message.str());
    }
    return st.st_size;
}

std::tuple<uid_t, gid_t> getOwner(const boost::filesystem::path& path) {
    struct stat sb;
    if(stat(path.c_str(), &sb) != 0) {
        auto message = boost::format("Failed to stat %s: %s") % path % strerror(errno);
        SARUS_THROW_ERROR(message.str());
    }
    return std::tuple<uid_t, gid_t>{sb.st_uid, sb.st_gid};
}

void setOwner(const boost::filesystem::path& path, uid_t uid, gid_t gid) {
    if(uid == static_cast<uid_t>(-1) || gid==static_cast<uid_t>(-1)) {
        assert( uid == static_cast<uid_t>(-1)
            && gid == static_cast<gid_t>(-1));
        return;
    }

    if(!boost::filesystem::exists(path)) {
        auto message = boost::format("attempted to change ownership of non existing path %s") % path;
        SARUS_THROW_ERROR(message.str());
    }

    if(chown(path.c_str(), uid, gid) != 0) {
        auto errorMessage = boost::format("failed to change ownership of path: %s") % path;
    }
}

bool isCentralizedRepositoryEnabled(const common::Config& config) {
    // centralized repository is enabled when a directory is specified
    return config.json.get().HasMember("centralizedRepositoryDir");
}

boost::filesystem::path getCentralizedRepositoryDirectory(const common::Config& config) {
    if(!isCentralizedRepositoryEnabled(config)) {
        SARUS_THROW_ERROR("failed to retrieve directory of centralized repository"
                            " because such feature is disabled. Please ask your system"
                            " administrator to enable the central read-only repository.");
    }
    return config.json.get()["centralizedRepositoryDir"].GetString();
}

boost::filesystem::path getLocalRepositoryDirectory(const common::Config& config) {
    auto baseDir = boost::filesystem::path{ config.json.get()["localRepositoryBaseDir"].GetString() };
    return getLocalRepositoryDirectory(baseDir, config.userIdentity.uid);
}

boost::filesystem::path getLocalRepositoryDirectory(const boost::filesystem::path& baseDir, uid_t uid) {
    auto userDir = baseDir / getUsername(uid);
    return userDir / common::Config::BuildTime{}.localRepositoryFolder;
}

/**
 * Generates a random suffix and append it to the given path. If the generated random
 * path exists, tries again with another suffix until the operation succeedes.
 * 
 * Note: boost::filesystem::unique_path offers a similar functionality. However, it
 * fails (throws exception) when the locale configuration is invalid. More specifically,
 * we experienced the problem when LC_CTYPE was set to UTF-8 and the locale UTF-8 was not
 * installed.
 */
boost::filesystem::path makeUniquePathWithRandomSuffix(const boost::filesystem::path& path) {
    auto uniquePath = std::string{};

    do {
        const size_t sizeOfRandomSuffix = 16;
        uniquePath = path.string() + "-" + generateRandomString(sizeOfRandomSuffix);
    } while(boost::filesystem::exists(uniquePath));
    
    return uniquePath;
}

std::string generateRandomString(size_t size) {
    auto dist = std::uniform_int_distribution<std::mt19937::result_type>(0, 'z'-'a');
    std::mt19937 generator;
    generator.seed(std::random_device()());
    
    auto string = std::string(size, '.');

    for(size_t i=0; i<string.size(); ++i) {
        auto randomCharacter = 'a' + dist(generator);
        string[i] = randomCharacter;
    }

    return string;
}

void createFoldersIfNecessary(const boost::filesystem::path& path, uid_t uid, gid_t gid) {
    auto currentPath = boost::filesystem::path("");

    for(const auto& element : path) {
        currentPath /= element;
        if(!boost::filesystem::exists(currentPath)) {    
            if(!boost::filesystem::create_directory(currentPath)) {
                // the creation might have failed because another process cuncurrently
                // created the same directory. So check whether the directory was indeed
                // created by another process.
                if(!boost::filesystem::is_directory(currentPath)) {
                    auto message = boost::format("Failed to create directory %s") % currentPath;
                    SARUS_THROW_ERROR(message.str());
                }
            }
            setOwner(currentPath, uid, gid);
        }
    }
}

void createFileIfNecessary(const boost::filesystem::path& path, uid_t uid, gid_t gid) {
    if(!boost::filesystem::exists(path)) {
        if(!boost::filesystem::exists(path.parent_path())) {
            createFoldersIfNecessary(path.parent_path(), uid, gid);
        }
        std::ofstream of(path.c_str());
        if(!of.is_open()) {
            auto message = boost::format("Failed to create file %s") % path;
            SARUS_THROW_ERROR(message.str());
        }
        setOwner(path, uid, gid);
    }
}

void copyFile(const boost::filesystem::path& src, const boost::filesystem::path& dst, uid_t uid, gid_t gid) {
    boost::filesystem::remove(dst); // remove dst if already exists
    boost::filesystem::copy_file(src, dst);
    setOwner(dst, uid, gid);
}

void copyFolder(const boost::filesystem::path& src, const boost::filesystem::path& dst, uid_t uid, gid_t gid) {
    if(!boost::filesystem::exists(src) || !boost::filesystem::is_directory(src)) {
        auto message = boost::format("Failed to copy %s to %s: source folder doesn't exist.") % src % dst;
        SARUS_THROW_ERROR(message.str());
    }

    if(boost::filesystem::exists(dst)) {
        auto message = boost::format("Failed to copy %s to %s: destination already exists.") % src % dst;
        SARUS_THROW_ERROR(message.str());
    }

    common::createFoldersIfNecessary(dst, uid, gid);

    // for each file/folder in the directory
    for(boost::filesystem::directory_iterator entry{src};
        entry != boost::filesystem::directory_iterator{};
        ++entry) {
        if(boost::filesystem::is_directory(entry->path())) {
            copyFolder(entry->path(), dst / entry->path().filename(), uid, gid);
        }
        else {
            copyFile(entry->path(), dst / entry->path().filename(), uid, gid);
        }
    }
}

void changeDirectory(const boost::filesystem::path& path) {
    if(!boost::filesystem::exists(path)) {
        auto message = boost::format("attemped to cd into %s, but directory doesn't exist") % path;
        SARUS_THROW_ERROR(message.str());
    }

    if(chdir(path.string().c_str()) != 0) {
        auto message = boost::format("failed to cd into %s: %s") % path % strerror(errno);
        SARUS_THROW_ERROR(message.str());
    }
}

static bool isSymlink(const boost::filesystem::path& path) {
    struct stat sb;
    if (lstat(path.string().c_str(), &sb) != 0) {
        return false;
    }
    return (sb.st_mode & S_IFMT) == S_IFLNK;
}

static boost::filesystem::path getSymlinkTarget(const boost::filesystem::path& path) {
    char buffer[PATH_MAX];
    auto count = readlink(path.string().c_str(), buffer, PATH_MAX);
    assert(count < PATH_MAX); // PATH_MAX is supposed to be large enough for any path + NULL terminating char
    buffer[count] = '\0';
    return buffer;
}

static boost::filesystem::path appendPathsWithinRootfs( const boost::filesystem::path& rootfs,
                                                        const boost::filesystem::path& path0,
                                                        const boost::filesystem::path& path1) {
    auto current = path0;

    for(const auto& element : path1) {
        if(element == "/") {
            continue;
        }
        else if(element == ".") {
            continue;
        }
        else if(element == "..") {
            if(current > rootfs) {
                current = current.remove_trailing_separator().parent_path();
            }
        }
        else if(isSymlink(current / element)) {
            auto target = getSymlinkTarget(current / element);
            bool isAbsoluteSymlink = target.string()[0] == '/';
            if(isAbsoluteSymlink) {
                current = realpathWithinRootfs(rootfs, target);
            }
            else {
                current = appendPathsWithinRootfs(rootfs, current, target);
            }
        }
        else {
            current /= element;
        }
    }

    return current;
}

boost::filesystem::path realpathWithinRootfs(const boost::filesystem::path& rootfs, const boost::filesystem::path& path) {
    if(path.string().substr(0, 1) != "/") {
        auto message = boost::format("Failed to determine realpath within rootfs. %s is not an absolute path.") % path;
        SARUS_THROW_ERROR(message.str());
    }

    return appendPathsWithinRootfs(rootfs, rootfs, path);
}

/**
 * Converts a string representing a list of key-value pairs to a map.
 *
 * If no separators are passed as arguments, the pairs are assumed to be separated by commas,
 * while keys and values are assumed to be separated by an = sign.
 * If a value is not specified (i.e. a character sequence between two pair separators does
 * not feature a key-value separator), the map entry is created with the value as an
 * empty string.
 */
std::unordered_map<std::string, std::string> convertListOfKeyValuePairsToMap(
    const std::string& kvList,
    const char pairSeparator,
    const char kvSeparator) {

    auto isSeparator = [pairSeparator, kvSeparator](char c) {
        return c == pairSeparator || c == kvSeparator;
    };

    auto map = std::unordered_map<std::string, std::string>{};
    auto keyBegin = kvList.cbegin();

    while(keyBegin != kvList.cend()) {
        auto keyEnd = std::find_if(keyBegin, kvList.cend(), isSeparator);

        // check for empty key
        if(std::distance(keyBegin, keyEnd) == 0) {
            SARUS_THROW_ERROR("error while parsing a malformed list of key-value pairs (found empty key)");
        }

        auto key = std::string(keyBegin, keyEnd);
        auto value = std::string{""};

        // key with associated value
        if(keyEnd != kvList.cend() && *keyEnd == kvSeparator) {
            auto valueBegin = keyEnd + 1;
            auto valueEnd = std::find(valueBegin, kvList.cend(), pairSeparator);

            if(std::distance(valueBegin, valueEnd) == 0) {
                SARUS_THROW_ERROR("error while parsing a malformed list of key-value pairs (found empty value)");
            }

            value = std::string(valueBegin, valueEnd);
            keyBegin = valueEnd;
        }
        // key without value
        else {
            value = "";
            keyBegin = keyEnd;
        }

        // check for duplicated keys
        if(map.find(key) != map.cend()) {
            SARUS_THROW_ERROR("error while parsing a malformed list of key-value pairs (found duplicated key)");
        }

        map[key] = value;

        // skip pair separator
        if(keyBegin != kvList.cend()) {
            ++keyBegin;
            if(keyBegin == kvList.cend()) {
                SARUS_THROW_ERROR("error while parsing a malformed list of key-value pairs (list terminated with \",\")");
            }
        }
    }

    return map;
}

/**
 * Converts a string representing a list of entries to a vector of strings.
 *
 * If no separator is passed as argument, the entries are assumed to be separated by semicolons.
 */
std::vector<std::string> convertStringListToVector(const std::string& input_string, const char separator) {
    auto vec = std::vector<std::string>{};
    
    auto is_not_separator = [separator](char c) {
        return c != separator;
    };
    
    auto entryBegin = std::find_if(input_string.cbegin(), input_string.cend(), is_not_separator);
    while(entryBegin != input_string.cend()) {
        auto entryEnd = std::find(entryBegin, input_string.cend(), separator);
        vec.emplace_back(entryBegin, entryEnd);

        entryBegin = entryEnd;
        entryBegin = std::find_if(entryBegin, input_string.cend(), is_not_separator);
    }

    return vec;
}

std::string readFile(const boost::filesystem::path& path) {
    std::ifstream ifs(path.string());
    auto s = std::string(   std::istreambuf_iterator<char>(ifs),
                            std::istreambuf_iterator<char>());
    return s;
}

rapidjson::Document readJSON(const boost::filesystem::path& filename) {
    rapidjson::Document json;
    try {
        std::ifstream schemaInputStream(filename.string());
        rapidjson::IStreamWrapper schemaStreamWrapper(schemaInputStream);
        json.ParseStream(schemaStreamWrapper);
    }
    catch (const std::exception& e) {
        auto message = boost::format("Error reading file %s") % filename;
        SARUS_RETHROW_ERROR(e, message.str());
    }

    if (json.HasParseError()) {
        auto message = boost::format("File %s is not a valid JSON.\n"
                                     "Error(offset %u): %s") %
                                     filename %
                                     static_cast<unsigned>(json.GetErrorOffset()) %
                                     rapidjson::GetParseError_En(json.GetParseError());
        SARUS_THROW_ERROR(message.str());
    }
    return json;
}

rapidjson::Document readAndValidateJSON(const boost::filesystem::path& configFilename, const rapidjson::SchemaDocument& schema) {
    rapidjson::Document json;

    // Use a reader object to parse the JSON storing configuration settings
    try {
        std::ifstream configInputStream(configFilename.string());
        rapidjson::IStreamWrapper configStreamWrapper(configInputStream);
        // Parse JSON from reader, validate the SAX events, and populate the configuration Document.
        rapidjson::SchemaValidatingReader<rapidjson::kParseDefaultFlags, rapidjson::IStreamWrapper, rapidjson::UTF8<> > reader(configStreamWrapper, schema);
        json.Populate(reader);

        // Check parsing outcome
        if (!reader.GetParseResult()) {
            // Not a valid JSON
            // When reader.GetParseResult().Code() == kParseErrorTermination,
            // it may be terminated by:
            // (1) the validator found that the JSON is invalid according to schema; or
            // (2) the input stream has I/O error.
            // Check the validation result
            if (!reader.IsValid()) {
                // Input JSON is invalid according to the schema
                // Output diagnostic information
                rapidjson::StringBuffer sb;
                reader.GetInvalidSchemaPointer().StringifyUriFragment(sb);
                auto message = boost::format("Invalid schema: %s\n") % sb.GetString();
                message = boost::format("%sInvalid keyword: %s\n") % message % reader.GetInvalidSchemaKeyword();
                sb.Clear();
                reader.GetInvalidDocumentPointer().StringifyUriFragment(sb);
                message = boost::format("%sInvalid document: %s\n") % message % sb.GetString();
                // Detailed violation report is available as a JSON value
                sb.Clear();
                rapidjson::PrettyWriter<rapidjson::StringBuffer> w(sb);
                reader.GetError().Accept(w);
                message = boost::format("%sError report:\n%s") % message % sb.GetString();
                SARUS_THROW_ERROR(message.str());
            }
            else {
                auto message = boost::format("Error parsing configuration file: %s") % configFilename;
                SARUS_THROW_ERROR(message.str());
            }
        }
    }
    catch(const common::Error&) {
        throw;
    }
    catch (const std::exception& e) {
        auto message = boost::format("Error reading configuration file %s") % configFilename;
        SARUS_RETHROW_ERROR(e, message.str());
    }

    return json;
}

void writeJSON(const rapidjson::Value& json, const boost::filesystem::path& filename) {
    try {
        createFoldersIfNecessary(filename.parent_path());
        std::ofstream ofs(filename.string());
        if(!ofs) {
            auto message = boost::format("Failed to open std::ofstream for %s") % filename;
            SARUS_THROW_ERROR(message.str());
        }
        rapidjson::OStreamWrapper osw(ofs);
        rapidjson::Writer<rapidjson::OStreamWrapper> writer(osw);
        json.Accept(writer);
    }
    catch(const std::exception& e) {
        auto message = boost::format("Failed to write JSON to %s") % filename;
        SARUS_RETHROW_ERROR(e, message.str());
    }
}

std::string serializeJSON(const rapidjson::Value& json) {
    namespace rj = rapidjson;
    rj::StringBuffer buffer;
    rj::Writer<rj::StringBuffer> writer(buffer);
    json.Accept(writer);
    return buffer.GetString();
}

rapidjson::Document convertCppRestJsonToRapidJson(web::json::value& cppRest) {
    try {
        std::stringstream is(cppRest.serialize());
        rapidjson::IStreamWrapper isw(is);
        rapidjson::Document doc;
        doc.ParseStream(isw);
        return doc;
    }
    catch(const std::exception& e) {
        auto message = boost::format("Failed to parse rapidjson object from JSON string %s")
            % cppRest.serialize();
        SARUS_RETHROW_ERROR(e, message.str());
    }
}

void logMessage(const boost::format& message, logType level) {
    logMessage(message.str(), level);
}

void logMessage(const std::string& message, logType level) {
    auto subsystemName = "CommonUtility";
    common::Logger::getInstance().log(message, subsystemName, level);
}

} // namespace
} // namespace
