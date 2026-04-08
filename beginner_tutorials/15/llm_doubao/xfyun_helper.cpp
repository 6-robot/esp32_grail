#include "xfyun_helper.h"

using namespace websockets;
WebsocketsClient client_iat;
String text_iat = "";

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

void setup_ntp_client() {
  timeClient.begin();
  timeClient.setTimeOffset(0);
}

String unixTimeToGMTString(time_t unixTime) {
  char buffer[80];
  struct tm timeinfo;
  gmtime_r(&unixTime, &timeinfo);
  strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S GMT", &timeinfo);
  return String(buffer);
}

String getDateTime() {
  timeClient.update();
  unsigned long epochTime = timeClient.getEpochTime();
  Serial.print("Epoch Time: ");
  Serial.println(epochTime);
  String timeString = unixTimeToGMTString(epochTime);
  Serial.println(timeString);
  return timeString;
}

//转换日期格式
String formatDateForURL(String dateString) {
  dateString.replace(" ", "+");
  dateString.replace(",", "%2C");
  dateString.replace(":", "%3A");
  return dateString;
}

void xfyun_init()
{
  // 从互联网同步时间
  setup_ntp_client();
  getDateTime();
  Serial.println("[xfyun_iat] 对时完毕");
  // 语音识别 IAT 回调函数
  client_iat.onMessage([&](WebsocketsMessage message) {
    Serial.print("[xfyun_iat] 回调消息: ");
    Serial.println(message.data());
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, message.data());
    if (error) {
      Serial.print(F("[xfyun_iat] Json 解包失败！"));
      Serial.println(error.f_str());
      return;
    }
    JsonArray ws = doc["data"]["result"]["ws"];
    for (JsonObject word : ws) {
      int bg = word["bg"];
      const char* w = word["cw"][0]["w"];
      text_iat += w;
    }
    if (doc["data"]["status"] == 2) {  //结束标志
      Serial.print("[xfyun_iat] 识别结果：");
      Serial.println(text_iat);
    }
  });
}

//构造讯飞云 Websocket URL 地址
String xfyun_url(const char* Secret, const char* Key, String request, String host) 
{
  String timeString = getDateTime();
  String signature_origin = "host: " + host;
  signature_origin += "\n";
  signature_origin += "date: ";
  signature_origin += timeString;
  signature_origin += "\n";
  signature_origin += "GET " + request + " HTTP/1.1";

  // 计算 HMAC-SHA256
  unsigned char hmacResult[32];
  mbedtls_md_context_t ctx;
  mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 1);
  mbedtls_md_hmac_starts(&ctx, (const unsigned char*)Secret, strlen(Secret));
  mbedtls_md_hmac_update(&ctx, (const unsigned char*)signature_origin.c_str(), signature_origin.length());
  mbedtls_md_hmac_finish(&ctx, hmacResult);
  mbedtls_md_free(&ctx);

  // 对结果进行 Base64 编码
  String base64Result = base64::encode(hmacResult, 32);

  String authorization_origin = "api_key=\"";
  authorization_origin += Key;
  authorization_origin += "\", algorithm=\"hmac-sha256\", headers=\"host date request-line\", signature=\"";
  authorization_origin += base64Result;
  authorization_origin += "\"";

  String authorization = base64::encode(authorization_origin);

  String url = "ws://" + host + request;
  url += "?authorization=";
  url += authorization;
  url += "&date=";
  url += formatDateForURL(timeString);
  url += "&host=" + host;
  return url;
}

//向讯飞云 IAT 发送音频数据
void xfyun_iat(int16_t* record_data, int data_length, const char* appID, const char* apiSecret, const char* apiKey) 
{
  text_iat = "";
  uint8_t status = 0;
  int dataSize = 1280 * 8;
  int audioDataSize = data_length * sizeof(int16_t);
  uint lan = (audioDataSize) / dataSize;
  uint lan_end = (audioDataSize) % dataSize;
  if (lan_end > 0) {
    lan++;
  }

  // 连接讯飞云 Websocket
  String host_url = xfyun_url(apiSecret, apiKey, "/v2/iat", "ws-api.xfyun.cn");
  Serial.println("[xfyun_iat] 开始连接 IAT 服务器 ...");
  bool connected = client_iat.connect(host_url);
  if (connected) {
    Serial.println("[xfyun_iat] 连接 IAT 服务器 成功!");
  } else {
    Serial.println("[xfyun_iat] 失败！无法连接到 IAT 服务器！");
  }

  //将音频数据拆解成小包发送
  for (int i = 0; i < lan; i++) {

    if (i == (lan - 1)) {
      status = 2;
    }
    if (status == 0) {
      String input = "{";
      String app_id = appID;
      input += "\"common\":{ \"app_id\":\"" + app_id + "\" },";
      input += "\"business\":{\"domain\": \"iat\", \"language\": \"zh_cn\", \"accent\": \"mandarin\", \"vinfo\":1,\"vad_eos\":10000},";
      input += "\"data\":{\"status\": 0, \"format\": \"audio/L16;rate=16000\",\"encoding\": \"raw\",\"audio\":\"";
      String base64audioString = base64::encode((uint8_t*)record_data, dataSize);
      input += base64audioString;
      input += "\"}}";
      Serial.printf("input: %d , status: %d \n", i, status);
      client_iat.send(input);
      status = 1;
    } else if (status == 1) {
      String input = "{";
      input += "\"data\":{\"status\": 1, \"format\": \"audio/L16;rate=16000\",\"encoding\": \"raw\",\"audio\":\"";
      String base64audioString = base64::encode((uint8_t*)record_data + (i * dataSize), dataSize);
      input += base64audioString;
      input += "\"}}";
      client_iat.send(input);
    } else if (status == 2) {
      if (lan_end == 0) {
        String input = "{";
        input += "\"data\":{\"status\": 2, \"format\": \"audio/L16;rate=16000\",\"encoding\": \"raw\",\"audio\":\"";
        String base64audioString = base64::encode((uint8_t*)record_data + (i * dataSize), dataSize);
        input += base64audioString;
        input += "\"}}";
        Serial.printf("input: %d , status: %d \n", i, status);
        client_iat.send(input);
      }
      if (lan_end > 0) {
        String input = "{";
        input += "\"data\":{\"status\": 2, \"format\": \"audio/L16;rate=16000\",\"encoding\": \"raw\",\"audio\":\"";

        String base64audioString = base64::encode((uint8_t*)record_data + (i * dataSize), lan_end);

        input += base64audioString;
        input += "\"}}";
        Serial.printf("input: %d , status: %d \n", i, status);
        client_iat.send(input);
      }
    }
    delay(30);
  }
}

String xfyun_get_result()
{
    if (client_iat.available()) {
      client_iat.poll();
      int text_len = text_iat.length();
      while(text_len > 0)
      {
        // 多等一会，看看是不是识别结果还没获取完毕
        delay(100);
        client_iat.poll();
        int new_len = text_iat.length();
        if(new_len == text_len)
        {
          break;
        }
        text_len = new_len;
      }
    }
    String result = text_iat;
    text_iat.clear();
    return result;
}