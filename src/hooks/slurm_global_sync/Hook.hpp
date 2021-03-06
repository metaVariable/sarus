/*
 * Sarus
 *
 * Copyright (c) 2018-2019, ETH Zurich. All rights reserved.
 *
 * Please, refer to the LICENSE file in the root directory.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#ifndef sarus_hooks_SlurmGlobalSyncHook_hpp
#define sarus_hooks_SlurmGlobalSyncHook_hpp

#include <linux/types.h>
#include <boost/filesystem.hpp>
#include <rapidjson/document.h>


namespace sarus {
namespace hooks {
namespace slurm_global_sync {

class Hook {
public:
    Hook();
    void performSynchronization() const;

    // these methods are public for test purpose
    void signalArrival() const;
    void signalDeparture() const;
    void cleanupSyncDir() const;

    bool allInstancesArrived() const;
    bool allInstancesDeparted() const;

private:
    void parseConfigJSONOfBundle();
    std::string getEnvironmentVariableInOCIBundleConfig(const rapidjson::Document& json,
                                                        const std::string& key) const;
    void createSyncFile(const boost::filesystem::path& file) const;
    size_t countFilesInDirectory(const boost::filesystem::path& directory) const;

private:
    bool isHookEnabled{ true };
    boost::filesystem::path bundleDir;
    boost::filesystem::path localRepositoryBaseDir;
    boost::filesystem::path localRepositoryDir;
    boost::filesystem::path syncDir;
    boost::filesystem::path syncDirArrival;
    boost::filesystem::path syncDirDeparture;
    boost::filesystem::path syncFileArrival;
    boost::filesystem::path syncFileDeparture;
    pid_t pidOfContainer;
    uid_t uidOfUser;
    gid_t gidOfUser;
    std::string slurmJobID;
    std::string slurmNTasks;
    std::string slurmProcID;
};

}}} // namespace

#endif
