package cn.simmed.fbbatis;

import lombok.Data;

@Data
public class BaseResponse {
    private int code;
    private String message;
    private Object data;

    public BaseResponse() {}

    public BaseResponse(int code) {
        this.code = code;
    }

    public BaseResponse(RetCode rsc) {
        this.code = rsc.getCode();
        this.message = rsc.getMessage();
    }

    /**
     * constructor.
     *
     * @param rsc not null
     * @param obj not null
     */
    public BaseResponse(RetCode rsc, Object obj) {
        this.code = rsc.getCode();
        this.message = rsc.getMessage();
        this.data = obj;
    }

    /**
     * constructor.
     *
     * @param code not null
     * @param message not null
     * @param obj not null
     */
    public BaseResponse(int code, String message, Object obj) {
        this.code = code;
        this.message = message;
        this.data = obj;
    }

    public BaseResponse(int code, String message) {
        this.code = code;
        this.message = message;
    }

    @Override
    public String toString() {
        return JsonUtils.objToString(this);
    }

}
