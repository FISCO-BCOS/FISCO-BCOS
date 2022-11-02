package cn.simmed.fbbatis;

import lombok.extern.slf4j.Slf4j;
import org.fisco.bcos.sdk.BcosSDK;
import org.fisco.bcos.sdk.BcosSDKException;
import org.fisco.bcos.sdk.client.Client;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.stereotype.Service;

@Slf4j
@Service
public class Web3ApiService {
    @Autowired
    private BcosSDK bcosSDK;

    public Client getWeb3j(Integer groupId) {
        Client web3j;
        try {
            web3j= bcosSDK.getClient(groupId);
        } catch (BcosSDKException e) {
            String errorMsg = e.getMessage();
            log.error("bcosSDK getClient failed: {}", errorMsg);
            // check client error type
            if (errorMsg.contains("available peers")) {
                log.error("no available node to connect to");
                throw new FrontException(ConstantCode.SYSTEM_ERROR_NODE_INACTIVE.getCode(),
                        "no available node to connect to");
            }
            if (errorMsg.contains("existence of group")) {
                log.error("group: {} of the connected node not exist!", groupId);
                throw new FrontException(ConstantCode.SYSTEM_ERROR_WEB3J_NULL.getCode(),
                        "group: " + groupId + " of the connected node not exist!");
            }
            if (errorMsg.contains("no peers set up the group")) {
                log.error("no peers belong to this group: {}!", groupId);
                throw new FrontException(ConstantCode.SYSTEM_ERROR_NO_NODE_IN_GROUP.getCode(),
                        "no peers belong to this group: " + groupId);
            }
            throw new FrontException(String.valueOf(ConstantCode.WEB3J_CLIENT_IS_NULL));
            // refresh group list
            // getGroupList();
        }
        return web3j;
    }
}
