package cn.simmed.fbbatis;

import lombok.Data;
import org.fisco.bcos.sdk.contract.precompiled.crud.common.Condition;

@Data
public class AbstractWrapper<T, R, Children extends AbstractWrapper<T, R, Children>> implements Compare<Children, R> {
    protected final Children typedThis = (Children) this;
    private T entity;
    private Condition condition;

    public  AbstractWrapper(){
        condition=new Condition();
    }

    @Override
    public Children eq(R column, String val) {
        this.condition.EQ(column.toString(),val);
        return typedThis;
    }

    @Override
    public Children eq(boolean condition, R column, String val) {
        if (condition){
            this.condition.EQ(column.toString(),val);
        }
        return typedThis;
    }

    @Override
    public Children ne(R column, String val) {
        this.condition.NE(column.toString(),val);
        return typedThis;
    }

    @Override
    public Children ne(boolean condition, R column, String val) {
        if (condition){
            this.condition.NE(column.toString(),val);
        }
        return typedThis;
    }

    @Override
    public Children gt(R column, String val) {
        this.condition.GT(column.toString(),val);
        return typedThis;
    }

    @Override
    public Children gt(boolean condition, R column, String val) {
        if (condition){
            this.condition.GT(column.toString(),val);
        }
        return typedThis;
    }

    @Override
    public Children ge(R column, String val) {
        this.condition.GE(column.toString(),val);
        return typedThis;
    }

    @Override
    public Children ge(boolean condition, R column, String val) {
        if (condition){
            this.condition.GE(column.toString(),val);
        }
        return typedThis;
    }

    @Override
    public Children lt(R column, String val) {
        this.condition.LT(column.toString(),val);
        return typedThis;
    }

    @Override
    public Children lt(boolean condition, R column, String val) {
        if (condition){
            this.condition.LT(column.toString(),val);
        }
        return typedThis;
    }

    @Override
    public Children le(R column, String val) {
        this.condition.LE(column.toString(),val);
        return typedThis;
    }

    @Override
    public Children le(boolean condition, R column, String val) {
        if (condition){
            this.condition.LE(column.toString(),val);
        }
        return typedThis;
    }

    @Override
    public Children between(R column, String val1, String val2) {
        this.condition.GE(column.toString(),val1);
        this.condition.LE(column.toString(),val2);
        return typedThis;
    }

    @Override
    public Children between(boolean condition, R column, String val1, String val2) {
        if (condition){
            this.condition.GE(column.toString(),val1);
            this.condition.LE(column.toString(),val2);
        }
        return typedThis;
    }

    @Override
    public Children notBetween(R column, String val1, String val2) {
        this.condition.LT(column.toString(),val1);
        this.condition.GT(column.toString(),val2);
        return typedThis;
    }

    @Override
    public Children notBetween(boolean condition, R column, String val1, String val2) {
        if (condition){
            this.condition.LT(column.toString(),val1);
            this.condition.GT(column.toString(),val2);
        }
        return typedThis;
    }

    @Override
    public Children limit(int pageSize) {
        this.condition.Limit(pageSize);
        return typedThis;
    }

    @Override
    public Children limit(int pageIndex, int pageSize) {
        this.condition.Limit((pageIndex-1)*pageSize,pageSize);
        return typedThis;
    }
}
