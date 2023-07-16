// SPDX-License-Identifier: Apache-2.0
pragma solidity >=0.6.10 <0.8.20;

abstract contract PaillierPrecompiled {
    function paillierAdd(
        string memory cipher1,
        string memory cipher2
    ) public virtual view returns (string memory);
}
