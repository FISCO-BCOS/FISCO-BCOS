package com.dfs.entity;

import com.alibaba.fastjson.JSONObject;

/**
 * Created by yanze on 2016/12/15.
 */
public class DataJson {

    private long total;
    private JSONObject[] items;

    public JSONObject[] getItems() {
        return items;
    }

    public void setItems(JSONObject[] items) {
        this.items = items;
    }

    public long getTotal() {
        return total;
    }

    public void setTotal(long total) {
        this.total = total;
    }
}
