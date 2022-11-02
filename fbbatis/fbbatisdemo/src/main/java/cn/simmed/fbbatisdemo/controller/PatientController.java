package cn.simmed.fbbatisdemo.controller;

import cn.simmed.fbbatis.QueryWrapper;
import cn.simmed.fbbatisdemo.entity.Patient;
import cn.simmed.fbbatisdemo.service.PatientService;
import com.alibaba.fastjson.JSONObject;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.ApiParam;
import org.fisco.bcos.sdk.BcosSDK;
import org.fisco.bcos.sdk.client.Client;
import org.fisco.bcos.sdk.contract.precompiled.crud.common.ConditionOperator;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.stereotype.Controller;
import org.springframework.web.bind.annotation.*;

import java.awt.*;
import java.util.HashMap;
import java.util.Map;

@Controller
@RequestMapping("/patient")
@Api(tags = "患者招募服务")
public class PatientController {


    @Autowired
    private PatientService paientService;

    @ResponseBody
    @GetMapping(value = "/create")
    @ApiOperation(value = "创建表",notes = "创建表")
    public Object create() throws ClassNotFoundException {
        return paientService.create(1,null);
    }


    @ResponseBody
    @PostMapping(value = "/insert")
    @ApiOperation(value = "插入",notes = "插入")
    public Object insert(Patient patient) throws Exception {
        return paientService.insert(1,null,patient);
    }


    @ResponseBody
    @PostMapping(value = "/update")
    @ApiOperation(value = "更新",notes = "更新")
    public Object update(String name,String updateJson) throws Exception {
        QueryWrapper<Patient> patientQuery=new QueryWrapper<Patient>();
        patientQuery.eq("patient_name",name);
        JSONObject  jsonObject = JSONObject.parseObject(updateJson);
        //json对象转Map
        Map<String,Object> map = (Map<String,Object>)jsonObject;
        return paientService.update(1,null,patientQuery,map);
    }


    @ResponseBody
    @PostMapping(value = "/delete")
    @ApiOperation(value = "删除",notes = "删除")
    public Object delete(String name) throws Exception {
        QueryWrapper<Patient> patientQuery=new QueryWrapper<Patient>();
        patientQuery.eq("patient_name",name);
        return paientService.remove(1,null,patientQuery);
    }


    @ResponseBody
    @PostMapping(value = "/select")
    @ApiOperation(value = "查询",notes = "查询")
    public Object select(String name) throws Exception {
        QueryWrapper<Patient> patientQuery=new QueryWrapper<Patient>();
        patientQuery.eq("patient_name",name);
        return paientService.select(1,patientQuery);
    }
}
