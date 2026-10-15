# 🤖 AGENTS.md — Autonomous & AI-Assisted Contribution Guidelines

This document governs the usage, attribution, and accountability standards for **Artificial Intelligence (AI) coding agents**, language models, and AI-assisted workflows within the **SDRSentinel** project.

---

## 🎯 Core Philosophy: Transparency Over Concealment

Unlike legacy projects that blindly ban artificial intelligence, **SDRSentinel embraces human-AI collaborative engineering**. We recognize that modern autonomous agents and LLMs are potent multipliers for code synthesis, refactoring, performance profiling, and test generation.

However, with autonomy comes the non-negotiable imperative of **Accountability (*Hesap Verilebilirlik*)**. 

> [!IMPORTANT]
> **Concealing AI involvement is strictly forbidden.** 
> If an AI agent or model authored, co-authored, or assisted in creating a commit, it **MUST** be explicitly declared in the commit metadata. Unattributed or obfuscated AI code will be rejected on sight.

---

## 🏷️ Mandatory Commit Attribution Standard

Every Git commit generated or assisted by an AI agent must include a dedicated **Git trailer** in the commit message footer specifying the agent name and underlying model.

### 1. Specification

```text
<type>(<scope>): <concise description>

[Optional detailed body explaining mathematical formulations, architectural trade-offs, and verification results]

Agent: <AgentName> (<ModelIdentifier>)
[Optional: Co-authored-by: <AgentName> <bot-email>]
[Optional: Verified-by: <HumanReviewer> <email>]
```

### 2. Format Rules
* The trailer key must be `Agent:` (case-insensitive, capitalized preferred).
* The value must specify the **agent/environment** and the **model identifier** in parentheses.
* If a human engineer reviewed and tested the agent's output, include `Verified-by: Name <email>`.

### 3. Examples

#### Autonomous Agent (Antigravity)
```gitcommit
feat(dsp): vectorize spatial covariance accumulator using Eigen3 SIMD

Implement batch accumulation of snapshot matrices Rxx = (1/K) * sum(x * x^H).
Replaces nested scalar loops with Eigen vectorization, achieving a 4.2x speedup
in DOA processing loops. Validated with test_kraken_doa_dsp.

Agent: Antigravity (gemini-2.5-pro)
Verified-by: Suleyman Poyraz <zaryob.dev@gmail.com>
```

#### Terminal Agent (Claude Code)
```gitcommit
fix(core): correct circular buffer index wrap under high sample rates

Prevents potential signed integer overflow in RingBuffer::read() when
stream position exceeds 2^31 samples.

Agent: Claude Code (claude-3.7-sonnet)
```

#### IDE / Copilot Assistant
```gitcommit
refactor(packaging): migrate loose build scripts to packaging/ hierarchy

Agent: Cursor (claude-3.5-sonnet)
Co-authored-by: Cursor <cursor@anysphere.co>
```

---

## 📐 The Four Pillars of Agent Accountability

When acting as an AI agent or committing AI-assisted code to SDRSentinel, adhere strictly to these four principles:

### 1. Empirical Verification (*Test Etmeden Commit Atma*)
* **No Blind Commits:** An agent must never submit code that has not been compiled or tested.
* **Build Verification:** Ensure the project compiles cleanly:
  ```bash
  cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
  ninja -C build
  ```
* **Test Verification:** Run the relevant DSP, PDW, or protocol test binaries before committing:
  ```bash
  ./build/misc_modules/rf_tools/test_rf_tools_pulse_detector
  ./build/misc_modules/kraken_doa/test_kraken_doa_dsp
  ```
* **Format Check:** Ensure formatting conforms to the repository style:
  ```bash
  ./scripts/check_clang_format.sh
  ```

### 2. Mathematical & Algorithmic Rigor
SDRSentinel processes high-bandwidth RF spectrum data, pulse descriptor words, and phase-interferometric direction finding:
* **No Hallucinated Math:** Never fabricate formulas, steering vectors, or DSP filtering windows. 
* **Cite Formulations:** Always document algorithms with their established mathematical basis (e.g., Schmidt 1986 for MUSIC, Bar-Shalom for tracking, GTRI algorithms for PDWs).

### 3. Zero Critical-Path Allocations
* In signal processing loops (IQ buffers, audio callbacks, FFT processors), dynamic heap allocations (`malloc`, `new`, `std::vector::push_back` beyond capacity) cause latency spikes and buffer underruns.
* Ensure real-time processing threads remain allocation-free.

### 4. Human-in-the-Loop Ownership
* An AI agent does not have legal personality; the **human committer or reviewer who authorizes or merges the pull request bears ultimate responsibility** for the committed logic.
* "The AI generated it" is never an acceptable defense for broken logic, memory leaks, or license violations.

---

## 📋 Pull Request Checklist for Agents & Engineers

When opening a Pull Request containing AI-assisted or agent-generated changes, include the following checklist in the PR description:

```markdown
### AI Agent Disclosure
- [x] This pull request was assisted or authored by an AI Agent.
- **Agent Name & Model:** Antigravity (gemini-2.5-pro)
- **Commit Metadata:** Verified that all relevant commits contain the `Agent:` trailer.

### Verification Log
- [ ] `./scripts/check_clang_format.sh` passes repository-wide.
- [ ] Clean compilation with Ninja / CMake (`-DCMAKE_BUILD_TYPE=Release`).
- [ ] Unit tests executed and passing.
- [ ] No heap allocations introduced inside hot DSP loops.
```

---

## 🚫 Enforcement & Disciplinary Action

1. **Undeclared AI Contributions:** Pull requests identified as containing undisclosed AI-generated content will be closed immediately.
2. **Low-Effort Hallucinations:** Repetitive spam or pull requests containing non-functional, hallucinated code will result in contributor restrictions.
3. **Rigorous, Audited AI Contributions:** Welcome with open arms. Transparent engineering elevates everyone.
