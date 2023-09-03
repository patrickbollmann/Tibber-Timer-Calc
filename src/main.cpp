#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

//constants
const char *ssid = "SSID_HERE";
const char *password = "PASSWORD_HERE";
const char *hostname = "Tibber-Timer-Calc";
const char* ntpServer = "pool.ntp.org";
const String TIBBER_API_KEY = "TIBBER_API_KEY_HERE";
const String TIBBER_API_ENDPOINT = "https://api.tibber.com/v1-beta/gql";

//functions declaration
String getHoursDifference(String start, String end);
String getHoursUntilBestPrice(DynamicJsonDocument doc);
String performTibberRequest();
String getMinutesOfCurrentHour();
String correctStartForRunningHour(String hours);

void setup()
{
  // put your setup code here, to run once:
  Serial.begin(115200);
  WiFi.hostname(hostname); // Set hostname
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(1000);
    Serial.println("Connecting...");
  }
}

void loop()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    String httpResult = performTibberRequest();
    //catch empty result
    if(httpResult == ""){
      Serial.println("Error: No result from Tibber API");
      return;
    }
    // Parse the JSON response
    DynamicJsonDocument doc(4096);
    deserializeJson(doc, httpResult);
    // Calculate the timer Setting
    String bestStartHour = getHoursUntilBestPrice(doc);
    //correct the calculation result for the running hour (because our washing machines timer only allows full hours)
    bestStartHour = correctStartForRunningHour(bestStartHour);
    //print the result
    Serial.println("Hours until best Price: " + bestStartHour);

    //------ FUTURE WORK ------
    //TODO: Write the result to E-Paper Display

    //sleep for 10 minutes
    //ESP.deepSleep(600000000);
  }

  delay(60000);
}
/**
 * Calculates the difference in hours between two dates in the format "2021-05-30T00:00:00Z".
 * Both dates could be one day apart.
 * 
 * @param start The start date in the format "2021-05-30T00:00:00Z".
 * @param end The end date in the format "2021-05-30T00:00:00Z".
 * @return The difference in hours between the two dates in HH format.
 */
String getHoursDifference(String start, String end)
{
  //get the number of hours between start and end. Both dates could be one day apart
  //start and end are in the format "2021-05-30T00:00:00Z"
  int hours = 0;
  int minutes = 0;
  int seconds = 0;
  int days = 0;
  int months = 0;
  int years = 0;
  sscanf(start.c_str(), "%d-%d-%dT%d:%d:%dZ", &years, &months, &days, &hours, &minutes, &seconds);
  int timeNowSeconds = seconds + minutes * 60 + hours * 3600 + days * 86400 + months * 2592000 + years * 31104000;
  sscanf(end.c_str(), "%d-%d-%dT%d:%d:%dZ", &years, &months, &days, &hours, &minutes, &seconds);
  int timeStartSeconds = seconds + minutes * 60 + hours * 3600 + days * 86400 + months * 2592000 + years * 31104000;
  int timeDiffSeconds = timeStartSeconds - timeNowSeconds;
  //get the result in HH format
  int timeDiffHours = timeDiffSeconds / 3600;
  return String(timeDiffHours);
}

/**
 * Calculates the number of hours until the best price for a subscription is available.
 * 
 * @param doc The JSON document containing the subscription price information.
 * @return The number of hours until the best price is available.
 */
String getHoursUntilBestPrice(DynamicJsonDocument doc){
  String timeNow = doc["data"]["viewer"]["homes"][0]["currentSubscription"]["priceInfo"]["current"]["startsAt"];
  DynamicJsonDocument dataToday(2048);
  DynamicJsonDocument dataTomorrow(2048);
  //get explicit data for today and tomorrow
  dataToday = doc["data"]["viewer"]["homes"][0]["currentSubscription"]["priceInfo"]["today"];
  dataTomorrow = doc["data"]["viewer"]["homes"][0]["currentSubscription"]["priceInfo"]["tomorrow"];

  // find the entry in dataToday with the lowest total price where the start time is after timeNow
  String timeStart = "";
  float price = 1000;
  for (int i = 0; i < dataToday.size(); i++)
  {
    if (dataToday[i]["total"].as<float>() < price && dataToday[i]["startsAt"].as<String>() > timeNow)
    {
      price = dataToday[i]["total"].as<float>();
      timeStart = dataToday[i]["startsAt"].as<String>();
    }
  }
  for (int i = 0; i < dataTomorrow.size(); i++)
  {
    if (dataTomorrow[i]["total"].as<float>() < price)
    {
      price = dataTomorrow[i]["total"].as<float>();
      timeStart = dataTomorrow[i]["startsAt"].as<String>();
    }
  }
  Serial.println("timeStart: " + timeStart);
  Serial.println("price: " + String(price));
  return getHoursDifference(timeNow, timeStart);
}

/**
 * @brief Performs a GraphQL request to Tibber API to get the current subscription price information.
 * 
 * @return String The response body as a String.
 */
String performTibberRequest(){
  String result = "";
  // Define the GraphQL query
  String query = "{\"query\":\"{viewer{homes{currentSubscription{priceInfo{current{startsAt}today{total startsAt}tomorrow{total startsAt}}}}}}\"}";

  // Create an HTTPClient object
  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();

  // Set the API endpoint URL
  http.begin(client, TIBBER_API_ENDPOINT);

  // Set the content type header
  http.addHeader("Content-Type", "application/json");

  // Set the Authorization header with the token value
  http.addHeader("Authorization", "Bearer " + TIBBER_API_KEY);

  // Send the POST request with the GraphQL query as the request body
  int httpCode = http.POST(query);

  // Check if the request was successful
  if (httpCode == HTTP_CODE_OK)
  {
    // Get the response body as a String
    result = http.getString();
  }
  else
  {
    Serial.println(httpCode);
    Serial.println("Error: " + http.errorToString(httpCode));
  }

  // Close the connection
  http.end();
  return result;
}

/**
 * Returns the minutes of the current hour using NTP time.
 * 
 * @return String containing the minutes of the current hour in two-digit format.
 */
String getMinutesOfCurrentHour()
{
  // Define NTP properties
  const long  gmtOffset_sec = 0;
  const int   daylightOffset_sec = 3600;

  // Define NTP client
  WiFiUDP ntpUDP;
  NTPClient timeClient(ntpUDP, ntpServer, gmtOffset_sec, daylightOffset_sec);

  // Initialize NTP client
  timeClient.begin();
  timeClient.update();

  // Get current time
  time_t now = timeClient.getEpochTime();
  struct tm* timeinfo = localtime(&now);

  // Get minutes of current hour
  char buffer[3];
  sprintf(buffer, "%02d", timeinfo->tm_min);
  String minutes = String(buffer);

  return minutes;
}

//the washing machine consumes the most energy in the first 45 mins of the process. The timer can only et by a full hour. Therefore, the start time should be corrected (by -1 hour) if the current time is after 40 mins into the current hour
//e.g. if the current time is xx:41 and the calculation returns 10:00, the start time should be 9:00 instead of 10:00 so that the most time of the washing process is covered by the cheapest price (full process is ca. 1:30h with first 45 mins energy intensive)
String correctStartForRunningHour(String hours){
  String minutes = getMinutesOfCurrentHour();
  if(minutes.toInt() > 40){
    return String((hours.toInt() - 1));
  }
  else{
    return hours;
  }
}