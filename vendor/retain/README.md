# retain/recall, vendored

A C++17 header by the owner: `retain<T> as( &thing );` in a frame publishes `thing` to everything
that frame calls, on a stack that is each thread's own, and `recall<T>()` there finds it. A
parameter that is not written. MIT (`LICENSE`), which GPL-3.0-or-later code may use.

- **From:** github.com/wmacevoy/retain-recall, commit `4d8becc3d1a708755d5106c71fc7f02156b9b143`.
- **What:** `include/retain.hpp` and `LICENSE`, copied unchanged. To update: copy the same two from
  a newer commit, and change the line above.
- **How it is built:** it is a header alone. `tools/build.sh` copies this directory into its copy
  of the core, at `vendor/retain/`; what uses it includes `"vendor/retain/retain.hpp"`.
- **Used for:** the seam's session (`src/seam/session.h`): gosh's main makes it and retains it, and
  the seam's hooks, which the core calls with nothing, recall it. So no value is the whole
  process's, and a session ends with the frame that made it.
- **How gosh uses it, which is less than it offers:** a retain is a local; what is recalled is
  used and not kept, and not pointed elsewhere; the frames above are not walked. The proving
  refuses the rest (the project strong-domain-driven-design: "lint", a context).
