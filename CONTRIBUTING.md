# Contributing and Review Guidelines

All contributions are welcome! 

## Branching

Our branching method is [git-flow](https://jeffkreeftmeijer.com/git-flow/)

- **master**: Latest stable branch
- **dev**: Stable branch waiting for release(merge to master)
- **feature-xxxx**: A developing branch of a new feature named xxxx
- **bugfix-xxxx**: A branch to fix the bug named xxxx

## How to

### Issue

Go to [issues page](https://github.com/FISCO-BCOS/lab-bcos/issues)

### Fix bugs

1. **Create** a new branch named **bugfix-xxxx** forked from **master** branch
2. **Fix** the bug..
3. **Test** the fixed code
4. Make **pull request** to **dev** and **master** simultaneously
5. Wait the community to review the code
6. Merged(**Bug fixed**)

### Develop a new feature

1. **Create** a new branch named **feature-xxxx** forked from **dev** branch
2. **Coding** in feature-xxxx
3. **Pull** origin dev branch to feature-xxxx constantly
4. **Test** your code
5. Make **pull request** back to dev branch
6. Wait the community to review the code
7. Merged !!!!

## Coding Style

See [CODING_STYLE](CODING_STYLE.md).

## Code formatting

Code formatting rules are described by the [Clang-Format Style Options] file [.clang-format].
Please use the [clang-format] (version 5.0 or higher recommended) tool to format your code _changes_ accordingly.
[git-clang-format] tool is useful to limit reformatting to your changes only:

```
git clang-format          # to reformat the changes in the staging area and put the result into working directory
git clang-format -f       # to reformat the changes in the working directory
git clang-format <commit> # to reformat commits between specified commit and HEAD
```

[Clang-Format Style Options]: https://clang.llvm.org/docs/ClangFormatStyleOptions.html
[clang-format]: https://clang.llvm.org/docs/ClangFormat.html
[.clang-format]: .clang-format
[git-clang-format]: https://llvm.org/svn/llvm-project/cfe/trunk/tools/clang-format/git-clang-format