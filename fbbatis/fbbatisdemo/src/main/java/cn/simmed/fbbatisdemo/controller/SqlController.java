package cn.simmed.fbbatisdemo.controller;

import cn.simmed.fbbatis.SQLCRUDService;
import cn.simmed.fbbatisdemo.entity.CrudHandle;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.stereotype.Controller;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.ResponseBody;

@Controller
@RequestMapping("/sql")
@Api(tags = "SQL执行器")
public class SqlController {

    @Autowired
    private SQLCRUDService sqlcrudService;

    @ResponseBody
    @ApiOperation(value = "crudManage", notes = "operate table by crud")
    @PostMapping("/crud")
    public Object crudManageControl(@RequestBody CrudHandle crudHandle) throws Exception {
       return sqlcrudService.exec(crudHandle.getGroupId(),crudHandle.getFromAddress(),crudHandle.getSql());
    }
}
