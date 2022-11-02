package cn.simmed.fbbatis;

public class ConstantCode {

    /* return success */
    public static final RetCode RET_SUCCEED = RetCode.mark(0, "success");

    /* paramaters check */
    public static final RetCode PARAM_FAIL_GROUP_ID_IS_EMPTY =  RetCode.mark(201001, "groupId cannot be empty");
    public static final String PARAM_FAIL_GROUP_ID_IS_EMPTY_STRING = "{\"code\":201001,\"message\":\"groupId cannot be empty\"}";
    public static final String PARAM_FAIL_USER_IS_EMPTY_STRING = "{\"code\":201002,\"message\":\"user cannot be empty\"}";
    public static final RetCode PARAM_FAIL_USER_IS_EMPTY = RetCode.mark(201002, "user cannot be empty");
    public static final String PARAM_FAIL_USEAES_IS_EMPTY =
            "{\"code\":201003,\"message\":\"useAes cannot be empty\"}";
    public static final String PARAM_FAIL_VERSION_IS_EMPTY = "{\"code\":201004,\"message\":\"version cannot be empty\"}";
    public static final String PARAM_FAIL_FUNCNAME_IS_EMPTY = "{\"code\":201005,\"message\":\"funcName cannot be empty\"}";
    public static final String PARAM_FAIL_ABIINFO_IS_EMPTY = "{\"code\":201006,\"message\":\"abiInfo cannot be empty\"}";
    public static final String PARAM_FAIL_BYTECODE_BIN_IS_EMPTY = "{\"code\":201007,\"message\":\"bytecodeBin cannot be empty\"}";
    public static final String PARAM_FAIL_SIGN_USER_ID_IS_EMPTY_STRING = "{\"code\":201008,\"message\":\"signUserId cannot be empty\"}";
    public static final RetCode PARAM_FAIL_SIGN_USER_ID_IS_EMPTY = RetCode.mark(201008, "signUserId cannot be empty");

    /* general error */
    public static final RetCode CONTRACT_NOT_DEPLOY_ERROR = RetCode.mark(201009, "contract is not deployed");
    public static final RetCode ABI_SAVE_ERROR = RetCode.mark(201010, "save abi error");
    public static final RetCode IN_FUNCPARAM_ERROR = RetCode.mark(201011, "contract funcParam is error");
    public static final RetCode BLOCK_NUMBER_ERROR = RetCode.mark(201012, "request blockNumber is greater than latest");
    public static final RetCode ABI_GET_ERROR = RetCode.mark(201013, "get abi error");
    public static final RetCode CONTRACT_DEPLOY_ERROR = RetCode.mark(201014, "contract deploy error");
    public static final RetCode PRIVATEKEY_IS_NULL = RetCode.mark(201015, "user's privateKey is null");
    public static final RetCode FILE_IS_NOT_EXIST = RetCode.mark(201016, "file is not exist");
    public static final RetCode CONTRACT_DEPLOYED_ERROR = RetCode.mark(201017, "contract's current version has been deployed");
    //    public static final RetCode GET_NODE_CONFIG_FAILE = RetCode.mark(201017, "failed to get node config");
    public static final RetCode BLOCKNUMBER_AND_PBFTVIEW_UNCHANGED = RetCode.mark(201018, "blockNumber and pbftView unchanged");
    public static final RetCode IN_FUNCTION_ERROR = RetCode.mark(201019, "request function is error");
    public static final RetCode TRANSACTION_QUERY_FAILED = RetCode.mark(201020, "transaction query from chain failed");
    public static final RetCode TRANSACTION_SEND_FAILED = RetCode.mark(201021, "transaction send to chain failed");
    public static final RetCode NODE_REQUEST_FAILED = RetCode.mark(201022, "node request failed");
    public static final RetCode CONTRACT_EXISTS = RetCode.mark(201023, "contract already exists");
    public static final RetCode CONTRACT_NAME_REPEAT = RetCode.mark(201024, "contract name cannot be repeated");
    public static final RetCode INVALID_CONTRACT_ID = RetCode.mark(201025, "invalid contract id");
    public static final RetCode CONTRACT_HAS_BEAN_DEPLOYED = RetCode.mark(201026, "contract has been deployed");
    public static final RetCode SEND_ABI_INFO_FAIL = RetCode.mark(201027, "send abiInfo fail");
    public static final RetCode CONTRACT_BIN_NULL = RetCode.mark(201028, "bytecodeBin is null");
    public static final RetCode CONTRACT_ADDRESS_NULL = RetCode.mark(201029, "contractAddress is null");
    public static final RetCode CONTRACT_ADDRESS_INVALID = RetCode.mark(201030, "contractAddress invalid");
    public static final RetCode PRIVATE_KEY_DECODE_FAIL = RetCode.mark(201031, "privateKey decode fail");
    public static final RetCode NO_CONFIG_KEY_SERVER = RetCode.mark(201032, "not found config of keyServer");
    public static final RetCode DATA_SIGN_ERROR = RetCode.mark(201033, "data request sign error");
    public static final RetCode DATA_SIGN_NOT_ACCESSIBLE = RetCode.mark(201033, "data request sign not accessible");
    public static final RetCode GROUPID_NOT_EXIST = RetCode.mark(201034, "groupId not exist");
    public static final RetCode VERSION_AND_ADDRESS_CANNOT_ALL_BE_NULL = RetCode.mark(201035, "version and address cannot all be null");
    public static final RetCode CONTRACT_COMPILE_FAIL = RetCode.mark(201036, "compile fail");
    public static final RetCode USER_NAME_NULL = RetCode.mark(201037, "user name is null");
    public static final RetCode USER_NAME_EXISTS = RetCode.mark(201038, "user name already exists");
    public static final RetCode KEYSTORE_EXISTS = RetCode.mark(201039, "private key already exists");
    public static final RetCode KEYSTORE_NOT_EXIST = RetCode.mark(201040, "private key not exists");
    public static final RetCode PARAM_FAIL_APPID_SIGN_USER_ID_EMPTY =  RetCode.mark(201041, "external user's appId and signUserId cannot be empty");
    public static final RetCode NO_SOL_FILES = RetCode.mark(201042, "There is no sol files in source");
    public static final RetCode INVALID_GROUP_OPERATE_TYPE = RetCode.mark(201043, "invalid group operate type");
    // freeze contract
    public static final RetCode INVALID_DATA_TYPE = RetCode.mark(201044, "invalid data type");
    // tx channel
    public static final RetCode ENCODE_STR_CANNOT_BE_NULL = RetCode.mark(201045, "encode string can not be empty!");
    public static final RetCode TRANSACTION_FAILED = RetCode.mark(201046, "transaction failed!");

    public static final RetCode FAIL_PARSE_JSON = RetCode.mark(201050, "Fail to parse json");
    public static final RetCode GET_CONSENSUS_STATUS_FAIL = RetCode.mark(201051, "get consensus status fail");

    /* system error */
    public static final RetCode SYSTEM_ERROR = RetCode.mark(101001, "system error");
    public static final RetCode PARAM_VAILD_FAIL = RetCode.mark(101002, "param valid fail");
    public static final RetCode SYSTEM_ERROR_WEB3J_NULL = RetCode.mark(101003, "web3j instance of groupId is null, please try again");
    public static final RetCode SYSTEM_ERROR_GROUP_LIST_EMPTY = RetCode.mark(101004, "No group belongs to this groupId(node not belongs to this group)");
    public static final RetCode SYSTEM_ERROR_NODE_INACTIVE = RetCode.mark(101005, "No available node to connect");
    public static final RetCode SYSTEM_ERROR_NO_NODE_IN_GROUP = RetCode.mark(101005, "No nodes belong to this group");
    public static final RetCode WEB3J_CLIENT_IS_NULL = RetCode.mark(101006, "get web3j client failed!");

    /* precompiled success */
    public static final RetCode RET_SUCCESS = RetCode.mark(0, "success");
    public static final RetCode RET_SUCCESS_EMPTY_LIST = RetCode.mark(0,"Empty Set ");

    /* precompiled check */
    public static final String PARAM_FAIL_GROUPID_IS_EMPTY = "{\"code\":201101,\"message\":\"groupId cannot be empty\"}";
    public static final RetCode PARAM_FAIL_TABLE_NAME_IS_EMPTY = RetCode.mark(201102, "tableName cannot be empty");
    public static final String PARAM_FAIL_PERMISSION_TYPE_IS_EMPTY = "{\"code\":201103,\"message\":\"permissionType cannot be empty\"}";
    public static final RetCode PARAM_FAIL_PERMISSION_TYPE_NOT_EXIST =  RetCode.mark(201104, "permissionType not exists");
    public static final String PARAM_FAIL_FROM_IS_EMPTY = "{\"code\":201105,\"message\":\"from address cannot be empty\"}";
    public static final String PARAM_FAIL_CONTRACT_NAME_IS_EMPTY = "{\"code\":201106,\"message\":\"contract name cannot be empty\"}";
    public static final String PARAM_FAIL_CONFIG_KEY_IS_EMPTY = "{\"code\":201107,\"message\":\"system config key cannot be empty\"}";
    public static final String PARAM_FAIL_CONFIG_VALUE_IS_EMPTY = "{\"code\":201108,\"message\":\"system config value cannot be empty\"}";
    public static final String PARAM_FAIL_NODE_ID_IS_EMPTY = "{\"code\":2011109,\"message\":\"node id cannot be empty\"}";
    public static final String PARAM_FAIL_NODE_TYPE_IS_EMPTY = "{\"code\":201110,\"message\":\"node type cannot be empty\"}";
    public static final String PARAM_FAIL_PERMISSION_STATE_ALL_CONNOT_BE_EMPTY = "{\"code\":201111,\"message\":\"Permission state cannot be all empty\"}";
    public static final String PARAM_FAIL_CONTRACT_ADDRESS_EMPTY = "{\"code\":201112,\"message\":\"contract address cannot be empty\"}";
    public static final String PARAM_FAIL_CONTRACT_HANDLE_TYPE_EMPTY = "{\"code\":201113,\"message\":\"contract handle type cannot be empty\"}";
    public static final RetCode PARAM_FAIL_GRANT_ADDRESS_EMPTY = RetCode.mark(201114, "grant Address cannot be empty");
    public static final RetCode INVALID_CONTRACT_HANDLE_TYPE = RetCode.mark(201115, "invalid contract handle type");
    public static final RetCode FAIL_CONTRACT_HANDLE = RetCode.mark(201116, "contract status handle fail");

    // group operate error code from sdk
    public static final RetCode GROUP_OPERATE_FAIL = RetCode.mark(201120, "group operate fail");
    public static final RetCode NODE_INTERNAL_ERROR = RetCode.mark(201121, "node internal error");
    public static final RetCode GROUP_ALREADY_EXISTS = RetCode.mark(201122, "group already exists");
    public static final RetCode GROUP_ALREADY_RUNNING = RetCode.mark(201123, "group already running");
    public static final RetCode GROUP_ALREADY_STOPPED = RetCode.mark(201124, "group already stopped");
    public static final RetCode GROUP_ALREADY_DELETED = RetCode.mark(201125, "group already deleted");
    public static final RetCode GROUP_NOT_FOUND = RetCode.mark(201126, "group not found");
    public static final RetCode GROUP_OPERATE_INVALID_PARAMS = RetCode.mark(201127, "group operate param error");
    public static final RetCode PEERS_NOT_CONNECTED = RetCode.mark(201128, "group peers not connected");
    public static final RetCode GENESIS_CONF_ALREADY_EXISTS = RetCode.mark(201129, "group genesis conf already exists");
    public static final RetCode GROUP_CONF_ALREADY_EXIST = RetCode.mark(201130, "group config.ini already exists");
    public static final RetCode GENESIS_CONF_NOT_FOUND = RetCode.mark(201131, "group genesis conf not found");
    public static final RetCode GROUP_CONF_NOT_FOUND = RetCode.mark(201132, "group config.ini not found");
    public static final RetCode GROUP_IS_STOPPING = RetCode.mark(201133, "group is stopping");
    public static final RetCode GROUP_NOT_DELETED = RetCode.mark(201134, "group not deleted");

    // contract type param error
    public static final RetCode CONTRACT_TYPE_ENCODED_ERROR = RetCode.mark(201151, "Unsupported contract param type to encoded");
    public static final RetCode CONTRACT_TYPE_DECODED_ERROR = RetCode.mark(201152, "Unsupported contract param type to decoded");
    public static final RetCode CONTRACT_TYPE_PARAM_ERROR = RetCode.mark(201153, "unable to create instance of type, check input params");

    // add in v1.4.2
    public static final RetCode CONTRACT_PATH_IS_EXISTS = RetCode.mark(201154, "contract path is exists.");
    // add in v1.4.3
    public static final String PARAM_FAIL_CONTRACT_PATH_IS_EMPTY_STRING = "{\"code\":201155,\"message\":\"contract path cannot be empty\"}";
    // add in v1.5.0
    public static final RetCode WRITE_SDK_CRT_KEY_FILE_FAIL = RetCode.mark(201156,"Write front's sdk cert and key fail!");
    public static final RetCode WRITE_PRIVATE_KEY_CRT_KEY_FILE_FAIL = RetCode.mark(201157,"Write private key file fail!");
    // add in v1.5.1
    public static final RetCode GENERATE_CONTRACT_PROJECT_FAIL = RetCode.mark(201161, "generate project failed in scaffold");
    // v1.5.2
    public static final RetCode IP_FORMAT_ERROR = RetCode.mark(201162, "IP format error.");


    /* precompiled runtime check or error */
    // param
    public static final RetCode PARAM_ERROR = RetCode.mark(201200,"params not fit");
    public static final RetCode PARAM_ADDRESS_IS_INVALID = RetCode.mark(201201, "address is invalid");
    // permission
    public static final RetCode PERMISSION_DENIED = RetCode.mark(201202, "permission denied, please check chain administrator permission");
    // sys config
    public static final RetCode UNSUPPORTED_SYSTEM_CONFIG_KEY = RetCode.mark(201208, "unsupported for this system config key");
    public static final RetCode FAIL_SET_SYSTEM_CONFIG_TOO_SMALL =  RetCode.mark(201209,
            "provide value by positive integer mode, from 100000 to 2147483647");
    public static final RetCode FAIL_SET_SYSTEM_CONFIG = RetCode.mark(201210, "set system config value fail for params error or permission denied ");
    public static final RetCode FAIL_SET_CONSENSUS_TIMEOUT_TOO_SMALL = RetCode.mark(201211, "set consensus_timeout failed, must greater than 3 seconds");
    // consensus (node manager)
    public static final RetCode INVALID_NODE_ID = RetCode.mark(201216,"node id is invalid");
    public static final RetCode INVALID_NODE_TYPE = RetCode.mark(201217,"invalid node type: sealer, observer, remove ");
    public static final RetCode FAIL_CHANGE_NODE_TYPE = RetCode.mark(201218,"set node consensus type fail, check permission or node's group config file");
    // cns
    public static final RetCode INVALID_VERSION = RetCode.mark(201221,"Contract version should only contains 'A-Z' or 'a-z' or '0-9' or dot mark ");
    public static final RetCode INVALID_VERSION_EXCEED_LENGTH = RetCode.mark(201222,"version of contract is out of length");
    // add in v1.4.3
    public static final RetCode CNS_REGISTER_FAIL = RetCode.mark(201223,"cns register fail");
    public static final RetCode VERSION_NOT_EXISTS = RetCode.mark(201224,"version not exists");
    public static final RetCode PARAM_FAIL_CNS_NAME_IS_EMPTY = RetCode.mark(201225,"cns name cannot be empty");
    public static final String PARAM_FAIL_CNS_NAME_IS_EMPTY_STRING = "{\"code\":201225,\"message\":\"cns name cannot be empty\"}";
    // crud
    public static final RetCode PARAM_FAIL_SQL_ERROR = RetCode.mark(201226, "sql syntax error");
    public static final RetCode SQL_ERROR = RetCode.mark(201227, "crud sql fail");
    public static final RetCode FAIL_TABLE_NOT_EXISTS = RetCode.mark(201228, "table not exists");
    public static final String CRUD_EMPTY_SET = "Empty Set.";

    // cert PEM_FORMAT_ERROR
    public static final RetCode CERT_FILE_NOT_FOUND = RetCode.mark(201231, "Cert file not found, please check cert path in config");
    public static final RetCode PEM_FORMAT_ERROR = RetCode.mark(201232, "Pem file format error, must surrounded by -----XXXXX PRIVATE KEY-----");
    public static final RetCode PEM_CONTENT_ERROR = RetCode.mark(201233, "Pem file content error");
    public static final RetCode P12_PASSWORD_NOT_CHINESE = RetCode.mark(201235, "p12's password cannot be chinese");
    public static final RetCode P12_PASSWORD_ERROR = RetCode.mark(201236, "p12's password not match");
    public static final RetCode P12_FILE_ERROR = RetCode.mark(201237, "P12 file content error");
    public static final RetCode SDK_CERT_FILE_NOT_FOUND = RetCode.mark(201238, "Sdk cert file not found, please check front's conf directory");
    public static final RetCode SDK_KEY_FILE_NOT_FOUND = RetCode.mark(201239, "Sdk key file not found, please check front's conf directory");

    // mq error
    public static final RetCode EXCHANGE_OR_QUEUE_NOT_EXIST_ERROR = RetCode.mark(201241, "Exchange or message queue not exists, please check mq server or mq configuration");
    public static final RetCode DATA_REPEAT_IN_DB_ERROR = RetCode.mark(201242, "Database error: data already exists in db");
    public static final RetCode BLOCK_RANGE_PARAM_INVALID = RetCode.mark(201243, "Block range error, from/toBlock must greater than 0, toBlock must be greater than fromBlock");
    public static final RetCode DATA_NOT_EXIST_ERROR = RetCode.mark(201244, "Database error: data not exists in db, please check your params");
    public static final RetCode PARAM_INVALID_LETTER_DIGIT = RetCode.mark(201245, "Only support letter and digit, please check your params");
    public static final RetCode REGISTER_FAILED_ERROR = RetCode.mark(201246, "Register contractEvent failed, please check your param");
    public static final RetCode UNREGISTER_FAILED_ERROR = RetCode.mark(201247, "Unregister event failed, please check mq server exchange");
    public static final RetCode PARAM_FAIL_ABI_INVALID = RetCode.mark(201248, "Contract abi invalid, please check abi");

    // abi import
    public static final RetCode CONTRACT_ADDRESS_ALREADY_EXISTS = RetCode.mark(201255, "contract address already exists");
    public static final RetCode ABI_INFO_NOT_EXISTS = RetCode.mark(201256, "abi info of this id not exists");
    public static final RetCode PARAM_FAIL_ABI_ID_EMPTY = RetCode.mark(201257, "Abi Id cannot be empty");

    // upload solc js file
    public static final RetCode PARAM_FAIL_SOLC_FILE_EMPTY = RetCode.mark(201261, "Solc js file cannot be empty");
    public static final RetCode PARAM_FAIL_SOLC_FILE_NAME_EMPTY = RetCode.mark(201262, "Solc js file name cannot be empty");
    public static final RetCode PARAM_FAIL_FILE_NAME_EXISTS = RetCode.mark(201263, "Solc js file name already exist");
    public static final RetCode PARAM_FAIL_FILE_NAME_NOT_EXISTS = RetCode.mark(201263, "Solc js file name not exist in db");
    public static final RetCode SAVE_SOLC_FILE_ERROR = RetCode.mark(201264, "Save solc js file error");
    public static final RetCode READ_SOLC_FILE_ERROR = RetCode.mark(201265, "read solc js file error, please check if file deleted");

    // chain governance
    public static final RetCode CHAIN_THRESHOLD_PARAM_ERROR = RetCode.mark(201301, "threshold must be greater than zero");
    public static final RetCode COMMITTEE_WEIGHT_PARAM_ERROR = RetCode.mark(201302, "committee weight must be greater than zero");
    public static final RetCode GOVERNANCE_ADDRESS_PARAM_ERROR = RetCode.mark(201303, "chain governance address cannot be blank");
    // event callback
    public static final RetCode GET_EVENT_CALLBACK_TIMEOUT_ERROR = RetCode.mark(201311, "get event callback fail for time out");
    public static final RetCode GET_EVENT_CALLBACK_ERROR = RetCode.mark(201312, "get event callback error");

    // v1.5.3
    public static final RetCode CNS_QUERY_FAIL = RetCode.mark(201321,"query cns info fail");
    public static final RetCode FUNC_PARAM_SIZE_NOT_MATCH = RetCode.mark(201322, "contract funcParam size not match with ABI");
    public static final RetCode FUNC_PARAM_BYTES_SIZE_NOT_MATCH = RetCode.mark(201323, "contract funcParam bytes array size not match");
    public static final RetCode FUNC_PARAM_BYTES_NOT_SUPPORT_HIGH_D = RetCode.mark(201324, "contract funcParam bytes array not support high dimensional array");



    /* classify common error of web3j*/
    // keystore
    public static final RetCode WEB3J_CREATE_KEY_PAIR_NULL = RetCode.mark(201501, "web3sdk create key pair fail and return null");
    public static final RetCode WEB3J_PEM_P12_MANAGER_GET_KEY_PAIR_ERROR = RetCode.mark(201502, "pem/p12 manager get key pair error for input params");
    public static final RetCode WEB3J_PEM_P12_MANAGER_DEPENDENCY_ERROR = RetCode.mark(201503, "pem/p12 manager get key pair error for bc dependency error");
    public static final RetCode REQUEST_SIGN_RETURN_ERROR = RetCode.mark(201504, "sign service return error");
    // transaction
    public static final RetCode TX_RECEIPT_CODE_ERROR = RetCode.mark(201510, "transaction receipt status return error");
    public static final RetCode CONTRACT_ABI_PARSE_JSON_ERROR = RetCode.mark(201511, "contract abi parse json error");
    public static final RetCode CALL_CONTRACT_IO_EXCEPTION = RetCode.mark(201512, "call contract error for io exception");
    public static final RetCode GET_TX_RECEIPT_EXEC_ERROR = RetCode.mark(201513, "get transaction receipt fail for exec");
    public static final RetCode GET_TX_RECEIPT_TIMEOUT_ERROR = RetCode.mark(201514, "get transaction receipt fail for time out");
    public static final RetCode TX_RECEIPT_OUTPUT_PARSE_JSON_FAIL = RetCode.mark(201515, "transaction receipt fail and parse output fail");
    public static final RetCode TX_RECEIPT_OUTPUT_NULL = RetCode.mark(201516, "transaction receipt fail and output is null");
    public static final RetCode CALL_CONTRACT_ERROR = RetCode.mark(201517, "call contract constant method fail");
    public static final RetCode GET_MESSAGE_HASH = RetCode.mark(201518, "get message's hash fail");
    // precompiled
    public static final RetCode GET_LIST_MANAGER_FAIL = RetCode.mark(201521, "get list of manager on chain fail");
    public static final RetCode CRUD_TABLE_KEY_LENGTH_ERROR = RetCode.mark(201522, "table key length error");
    public static final RetCode CRUD_PARSE_CONDITION_ENTRY_FIELD_JSON_ERROR = RetCode.mark(201523, "crud's param parse json error");
    public static final RetCode PRECOMPILED_COMMON_TRANSFER_JSON_FAIL = RetCode.mark(201524, "precompiled common transfer to json fail");

    /* java sdk missing web3sdk's retcode */
    public static final RetCode ALREADY_REMOVED_FROM_THE_GROUP = RetCode.mark(-51103, "The node already has been removed from the group");
    public static final RetCode ALREADY_EXISTS_IN_SEALER_LIST = RetCode.mark(-51104, "The node already exists in the sealerList");
    public static final RetCode ALREADY_EXISTS_IN_OBSERVER_LIST = RetCode.mark(-51105, "The node already exists in the observerList");
    //public static final RetCode ALREADY_EXISTS_IN_OBSERVER_LIST = RetCode.mark(51105, "The");



}
