package cn.simmed.fbbatis;

import lombok.extern.slf4j.Slf4j;
import org.fisco.bcos.sdk.model.TransactionReceipt;
import org.fisco.bcos.sdk.utils.Numeric;

import java.util.Optional;

@Slf4j
public class CommonUtils {
    public static void processReceiptHexNumber(TransactionReceipt receipt) {
        log.info("sendMessage. receipt:[{}]", JsonUtils.toJSONString(receipt));
        if (receipt == null) {
            return;
        }
        String gasUsed = Optional.ofNullable(receipt.getGasUsed()).orElse("0");
        String blockNumber = Optional.ofNullable(receipt.getBlockNumber()).orElse("0");
        receipt.setGasUsed(Numeric.toBigInt(gasUsed).toString(10));
        receipt.setBlockNumber(Numeric.toBigInt(blockNumber).toString(10));
    }

}
