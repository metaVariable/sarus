#ifndef sarus_hooks_common_Utility_hpp
#define sarus_hooks_common_Utility_hpp

#include <tuple>
#include <string>
#include <unordered_map>
#include <sys/types.h>
#include <boost/filesystem.hpp>

namespace sarus {
namespace hooks {
namespace common {
namespace utility {

std::tuple<boost::filesystem::path, pid_t> parseStateOfContainerFromStdin();
std::unordered_map<std::string, std::string> parseEnvironmentVariablesFromOCIBundle(const boost::filesystem::path&);
void enterNamespacesOfProcess(pid_t);

}}}} // namespace

#endif
