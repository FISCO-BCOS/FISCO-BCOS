package cn.simmed.fbbatis;

import com.fasterxml.jackson.core.JsonProcessingException;
import lombok.extern.slf4j.Slf4j;
import org.fisco.bcos.sdk.contract.precompiled.crud.TableCRUDService;
import org.fisco.bcos.sdk.contract.precompiled.crud.common.Condition;
import org.fisco.bcos.sdk.contract.precompiled.crud.common.Entry;
import org.fisco.bcos.sdk.model.PrecompiledConstant;
import org.fisco.bcos.sdk.model.RetCode;
import org.fisco.bcos.sdk.model.TransactionReceipt;
import org.fisco.bcos.sdk.transaction.codec.decode.ReceiptParser;
import org.fisco.bcos.sdk.transaction.model.exception.ContractException;
import org.fisco.bcos.sdk.utils.ObjectMapperFactory;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.stereotype.Service;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;

import static org.fisco.bcos.sdk.contract.precompiled.crud.CRUD.*;
import static org.fisco.bcos.sdk.contract.precompiled.crud.table.TableFactory.FUNC_CREATETABLE;

@Slf4j
@Service
public class PrecompiledService {

    @Autowired
    private Web3ApiService web3ApiService;

    @Autowired
    private KeyStoreService keyStoreService;

    @Autowired
    TransService transService;

    public TransactionReceipt createTable(int groupId, String userAddress, Table table) {
        List<Object> funcParams = new ArrayList<>();
        funcParams.add(table.getTableName());
        funcParams.add(table.getKey());
        String valueFieldsString = TableCRUDService.convertValueFieldsToString(table.getValueFields());
        funcParams.add(valueFieldsString);
        String contractAddress = PrecompiledCommonInfo.getAddress(PrecompiledTypes.TABLE_FACTORY);
        String abiStr = PrecompiledCommonInfo.getAbi(PrecompiledTypes.TABLE_FACTORY);
        TransactionReceipt receipt =
                (TransactionReceipt) transService.transHandleWithSign(groupId,
                        userAddress, contractAddress, abiStr, FUNC_CREATETABLE, funcParams);
        return receipt;
    }

    /**
     * CRUD: insert table through webase-sign
     */
    public TransactionReceipt insert(int groupId, String userAddress, Table table, Entry entry) {
        checkTableKeyLength(table);
        // trans
        String entryJsonStr;
        try {
            entryJsonStr =
                    ObjectMapperFactory.getObjectMapper().writeValueAsString(entry.getFieldNameToValue());
        } catch (JsonProcessingException e) {
            log.error("remove JsonProcessingException:[]", e);
            throw new FrontException(String.valueOf(ConstantCode.CRUD_PARSE_CONDITION_ENTRY_FIELD_JSON_ERROR));
        }
        List<Object> funcParams = new ArrayList<>();
        funcParams.add(table.getTableName());
        funcParams.add(table.getKey());
        funcParams.add(entryJsonStr);
        funcParams.add(table.getOptional());
        String contractAddress = PrecompiledCommonInfo.getAddress(PrecompiledTypes.CRUD);
        String abiStr = PrecompiledCommonInfo.getAbi(PrecompiledTypes.CRUD);
        TransactionReceipt receipt =
                (TransactionReceipt) transService.transHandleWithSign(groupId,
                        userAddress, contractAddress, abiStr, FUNC_INSERT, funcParams);
        return receipt;
    }

    /**
     * CRUD: update table through webase-sign
     */
    public TransactionReceipt update(int groupId, String userAddress, Table table, Entry entry,
                         Condition condition) {
        checkTableKeyLength(table);
        // trans
        String entryJsonStr, conditionStr;
        try {
            entryJsonStr =
                    ObjectMapperFactory.getObjectMapper().writeValueAsString(entry.getFieldNameToValue());
            conditionStr = ObjectMapperFactory.getObjectMapper()
                    .writeValueAsString(condition.getConditions());
        } catch (JsonProcessingException e) {
            log.error("update JsonProcessingException:[]", e);
            throw new FrontException(ConstantCode.CRUD_PARSE_CONDITION_ENTRY_FIELD_JSON_ERROR);
        }
        List<Object> funcParams = new ArrayList<>();
        funcParams.add(table.getTableName());
        funcParams.add(table.getKey());
        funcParams.add(entryJsonStr);
        funcParams.add(conditionStr);
        funcParams.add(table.getOptional());
        String contractAddress = PrecompiledCommonInfo.getAddress(PrecompiledTypes.CRUD);
        String abiStr = PrecompiledCommonInfo.getAbi(PrecompiledTypes.CRUD);
        TransactionReceipt receipt =
                (TransactionReceipt) transService.transHandleWithSign(groupId,
                        userAddress, contractAddress, abiStr, FUNC_UPDATE, funcParams);
        return receipt;
    }

    /**
     * CRUD: remove table through webase-sign
     */
    public TransactionReceipt remove(int groupId, String signUserId, Table table, Condition condition) {
        checkTableKeyLength(table);
        // trans
        String conditionStr;
        try {
            conditionStr = ObjectMapperFactory.getObjectMapper()
                    .writeValueAsString(condition.getConditions());
        } catch (JsonProcessingException e) {
            log.error("remove JsonProcessingException:[]", e);
            throw new FrontException(ConstantCode.CRUD_PARSE_CONDITION_ENTRY_FIELD_JSON_ERROR);
        }
        List<Object> funcParams = new ArrayList<>();
        funcParams.add(table.getTableName());
        funcParams.add(table.getKey());
        funcParams.add(conditionStr);
        funcParams.add(table.getOptional());
        String contractAddress = PrecompiledCommonInfo.getAddress(PrecompiledTypes.CRUD);
        String abiStr = PrecompiledCommonInfo.getAbi(PrecompiledTypes.CRUD);
        TransactionReceipt receipt =
                (TransactionReceipt) transService.transHandleWithSign(groupId,
                        signUserId, contractAddress, abiStr, FUNC_REMOVE, funcParams);
        return receipt;
    }

    public List<Map<String, String>> desc(int groupId, String tableName) throws Exception {
        TableCRUDService crudService = new TableCRUDService(web3ApiService.getWeb3j(groupId), keyStoreService.getCredentialsForQuery());
        List<Map<String, String>> descRes = crudService.desc(tableName);
        if (!CRUDParseUtils.checkTableExistence(descRes)) {
            throw new FrontException(ConstantCode.FAIL_TABLE_NOT_EXISTS);
        }
        return descRes;
    }

    private void checkTableKeyLength(Table table) {
        if (table.getKey().length() > PrecompiledConstant.TABLE_KEY_MAX_LENGTH) {
            throw new FrontException(ConstantCode.CRUD_TABLE_KEY_LENGTH_ERROR.getCode(),
                    "The value of the table key exceeds the maximum limit("
                            + PrecompiledConstant.TABLE_KEY_MAX_LENGTH + ").");
        }
    }

    public List<Map<String, String>> select(int groupId, Table table,
                                            Condition conditions) throws Exception {
        TableCRUDService crudService = new TableCRUDService(web3ApiService.getWeb3j(groupId),
                keyStoreService.getCredentialsForQuery());
        List<Map<String, String>> selectRes = crudService.select(table.getTableName(), table.getKey(), conditions);
        return selectRes;
    }
}
