package cn.simmed.fbbatis;

import com.fasterxml.jackson.core.JsonParseException;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.apache.commons.lang3.StringUtils;
import org.fisco.bcos.sdk.model.PrecompiledConstant;

import java.io.IOException;
import java.util.Map;

public class PrecompiledUtils {


    public static final String ContractLogFileName = "deploylog.txt";

    // SystemConfig key
    public static final String TxCountLimit = "tx_count_limit";
    public static final String TxGasLimit = "tx_gas_limit";
    public static final String ConsensusTimeout = "consensus_timeout";
    // node consensus type
    public static final String NODE_TYPE_SEALER = "sealer";
    public static final String NODE_TYPE_OBSERVER = "observer";
    public static final String NODE_TYPE_REMOVE = "remove";
    // permission manage type
    public static final String PERMISSION_TYPE_PERMISSION = "permission";
    public static final String PERMISSION_TYPE_DEPLOY_AND_CREATE = "deployAndCreate";
    public static final String PERMISSION_TYPE_USERTABLE = "userTable";
    public static final String PERMISSION_TYPE_NODE = "node";
    public static final String PERMISSION_TYPE_SYS_CONFIG = "sysConfig";
    public static final String PERMISSION_TYPE_CNS = "cns";
    // contract manage type
    public static final String CONTRACT_MANAGE_FREEZE = "freeze";
    public static final String CONTRACT_MANAGE_UNFREEZE = "unfreeze";
    public static final String CONTRACT_MANAGE_GETSTATUS = "getStatus";
    public static final String CONTRACT_MANAGE_GRANTMANAGER = "grantManager";
    public static final String CONTRACT_MANAGE_LISTMANAGER = "listManager";

    public static final int InvalidReturnNumber = -100;
    public static final int QueryLogCount = 20;
    public static final int LogMaxCount = 10000;
    public static final String PositiveIntegerRange = "from 1 to 2147483647";
    public static final String NonNegativeIntegerRange = "from 0 to 2147483647";
    public static final String DeployLogntegerRange = "from 1 to 100";
    public static final String NodeIdLength = "128";
    public static final String TxGasLimitRange = "from 100000 to 2147483647";
    public static final String EMPTY_CONTRACT_ADDRESS =
            "0x0000000000000000000000000000000000000000";
    public static final String EMPTY_OUTPUT = "0x";
    public static final int TxGasLimitMin = 10000;
    // unit: second
    public static final int ConsensusTimeoutMin = 3;

    public static int PermissionCode = 0;
    public static int TableExist = 0;
    public static int PRECOMPILED_SUCCESS = 0; // permission denied
    public static int TABLE_NAME_AND_ADDRESS_ALREADY_EXIST = -51000; // table name and address already exist
    public static int TABLE_NAME_AND_ADDRESS_NOT_EXIST = -51001; // table name and address does not exist
    public static int CRUD_SQL_ERROR = -51503; // process sql error

    public static int SYS_TABLE_KEY_MAX_LENGTH = 58; // 64- "_user_".length
    public static int SYS_TABLE_KEY_FIELD_NAME_MAX_LENGTH = 64;
    public static int SYS_TABLE_VALUE_FIELD_MAX_LENGTH = 1024;
    public static int USER_TABLE_KEY_VALUE_MAX_LENGTH = 255;
    public static int USER_TABLE_FIELD_NAME_MAX_LENGTH = 64;
    public static int USER_TABLE_FIELD_VALUE_MAX_LENGTH = 16 * 1024 * 1024 - 1;


    public static boolean checkVersion(String version) {

        if (StringUtils.isBlank(version)) {
            return false;
        }else if (version.length() > PrecompiledConstant.CNS_MAX_VERSION_LENGTH) { // length exceeds
            return false;
        }else if (!version.matches("^[A-Za-z0-9.]+$")) { // check version's character
            return false;
        }else {
            return true;
        }
    }

    public static boolean checkNodeId(String nodeId) {
        if (nodeId.length() != 128) {
            return false;
        }else {
            return true;
        }
    }


    public static Map string2Map(String str)
            throws JsonParseException, IOException {
        Map<String, Object> resMap;
        ObjectMapper mapper = new ObjectMapper();
        resMap = mapper.readValue(str, Map.class);
        return resMap;
    }

}
