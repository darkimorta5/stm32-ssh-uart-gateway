# Source provenance and dependencies

This repository is a separate demonstration implementation. Its C core, tests, example, build helper, diagram, and documentation were authored for this repository. No source files were copied from a customer deliverable. No customer firmware, configuration, terminal logs, keys, or Git history are included.

The design describes general engineering patterns: exclusive stream ownership, bounded queues, partial write accounting, passive serial operation, and explicit error reporting. It does not reproduce a board specific SSH or USB stack.

The repository includes no third party library source or binary. It uses C standard library headers and Python standard library modules. A compiler, Python, and optionally CMake are separate build tools. Their installation packages and licenses are not part of this repository.

The project author and copyright holder is [darkimorta5](https://github.com/darkimorta5). Original repository material is offered under the [MIT license](../LICENSE). This provenance note is not a review of a private customer contract; do not add customer material, names, screenshots, or claims without the required rights.

The CI workflow references the external [actions/checkout](https://github.com/actions/checkout) action at commit `d23441a48e516b6c34aea4fa41551a30e30af803`. That action is MIT licensed and runs on GitHub's runner. Its implementation is not copied into this repository. The workflow grants only `contents: read` and disables credential persistence. It does not publish packages or deploy anything.

Future platform integrations need their own dependency inventory, exact versions, license texts, and any required notices or source distribution obligations. In particular, selecting an SSH library or a board support package is not covered by this repository's MIT license.

Only tests run against this independent source may be cited as its validation. Prior tests of a different implementation are not hardware evidence for this code.
