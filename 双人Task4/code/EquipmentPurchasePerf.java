package org.fisco.bcos.sdk.demo.perf;

import java.io.IOException;
import java.math.BigInteger;
import java.net.URL;
import org.fisco.bcos.sdk.BcosSDK;
import org.fisco.bcos.sdk.client.Client;
import org.fisco.bcos.sdk.demo.contract.EquipmentPurchase;
import org.fisco.bcos.sdk.demo.perf.model.DagUserInfo;
import org.fisco.bcos.sdk.demo.perf.parallel.EquipmentPurchaseDemo;
import org.fisco.bcos.sdk.model.ConstantConfig;
import org.fisco.bcos.sdk.transaction.model.exception.ContractException;
import org.fisco.bcos.sdk.utils.ThreadPoolService;

public class EquipmentPurchasePerf {

    private static Client client;
    private static DagUserInfo dagUserInfo = new DagUserInfo();

    public static void Usage() {
        System.out.println(" Usage:");
        System.out.println("===== EquipmentPurchasePerf test===========");
        System.out.println(
                " \t java -cp 'conf/:lib/*:apps/*' org.fisco.bcos.sdk.demo.perf.EquipmentPurchasePerf [EquipmentPurchasePerf] [groupID] [deposit] [count] [tps] [file].");
        System.out.println(
                " \t java -cp 'conf/:lib/*:apps/*' org.fisco.bcos.sdk.demo.perf.EquipmentPurchasePerf [EquipmentPurchasePerf] [groupID] [buyEquipment] [count] [tps] [file].");
    }

    public static void main(String[] args)
            throws ContractException, IOException, InterruptedException {
        try {
            String configFileName = ConstantConfig.CONFIG_FILE_NAME;
            URL configUrl =
                    EquipmentPurchasePerf.class.getClassLoader().getResource(configFileName);
            if (configUrl == null) {
                System.out.println("The configFile " + configFileName + " doesn't exist!");
                return;
            }
            if (args.length < 6) {
                Usage();
                return;
            }
            String perfType = args[0];
            Integer groupId = Integer.valueOf(args[1]);
            String command = args[2];
            Integer count = Integer.valueOf(args[3]);
            Integer qps = Integer.valueOf(args[4]);
            String userFile = args[5];

            String configFile = configUrl.getPath();
            BcosSDK sdk = BcosSDK.build(configFile);
            client = sdk.getClient(Integer.valueOf(groupId));
            dagUserInfo.setFile(userFile);
            ThreadPoolService threadPoolService =
                    new ThreadPoolService(
                            "EquipmentPurchasePerf",
                            sdk.getConfig().getThreadPoolConfig().getMaxBlockingQueueSize());

            if (perfType.compareToIgnoreCase("equipmentPurchase") == 0) {
                EquipmentPurchasePerf(groupId, command, count, qps, threadPoolService);
            } else {
                System.out.println(
                        "invalid perf option: "
                                + perfType
                                + ", only support equipmentPurchase/precompiled now");
                Usage();
            }
        } catch (Exception e) {
            System.out.println("EquipmentPurchasePerf test failed, error info: " + e.getMessage());
            System.exit(0);
        }
    }

    public static void EquipmentPurchasePerf(
            Integer groupId,
            String command,
            Integer count,
            Integer qps,
            ThreadPoolService threadPoolService)
            throws IOException, InterruptedException, ContractException {
        System.out.println(
                "====== EquipmentPurchase trans, count: "
                        + count
                        + ", qps:"
                        + qps
                        + ", groupId: "
                        + groupId);
        EquipmentPurchase equipmentPurchase;
        EquipmentPurchaseDemo equipmentPurchaseDemo;
        switch (command) {
            case "deposit":
                // deploy EquipmentPurchase
                equipmentPurchase =
                        EquipmentPurchase.deploy(
                                client, client.getCryptoSuite().getCryptoKeyPair());
                // enable parallel
                equipmentPurchase.enableParallel();
                System.out.println(
                        "====== EquipmentPurchase userAdd, deploy success, address: "
                                + equipmentPurchase.getContractAddress());
                equipmentPurchaseDemo =
                        new EquipmentPurchaseDemo(
                                equipmentPurchase, dagUserInfo, threadPoolService);
                equipmentPurchaseDemo.userAdd(BigInteger.valueOf(count), BigInteger.valueOf(qps));
                break;
            case "buyEquipment":
                dagUserInfo.loadDagTransferUser();
                equipmentPurchase =
                        EquipmentPurchase.load(
                                dagUserInfo.getContractAddr(),
                                client,
                                client.getCryptoSuite().getCryptoKeyPair());
                System.out.println(
                        "====== EquipmentPurchase trans, load success, address: "
                                + equipmentPurchase.getContractAddress());
                equipmentPurchaseDemo =
                        new EquipmentPurchaseDemo(
                                equipmentPurchase, dagUserInfo, threadPoolService);
                equipmentPurchaseDemo.userTransfer(
                        BigInteger.valueOf(count), BigInteger.valueOf(qps));
                break;

            default:
                System.out.println("invalid command: " + command);
                Usage();
                break;
        }
    }
}
