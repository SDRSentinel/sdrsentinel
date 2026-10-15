# Contributing to SDRSentinel

Welcome to **SDRSentinel**! We are building a high-performance, modular, and academically rigorous suite for passive RF signal characterization, pulse descriptor word (PDW) extraction, and radio direction finding.

This document outlines our philosophy, standards, and workflow. Like [Bevy](https://bevyengine.org/), we believe in simplicity, performance, radical transparency, and above all: **accountable engineering**.

---

## 📜 The SDRSentinel Manifesto

### 1. Radical Accountability (*Hesap Verilebilirlik*)
Every line of code, every equation, and every state mutation must justify its existence:
* **No Unverified Magic:** Real-time digital signal processing (DSP) leaves no room for guesswork. Phase angles, spatial covariance matrices, and threshold triggers are empirical realities. If you write or refactor an algorithm, cite the mathematical basis and back it up with unit tests or synthetic datasets.
* **Explainable Architecture:** Code should be clear, self-documenting, and clean. If a design decision involves performance trade-offs or non-obvious workarounds, document the rationale directly in code and commit logs.
* **Ownership:** Whether written by a human engineer or assisted by an autonomous AI agent, someone must stand behind every commit. "The computer did it" is never an acceptable justification for defective or unvetted logic.

### 2. Radical Transparency: AI-Native & Fully Disclosed
The legacy predecessor to this codebase banned AI use out of fear of low-quality spam. **We take the opposite stance:**
* We actively embrace modern AI coding agents, generative engineering, and automated reasoning tools.
* **However, concealment is strictly prohibited.** If code is generated, co-authored, or assisted by an AI agent, it **must** be explicitly attributed in the Git commit metadata.
* See [**AGENTS.md**](AGENTS.md) for the mandatory commit trailer specifications (`Agent: <name> (<model>)`).

### 3. High Performance on the Critical Path
* **Zero Allocations in the DSP Loop:** Buffers, ringbuffers, and sample streams must operate without dynamic heap allocations during runtime processing.
* **Cache & SIMD Awareness:** Leverage SIMD vectorization (Volk, Eigen3) where possible and maintain cache locality.
* **Modularity:** Core logic must remain decoupled from specific SDR hardware frontends and GUI rendering widgets.

### 4. Welcoming & Pragmatic
* **Pull Requests Are Welcome:** We encourage bug reports, performance enhancements, hardware modules, and documentation improvements.
* **Constructive Collaboration:** Critique code, not people. Strive for technical excellence through civil, rigorous code review.

---

## 🛠️ Contribution Workflow

### 1. Code Standards
* **Language Standard:** Modern C++17.
* **Code Formatting:** We enforce clang-format repository-wide. Always verify before submitting:
  ```bash
  # Check formatting compliance
  ./scripts/check_clang_format.sh

  # Automatically format code
  ./scripts/run_clang_format.sh
  ```
* **Build Verification:** Ensure your changes compile cleanly without warnings:
  ```bash
  cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
  ninja -C build
  ```

### 2. Testing & Verification
If your pull request modifies DSP routines, protocol parsers, or hardware sinks/sources:
1. Run the existing test suites:
   ```bash
   cd build
   ./misc_modules/rf_tools/test_rf_tools_pulse_detector
   ./misc_modules/rf_tools/test_root_dev_integration
   ./misc_modules/kraken_doa/test_kraken_doa_dsp
   ./misc_modules/kraken_doa/test_kraken_heimdall_client
   ```
2. Add new unit tests covering regression cases and boundary conditions.

### 3. Commit Message Convention
Commits must follow [Conventional Commits](https://www.conventionalcommits.org/) format:

```text
<type>(<scope>): <short description>

[optional body explaining rationale, mathematical formulations, and benchmarks]

[optional footer(s) / trailers]
```

**Common Types:**
* `feat`: A new feature or module
* `fix`: A bug fix
* `perf`: Performance optimization
* `refactor`: Code reorganization without functional changes
* `docs`: Documentation updates
* `test`: Adding or updating test cases
* `build` / `ci`: Packaging, dependencies (`vcpkg`), or CI/CD pipelines

**AI Attribution Trailer (Mandatory for AI-assisted commits):**
```text
Agent: <Agent-Name> (<Model-Identifier>)
```
*(Refer to [AGENTS.md](AGENTS.md) for complete requirements).*

---

## 📻 Community Assets & JSON Contributions

We welcome community-contributed custom frequency band allocations and waterfall colormaps.

### Band Frequency Allocations
JSON definitions live in `root/res/bandplans/`:
```json
{
    "name": "Short Name",
    "country_name": "Country or Region ('--' for international)",
    "country_code": "ISO-3166 Two-Letter Code ('--' otherwise)",
    "author_name": "Author Name",
    "author_url": "https://example.com",
    "bands": [
        {
            "name": "Band Identifier",
            "type": "amateur",
            "start": 144000000,
            "end": 146000000
        }
    ]
}
```
*Bands must be sorted in ascending order by their start frequency in Hz.*

### Waterfall Color Maps
Custom palettes live in `root/res/colormaps/`:
```json
{
    "name": "Palette Name",
    "author": "Author Name",
    "map": [
        "#000020",
        "#000050",
        "#1E90FF",
        "#FFFFFF",
        "#FE6D16",
        "#FF0000"
    ]
}
```
*Hex strings must use `#RRGGBB` format.*

---

## ⚖️ License

By contributing to **SDRSentinel**, you agree that your contributions will be licensed under the project's [GNU General Public License v3.0 (GPL-3.0)](LICENSE.md).
