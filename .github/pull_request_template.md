## Summary

Describe the problem and the resulting behavior. Link any related issues.

## Verification

List the checks you ran and their results. For documentation or asset changes, describe the links or visual presentation you verified.

## Contribution checklist

- [ ] I followed [CONTRIBUTING.md](https://github.com/SDRSentinel/sdrsentinel/blob/master/CONTRIBUTING.md).
- [ ] I preserved relevant upstream credits and license notices.
- [ ] Changes include the relevant tests or verification evidence.

### AI Agent Disclosure

- [ ] This pull request was assisted or authored by an AI agent.
- **Agent Name & Model:** <!-- If applicable, enter the agent and model identifier. -->
- **Commit Metadata:** <!-- If applicable, verify that all assisted commits contain the Agent: trailer required by AGENTS.md. -->

### Verification Log

For code changes, complete the relevant checks and explain any that do not apply:

- [ ] `./scripts/check_clang_format.sh` passes repository-wide.
- [ ] Clean compilation with Ninja / CMake (`-DCMAKE_BUILD_TYPE=Release`).
- [ ] Unit tests executed and passing.
- [ ] No heap allocations introduced inside hot DSP loops.

See [AGENTS.md](https://github.com/SDRSentinel/sdrsentinel/blob/master/AGENTS.md) for AI contribution and verification requirements.
