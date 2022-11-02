package cn.simmed.fbbatis;

import lombok.Data;
import org.fisco.bcos.sdk.contract.precompiled.crud.common.Condition;

import java.util.Map;
import java.util.function.BiPredicate;
@Data
public class QueryWrapper<T> extends AbstractWrapper<T, String, QueryWrapper<T>> {
    public QueryWrapper() {
        this(null);
    }

    public QueryWrapper(T entity) {
        super.setEntity(entity);
    }

}
