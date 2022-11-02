package cn.simmed.fbbatis;

import org.fisco.bcos.sdk.contract.precompiled.cns.CNSPrecompiled;
import org.fisco.bcos.sdk.contract.precompiled.consensus.ConsensusPrecompiled;
import org.fisco.bcos.sdk.contract.precompiled.contractmgr.ContractLifeCyclePrecompiled;
import org.fisco.bcos.sdk.contract.precompiled.crud.CRUD;
import org.fisco.bcos.sdk.contract.precompiled.crud.table.TableFactory;
import org.fisco.bcos.sdk.contract.precompiled.model.PrecompiledAddress;
import org.fisco.bcos.sdk.contract.precompiled.permission.ChainGovernancePrecompiled;
import org.fisco.bcos.sdk.contract.precompiled.permission.PermissionPrecompiled;
import org.fisco.bcos.sdk.contract.precompiled.sysconfig.SystemConfigPrecompiled;

public class PrecompiledCommonInfo {

    public static String getAddress(PrecompiledTypes types) {
        switch (types) {
            case SYSTEM_CONFIG:
                return PrecompiledAddress.SYSCONFIG_PRECOMPILED_ADDRESS;
            case TABLE_FACTORY:
                return PrecompiledAddress.TABLEFACTORY_PRECOMPILED_ADDRESS;
            case CRUD:
                return PrecompiledAddress.CRUD_PRECOMPILED_ADDRESS;
            case CONSENSUS:
                return PrecompiledAddress.CONSENSUS_PRECOMPILED_ADDRESS;
            case CNS:
                return PrecompiledAddress.CNS_PRECOMPILED_ADDRESS;
            case PERMISSION:
                return PrecompiledAddress.PERMISSION_PRECOMPILED_ADDRESS;
            case CSM:
                return PrecompiledAddress.CONTRACT_LIFECYCLE_PRECOMPILED_ADDRESS;
            case CHAIN_GOVERN:
                return PrecompiledAddress.CHAINGOVERNANCE_PRECOMPILED_ADDRESS;
            default:
                return "";
        }
    }

    public static String getAbi(PrecompiledTypes types) {
        switch (types) {
            case SYSTEM_CONFIG:
                return SystemConfigPrecompiled.ABI;
            case TABLE_FACTORY:
                return TableFactory.ABI;
            case CRUD:
                return CRUD.ABI;
            case CONSENSUS:
                return ConsensusPrecompiled.ABI;
            case CNS:
                return CNSPrecompiled.ABI;
            case PERMISSION:
                return PermissionPrecompiled.ABI;
            case CSM:
                return ContractLifeCyclePrecompiled.ABI;
            case CHAIN_GOVERN:
                return ChainGovernancePrecompiled.ABI;
            default:
                return "";
        }
    }

}
