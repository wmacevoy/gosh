# The core's classes that the seam proves

Each `D.from` here says how the prover reads one class of the core where it stands in `.build/core`
(the core at the pin with the seam's patches): `tools/proofs.sh` proves them after gosh's own
domains, with `D.claims.lean` beside it. They are what reads bytes from anyone (the owner,
2026-11-28): first the cipher's `Session::decrypt` (seam patch 23), with the `Nonce` it makes.

**What is assumed, and who signs it.** `Session.from`'s `outside ae_decrypt` lines are the contract
of OCB's `ae_decrypt` (Krovetz's, `src/crypto/imp/ae.h`), as its comment there says it, for the
call `decrypt` makes (12-byte nonce, tag bundled, `final` set, no associated data): it keeps its
context, reads the 12 bytes of the nonce and the `ct_len` bytes of the ciphertext, writes at most
`ct_len - 16` bytes of plaintext, and gives back the number written or a negative error. That the
function keeps it is the one thing `decrypt`'s proof assumes of it. Drafted by the agent from the
comment, 2026-11-28; reviewed by the owner at the merge.

**Not compared.** `Session` cannot be made here as the C++ makes it (its constructor takes a
`Base64Key` and calls `ae_init`, neither read), so its model is proved and not run beside the C++;
every construct `decrypt` uses is compared in one of the prover's own samples.
