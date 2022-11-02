package cn.simmed.fbbatis;

import com.google.common.reflect.TypeToken;
import org.fisco.bcos.sdk.contract.precompiled.crud.common.Condition;
import org.fisco.bcos.sdk.contract.precompiled.crud.common.Entry;
import org.springframework.beans.factory.annotation.Autowired;

import java.lang.reflect.Field;
import java.lang.reflect.Type;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

public class DataSet<T> {

    private final TypeToken<T> typeToken = new TypeToken<T>(getClass()) { };
    private final Type type = typeToken.getType(); // or getRawType() to return Class<? super T>

    public Type getType() {
        return type;
    }

    @Autowired
    public SQLCRUDService sqlcrudService;

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
                if(!keyFieldName.equals(annotation.name())){
                    valueFields.add(annotation.name());
                }
            }
        }
        Table table=new Table();
        table.setTableName(tableName);
        table.setKey(keyFieldName);
        table.setKeyFieldName(keyFieldName);
        table.setValueFields(valueFields);
        return  precompiledService.createTable(groupId,fromAddress,table);
    }

    public Object save(int groupId, String fromAddress,T entity) throws Exception {
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
        }
        String keyFieldName="";
        if(tClass.isAnnotationPresent(PrimaryKey.class)){
            PrimaryKey primaryKeyField = tClass.getAnnotation(PrimaryKey.class);
            keyFieldName=primaryKeyField.name();
            table.setKeyFieldName(keyFieldName);
        }
        // 获取所有字段
        List<String> valueFields=new ArrayList<>();
        for(Field f : tClass.getDeclaredFields()){
            if(f.isAnnotationPresent(Column.class)){
                Column annotation = f.getAnnotation(Column.class);
                if(!annotation.name().equals(keyFieldName)){
                    valueFields.add(annotation.name());
                    Field field = entity.getClass().getDeclaredField(f.getName());
                    field.setAccessible(true);
                    entry.getFieldNameToValue().put(annotation.name(),field.get(entity).toString());
                }
                else{
                    Field field = entity.getClass().getDeclaredField(f.getName());
                    field.setAccessible(true);
                    table.setKey(field.get(entity).toString());
                }
            }
        }
        table.setValueFields(valueFields);
        return precompiledService.insert(groupId,fromAddress,table,entry);
    }

    public Object updateByKey(int groupId, String fromAddress, String key,Map<String, Object> columnMap) throws Exception{
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
        }
        String keyFieldName="";
        if(tClass.isAnnotationPresent(PrimaryKey.class)){
            PrimaryKey primaryKeyField = tClass.getAnnotation(PrimaryKey.class);
            keyFieldName=primaryKeyField.name();
            table.setKeyFieldName(keyFieldName);
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
        table.setKey(key);
        condition.EQ(keyFieldName,key);
        table.setValueFields(valueFields);
        return precompiledService.update(groupId,fromAddress,table,entry,condition);
    }

    public Object removeByKey(int groupId, String fromAddress,String key) throws Exception{
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
        }
        String keyFieldName="";
        if(tClass.isAnnotationPresent(PrimaryKey.class)){
            PrimaryKey primaryKeyField = tClass.getAnnotation(PrimaryKey.class);
            keyFieldName=primaryKeyField.name();
            table.setKeyFieldName(keyFieldName);
        }
        table.setKey(key);
        condition.EQ(keyFieldName,key);
        return precompiledService.remove(groupId,fromAddress,table,condition);
    }

    public Object getByKey(int groupId,String key) throws Exception {
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
        }
        String keyFieldName="";
        if(tClass.isAnnotationPresent(PrimaryKey.class)){
            PrimaryKey primaryKeyField = tClass.getAnnotation(PrimaryKey.class);
            keyFieldName=primaryKeyField.name();
            table.setKeyFieldName(keyFieldName);
        }
        // 获取所有字段
        List<String> valueFields=new ArrayList<>();
        for(Field f : tClass.getDeclaredFields()){
            if(f.isAnnotationPresent(Column.class)){
                Column annotation = f.getAnnotation(Column.class);
                if(!keyFieldName.equals(annotation.name())){
                    valueFields.add(annotation.name());
                }
            }
        }
        table.setValueFields(valueFields);
        table.setKey(key);
        condition.EQ(keyFieldName,key);
        return precompiledService.select(groupId,table,condition);
    }
}
