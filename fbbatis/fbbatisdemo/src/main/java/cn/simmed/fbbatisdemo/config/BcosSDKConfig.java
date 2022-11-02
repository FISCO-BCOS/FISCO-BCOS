package cn.simmed.fbbatisdemo.config;

import cn.simmed.fbbatis.Constants;
import io.netty.channel.ChannelHandlerContext;
import lombok.Data;
import lombok.extern.slf4j.Slf4j;
import org.fisco.bcos.sdk.BcosSDK;
import org.fisco.bcos.sdk.BcosSDKException;
import org.fisco.bcos.sdk.client.Client;
import org.fisco.bcos.sdk.config.ConfigOption;
import org.fisco.bcos.sdk.config.exceptions.ConfigException;
import org.fisco.bcos.sdk.config.model.AmopTopic;
import org.fisco.bcos.sdk.config.model.ConfigProperty;
import org.fisco.bcos.sdk.crypto.CryptoSuite;
import org.fisco.bcos.sdk.model.Message;
import org.fisco.bcos.sdk.model.NodeVersion;
import org.fisco.bcos.sdk.network.MsgHandler;
import org.springframework.boot.context.properties.ConfigurationProperties;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

@Data
@Slf4j
@Configuration
@ConfigurationProperties(prefix = "sdk")
public class BcosSDKConfig {
    public static String orgName = "fisco";
    public String certPath = "conf";
    public String useSMCrypto = "false";
    private List<Integer> groupIdList;
    /* use String in java sdk*/
    private String corePoolSize;
    private String maxPoolSize;
    private String queueCapacity;
    private Integer groupId = 1;
    /* use String in java sdk*/
    private String ip = "127.0.0.1";
    private String channelPort = "20200";

    // add channel disconnect
    public static boolean PEER_CONNECTED = true;
    static class Web3ChannelMsg implements MsgHandler {
        @Override
        public void onConnect(ChannelHandlerContext ctx) {
            PEER_CONNECTED = true;
            log.info("Web3ChannelMsg onConnect:{}, status:{}", ctx.channel().remoteAddress(), PEER_CONNECTED);
        }

        @Override
        public void onMessage(ChannelHandlerContext ctx, Message msg) {
            // not added in message handler, ignore this override
            log.info("Web3ChannelMsg onMessage:{}, status:{}", ctx.channel().remoteAddress(), PEER_CONNECTED);
        }

        @Override
        public void onDisconnect(ChannelHandlerContext ctx) {
            PEER_CONNECTED = false;
            log.error("Web3ChannelMsg onDisconnect:{}, status:{}", ctx.channel().remoteAddress(), PEER_CONNECTED);
        }
    }

    @Bean
    public BcosSDK getBcosSDK() throws ConfigException {
        log.info("start init ConfigProperty");
        // cert config, encrypt type
        Map<String, Object> cryptoMaterial = new HashMap<>();
        // cert use conf
        cryptoMaterial.put("certPath", certPath);
        cryptoMaterial.put("useSMCrypto", useSMCrypto);
        // user no need set this:cryptoMaterial.put("sslCryptoType", encryptType);
        log.info("init cert cryptoMaterial:{}, (using conf as cert path)", cryptoMaterial);

        // peers, default one node in front
        Map<String, Object> network = new HashMap<>();
        List<String> peers = new ArrayList<>();
        peers.add(ip + ":" + channelPort);
        network.put("peers", peers);
        log.info("init node network property :{}", peers);

        // thread pool config
        log.info("init thread pool property");
        Map<String, Object> threadPool = new HashMap<>();
        threadPool.put("channelProcessorThreadSize", corePoolSize);
        threadPool.put("receiptProcessorThreadSize", corePoolSize);
        threadPool.put("maxBlockingQueueSize", queueCapacity);
        log.info("init thread pool property:{}", threadPool);

        // init property
        ConfigProperty configProperty = new ConfigProperty();
        configProperty.setCryptoMaterial(cryptoMaterial);
        configProperty.setNetwork(network);
        configProperty.setThreadPool(threadPool);
        // init config option
        log.info("init configOption from configProperty");
        ConfigOption configOption = new ConfigOption(configProperty);
        // init bcosSDK
        log.info("init bcos sdk instance, please check sdk.log");
        BcosSDK bcosSDK = new BcosSDK(configOption);

        log.info("init client version");
        NodeVersion.ClientVersion version = bcosSDK.getGroupManagerService().getNodeVersion(ip + ":" + channelPort)
                .getNodeVersion();
        Constants.version = version.getVersion();
        Constants.chainId = version.getChainId();

        Web3ChannelMsg disconnectMsg = new Web3ChannelMsg();
        bcosSDK.getChannel().addConnectHandler(disconnectMsg);
        bcosSDK.getChannel().addDisconnectHandler(disconnectMsg);

        return bcosSDK;
    }

    /**
     * 覆盖EncryptType构造函数
     * @return
     */
    @Bean(name = "common")
    public CryptoSuite getCommonSuite(BcosSDK bcosSDK) {
        int encryptType = bcosSDK.getGroupManagerService().getCryptoType(ip + ":" + channelPort);
        log.info("getCommonSuite init encrypt type:{}", encryptType);
        return new CryptoSuite(encryptType);
    }

    @Bean(name = "rpcClient")
    public Client getRpcWeb3j(BcosSDK bcosSDK) {
        // init rpc client(web3j)
        Client rpcWeb3j = Client.build(bcosSDK.getChannel());
        log.info("get rpcWeb3j(only support rpc) client:{}", rpcWeb3j);
        return rpcWeb3j;
    }
}
