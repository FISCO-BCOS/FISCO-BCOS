package org.fisco.bcos.sdk.demo.perf.parallel;

import com.google.common.util.concurrent.RateLimiter;
import java.io.IOException;
import java.math.BigInteger;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.List;
import java.util.Random;
import java.util.concurrent.atomic.AtomicInteger;
import org.fisco.bcos.sdk.demo.contract.EquipmentPurchase;
import org.fisco.bcos.sdk.demo.perf.callback.ParallelOkCallback;
import org.fisco.bcos.sdk.demo.perf.collector.PerformanceCollector;
import org.fisco.bcos.sdk.demo.perf.model.DagTransferUser;
import org.fisco.bcos.sdk.demo.perf.model.DagUserInfo;
import org.fisco.bcos.sdk.model.TransactionReceipt;
import org.fisco.bcos.sdk.transaction.model.exception.ContractException;
import org.fisco.bcos.sdk.utils.ThreadPoolService;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class EquipmentPurchaseDemo {

    private static final Logger logger = LoggerFactory.getLogger(ParallelOkDemo.class);
    private static AtomicInteger sended = new AtomicInteger(0);
    private AtomicInteger getted = new AtomicInteger(0);

    private SimpleDateFormat dateFormat = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss");
    private final EquipmentPurchase equipmentPurchase;
    private final ThreadPoolService threadPoolService;
    private final PerformanceCollector collector;
    private final DagUserInfo dagUserInfo;

    public EquipmentPurchaseDemo(
            EquipmentPurchase equipmentPurchase,
            DagUserInfo dagUserInfo,
            ThreadPoolService threadPoolService) {
        this.threadPoolService = threadPoolService;
        this.equipmentPurchase = equipmentPurchase;
        this.dagUserInfo = dagUserInfo;
        this.collector = new PerformanceCollector();
    }

    public void veryTransferData(BigInteger qps) throws InterruptedException {
        RateLimiter rateLimiter = RateLimiter.create(qps.intValue());
        System.out.println("===================================================================");
        AtomicInteger verifyFailed = new AtomicInteger(0);
        AtomicInteger verifySuccess = new AtomicInteger(0);

        final List<DagTransferUser> userInfo = dagUserInfo.getUserList();
        int userSize = userInfo.size();
        for (int i = 0; i < userSize; i++) {
            rateLimiter.acquire();
            final int userIndex = i;
            threadPoolService
                    .getThreadPool()
                    .execute(
                            new Runnable() {
                                @Override
                                public void run() {
                                    try {
                                        String user = userInfo.get(userIndex).getUser();
                                        BigInteger balance = equipmentPurchase.getUserBalance(user);
                                        BigInteger localAmount =
                                                userInfo.get(userIndex).getAmount();
                                        if (localAmount.compareTo(balance) != 0) {
                                            logger.error(
                                                    "local balance is not the same as the remote, user: {}, local balance: {}, remote balance: {}",
                                                    user,
                                                    localAmount,
                                                    balance);
                                            verifyFailed.incrementAndGet();
                                        } else {
                                            verifySuccess.incrementAndGet();
                                        }
                                    } catch (ContractException exception) {
                                        verifyFailed.incrementAndGet();
                                        logger.error(
                                                "get remote balance failed, error info: "
                                                        + exception.getMessage());
                                    }
                                }
                            });
        }
        while (verifySuccess.get() + verifyFailed.get() < userSize) {
            Thread.sleep(40);
        }

        System.out.println("validation:");
        System.out.println(" \tuser count is " + userSize);
        System.out.println(" \tverify_success count is " + verifySuccess);
        System.out.println(" \tverify_failed count is " + verifyFailed);
    }

    public void userAdd(BigInteger userCount, BigInteger qps)
            throws InterruptedException, IOException {
        System.out.println("===================================================================");
        System.out.println("Start UserAdd test, count " + userCount);
        RateLimiter limiter = RateLimiter.create(qps.intValue());

        long currentSeconds = System.currentTimeMillis() / 1000L;
        Integer area = userCount.intValue() / 10;
        long startTime = System.currentTimeMillis();
        collector.setTotal(userCount.intValue());
        collector.setStartTimestamp(startTime);
        AtomicInteger sendFailed = new AtomicInteger(0);
        for (Integer i = 0; i < userCount.intValue(); i++) {
            final Integer index = i;
            limiter.acquire();
            threadPoolService
                    .getThreadPool()
                    .execute(
                            new Runnable() {
                                @Override
                                public void run() {
                                    // generate the user according to currentSeconds
                                    String user =
                                            Long.toHexString(currentSeconds)
                                                    + Integer.toHexString(index);
                                    BigInteger amount = new BigInteger("1000000000");
                                    DagTransferUser dtu = new DagTransferUser();
                                    dtu.setUser(user);
                                    dtu.setAmount(amount);
                                    ParallelOkCallback callback =
                                            new ParallelOkCallback(
                                                    collector,
                                                    dagUserInfo,
                                                    ParallelOkCallback.ADD_USER_CALLBACK);
                                    callback.setTimeout(0);
                                    callback.setUser(dtu);
                                    try {
                                        callback.recordStartTime();
                                        equipmentPurchase.deposit(user, amount, callback);
                                        int current = sended.incrementAndGet();

                                        if (current >= area && ((current % area) == 0)) {
                                            long elapsed = System.currentTimeMillis() - startTime;
                                            double sendSpeed = current / ((double) elapsed / 1000);
                                            System.out.println(
                                                    "Already sended: "
                                                            + current
                                                            + "/"
                                                            + userCount
                                                            + " transactions"
                                                            + ",QPS="
                                                            + sendSpeed);
                                        }

                                    } catch (Exception e) {
                                        logger.warn(
                                                "addUser failed, error info: {}", e.getMessage());
                                        sendFailed.incrementAndGet();
                                        TransactionReceipt receipt = new TransactionReceipt();
                                        receipt.setStatus("-1");
                                        receipt.setMessage(
                                                "userAdd failed, error info: " + e.getMessage());
                                        callback.onResponse(receipt);
                                    }
                                }
                            });
        }
        while (collector.getReceived().intValue() != userCount.intValue()) {
            logger.info(
                    " sendFailed: {}, received: {}, total: {}",
                    sendFailed.get(),
                    collector.getReceived().intValue(),
                    collector.getTotal());
            Thread.sleep(100);
        }
        dagUserInfo.setContractAddr(equipmentPurchase.getContractAddress());
        dagUserInfo.writeDagTransferUser();
        System.exit(0);
    }

    public void queryAccount(BigInteger qps) throws InterruptedException {
        final List<DagTransferUser> allUsers = dagUserInfo.getUserList();
        RateLimiter rateLimiter = RateLimiter.create(qps.intValue());
        AtomicInteger sent = new AtomicInteger(0);
        for (Integer i = 0; i < allUsers.size(); i++) {
            final Integer index = i;
            rateLimiter.acquire();
            threadPoolService
                    .getThreadPool()
                    .execute(
                            new Runnable() {
                                @Override
                                public void run() {
                                    try {
                                        BigInteger result =
                                                equipmentPurchase.getUserBalance(
                                                        allUsers.get(index).getUser());
                                        allUsers.get(index).setAmount(result);
                                        int all = sent.incrementAndGet();
                                        if (all >= allUsers.size()) {
                                            System.out.println(
                                                    dateFormat.format(new Date())
                                                            + " Query account finished");
                                        }
                                    } catch (ContractException exception) {
                                        logger.warn(
                                                "queryAccount for {} failed, error info: {}",
                                                allUsers.get(index).getUser(),
                                                exception.getMessage());
                                        System.exit(0);
                                    }
                                }
                            });
        }
        while (sent.get() < allUsers.size()) {
            Thread.sleep(50);
        }
    }

    public void userTransfer(BigInteger count, BigInteger qps)
            throws InterruptedException, IOException {
        System.out.println("Querying account info...");
        queryAccount(qps);
        System.out.println("Sending transfer transactions...");
        RateLimiter limiter = RateLimiter.create(qps.intValue());
        int division = count.intValue() / 10;
        long startTime = System.currentTimeMillis();
        collector.setStartTimestamp(startTime);
        collector.setTotal(count.intValue());
        AtomicInteger sendFailed = new AtomicInteger(0);
        for (Integer i = 0; i < count.intValue(); i++) {
            limiter.acquire();
            final int index = i;
            threadPoolService
                    .getThreadPool()
                    .execute(
                            new Runnable() {
                                @Override
                                public void run() {
                                    try {
                                        Random random = new Random();
                                        int r = random.nextInt(100);
                                        BigInteger amount = BigInteger.valueOf(r);

                                        ParallelOkCallback callback =
                                                new ParallelOkCallback(
                                                        collector,
                                                        dagUserInfo,
                                                        ParallelOkCallback.TRANS_CALLBACK);
                                        callback.setTimeout(0);
                                        DagTransferUser from = dagUserInfo.getFrom(index);

                                        callback.setFromUser(from);
                                        callback.setAmount(amount);
                                        callback.recordStartTime();
                                        equipmentPurchase.buyEquipment(
                                                from.getUser(), amount, callback);
                                        int current = sended.incrementAndGet();
                                        if (current >= division && ((current % division) == 0)) {
                                            long elapsed = System.currentTimeMillis() - startTime;
                                            double sendSpeed = current / ((double) elapsed / 1000);
                                            System.out.println(
                                                    "Already sent: "
                                                            + current
                                                            + "/"
                                                            + count
                                                            + " transactions"
                                                            + ",QPS="
                                                            + sendSpeed);
                                        }
                                    } catch (Exception e) {
                                        logger.error(
                                                "call transfer failed, error info: {}",
                                                e.getMessage());
                                        TransactionReceipt receipt = new TransactionReceipt();
                                        receipt.setStatus("-1");
                                        receipt.setMessage(
                                                "call transfer failed, error info: "
                                                        + e.getMessage());
                                        collector.onMessage(receipt, Long.valueOf(0));
                                        sendFailed.incrementAndGet();
                                    }
                                }
                            });
        }

        while (collector.getReceived().intValue() != count.intValue()) {
            Thread.sleep(3000);
            logger.info(
                    "userTransfer: sendFailed: {}, received: {}, total: {}",
                    sendFailed.get(),
                    collector.getReceived().intValue(),
                    collector.getTotal());
        }
        veryTransferData(qps);
        System.exit(0);
    }
}
