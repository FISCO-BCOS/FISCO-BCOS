// SPDX-License-Identifier: Apache-2.0
pragma solidity >=0.6.10 <0.8.20;
pragma experimental ABIEncoderV2;

abstract contract ZkpPrecompiled
{
    function verifyEitherEqualityProof(bytes memory c1_point, bytes memory c2_point, bytes memory c3_point, bytes memory proof, bytes memory c_basepoint, bytes memory blinding_basepoint) public virtual view returns(bool);
    function verifyKnowledgeProof(bytes memory c_point, bytes memory proof, bytes memory c_basepoint, bytes memory blinding_basepoint) public virtual view returns(bool);
    function verifyFormatProof(bytes memory c1_point, bytes memory c2_point, bytes memory proof, bytes memory c1_basepoint, bytes memory c2_basepoint, bytes memory blinding_basepoint) public virtual view returns(bool);
    function verifySumProof(bytes memory c1_point, bytes memory c2_point, bytes memory c3_point, bytes memory proof, bytes memory value_basepoint, bytes memory blinding_basepoint)public virtual view returns(bool);
    function verifyProductProof(bytes memory c1_point, bytes  memory c2_point, bytes memory c3_point, bytes memory proof, bytes memory value_basepoint, bytes memory blinding_basepoint) public virtual view returns(bool);
    function verifyEqualityProof(bytes memory c1_point, bytes memory c2_point, bytes memory proof, bytes memory basepoint1, bytes memory basepoint2)public virtual view returns(bool);

    function aggregatePoint(bytes memory point1, bytes memory point2) public virtual view returns(bool, bytes memory);
}
