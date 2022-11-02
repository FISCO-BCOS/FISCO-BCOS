package cn.simmed.fbbatisdemo.entity;

import io.swagger.annotations.ApiModelProperty;
import lombok.Data;

@Data
public class CrudHandle {
    @ApiModelProperty(value = "组ID")
    private int groupId;
    @ApiModelProperty(value = "sql")
    private String sql;
    @ApiModelProperty(value = "交易发起方")
    private String fromAddress;
}

