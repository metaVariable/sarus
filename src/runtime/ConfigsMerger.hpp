#ifndef sarus_runtime_ConfigsMerger_hpp
#define sarus_runtime_ConfigsMerger_hpp

#include <memory>
#include <unordered_map>
#include <string>
#include <boost/filesystem.hpp>

#include "common/Config.hpp"
#include "common/ImageMetadata.hpp"
#include "common/CLIArguments.hpp"


namespace sarus {
namespace runtime {

/**
 * This class merges configurations from different sources (CLI arguments, host environment,
 * image's metadata) and produces the final configuration that should be used in the container
 * (command to execute, CWD, environment variables, ...).
 */
class ConfigsMerger {
public:
    ConfigsMerger(std::shared_ptr<const common::Config>, const common::ImageMetadata&);
    boost::filesystem::path getCwdInContainer() const;
    std::unordered_map<std::string, std::string> getEnvironmentInContainer() const;
    common::CLIArguments getCommandToExecuteInContainer() const;

private:
    std::shared_ptr<const common::Config> config;
    common::ImageMetadata metadata;
    void setNvidiaEnvironmentVariables(
            const std::unordered_map<std::string, std::string>& hostEnvironment,
            std::unordered_map<std::string, std::string>& containerEnvironment) const;
    void setHooksEnvironmentVariables(
        const common::Config::CommandRun& commandRun,
        std::unordered_map<std::string, std::string>& containerEnvironment) const;
};

}
}

#endif