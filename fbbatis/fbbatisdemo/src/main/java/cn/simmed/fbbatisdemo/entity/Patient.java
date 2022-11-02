package cn.simmed.fbbatisdemo.entity;

import cn.simmed.fbbatis.Column;
import cn.simmed.fbbatis.PrimaryKey;
import cn.simmed.fbbatis.TableName;
import lombok.Data;

@TableName(name = "tb_patient",title = "患者表合约")
@PrimaryKey(name = "phone")
@Data
public class Patient {

    @Column(name = "phone",title = "手机号")
    private String phone;

    @Column(name = "patient_name",title = "患者名称")
    private String patientName;

    @Column(name = "email",title = "邮箱")
    private String email;

    @Column(name = "group",title = "组")
    private int group;
}