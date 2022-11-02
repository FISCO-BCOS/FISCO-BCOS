package cn.simmed.fbbatis;

import java.io.Serializable;
import java.util.Map;
import java.util.function.BiPredicate;

public interface Compare<Children, R> extends Serializable {

    /**
     * ignore
     */
    default Children eq(R column, String val) {
        return eq(true, column, val);
    }

    /**
     * 等于 =
     *
     * @param condition 执行条件
     * @param column    字段
     * @param val       值
     * @return children
     */
    Children eq(boolean condition, R column, String val);

    /**
     * ignore
     */
    default Children ne(R column, String val) {
        return ne(true, column, val);
    }

    /**
     * 不等于 &lt;&gt;
     *
     * @param condition 执行条件
     * @param column    字段
     * @param val       值
     * @return children
     */
    Children ne(boolean condition, R column, String val);

    /**
     * ignore
     */
    default Children gt(R column, String val) {
        return gt(true, column, val);
    }

    /**
     * 大于 &gt;
     *
     * @param condition 执行条件
     * @param column    字段
     * @param val       值
     * @return children
     */
    Children gt(boolean condition, R column, String val);

    /**
     * ignore
     */
    default Children ge(R column, String val) {
        return ge(true, column, val);
    }

    /**
     * 大于等于 &gt;=
     *
     * @param condition 执行条件
     * @param column    字段
     * @param val       值
     * @return children
     */
    Children ge(boolean condition, R column, String val);

    /**
     * ignore
     */
    default Children lt(R column, String val) {
        return lt(true, column, val);
    }

    /**
     * 小于 &lt;
     *
     * @param condition 执行条件
     * @param column    字段
     * @param val       值
     * @return children
     */
    Children lt(boolean condition, R column, String val);

    /**
     * ignore
     */
    default Children le(R column, String val) {
        return le(true, column, val);
    }

    /**
     * 小于等于 &lt;=
     *
     * @param condition 执行条件
     * @param column    字段
     * @param val       值
     * @return children
     */
    Children le(boolean condition, R column, String val);

    /**
     * ignore
     */
    default Children between(R column, String val1, String val2) {
        return between(true, column, val1, val2);
    }

    /**
     * BETWEEN 值1 AND 值2
     *
     * @param condition 执行条件
     * @param column    字段
     * @param val1      值1
     * @param val2      值2
     * @return children
     */
    Children between(boolean condition, R column, String val1, String val2);

    /**
     * ignore
     */
    default Children notBetween(R column, String val1, String val2) {
        return notBetween(true, column, val1, val2);
    }

    /**
     * NOT BETWEEN 值1 AND 值2
     *
     * @param condition 执行条件
     * @param column    字段
     * @param val1      值1
     * @param val2      值2
     * @return children
     */
    Children notBetween(boolean condition, R column, String val1, String val2);

    Children limit(int pageSize);

    Children limit(int pageIndex,int pageSize);
}
