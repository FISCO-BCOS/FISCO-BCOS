// Vendored for the native CI suite (tools/.ci/scenarios). Do not edit here.
// Source: https://github.com/FISCO-BCOS/java-sdk-demo
// Path: src/main/java/org/fisco/bcos/sdk/demo/contract/sol/DeployTreeTest.sol
// Commit: 65e9cbd95f67253e5e5c2737437545478582e2f3
// License: Apache-2.0 (the source repository's LICENSE)
// Regenerate the .bin/.sm.bin/.abi beside it with tools/.ci/contracts/regenerate.sh
pragma solidity >=0.6.0 <0.8.12;

interface INode {
    function init(uint i, uint limit, bool enableRevert) external;
    function values() external view returns (uint[] memory);
    function sets(uint x) external;
    function setsAndRevert(uint x, uint revertNodeId) external;
}

contract NodeFactory {
    function create(uint i, uint limit, bool enableRevert) public returns (INode) {
        INode node = INode(new Node(address(this)));
        node.init(i, limit, enableRevert);
        return node;
    }
}

contract Node is INode {
    INode public lchild;
    INode public rchild;
    NodeFactory public factory;
    uint public value;
    uint public id;

    constructor(address _factory) public{
        factory = NodeFactory(_factory);
    }

    function init(uint i, uint limit, bool enableRevert) public override{
        value = i;
        id = i;

        uint li = i * 2;
        if (li > limit) {
            require(!enableRevert, "if enableRevert, revert by force");
            return;
        }
        lchild = factory.create(i * 2, limit, enableRevert);

        uint ri = i * 2 + 1;
        if (ri > limit) {
            require(!enableRevert, "if enableRevert, revert by force");
            return;
        }
        rchild = factory.create(i * 2 + 1, limit, enableRevert);
    }

    function sets(uint x) public override {
        value = x;
        if (address(lchild) != address(0x0)) {
            lchild.sets(x);
        }

        if (address(rchild) != address(0x0)) {
            rchild.sets(x);
        }
    }

    function setsAndRevert(uint x, uint revertNodeId) public override {
        value = x;
        if (address(lchild) != address(0x0)) {
            try lchild.setsAndRevert(x, revertNodeId) {
            } catch (bytes memory /*lowLevelData*/) {
            }
        }

        if (address(rchild) != address(0x0)) {
            try rchild.setsAndRevert(x, revertNodeId) {
            } catch (bytes memory /*lowLevelData*/) {
            }
        }

        if (revertNodeId == id) {
            revert("test");
        }
    }

    function values() public override view returns (uint[] memory) {
        uint[] memory lvalues = address(lchild) != address(0x0) ? lchild.values() : new uint[](0);
        uint[] memory rvalues = address(rchild) != address(0x0) ? rchild.values() : new uint[](0);
        uint[] memory values = new uint[](lvalues.length + rvalues.length + 1);
        values[0] = value;
        for (uint i = 0; i < lvalues.length; i++) {
            values[i + 1] = lvalues[i];
        }
        for (uint i = 0; i < rvalues.length; i++) {
            values[i + 1 + lvalues.length] = rvalues[i];
        }
        return values;
    }
}

contract DeployTreeTest {
    NodeFactory public factory;
    constructor() public {
        factory = new NodeFactory();
    }

    function testDeploy() public {
        INode root = new Node(address(factory));
        root.init(1, 7, false);
    }

    function testRevert() public {
        INode root = new Node(address(factory));
        try root.init(1, 7, true) {
        } catch (bytes memory /*lowLevelData*/) {
        }
    }

    function testSetTreeValue() public returns (uint[] memory) {
        uint limit = 7;
        uint x = 6;
        INode root = new Node(address(factory));
        root.init(1, limit, false);
        root.sets(x);

        // check all values is x
        uint[] memory values = root.values();
        for (uint i = 0; i < values.length; i++) {
            require(values[i] == x, "value not equal");
        }

        return values;
    }

    function setAndRevertTreeValue(INode root, uint x) public {
        root.sets(x);
        revert("test");
    }

    function testSetAndRevertTreeValue() public returns (uint[] memory) {
        uint limit = 7;
        uint x = 6;
        INode root = new Node(address(factory));
        root.init(1, limit, false);
        root.sets(x);

        // check all values is x
        uint[] memory values = root.values();
        for (uint i = 0; i < values.length; i++) {
            require(values[i] == x, "value not equal");
        }

        try this.setAndRevertTreeValue(root, 9999) {
        } catch (bytes memory /*lowLevelData*/) {
        }

        // check all value still is x
        values = root.values();
        for (uint i = 0; i < values.length; i++) {
            require(values[i] == x, "value not equal");
        }

        return values;
    }

    function contains(uint[] memory values, uint x) public pure returns (bool) {
        for (uint i = 0; i < values.length; i++) {
            if (values[i] == x) {
                return true;
            }
        }
        return false;
    }

    function genPartialSonId(uint i, uint limit) public pure returns (uint[] memory) {
        if (i > limit) {
            return new uint[](0);
        }

        uint[] memory lsonIds = genPartialSonId(i * 2, limit);
        uint[] memory rsonIds = genPartialSonId(i * 2 + 1, limit);
        uint[] memory sonIds = new uint[](lsonIds.length + rsonIds.length + 1);
        sonIds[0] = i;
        for (uint j = 0; j < lsonIds.length; j++) {
            sonIds[j + 1] = lsonIds[j];
        }
        for (uint j = 0; j < rsonIds.length; j++) {
            sonIds[j + 1 + lsonIds.length] = rsonIds[j];
        }
        return sonIds;
    }

    event ID(uint id);
    function testPartialRevertTreeValue() public returns (uint[] memory) {
        uint limit = 31;
        uint x = 6666;
        uint revertNodeId = 5;
        INode root = new Node(address(factory));
        root.init(1, limit, false);

        try root.setsAndRevert(x, revertNodeId) {
        } catch (bytes memory /*lowLevelData*/) {
        }

        uint[] memory values = root.values();
        // must contain
        uint[] memory sonIds = genPartialSonId(revertNodeId, limit);
        for (uint i = 0; i < sonIds.length; i++) {
            require(contains(values, sonIds[i]), "missing not revert node");
        }

        // emit sonIds
        for (uint i = 0; i < sonIds.length; i++) {
            emit ID(sonIds[i]);
        }

        return values;
    }

    function check() public {
        factory = new NodeFactory();

        testDeploy();
        testRevert();
        testSetTreeValue();
        testSetAndRevertTreeValue();
        testPartialRevertTreeValue();
    }
}