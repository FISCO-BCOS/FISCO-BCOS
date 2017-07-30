package com.dfs.entity;

public enum RetEnum {

    RET_SUCCESS(0,"成功"),
    RET_FAIL(1,"失败");

    private String name;
    private int code;

    private RetEnum(int code, String name){
        this.name = name;
        this.code = code;
    }

    public String getName() {
        return name;
    }

    public void setName(String name) {
        this.name = name;
    }

    public int getCode() {
        return code;
    }

    public void setCode(int code) {
        this.code = code;
    }

    public static RetEnum getEnumByCodeValue(int code){
        RetEnum[] allEnums = values();
        for(RetEnum enableStatus : allEnums){
            if(enableStatus.getCode()==code)
                return enableStatus;
        }
        return null;
    }
}
