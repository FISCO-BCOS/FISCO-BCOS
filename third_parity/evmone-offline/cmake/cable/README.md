# Cable

[![readme style: standard][readme style standard badge]][standard readme]

> Cable: CMake Bootstrap Library

Cable is a set of CMake modules and scripts containing common patterns used
in CMake-based C++ projects. The design goal is to be pragmatic rather than
generic so the number of provided options is minimal. The Cable modules are
independent and it is easy to use them individually.


## Table of Contents

- [Install](#install)
- [Usage](#usage)
- [Maintainer](#maintainer)
- [License](#license)


## Install

The suggested Cable location is `cmake/cable` relative to your project root directory.

### As git subtree

Adding a dependency project as a [git subtree] is just a copy of the source code
done in a bit more systematic way.

If you are not familiar with managing dependencies with git subtree read the
[Git subtree: the alternative to Git submodule][git subtree tutorial].

#### To install

```sh
git remote add cable https://github.com/ethereum/cable
git subtree add --prefix cmake/cable cable master --squash
```

#### To update

```sh
git subtree pull --prefix cmake/cable cable master --squash
```

### As git submodule

Include the Cable library as [git submodule] in your project.

```sh
git submodule add https://github.com/ethereum/cable cmake/cable
```

## Usage

Cable contains the `bootstrap.cmake` file that initializes the library.
Start by including this file in your main `CMakeLists.txt` from the Cable 
submodule/subtree or any other location. The `bootstrap.cmake` must be included 
before the `project()` command. After that, you can include and use other
Cable modules.

### Example

```cmake
cmake_minimum_required(VERSION 3.5)

include(cmake/cable/bootstrap.cmake)
include(CableBuildType)

project(tothemoon)

cable_set_build_type(DEFAULT RelWithDebInfo CONFIGURATION_TYPES Debug Release RelWithDebInfo)
```


## Maintainer

Paweł Bylica [@chfast]

## License

Licensed under the [Apache License, Version 2.0].


[@chfast]: https://github.com/chfast
[Apache License, Version 2.0]: LICENSE
[git submodule]: https://git-scm.com/book/en/v2/Git-Tools-Submodules
[git subtree]: https://github.com/git/git/blob/master/contrib/subtree/git-subtree.txt
[git subtree tutorial]: https://www.atlassian.com/blog/git/alternatives-to-git-submodule-git-subtree
[standard readme]: https://github.com/RichardLitt/standard-readme

[readme style standard badge]: https://img.shields.io/badge/readme%20style-standard-brightgreen.svg?style=flat-square
