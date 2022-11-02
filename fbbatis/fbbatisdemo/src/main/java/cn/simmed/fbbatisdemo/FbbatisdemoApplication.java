package cn.simmed.fbbatisdemo;

import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.context.annotation.ComponentScan;

@SpringBootApplication(scanBasePackages = {"cn.simmed.fbbatisdemo", "cn.simmed.fbbatis"})
public class FbbatisdemoApplication {

    public static void main(String[] args) {
        SpringApplication.run(FbbatisdemoApplication.class, args);
    }

}
