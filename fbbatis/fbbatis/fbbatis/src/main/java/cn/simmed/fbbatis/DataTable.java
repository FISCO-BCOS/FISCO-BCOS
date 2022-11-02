package cn.simmed.fbbatis;

import lombok.Data;
import lombok.extern.slf4j.Slf4j;
import org.fisco.bcos.sdk.client.Client;
import org.fisco.bcos.sdk.contract.precompiled.crud.TableCRUDService;
import org.fisco.bcos.sdk.contract.precompiled.crud.common.Condition;
import org.fisco.bcos.sdk.contract.precompiled.crud.common.ConditionOperator;
import org.fisco.bcos.sdk.contract.precompiled.crud.common.Entry;
import org.fisco.bcos.sdk.model.PrecompiledConstant;
import org.fisco.bcos.sdk.model.RetCode;
import org.fisco.bcos.sdk.model.TransactionReceipt;
import org.fisco.bcos.sdk.transaction.model.exception.ContractException;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.stereotype.Service;

import java.lang.reflect.Field;
import java.lang.reflect.Type;
import java.time.Duration;
import java.time.Instant;
import java.util.*;

import com.google.common.reflect.TypeToken;
import org.springframework.web.bind.annotation.RequestBody;

@Slf4j
@Service
@Data
public class DataTable<T> {

    private final TypeToken<T> typeToken = new TypeToken<T>(getClass()) { };
    private final Type type = typeToken.getType(); // or getRawType() to return Class<? super T>

    public Type getType() {
        return type;
    }

    @Autowired
    public SQLCRUDService sqlcrudService;

    private String KeyFeildName="table";
    @Autowired
    public PrecompiledService precompiledService;

    public Object create(int groupId, String fromAddress) throws ClassNotFoundException {
        // 获取类模板
        Type tType = getType();
        Class<T> tClass= (Class<T>) Class.forName(tType.getTypeName());
        String tableName="";
        if(tClass.isAnnotationPresent(TableName.class)){
            TableName tableNameField = (TableName)tClass.getAnnotation(TableName.class);
            tableName=tableNameField.name();
        }
        String keyFieldName="";
        if(tClass.isAnnotationPresent(PrimaryKey.class)){
            PrimaryKey primaryKeyField = (PrimaryKey)tClass.getAnnotation(PrimaryKey.class);
            keyFieldName=primaryKeyField.name();
        }
        List<String> valueFields=new ArrayList<>();
        // 获取所有字段
        for(Field f : tClass.getDeclaredFields()){
            if(f.isAnnotationPresent(Column.class)){
                Column annotation = f.getAnnotation(Column.class);
                valueFields.add(annotation.name());
            }
        }
        Table table=new Table();
        table.setTableName(tableName);
        table.setKey(KeyFeildName);
        table.setKeyFieldName(KeyFeildName);
        table.setValueFields(valueFields);
        return  precompiledService.createTable(groupId,fromAddress,table);
    }

    public Object insert(int groupId, String fromAddress,T entity) throws Exception {
        // 获取类模板
        Type tType = getType();
        Class<T> tClass= (Class<T>) Class.forName(tType.getTypeName());
        Table table=new Table();
        Entry entry=new Entry();
        String tableName="";
        if(tClass.isAnnotationPresent(TableName.class)){
            TableName tableNameField = (TableName)tClass.getAnnotation(TableName.class);
            tableName=tableNameField.name();
            table.setTableName(tableName);
            table.setKey(tableName);
            table.setKeyFieldName(KeyFeildName);
        }
        String keyFieldName="";
        if(tClass.isAnnotationPresent(PrimaryKey.class)){
            PrimaryKey primaryKeyField = tClass.getAnnotation(PrimaryKey.class);
            keyFieldName=primaryKeyField.name();
        }
        // 获取所有字段
        List<String> valueFields=new ArrayList<>();
        for(Field f : tClass.getDeclaredFields()){
            if(f.isAnnotationPresent(Column.class)){
                Column annotation = f.getAnnotation(Column.class);
                valueFields.add(annotation.name());
                Field field = entity.getClass().getDeclaredField(f.getName());
                field.setAccessible(true);
                entry.getFieldNameToValue().put(annotation.name(),field.get(entity).toString());
            }
        }
        table.setValueFields(valueFields);
        return precompiledService.insert(groupId,fromAddress,table,entry);
    }

    public Object update(int groupId,String fromAddress,QueryWrapper<T> queryWrapper,Map<String, Object> columnMap) throws Exception{
        // 获取类模板
        Type tType = getType();
        Class<T> tClass= (Class<T>) Class.forName(tType.getTypeName());
        Table table=new Table();
        Entry entry=new Entry();
        Condition condition = new Condition();
        String tableName="";
        if(tClass.isAnnotationPresent(TableName.class)){
            TableName tableNameField = (TableName)tClass.getAnnotation(TableName.class);
            tableName=tableNameField.name();
            table.setTableName(tableName);
            table.setKey(tableName);
            table.setKeyFieldName(KeyFeildName);
        }
        String keyFieldName="";
        if(tClass.isAnnotationPresent(PrimaryKey.class)){
            PrimaryKey primaryKeyField = tClass.getAnnotation(PrimaryKey.class);
            keyFieldName=primaryKeyField.name();
        }
        List<String> valueFields=new ArrayList<>();
        for(Field f : tClass.getDeclaredFields()){
            if(f.isAnnotationPresent(Column.class)){
                Column annotation = f.getAnnotation(Column.class);
                valueFields.add(annotation.name());
                if(columnMap.containsKey(annotation.name())){
                    entry.getFieldNameToValue().put(annotation.name(),columnMap.get(annotation.name()).toString());
                }
                if(columnMap.containsKey(f.getName())){
                    entry.getFieldNameToValue().put(annotation.name(),columnMap.get(f.getName()).toString());
                }
            }
        }
        table.setValueFields(valueFields);
        return precompiledService.update(groupId,fromAddress,table,entry,queryWrapper.getCondition());
    }

    public Object remove(int groupId, String fromAddress,QueryWrapper<T> queryWrapper) throws Exception{
        // 获取类模板
        Type tType = getType();
        Class<T> tClass= (Class<T>) Class.forName(tType.getTypeName());
        Table table=new Table();
        Entry entry=new Entry();
        Condition condition = new Condition();
        String tableName="";
        if(tClass.isAnnotationPresent(TableName.class)){
            TableName tableNameField = (TableName)tClass.getAnnotation(TableName.class);
            tableName=tableNameField.name();
            table.setTableName(tableName);
            table.setKey(tableName);
            table.setKeyFieldName(KeyFeildName);
        }
        String keyFieldName="";
        if(tClass.isAnnotationPresent(PrimaryKey.class)){
            PrimaryKey primaryKeyField = tClass.getAnnotation(PrimaryKey.class);
            keyFieldName=primaryKeyField.name();
        }
        return precompiledService.remove(groupId,fromAddress,table,queryWrapper.getCondition());
    }

    public Object select(int groupId,QueryWrapper<T> queryWrapper) throws Exception{
        return select(groupId,queryWrapper,0,0);
    }

    public Object select(int groupId,QueryWrapper<T> queryWrapper,int pageIndex,int pageSize) throws Exception {
        // 获取类模板
        Type tType = getType();
        Class<T> tClass= (Class<T>) Class.forName(tType.getTypeName());
        Table table=new Table();
        Entry entry=new Entry();
        Condition condition = new Condition();
        String tableName="";
        if(tClass.isAnnotationPresent(TableName.class)){
            TableName tableNameField = (TableName)tClass.getAnnotation(TableName.class);
            tableName=tableNameField.name();
            table.setTableName(tableName);
            table.setKey(tableName);
            table.setKeyFieldName(KeyFeildName);
        }
        String keyFieldName="";
        if(tClass.isAnnotationPresent(PrimaryKey.class)){
            PrimaryKey primaryKeyField = tClass.getAnnotation(PrimaryKey.class);
            keyFieldName=primaryKeyField.name();
        }
        // 获取所有字段
        List<String> valueFields=new ArrayList<>();
        for(Field f : tClass.getDeclaredFields()){
            if(f.isAnnotationPresent(Column.class)){
                Column annotation = f.getAnnotation(Column.class);
                valueFields.add(annotation.name());
            }
        }
        table.setValueFields(valueFields);
        if(pageSize>0){
            if(pageIndex>0){
                queryWrapper.limit(pageIndex,pageSize);
            }
            else{
                queryWrapper.limit(pageSize);
            }
        }
        return precompiledService.select(groupId,table,queryWrapper.getCondition());
    }
}
