package cn.simmed.fbbatis;

import com.fasterxml.jackson.annotation.JsonInclude;
import com.fasterxml.jackson.core.JsonParser;
import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.*;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

import java.io.IOException;
import java.text.SimpleDateFormat;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.function.Supplier;

@Slf4j
public class JsonUtils {
    private static final String STANDARD_FORMAT = "yyyy-MM-dd HH:mm:ss";
    /**
     * 设置一些通用的属性
     */
    private static final ThreadLocal<ObjectMapper> OBJECT_MAPPER = ThreadLocal.withInitial(() -> {
        ObjectMapper objectMapper = new ObjectMapper();
        // 如果存在未知属性，则忽略不报错
        objectMapper.configure(DeserializationFeature.FAIL_ON_UNKNOWN_PROPERTIES, false);
        objectMapper.configure(SerializationFeature.FAIL_ON_EMPTY_BEANS, false);
        // 允许key没有双引号
        objectMapper.configure(JsonParser.Feature.ALLOW_UNQUOTED_FIELD_NAMES, true);
        // 允许key有单引号
        objectMapper.configure(JsonParser.Feature.ALLOW_SINGLE_QUOTES, true);
        // 属性值为null的不参与序列化
        // objectMapper.setSerializationInclusion(JsonInclude.Include.NON_NULL);
        objectMapper.setSerializationInclusion(JsonInclude.Include.ALWAYS);
        // timestamp
        objectMapper.configure(SerializationFeature.WRITE_DATES_AS_TIMESTAMPS, false);
        // date format
        objectMapper.setDateFormat(new SimpleDateFormat(STANDARD_FORMAT));
        return objectMapper;
    });

    public static String toJSONString(Object obj) {
        return obj != null ? toJSONString(obj, () -> "") : "";
    }

    public static String toJSONString(Object obj, Supplier<String> defaultSupplier) {
        try {
            return obj != null ? OBJECT_MAPPER.get().writeValueAsString(obj) : defaultSupplier.get();
        } catch (Throwable e) {
            log.error(String.format("toJSONString %s", obj != null ? obj.toString() : "null"), e);
        }
        return defaultSupplier.get();
    }

    public static <T> T toJavaObject(String value, Class<T> tClass) {
        return StringUtils.isNotBlank(value) ? toJavaObject(value, tClass, () -> null) : null;
    }

    public static <T> T toJavaObject(Object obj, Class<T> tClass) {
        return obj != null ? toJavaObject(toJSONString(obj), tClass, () -> null) : null;
    }

    public static <T> T toJavaObject(String value, Class<T> tClass, Supplier<T> defaultSupplier) {
        try {
            if (StringUtils.isBlank(value)) {
                return defaultSupplier.get();
            }
            return OBJECT_MAPPER.get().readValue(value, tClass);
        } catch (Throwable e) {
            log.error(String.format("toJavaObject exception: \n %s\n %s", value, tClass), e);
        }
        return defaultSupplier.get();
    }

    public static <T> List<T> toJavaObjectList(String value, Class<T> tClass) {
        return StringUtils.isNotBlank(value) ? toJavaObjectList(value, tClass, () -> null) : null;
    }

    public static <T> List<T> toJavaObjectList(Object obj, Class<T> tClass) {
        return obj != null ? toJavaObjectList(toJSONString(obj), tClass, () -> null) : null;
    }

    public static <T> List<T> toJavaObjectList(String value, Class<T> tClass, Supplier<List<T>> defaultSupplier) {
        try {
            if (StringUtils.isBlank(value)) {
                return defaultSupplier.get();
            }
            JavaType javaType = OBJECT_MAPPER.get().getTypeFactory().constructParametricType(List.class, tClass);
            return OBJECT_MAPPER.get().readValue(value, javaType);
        } catch (Throwable e) {
            log.error(String.format("toJavaObjectList exception \n%s\n%s", value, tClass), e);
        }
        return defaultSupplier.get();
    }

    // 简单地直接用json复制或者转换(Cloneable)
    public static <T> T jsonCopy(Object obj, Class<T> tClass) {
        return obj != null ? toJavaObject(toJSONString(obj), tClass) : null;
    }

    public static Map<String, Object> toMap(String value) {
        return StringUtils.isNotBlank(value) ? toMap(value, () -> null) : null;
    }

    public static Map<String, Object> toMap(Object value) {
        return value != null ? toMap(value, () -> null) : null;
    }

    public static Map<String, Object> toMap(Object value, Supplier<Map<String, Object>> defaultSupplier) {
        if (value == null) {
            return defaultSupplier.get();
        }
        try {
            if (value instanceof Map) {
                return (Map<String, Object>) value;
            }
        } catch (Exception e) {
            log.error("fail to convert" + toJSONString(value), e);
        }
        return toMap(toJSONString(value), defaultSupplier);
    }

    public static Map<String, Object> toMap(String value, Supplier<Map<String, Object>> defaultSupplier) {
        if (StringUtils.isBlank(value)) {
            return defaultSupplier.get();
        }
        try {
            return toJavaObject(value, LinkedHashMap.class);
        } catch (Exception e) {
            log.error(String.format("toMap exception\n%s", value), e);
        }
        return defaultSupplier.get();
    }


    public static List<Object> toList(String value) {
        return StringUtils.isNotBlank(value) ? toList(value, () -> null) : null;
    }

    public static List<Object> toList(Object value) {
        return value != null ? toList(value, () -> null) : null;
    }

    public static List<Object> toList(String value, Supplier<List<Object>> defaultSuppler) {
        if (StringUtils.isBlank(value)) {
            return defaultSuppler.get();
        }
        try {
            return toJavaObject(value, List.class);
        } catch (Exception e) {
            log.error("toList exception\n" + value, e);
        }
        return defaultSuppler.get();
    }

    public static List<Object> toList(Object value, Supplier<List<Object>> defaultSuppler) {
        if (value == null) {
            return defaultSuppler.get();
        }
        if (value instanceof List) {
            return (List<Object>) value;
        }
        return toList(toJSONString(value), defaultSuppler);
    }

    /* author: clk */

    public static boolean isJson(String str) {
        try {
            OBJECT_MAPPER.get().readTree(str);
            return true;
        } catch (IOException e) {
            return false;
        }
    }

    public static JsonNode stringToJsonNode(String str) {
        try {
            return OBJECT_MAPPER.get().readTree(str);
        } catch (IOException e) {
            log.error("Parse String to JsonNode error : {}", e.getMessage());
            return null;
        }
    }

    public static <T> String objToString(T obj) {
        if (obj == null) {
            return null;
        }
        try {
            return obj instanceof String ? (String) obj
                    : OBJECT_MAPPER.get().writerWithDefaultPrettyPrinter().writeValueAsString(obj);
        } catch (JsonProcessingException e) {
            log.error("Parse Object to String error : {}", e.getMessage());
            return null;
        }
    }

    @SuppressWarnings("unchecked")
    public static <T> T stringToObj(String str, Class<T> clazz) {
        if (StringUtils.isEmpty(str) || clazz == null) {
            return null;
        }
        try {
            return clazz.equals(String.class) ? (T) str : OBJECT_MAPPER.get().readValue(str, clazz);
        } catch (Exception e) {
            log.error("Parse String to Object error : {}", e.getMessage());
            return null;
        }
    }

    @SuppressWarnings("unchecked")
    public static <T> T stringToObj(String str, TypeReference<T> typeReference) {
        if (StringUtils.isEmpty(str) || typeReference == null) {
            return null;
        }
        try {
            return (T) (typeReference.getType().equals(String.class) ? str
                    : OBJECT_MAPPER.get().readValue(str, typeReference));
        } catch (IOException e) {
            log.error("Parse String to Object error", e);
            return null;
        }
    }

    public static <T> T stringToObj(String str, Class<?> collectionClazz,
                                    Class<?>... elementClazzes) {
        JavaType javaType = OBJECT_MAPPER.get().getTypeFactory()
                .constructParametricType(collectionClazz, elementClazzes);
        try {
            return OBJECT_MAPPER.get().readValue(str, javaType);
        } catch (IOException e) {
            log.error("Parse String to Object error : {}" + e.getMessage());
            return null;
        }
    }
}
