package cn.simmed.fbbatis;

import lombok.extern.slf4j.Slf4j;
import org.fisco.bcos.sdk.contract.precompiled.crud.common.Condition;
import org.fisco.bcos.sdk.contract.precompiled.crud.common.Entry;
import org.fisco.bcos.sdk.model.PrecompiledConstant;
import org.fisco.bcos.sdk.model.TransactionReceipt;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.context.annotation.Bean;
import org.springframework.stereotype.Service;

import java.time.Duration;
import java.time.Instant;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;

@Slf4j
@Service
public class SQLCRUDService {
    @Autowired
    private  PrecompiledService precompiledService;
    public Object exec(Integer groupId,String from,String sql) throws Exception {
        String[] sqlParams = sql.trim().split(" ");
        switch (sqlParams[0].toLowerCase()) {
            case "create":
                return createTable(groupId, from, sql);
            case "desc":
                return desc(groupId, sql);
            case "select":
                return select(groupId, sql);
            case "insert":
                return insert(groupId, from, sql);
            case "update":
                return update(groupId, from, sql);
            case "delete":
                return remove(groupId, from, sql);
            default:
                log.debug("end crudManageControl no such crud operation");
                return new BaseResponse(ConstantCode.PARAM_FAIL_SQL_ERROR,
                        "no such crud operation");
        }
    }


    public Object createTable(int groupId, String fromAddress, String sql) {
        Instant startTime = Instant.now();
        log.info("start createTable startTime:{}, groupId:{},fromAddress:{},sql:{}",
                startTime.toEpochMilli(), groupId, fromAddress, sql);
        Table table = new Table();
        try {
            log.debug("start parseCreateTable.");
            CRUDParseUtils.parseCreateTable(sql, table);
            log.debug("end parseCreateTable. table:{}, key:{}, keyField:{}, values:{}",
                    table.getTableName(), table.getKey(), table.getKeyFieldName(), table.getValueFields());
        } catch (Exception e) {
            log.error("parseCreateTable. table:{},exception:{}", table, e);
            CRUDParseUtils.invalidSymbol(sql);
            return new BaseResponse(PrecompiledUtils.CRUD_SQL_ERROR,
                    "Could not parse SQL statement.");
        }
        // CRUDParseUtils.checkTableParams(table);
        TransactionReceipt result = precompiledService.createTable(groupId, fromAddress, table);
        log.info("end createTable useTime:{} res:{}",
                Duration.between(startTime, Instant.now()).toMillis(), result);
        return result;

    }

    // check table name exist by desc(tableName)
    public Object desc(int groupId, String sql) {
        Instant startTime = Instant.now();
        log.info("start descTable startTime:{}, groupId:{},sql:{}", startTime.toEpochMilli(),
                groupId, sql);
        String[] sqlParams = sql.split(" ");
        // "desc t_demo"
        String tableName = sqlParams[1];

        if (tableName.length() > PrecompiledUtils.SYS_TABLE_KEY_MAX_LENGTH) {
            return new BaseResponse(PrecompiledUtils.CRUD_SQL_ERROR,
                    "The table name length is greater than "
                            + PrecompiledUtils.SYS_TABLE_KEY_MAX_LENGTH + ".",
                    "The table name length is greater than "
                            + PrecompiledUtils.SYS_TABLE_KEY_MAX_LENGTH + ".");
        }
        CRUDParseUtils.invalidSymbol(tableName);
        if (tableName.endsWith(";")) {
            tableName = tableName.substring(0, tableName.length() - 1);
        }
        try {
            List<Map<String, String>> table = precompiledService.desc(groupId, tableName);
            log.info("end descTable useTime:{} res:{}",
                    Duration.between(startTime, Instant.now()).toMillis(), table);
            return new BaseResponse(ConstantCode.RET_SUCCESS, table);
        } catch (Exception e) {
            log.error("descTable.exception:[] ", e);
            return new BaseResponse(ConstantCode.FAIL_TABLE_NOT_EXISTS, e.getMessage());
        }
    }

    public Object select(int groupId, String sql) throws Exception {
        Instant startTime = Instant.now();
        log.info("start select startTime:{}, groupId:{},sql:{}", startTime.toEpochMilli(), groupId,
                sql);
        Table table = new Table();
        Condition conditions = new Condition();
        List<String> selectColumns = new ArrayList<>();

        try { // 转化select语句
            log.debug("start parseSelect. sql:{}", sql);
            CRUDParseUtils.parseSelect(sql, table, conditions, selectColumns);
            log.debug("end parseSelect. table:{}, conditions:{}, selectColumns:{}", table,
                    conditions, selectColumns);
        } catch (Exception e) {
            log.error("parseSelect Error exception:[]", e);
            CRUDParseUtils.invalidSymbol(sql);
            return new BaseResponse(PrecompiledUtils.CRUD_SQL_ERROR, "Could not parse SQL statement.");
        }

        List<Map<String, String>> descTable = null;
        try {
            descTable = precompiledService.desc(groupId, table.getTableName());
        } catch (Exception e) {
            log.error("select in descTable Error exception:[]", e);
            return new BaseResponse(ConstantCode.FAIL_TABLE_NOT_EXISTS, "Table not exists ");
        }
        String keyField = descTable.get(0).get(PrecompiledConstant.KEY_FIELD_NAME);
        table.setKey(keyField);
        CRUDParseUtils.handleKey(table, conditions);

        List<Map<String, String>> result = precompiledService.select(groupId, table, conditions);
        log.info("end select useTime:{} res:{}",
                Duration.between(startTime, Instant.now()).toMillis(), result);
        if (result.size() == 0) {
            return new BaseResponse(ConstantCode.RET_SUCCESS_EMPTY_LIST,
                    ConstantCode.CRUD_EMPTY_SET);
        }
        result = CRUDParseUtils.filterSystemColum(result);
        if ("*".equals(selectColumns.get(0))) {
            selectColumns.clear();
            selectColumns.add(keyField);
            String[] valueArr = descTable.get(0).get(PrecompiledConstant.VALUE_FIELD_NAME).split(",");
            selectColumns.addAll(Arrays.asList(valueArr));
            result = CRUDParseUtils.getSelectedColumn(selectColumns, result);
            log.info("end select. result:{}", result);
            return new BaseResponse(ConstantCode.RET_SUCCESS, result);
        } else {
            List<Map<String, String>> selectedResult =
                    CRUDParseUtils.getSelectedColumn(selectColumns, result);
            log.info("end select. selectedResult:{}", selectedResult);
            return new BaseResponse(ConstantCode.RET_SUCCESS, selectedResult);
        }

    }

    public Object insert(int groupId, String fromAddress, String sql) throws Exception {
        Instant startTime = Instant.now();
        log.info("start insert startTime:{}, groupId:{},fromAddress:{},sql:{}",
                startTime.toEpochMilli(), groupId, fromAddress, sql);
        Table table = new Table();
        Entry entry = new Entry();
        List<Map<String, String>>  descTable = null;

        String tableName = CRUDParseUtils.parseInsertedTableName(sql);
        descTable = precompiledService.desc(groupId, tableName);
        log.debug(
                "insert, tableName: {}, descTable: {}", tableName, descTable.get(0).toString());
        CRUDParseUtils.parseInsert(sql, table, entry, descTable.get(0));
        String keyName = descTable.get(0).get(PrecompiledConstant.KEY_FIELD_NAME);
        String keyValue = entry.getFieldNameToValue().get(keyName);
        log.debug(
                "fieldNameToValue: {}, keyName: {}, keyValue: {}", entry.getFieldNameToValue(), keyName, keyValue);
        if (keyValue == null) {
            log.error("Please insert the key field '" + keyName + "'.");
            throw new FrontException("Please insert the key field '" + keyName + "'.");
        }
        // table primary key
        table.setKey(keyValue);
        TransactionReceipt insertResult = precompiledService.insert(groupId, fromAddress, table, entry);
        return insertResult;

    }
    public Object update(int groupId, String fromAddress, String sql) throws Exception {
        Instant startTime = Instant.now();
        log.info("start update startTime:{}, groupId:{},fromAddress:{},sql:{}",
                startTime.toEpochMilli(), groupId, fromAddress, sql);
        Table table = new Table();
        Entry entry = new Entry();
        Condition conditions = new Condition();

        try {
            log.debug("start parseUpdate. sql:{}", sql);
            CRUDParseUtils.parseUpdate(sql, table, entry, conditions);
            log.debug("end parseUpdate. table:{}, entry:{}, conditions:{}", table, entry,
                    conditions);
        } catch (Exception e) {
            log.error("parseUpdate error exception:[]", e);
            CRUDParseUtils.invalidSymbol(sql);
            return new BaseResponse(PrecompiledUtils.CRUD_SQL_ERROR, "Could not parse SQL statement.");
        }

        String tableName = table.getTableName();
        List<Map<String, String>> descTable = null;
        try {
            descTable = precompiledService.desc(groupId, tableName);
        } catch (Exception e) {
            log.error("updateTable Error exception:[]", e);
            return new BaseResponse(ConstantCode.FAIL_TABLE_NOT_EXISTS, "Table not exists");
        }

        String keyName = descTable.get(0).get(PrecompiledConstant.KEY_FIELD_NAME);
        if (entry.getFieldNameToValue().containsKey(keyName)) {
            return new BaseResponse(PrecompiledUtils.CRUD_SQL_ERROR,
                    "Please don't set the key field '" + keyName + "'.",
                    "Please don't set the key field '" + keyName + "'.");
        }
        table.setKey(keyName);
        CRUDParseUtils.handleKey(table, conditions);
        TransactionReceipt updateResult = precompiledService.update(groupId, fromAddress, table, entry, conditions);
        log.info("end update useTime:{} updateResult:{}",
                Duration.between(startTime, Instant.now()).toMillis(), updateResult);
        return updateResult;
    }
    public Object remove(int groupId, String fromAddress, String sql) {
        Instant startTime = Instant.now();
        log.info("start remove startTime:{}, groupId:{},fromAddress:{},sql:{}",
                startTime.toEpochMilli(), groupId, fromAddress, sql);
        Table table = new Table();
        Condition conditions = new Condition();

        try {
            log.debug("start parseRemove. sql:{}", sql);
            CRUDParseUtils.parseRemove(sql, table, conditions);
            log.debug("end parseRemove. table:{}, conditions:{}", table, conditions);
        } catch (Exception e) {
            log.error("parseRemove Error exception:[]", e);
            CRUDParseUtils.invalidSymbol(sql);
            return new BaseResponse(PrecompiledUtils.CRUD_SQL_ERROR, "Could not parse SQL statement.");
        }

        List<Map<String, String>> descTable = null;
        try {
            descTable = precompiledService.desc(groupId, table.getTableName());
        } catch (Exception e) {
            log.error("removeTable Error exception:[]", e);
            return new BaseResponse(ConstantCode.FAIL_TABLE_NOT_EXISTS, "Table not exists");
        }
        table.setKey(descTable.get(0).get(PrecompiledConstant.KEY_FIELD_NAME));
        CRUDParseUtils.handleKey(table, conditions);
        TransactionReceipt removeResult = precompiledService.remove(groupId, fromAddress, table, conditions);
        log.info("end remove useTime:{} removeResult:{}",
                Duration.between(startTime, Instant.now()).toMillis(), removeResult);
        return removeResult;

    }
}
