package cn.simmed.fbbatis;

public class FrontException extends RuntimeException {

    private static final long serialVersionUID = 1L;
    private RetCode retCode;
    /**
     * detail message
     */
    private String detail;

    public FrontException(RetCode retCode) {
        super(retCode.getMessage());
        this.retCode = retCode;
    }

    public FrontException(RetCode retCode, String data) {
        super(retCode.getMessage());
        this.retCode = retCode;
        this.detail = data;
    }

    public FrontException(int code, String msg) {
        super(msg);
        this.retCode = new RetCode(code, msg);
    }

    public FrontException(int code, String msg, String data) {
        super(msg);
        this.retCode = new RetCode(code, msg);
        this.detail = data;
    }

    public FrontException(String msg) {
        super(msg);
    }

    public RetCode getRetCode() {
        return retCode;
    }

    public String getDetail() {
        return detail;
    }
}
