# Sarus
#
# Copyright (c) 2018-2019, ETH Zurich. All rights reserved.
#
# Please, refer to the LICENSE file in the root directory.
# SPDX-License-Identifier: BSD-3-Clause

import common.util as util
from common.template_test_mpi_hook import TemplateTestMPIHook

class TestMPISupport(TemplateTestMPIHook):

    def _get_command_output_in_container(self, command):
        options = []
        if self._mpi_command_line_option:
            options.append("--mpi")
        return util.run_command_in_container(is_centralized_repository=False,
                                                    image=self._container_image,
                                                    command=command,
                                                    options_of_run_command=options)
