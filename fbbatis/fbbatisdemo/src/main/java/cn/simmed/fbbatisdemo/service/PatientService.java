package cn.simmed.fbbatisdemo.service;

import cn.simmed.fbbatis.DataTable;
import cn.simmed.fbbatisdemo.config.BcosSDKConfig;
import cn.simmed.fbbatisdemo.entity.Patient;
import org.fisco.bcos.sdk.BcosSDK;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.stereotype.Service;

import javax.swing.*;

@Service
public class PatientService extends DataTable<Patient> {

}
