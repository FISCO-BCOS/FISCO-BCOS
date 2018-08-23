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

See [CODING_STYLE](CODING_STYLE.md).​