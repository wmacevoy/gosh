# ssh

How gosh runs ssh, and what it reads of it, apart from the parts that use it (2026-11-26, the owner: "ssh is its own
domain"; "Isolating crypto and ssh seems particularly important"). A domain laid out
(`../DOMAINS`): its ABI is the headers here, in `Gosh::Ssh`; its inside is `imp/`, in
`Gosh::Ssh::Imp`.

| File | What it holds | Test |
| --- | --- | --- |
| **ABI** (in `Gosh::Ssh`) | | |
| `config.h` | what `ssh -G` says of a host: its HostName, ProxyJump and ProxyCommand; whether a ProxyCommand runs gosh; the address to take for the server, and for a meetup, by them (from `../command/`'s `sshconfig.h`). Logic | `imp/config-test.cc` |
| `connection.h` | what ssh says of the connection it made (`SSH_CONNECTION`): the client's address, and the server's (from `../relay/`'s `settings.h`). Logic | `../relay/imp/settings-test.cc` |
| `shell.h` | the host's shell's words: a line split into words, and words quoted into a line (from `../command/`'s `words.*`). Logic | `imp/login-test.cc` |
| `login.h` | ssh's words for each login: to start the server (by proxy, remote or local), and to run something with nothing asked (the relay, the meetup's ticket); `ssh -G` run; and a process made ssh (from the command's `words.*` and `login.*`). Logic, and an edge | `imp/login-test.cc`; running ssh, the outside tests |
| `proxy.h` | this program as ssh's ProxyCommand: the host's addresses by `--family`, this program's path, the ProxyCommand's line, and the bytes carried (from the command's `proxy.*`). An edge | `imp/login-test.cc` (the line, and addresses as numbers); the outside tests |
| **inside** (`imp/`) | | |
| `config.cc`, `shell.cc`, `login.cc`, `proxy.cc` | what the headers say | as above |

Who uses it: the command (to read a host's config, build ssh's words and run it, and quote what the
host's shell is given), the remote relay (`connection.h`, to know the server's address as ssh
reached it), and the seam (`proxy.h`: `gosh-client --mosh-fake-proxy` is ssh's ProxyCommand).

What ssh's functions say when they fail is a reason, not the command's `Failure`: the command
adds its name and throws (`resolve`), so ssh knows nothing of the command.
