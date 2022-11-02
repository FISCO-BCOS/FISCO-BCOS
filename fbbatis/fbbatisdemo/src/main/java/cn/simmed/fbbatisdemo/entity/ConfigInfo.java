package cn.simmed.fbbatisdemo.entity;

import cn.simmed.fbbatis.Column;
import cn.simmed.fbbatis.PrimaryKey;
import cn.simmed.fbbatis.TableName;
import lombok.Data;

@TableName(name = "tb_config",title = "配置表")
@PrimaryKey(name = "name")
@Data
public class ConfigInfo {

    @Column(name = "name",title = "配置名")
    private String name;

    @Column(name = "value",title = "配置值")
    private String value;

    @Column(name = "category",title = "分组")
    private String category;
}
