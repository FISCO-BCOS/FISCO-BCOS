package cn.simmed.fbbatisdemo.controller;

import cn.simmed.fbbatis.QueryWrapper;
import cn.simmed.fbbatisdemo.entity.ConfigInfo;
import cn.simmed.fbbatisdemo.entity.Patient;
import cn.simmed.fbbatisdemo.service.ConfigService;
import cn.simmed.fbbatisdemo.service.PatientService;
import com.alibaba.fastjson.JSONObject;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.stereotype.Controller;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.ResponseBody;

import java.util.Map;

@Controller
@RequestMapping("/config")
@Api(tags = "配置服务")
public class ConfigController {


    @Autowired
    private ConfigService configService;

    @ResponseBody
    @GetMapping(value = "/create")
    @ApiOperation(value = "创建表",notes = "创建表")
    public Object create() throws ClassNotFoundException {
        return configService.create(1,null);
    }


    @ResponseBody
    @PostMapping(value = "/save")
    @ApiOperation(value = "保存",notes = "保存")
    public Object insert(ConfigInfo config) throws Exception {
        return configService.save(1,null,config);
    }


    @ResponseBody
    @PostMapping(value = "/update")
    @ApiOperation(value = "更新",notes = "更新")
    public Object update(String key,String value) throws Exception {
        JSONObject jsonObject = JSONObject.parseObject(value);
        //json对象转Map
        Map<String,Object> map = (Map<String,Object>)jsonObject;
        return configService.updateByKey(1,null,key,map);
    }


    @ResponseBody
    @PostMapping(value = "/remove")
    @ApiOperation(value = "删除",notes = "删除")
    public Object remove(String key) throws Exception {
        return configService.removeByKey(1,null,key);
    }


    @ResponseBody
    @PostMapping(value = "/get")
    @ApiOperation(value = "查找",notes = "查找")
    public Object get(String key) throws Exception {
        return configService.getByKey(1,key);
    }
}