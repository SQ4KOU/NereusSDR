Triage the specified GitHub issue for the NereusSDR project.

1. Read the issue (use `gh issue view <number>`)
2. Check if it's a duplicate of an existing issue
3. Categorize: bug, enhancement, documentation, protocol, dsp, ui
4. Assess priority:
   - **P0**: crashes, data loss, unable to connect to radio
   - **P1**: feature broken, incorrect DSP output, protocol compliance
   - **P2**: UI glitch, minor inconvenience, cosmetic
   - **P3**: nice-to-have, future consideration
5. Add appropriate labels
6. If it's a bug, check if you can identify the root cause from the description
7. Post a triage comment summarizing your findings and recommended next steps
