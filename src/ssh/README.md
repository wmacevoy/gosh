# ssh

What gosh reads of ssh, apart from the parts that use it (2026-11-26, the owner: "ssh is its own
domain"; "Isolating crypto and ssh seems particularly important"). A domain laid out
(`../DOMAINS`): its ABI is the headers here, in `Gosh::Ssh`; its inside is `imp/`, in
`Gosh::Ssh::Imp`.

| File | What it holds | Test |
| --- | --- | --- |
| **ABI** (in `Gosh::Ssh`) | | |
| `config.h` | what `ssh -G` says of a host: its HostName, ProxyJump and ProxyCommand; whether a ProxyCommand runs gosh; the address to take for the server, and for a meetup, by them (from `../command/`'s `sshconfig.h`). Logic | `imp/config-test.cc` |
| `connection.h` | what ssh says of the connection it made (`SSH_CONNECTION`): the client's address, and the server's (from `../relay/`'s `settings.h`). Logic | `../relay/imp/settings-test.cc` |
| **inside** (`imp/`) | | |
| `config.cc` | what `config.h` says | as above |

Who uses it: the command (`config.h`, to read a host's config before it runs ssh) and the remote
relay (`connection.h`, to know the server's address as ssh reached it).

**Not here yet**, and still the command's: running ssh (`../command/imp/login.cc`), the words of
ssh's command line and of what it runs on the host (`../command/imp/words.cc`), and this program as
ssh's ProxyCommand (`../command/imp/proxy.*`, which throws the command's `Failure` and quotes with
its words). Each is to come here, so that one part says how gosh runs ssh.
