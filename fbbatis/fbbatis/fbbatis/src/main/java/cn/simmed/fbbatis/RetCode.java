package cn.simmed.fbbatis;

import lombok.Data;

@Data
public class RetCode {
    private Integer code;
    private String message;

    public RetCode() {}

    public RetCode(int code, String message) {
        this.code = code;
        this.message = message;
    }

    public static RetCode mark(int code, String message) {
        return new RetCode(code, message);
    }

    public static RetCode mark(Integer code) {
        return new RetCode(code, null);
    }

    @Override
    public String toString() {
        return "{" + "\"code\":" + code + ", \"msg\":\"" + message + "\"}";
    }
}
