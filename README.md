# lab-bcos
A lab version of fisco bcos for research

## Contact
[![Gitter](https://img.shields.io/gitter/room/fisco-bcos/Lobby.svg)](https://gitter.im/fisco-bcos/Lobby)
[![GitHub Issues](https://img.shields.io/github/issues-raw/FISCO-BCOS/lab-fisco.svg)](https://github.com/FISCO-BCOS/lab-bcos/issues)

- Chat in [lab-bcos](https://gitter.im/fisco-bcos/Lobby) channel on Gitter
- Report bugs, issues or feature requests using [Github Issues](https://github.com/FISCO-BCOS/lab-bcos/issues)


[![Build Status](https://travis-ci.org/FISCO-BCOS/lab-bcos.svg)](https://travis-ci.org/FISCO-BCOS/lab-bcos)

## Branching

The branching method is [git-flow](https://jeffkreeftmeijer.com/git-flow/)

* **master**: Latest stable branch
* **dev**: Stable branch waiting for release(merge to master)
* **feature-xxxx**: A developing branch of a new feature named xxxx
* **bugfix-xxxx**: A branch to fix the bug named xxxx

## Contributing

### To new feature

1. **Create** a new branch named **feature-xxxx** forked from **dev** branch
2. **Coding** in feature-xxxx
3. **Pull** origin dev branch to feature-xxxx constantly
4. **Test** your code
5. Make **pull request** back to dev branch
6. Wait for merging
7. Merged !!!!

### To fix bugs

1. **Create** a new branch named **bugfix-xxxx** forked from **master** branch
2. **Fix** the bug..
3. **Test** the fixed code
4. Make **pull request** to **dev** and **master** simultaneously
5. Wait for merging
6. Merged(**Bug fixed**)

## Coding Guide

* [Google C++ Style](https://google.github.io/styleguide/cppguide.html)
