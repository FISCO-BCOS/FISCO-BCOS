// Vendored for the native CI suite (tools/.ci/scenarios). Do not edit here.
// Source: https://github.com/FISCO-BCOS/java-sdk-demo
// Path: src/main/java/org/fisco/bcos/sdk/demo/contract/sol/EventTest.sol
// Commit: 65e9cbd95f67253e5e5c2737437545478582e2f3
// License: Apache-2.0 (the source repository's LICENSE)
// Regenerate the .bin/.sm.bin/.abi beside it with tools/.ci/contracts/regenerate.sh

contract EventTest {
    event Echo(uint);

    function check() public {
        this.echoDFS();
    }

    function echoDFS() public {
        this.dfs(1);
        // Event: {"Echo":[[4],[5],[2],[6],[7],[3],[1]]}
    }

    function dfs(uint i) public  {
        if (i >= 8) {
            return;
        }

        this.dfs(i * 2);
        this.dfs(i * 2 + 1);
        emit Echo(i);
    }
}