/*
 * Sarus
 *
 * Copyright (c) 2018-2019, ETH Zurich. All rights reserved.
 *
 * Please, refer to the LICENSE file in the root directory.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include <sys/types.h>
#include <sys/mount.h>
#include <sys/signal.h>
#include <boost/regex.hpp>

#include "common/Logger.hpp"
#include "hooks/common/Utility.hpp"
#include "runtime/mount_utilities.hpp"
#include "hooks/ssh/SshHook.hpp"
#include "test_utility/Misc.hpp"
#include "test_utility/config.hpp"
#include "test_utility/filesystem.hpp"
#include "test_utility/OCIHooks.hpp"
#include "test_utility/unittest_main_function.hpp"

namespace rj = rapidjson;

namespace sarus {
namespace hooks {
namespace ssh {
namespace test {

class Helper {
public:
    void setupTestEnvironment() const {
        // host test environment
        sarus::common::createFoldersIfNecessary(localRepositoryDir);
        sarus::common::setOwner(localRepositoryDir, std::get<0>(idsOfUser), std::get<1>(idsOfUser));

        sarus::common::createFoldersIfNecessary(opensshDirInHost);
        boost::format extractArchiveCommand = boost::format("tar xf %s -C %s --strip-components=1")
            % sarus::common::Config::BuildTime{}.openSshArchive
            % opensshDirInHost;
        sarus::common::executeCommand(extractArchiveCommand.str());

        sarus::common::setEnvironmentVariable("SARUS_LOCAL_REPOSITORY_BASE_DIR=" + localRepositoryBaseDir.string());
        sarus::common::setEnvironmentVariable("SARUS_LOCAL_REPOSITORY_DIR=" + localRepositoryDir.string());
        sarus::common::setEnvironmentVariable("SARUS_OPENSSH_DIR=" + opensshDirInHost.string());

        // bundle test environment
        createOCIBundleConfigJSON();

        for(const auto& folder : rootfsFolders) {
            sarus::common::createFoldersIfNecessary(rootfsDir / folder);
            runtime::bindMount(boost::filesystem::path{"/"} / folder, rootfsDir / folder);
        }

        sarus::common::createFoldersIfNecessary(rootfsDir / "proc");
        if(mount(NULL, (rootfsDir / "proc").c_str(), "proc", 0, NULL) != 0) {
            auto message = boost::format("Failed to setup proc filesystem on %s: %s")
                % (rootfsDir / "proc")
                % strerror(errno);
            SARUS_THROW_ERROR(message.str());
        }
    }

    void writeContainerStateToStdin() const {
        test_utility::ocihooks::writeContainerStateToStdin(bundleDir);
    }

    void cleanupTestEnvironment() const {
        // bundle test environment
        CHECK(umount((rootfsDir / "usr/bin/ssh").c_str()) == 0);

        for(const auto& folder : rootfsFolders) {
            CHECK(umount2((rootfsDir / folder).c_str(), MNT_FORCE | MNT_DETACH) == 0);
            boost::filesystem::remove_all(rootfsDir / folder);
        }

        CHECK(umount2((rootfsDir / "proc").c_str(), MNT_FORCE | MNT_DETACH) == 0);
        boost::filesystem::remove_all(bundleDir);

        // host test environment
        boost::filesystem::remove_all(localRepositoryBaseDir);
        boost::filesystem::remove_all(opensshDirInHost);
    }

    void setUserIds() const {
        if(setresuid(std::get<0>(idsOfUser), std::get<0>(idsOfUser), std::get<0>(idsOfRoot)) != 0) {
            auto message = boost::format("Failed to set uid %d: %s") % std::get<0>(idsOfUser) % strerror(errno);
            SARUS_THROW_ERROR(message.str());
        }
    }

    void setRootIds() const {
        if(setresuid(std::get<0>(idsOfRoot), std::get<0>(idsOfRoot), std::get<0>(idsOfRoot)) != 0) {
            auto message = boost::format("Failed to set uid %d: %s") % std::get<0>(idsOfRoot) % strerror(errno);
            SARUS_THROW_ERROR(message.str());
        }
    }

    void checkLocalRepositoryHasSshKeys() const {
        CHECK(boost::filesystem::exists(localRepositoryDir / "ssh/ssh_host_rsa_key"));
        CHECK(boost::filesystem::exists(localRepositoryDir / "ssh/ssh_host_rsa_key.pub"));
        CHECK(boost::filesystem::exists(localRepositoryDir / "ssh/id_rsa"));
        CHECK(boost::filesystem::exists(localRepositoryDir / "ssh/id_rsa.pub"));
    }

    void checkContainerHasServerKeys() const {
        CHECK(boost::filesystem::exists(opensshDirInContainer / "etc/ssh_host_rsa_key"));
        CHECK(sarus::common::getOwner(opensshDirInContainer / "etc/ssh_host_rsa_key") == idsOfRoot)
        CHECK(boost::filesystem::exists(opensshDirInContainer / "etc/ssh_host_rsa_key.pub"));
        CHECK(sarus::common::getOwner(opensshDirInContainer / "etc/ssh_host_rsa_key.pub") == idsOfRoot);
    }

    void checkContainerHasClientKeys() const {
        CHECK(boost::filesystem::exists(opensshDirInContainer / "etc/userkeys/id_rsa"));
        CHECK(sarus::common::getOwner(opensshDirInContainer / "etc/userkeys/id_rsa") == idsOfUser);
        CHECK(boost::filesystem::exists(opensshDirInContainer / "etc/userkeys/id_rsa.pub"));
        CHECK(sarus::common::getOwner(opensshDirInContainer / "etc/userkeys/id_rsa.pub") == idsOfUser);
        CHECK(boost::filesystem::exists(opensshDirInContainer / "etc/userkeys/authorized_keys"));
        CHECK(sarus::common::getOwner(opensshDirInContainer / "etc/userkeys/authorized_keys") == idsOfUser);
    }

    void checkContainerHasChrootFolderForSshd() const {
        CHECK(boost::filesystem::exists(rootfsDir / "tmp/var/empty"));
        CHECK(sarus::common::getOwner(rootfsDir / "tmp/var/empty") == idsOfRoot);
    }

    bool isSshdRunningInContainer() const {
        auto out = sarus::common::executeCommand("ps ax -o pid,args");
        std::stringstream ss{out};
        std::string line;

        boost::smatch matches;
        boost::regex pattern("^ *[0-9]+ +/opt/sarus/openssh/sbin/sshd.*$");

        while(std::getline(ss, line)) {
            if(boost::regex_match(line, matches, pattern)) {
                return true;
            }
        }
        return false;
    }

    void checkContainerHasSshBinary() const {
        //check container's /usr/bin/ssh is /opt/sarus/openssh/bin/ssh
        CHECK(test_utility::filesystem::isSameBindMountedFile(rootfsDir / "usr/bin/ssh", opensshDirInContainer / "bin/ssh"));
    }

private:
    void createOCIBundleConfigJSON() const {
        auto doc = test_utility::ocihooks::createBaseConfigJSON(rootfsDir, idsOfUser);
        auto& allocator = doc.GetAllocator();
        doc["process"]["env"].PushBack(rj::Value{"SARUS_SSH_HOOK=1", allocator}, allocator);

        try {
            sarus::common::writeJSON(doc, bundleDir / "config.json");
        }
        catch(const std::exception& e) {
            auto message = boost::format("Failed to write OCI Bundle's JSON configuration");
            SARUS_RETHROW_ERROR(e, message.str());
        }
    }

private:
    std::tuple<uid_t, gid_t> idsOfRoot{0, 0};
    std::tuple<uid_t, gid_t> idsOfUser = test_utility::misc::getNonRootUserIds();

    sarus::common::Config config = test_utility::config::makeConfig();
    boost::filesystem::path bundleDir = boost::filesystem::path{ config.json.get()["OCIBundleDir"].GetString() };
    boost::filesystem::path rootfsDir = bundleDir / config.json.get()["rootfsFolder"].GetString();
    boost::filesystem::path localRepositoryBaseDir = boost::filesystem::absolute(
        sarus::common::makeUniquePathWithRandomSuffix("./sarus-test-localrepositorybase"));
    boost::filesystem::path localRepositoryDir =
        sarus::common::getLocalRepositoryDirectory(localRepositoryBaseDir, std::get<0>(idsOfUser));
    boost::filesystem::path opensshDirInHost = boost::filesystem::absolute(
        sarus::common::makeUniquePathWithRandomSuffix("./sarus-test-opensshstatic"));
    boost::filesystem::path opensshDirInContainer = rootfsDir / "opt/sarus/openssh";
    std::vector<std::string> rootfsFolders = {"etc", "dev", "bin", "sbin", "usr", "lib", "lib64"}; // necessary to chroot into rootfs
};

TEST_GROUP(SSHHookTestGroup) {
};

TEST(SSHHookTestGroup, testSshHook) {
    auto helper = Helper{};

    helper.setupTestEnvironment();

    // generate + check SSH keys in local repository
    helper.setUserIds(); // keygen is executed with user privileges
    SshHook{}.generateSshKeys();
    helper.setRootIds();
    helper.checkLocalRepositoryHasSshKeys();
    SshHook{}.checkLocalRepositoryHasSshKeys();

    // start sshd
    helper.writeContainerStateToStdin();
    SshHook{}.startSshd();
    helper.checkContainerHasServerKeys();
    helper.checkContainerHasClientKeys();
    helper.checkContainerHasChrootFolderForSshd();
    CHECK(helper.isSshdRunningInContainer());
    helper.checkContainerHasSshBinary();

    helper.cleanupTestEnvironment();
}

}}}} // namespace

SARUS_UNITTEST_MAIN_FUNCTION();
