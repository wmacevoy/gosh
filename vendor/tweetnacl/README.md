# TweetNaCl, carried

`tweetnacl.c` and `tweetnacl.h` are **TweetNaCl, version 20140427**, by Daniel J. Bernstein,
Bernard van Gastel, Wesley Janssen, Tanja Lange, Peter Schwabe and Sjaak Smetsers, exactly as
its authors publish it. They are not gosh's and are not edited.

| | |
| --- | --- |
| From | `https://tweetnacl.cr.yp.to/20140427/tweetnacl.c` and `tweetnacl.h`, fetched 2026-11-15 |
| Licence | Public domain, by its authors' statement ("TweetNaCl is a self-contained public-domain C library", `https://tweetnacl.cr.yp.to/`) |
| `tweetnacl.c` | SHA-256 `02e65bc3013ff2168983365e55906bc783c4c7e0a60d8100f17bb303a17175c4` |
| `tweetnacl.h` | SHA-256 `43f29ad721d9927b747b0100ab4160c119e7bb180c7c98a66e4bf79d31244287` |

**How it was checked.** Against a second copy from elsewhere (a mirror on GitHub,
`dominictarr/tweetnacl`): the header is the same byte for byte; the mirror's `.c` differs only
where its keeper commented out the two functions that make keys, every other line the same.
And by `tweetnacl-test.c`, which is gosh's and is run by `tools/check.sh`: the carried file
gives the published answers for the key agreement (RFC 7748), a box (NaCl's own test), a
signature (RFC 8032) and SHA-512.

**Why it is here** (the owner, 2026-11-15: "tweet it is"). A meetup's registrations need a box
from one key pair to another and a signature (`docs/PROPOSAL_meetup-registration.md`). These
700 lines give both. Carried as source it is not a library a machine can lack
(`REQ1/SHAPE-3`); the cost of carrying it is that it is gosh's to read and to test.

**What to know about it.**

- It was written to be small and readable, not fast. It is used when an end registers with a
  meetup, never on a relayed datagram.
- It asks its user for random bytes (`randombytes`), to make keys. Whoever links it supplies
  that, and must stop the program if random bytes cannot be had.
- It is C. A C++ file includes the header inside `extern "C"`.
- A checker for undefined behaviour reports a left shift of a negative number
  (`tweetnacl.c:281`), which C before 2023 leaves undefined and which GCC and Clang both define
  to do what the code intends. The published answers come out right with and without the
  checker. Known to its authors' later verifiers; not changed here.
- Its signature check accepts some signatures in more than one form, and neither it nor its
  box refuses a few degenerate public keys. gosh's wrapper refuses a box whose shared secret is
  all zero, and remembers nothing by a signature's bytes.
- **To replace it:** fetch the new files into an empty directory, compare with these, change
  the checksums here in the same change, and run the check.

`Makefile.am` and `tweetnacl-test.c` are gosh's.
