# Releasing SDRSentinel

The release workflow is `.github/workflows/package_builds.yml`. Pushes and pull requests against `master` build and test Windows x64, macOS arm64, macOS x86_64, Ubuntu 22.04 amd64, and Ubuntu 24.04 amd64. Only a matching `v` tag publishes a GitHub Release.

## Repository setup

In **Settings → Actions → General → Actions permissions**, allow actions created by GitHub. All actions used by the workflows belong to the `actions` organization. A local-actions-only policy prevents these workflows from starting. Enable GitHub-authored actions before the first run.

Workflow tokens default to read-only access. Only the release publishing job requests `contents: write`; no personal access token or additional secret is needed. Repository or organization policies must permit that job permission.

## Prepare and verify

1. Set `VERSION_STR` in `core/src/version.h` and `version-string` in `vcpkg.json` to the same version. Debian and macOS package metadata derive from the header. The prepared version is `1.3.0`.
2. Build and run the project tests locally:

   ```sh
   cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5
   cmake --build build --parallel 3
   ctest --test-dir build -L sdrsentinel --output-on-failure --no-tests=error
   sh packaging/linux/tests/test_make_debian_package.sh
   ```

   On MSVC use its configured generator, `cmake --build build --config Release`, and add `-C Release` to CTest. The two fake-server tests require POSIX sockets and run on Linux/macOS; Windows runs the other five suites. The `sdrsentinel` label excludes third-party libcorrect tests that are not built by the default target.

3. Check the workflows and scripts:

   ```sh
   actionlint .github/workflows/package_builds.yml .github/workflows/build_all.yml
   shellcheck packaging/linux/make_debian_package.sh packaging/linux/tests/test_make_debian_package.sh scripts/check_clang_format.sh
   git diff --check
   ./scripts/check_clang_format.sh
   ```

   `clang-format` must be on `PATH`. The formatting check is read-only and reports existing formatting violations as failures; it does not format files or silently pass when the tool is missing.

4. Review and commit the changes. Commits prepared by Codex use the configured Git identity and disclose assistance with an `Agent:` trailer.

## Publish after review

Push the reviewed commit and wait for **SDRSentinel Package Builds (Windows, macOS, Ubuntu)** to succeed on `master`:

```sh
git push origin master
gh run list --workflow package_builds.yml --branch master
```

Inspect the packages from that run. When the checks pass, create and push the release tag:

```sh
git tag -a v1.3.0 -m "SDRSentinel v1.3.0"
git push origin v1.3.0
```

The tag must point to the reviewed commit. The workflow checks that the tag matches the application version, rebuilds and tests all five packages, validates their presence, and publishes them with `SHA256SUMS` and generated release notes. A failed package job blocks publication. Neither a normal branch push nor **Run workflow** creates a release.

## Package limits

The macOS bundles use ad hoc signatures, without Apple notarization. The Windows SDK currently uses the PothosSDR paths expected by the MSVC modules, alongside a pinned vcpkg revision. Package contents reflect enabled modules; advanced hardware integrations from the extended upstream matrix are not all included in the five release packages.

`build_all.yml` retains the wider upstream matrix as a manual workflow. It is separate from the five-package release gate and no longer uploads to the inherited nightly release.
