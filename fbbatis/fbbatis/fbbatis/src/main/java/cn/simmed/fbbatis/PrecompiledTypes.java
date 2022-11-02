package cn.simmed.fbbatis;

/**
 * @author marsli
 */
public enum PrecompiledTypes {
    /**
     * System Config Precompile
     */
    SYSTEM_CONFIG(1000),

    /**
     * Table Factory Precompile
     */
    TABLE_FACTORY(1001),

    /**
     * CRUD Precompile
     */
    CRUD(1002),

    /**
     * Consensus Precompile
     */
    CONSENSUS(1003),

    /**
     * Registry Contract
     */
    CNS(1004),
    /**
     * Permission Precompile
     */
    PERMISSION(1005),
    /**
     * Contract status Manage (Contract life cycle)
     */
    CSM(1007),
    /**
     * Chain governance
     */
    CHAIN_GOVERN(1008),
    /**
     * Chain governance
     */
    KV_TABLE(1010);

    private int value;

    PrecompiledTypes(Integer type) {
        this.value = type;
    }

    public int getValue() {
        return this.value;
    }
}
