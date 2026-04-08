#include "doubao.h"

String answer_list[10];
uint8_t answer_list_num = 0;

// 向 answer_list 添加新的对话记录
void AddToList(String new_text) {
 String temp_text = new_text;
    temp_text.replace("\n", "");
    if (answer_list_num >= 9) {
      for (int i = 0; i < 9; i++) {
        answer_list[i] = answer_list[i + 1];
      }
      answer_list[9] = "";
      answer_list_num = 9;
    }
    answer_list[answer_list_num] = temp_text;
    answer_list_num++;
}

// 向豆包云服务发送问题
String POSTtoDoubao(String* answerlist, int listnum, const char* apiKey, const char* endpointId, const String& roleSystem) {
  Serial.println("[POSTtoDoubao]");
  String answer;

  HTTPClient http;
  http.begin("https://ark.cn-beijing.volces.com/api/v3/chat/completions");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + String(apiKey));

  DynamicJsonDocument requestJson(5120);
  requestJson["model"] = endpointId;
  JsonArray list = requestJson.createNestedArray("messages");

  JsonObject item = list.createNestedObject();
  item["role"] = "system";
  item["content"] = roleSystem;

  for (int i = 0; i < listnum; i += 2) {
    item = list.createNestedObject();
    item["role"] = "user";
    item["content"] = answerlist[i];
    Serial.print("answer user: ");
    Serial.println(answerlist[i]);
    if (listnum > 1 && i != listnum - 1) {
      if (answerlist[i + 1] != "") {
        item = list.createNestedObject();
        item["role"] = "assistant";
        item["content"] = answerlist[i + 1];
      }
      Serial.print("answer assistant: ");
      Serial.println(answerlist[i + 1]);
    }
  }

  requestJson["stream"] = false;
  String requestBody;
  serializeJson(requestJson, requestBody);
  Serial.print("payload: ");
  Serial.println(requestBody);

  int httpResponseCode = http.POST(requestBody);

  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println("HTTP Response Code: " + String(httpResponseCode));
    Serial.println("Response: " + response);
    DynamicJsonDocument doc(1024);

    // 解包
    deserializeJson(doc, response);
    String content = doc["choices"][0]["message"]["content"];
    Serial.println("Doubao Response:");
    Serial.println(content);
    answer = content;
  } else {
    Serial.println("Error on HTTP request");
    return answer = "对不起，我没理解，请再说一遍。";
  }

  http.end();
  return answer;
}

String Doubao(String text, const char* apiKey, const char* endpointId, const String& roleSystem)
{
    AddToList(text);
    String answer = POSTtoDoubao(answer_list, answer_list_num, apiKey, endpointId, roleSystem);
    AddToList(answer);
    return answer;
}

String WrapText(String input, int max_length) { 
  String output = "";
  int start = 0;
  uint length = input.length();
  uint n = 0; 
  for (int i = 0; i < length; i++) {
    unsigned char firstByte = input[i];
    if ((firstByte & 0x80) != 0) {
      if ((firstByte >= 0xE0) && (firstByte <= 0xEF)) { 
        unsigned char secondByte = input[++i];
        unsigned char thirdByte = input[++i];
        if (
          (secondByte >= 0x80) && 
          (secondByte <= 0xBF) && 
          (thirdByte >= 0x80) && 
          (thirdByte <= 0xBF)
          ) 
        {
          output += input.substring(i - 2, i + 1);
          n = n + 2;
          if (n > max_length) {
            output += "\n";
            n = 0;
          }
        }
      }
    } 
    else 
    {
      output += input[i];
      n++;
      if (n > max_length) {
        output += "\n";
        n = 0;
      }
    }
  }
  return output;
}