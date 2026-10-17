# PulseAudio selected API provenance

Only selected public API declarations are copied into
`src/core/audio/PulseControlAbi.h`; control and ownership logic are
NereusSDR-original. No PulseAudio implementation is vendored.

Upstream: [PulseAudio v17.0](https://github.com/pulseaudio/pulseaudio/tree/v17.0/src/pulse).
The copied declarations remain LGPL-2.1-or-later, with each source file's complete
original notice and copied inline comments retained verbatim. The combined
NereusSDR application remains GPLv3. The full LGPL-2.1 text already ships as
`packaging/third-party-licenses/LGPLv2.1.txt`; `pulseaudio.txt` includes the notices.

| NereusSDR file | Source | Selected lines [v17.0] | Adaptation |
| --- | --- | --- | --- |
| `src/core/audio/PulseControlAbi.h` | `src/pulse/def.h` | 36–45, 92–102, 114–124 | context, operation and connection-flag enums |
| same | `src/pulse/mainloop.h` | 77–96, 114–118 | opaque mainloop and finite polling API |
| same | `src/pulse/context.h` | 153–160, 169–172, 179–180, 198–213 | opaque context, callback and connection lifetime |
| same | `src/pulse/operation.h` | 32–33, 41–52 | operation cancellation/state/lifetime |
| same | `src/pulse/introspect.h` | 464–479, 484–485, 493–494 | module record layout, list and unload API |

Unrelated API declarations and preprocessor aliases are omitted. Selected
declarations retain their types, enum values, field order, signatures and
comments inside a NereusSDR namespace. Opaque types have forward declarations.
`decltype` supplies runtime function pointer types without importing development
headers. Qt loads the system runtime; it is replaceable and not statically linked
by this adapter. Date: 2026-10-14; human author: J.J. Boyd (KG4VCF); AI tooling:
OpenAI Codex.

Source SHA-256 receipts:

| Header | SHA-256 |
| --- | --- |
| `def.h` | `57241aeafcce72061367ba0b04fd96a254b64591ff191a5fa6cea11544b27481` |
| `mainloop.h` | `db728dea865ace80fcee233028a9ff03792952065c0867218fe16f30f25c1735` |
| `context.h` | `9c11f1c982c990e122592ec7e428cc8cf07d7bfbda177a37ab34d8fb682a96e3` |
| `operation.h` | `f5afe1deb0ac539aeb74a23e646c6a2f3d4f6aead06d9f24bda717695f75f21c` |
| `introspect.h` | `fdf38178ac144476d706abde9456ce5a0d1bae6ffc2a58b42ff9a2097ab81f84` |
