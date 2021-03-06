*****************************
Native MPI hook (MPICH-based)
*****************************

Sarus's source code includes a hook able to import native MPICH-based
MPI implementations inside the container. This is useful in case the host system
features a vendor-specific or high-performance MPI stack based on MPICH (e.g.
Intel MPI, Cray MPI, MVAPICH) which is required to fully leverage a high-speed
interconnect.

When activated, the hook will enter the mount namespace of the container, search
for dynamically-linkable MPI libraries and replace them with functional
equivalents bind-mounted from the host system.

In order for the replacements to work seamlessly, the hook will check that the
host and container MPI implementations are ABI-compatible according to the
standards defined by the `MPICH ABI Compatibility Initiative
<https://www.mpich.org/abi/>`_. The Initiative is supported by several
MPICH-based implementations, among which MVAPICH, Intel MPI, and Cray MPT.

Hook installation
=================

The hook is written in C++ and it will be compiled when building Sarus without
the need of additional dependencies. Sarus's installation scripts will also
automatically install the hook in the ``$CMAKE_INSTALL_PREFIX/bin`` directory.
In short, no specific action is required to install the MPI hook.

Sarus configuration
=====================

The program is meant to be run as a **prestart** hook and does not accept
arguments, but its actions are controlled through a few environment variables:

* ``SARUS_MPI_LIBS``: Colon separated list of full paths to the host's
  libraries that will substitute the container's libraries. The ABI
  compatibility check is performed by comparing the version numbers specified in
  the libraries' file names as follows:

      - The major numbers (first from the left) must be equal.
      - The site's minor number (second from the left) must be greater or equal
        to the container's minor number.
      - If the host's library name does not contain the version numbers or
        contains only the major version number, the missing numbers are assumed
        to be zero.

  This compatibility check is in agreement with the MPICH ABI version number
  schema.

* ``SARUS_MPI_DEPENDENCY_LIBS``: Colon separated list of absolute paths to
  libraries that are dependencies of the ``SARUS_MPI_LIBS``. These libraries
  are always bind mounted in the container under ``/usr/lib``.

* ``SARUS_MPI_BIND_MOUNTS``: Colon separated list of absolute paths to generic
  files or directories that are required for the correct functionality of the
  host MPI implementation (e.g. specific device files). These resources will
  be bind mounted inside the container with the same path they have on the host.

* ``PATH``: Absolute path to a directory containing a trusted ``ldconfig``
  program **on the host**.

The following is an example ``OCIHooks`` object enabling the MPI hook:

.. code-block:: json


    {
        "prestart": [
            {
                "path": "/opt/sarus/bin/mpi_hook",
                "env": [
                    "SARUS_MPI_LIBS=/usr/lib64/mvapich2-2.2/lib/libmpi.so.12.0.5:/usr/lib64/mvapich2-2.2/lib/libmpicxx.so.12.0.5:/usr/lib64/mvapich2-2.2/lib/libmpifort.so.12.0.5",
                    "SARUS_MPI_DEPENDENCY_LIBS=",
                    "SARUS_MPI_BIND_MOUNTS=",
                    "PATH=/sbin"
                ]
            }
        ]
    }

Sarus support at runtime
========================

The hook alters the container filesystem only if the environment variable
``SARUS_MPI_HOOK=1`` is present in the container. This environment variable is
automatically set by Sarus if the ``--mpi`` command line option is passed to
:program:`sarus run`.
