#ifndef DOUBAO_H
#define DOUBAO_H
#include <ArduinoJson.h>
#include <HTTPClient.h>

String Doubao(String text, const char* apiKey, const char* endpointId, const String& roleSystem);
String WrapText(String input, int max_length);

#endif // DOUBAO_H