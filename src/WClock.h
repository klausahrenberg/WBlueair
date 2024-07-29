#ifndef __WCLOCK_H__
#define __WCLOCK_H__

#include "Arduino.h"
#ifdef ESP8266
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#elif ESP32
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#endif
#include <NTPClient.h>
#include <WiFiUdp.h>

#include "TimeLib.h"
#include "WDevice.h"
#include "WNetwork.h"

const char* DEFAULT_NTP_SERVER = "pool.ntp.org";
const char* DEFAULT_TIME_ZONE_SERVER = "http://worldtimeapi.org/api/ip";
const byte STD_MONTH = 0;
const byte STD_WEEK = 1;
const byte STD_WEEKDAY = 2;
const byte STD_HOUR = 3;
const byte DST_MONTH = 4;
const byte DST_WEEK = 5;
const byte DST_WEEKDAY = 6;
const byte DST_HOUR = 7;
const byte* DEFAULT_DST_RULE = (const byte[]){10, 0, 0, 3, 3, 0, 0, 2};
const byte* DEFAULT_NIGHT_SWITCHES = (const byte[]){22, 00, 7, 00};

class WClock : public WDevice {
 public:
  typedef std::function<void(void)> THandlerFunction;

  WClock(WNetwork* network, bool supportNightMode)
      : WDevice(network, "clock", "clock", DEVICE_TYPE_TEXT_DISPLAY) {
    setMainDevice(false);
    setVisibility(MQTT);
    this->ntpServer = network->settings()->setString("ntpServer", DEFAULT_NTP_SERVER);
    this->ntpServer->readOnly(true);
    this->ntpServer->visibility(MQTT);
    this->addProperty(ntpServer);
    this->useTimeZoneServer = network->settings()->setBoolean("useTimeZoneServer", true);
    this->useTimeZoneServer->readOnly(true);
    this->useTimeZoneServer->visibility(NONE);
    this->addProperty(useTimeZoneServer);
    this->timeZoneServer = network->settings()->setString("timeZoneServer", DEFAULT_TIME_ZONE_SERVER);
    this->timeZoneServer->readOnly(true);
    this->timeZoneServer->visibility(this->useTimeZoneServer->asBool() ? MQTT : NONE);
    // this->ntpServer->setVisibility(MQTT);
    this->addProperty(timeZoneServer);
    _epochTimeFormatted = WProps::createStringProperty("epochTimeFormatted", "epochTimeFormatted");
    _epochTimeFormatted->readOnly(true);
    _epochTimeFormatted->onValueRequest([this]() { updateFormattedTime(); });
    this->addProperty(_epochTimeFormatted);
    this->validTime = WProps::createOnOffProperty("validTime", "validTime");
    this->validTime->asBool(false);
    this->validTime->readOnly(true);
    this->addProperty(validTime);
    if (this->useTimeZoneServer->asBool()) {
      this->timeZone = WProps::createStringProperty("timezone", "timeZone");
      this->timeZone->readOnly(true);
      this->addProperty(timeZone);
    } else {
      this->timeZone = nullptr;
    }
    this->rawOffset = WProps::createIntegerProperty("raw_offset", "rawOffset");
    this->rawOffset->asInt(3600);
    this->rawOffset->visibility(NONE);
    network->settings()->add(this->rawOffset);
    this->rawOffset->readOnly(true);
    this->addProperty(rawOffset);
    this->dstOffset = WProps::createIntegerProperty("dst_offset", "dstOffset");
    this->dstOffset->asInt(3600);
    this->dstOffset->visibility(NONE);
    network->settings()->add(this->dstOffset);
    this->dstOffset->readOnly(true);
    this->addProperty(dstOffset);
    this->useDaySavingTimes = network->settings()->setBoolean("useDaySavingTimes", false);
    this->useDaySavingTimes->visibility(NONE);
    this->dstRule = network->settings()->setByteArray("dstRule", 8, DEFAULT_DST_RULE);
    // HtmlPages
    WPage* configPage = new WPage(network, this->id(), "Configure clock");
    configPage->onPrintPage(std::bind(&WClock::printConfigPage, this, std::placeholders::_1));
    configPage->onSubmitPage(std::bind(&WClock::submitConfigPage, this, std::placeholders::_1));
    network->addCustomPage(configPage);

    lastTry = lastNtpSync = lastTimeZoneSync = ntpTime = dstStart = dstEnd = 0;
    failedTimeZoneSync = 0;
    // enableNightMode
    this->enableNightMode = nullptr;
    this->nightMode = nullptr;
    this->nightSwitches = nullptr;
    if (supportNightMode) {
      this->enableNightMode = network->settings()->setBoolean("enableNightMode", true);
      this->nightMode = WProps::createBooleanProperty("nightMode", "nightMode");
      this->addProperty(this->nightMode);
      this->nightSwitches = network->settings()->setByteArray("nightSwitches", 4, DEFAULT_NIGHT_SWITCHES);
    }
  }

  void loop(unsigned long now) {
    // Invalid after 3 hours
    validTime->asBool((lastNtpSync > 0) && ((!this->useTimeZoneServer->asBool()) || (lastTimeZoneSync > 0)) && (now - lastTry < (3 * 60 * 60000)));
    if (((lastTry == 0) || (now - lastTry > 10000)) && (WiFi.status() == WL_CONNECTED)) {
      bool timeUpdated = false;
      // 1. Sync ntp
      if ((!isValidTime()) && ((lastNtpSync == 0) || (now - lastNtpSync > 60000))) {
        network()->debug(F("Time via NTP server '%s'"), ntpServer->c_str());
        WiFiUDP ntpUDP;
        NTPClient ntpClient(ntpUDP, ntpServer->c_str());
        if (ntpClient.update()) {
          lastNtpSync = millis();
          ntpTime = ntpClient.getEpochTime();
          this->calculateDstStartAndEnd();
          validTime->asBool(!this->useTimeZoneServer->asBool());
          network()->debug(F("NTP time synced: %s"), _epochTimeFormatted->c_str());
          timeUpdated = true;
        } else {
          network()->error(F("NTP sync failed. "));
        }
      }
      // 2. Sync time zone
      if ((!isValidTime()) && ((lastNtpSync > 0) && ((lastTimeZoneSync == 0) || (now - lastTimeZoneSync > 60000))) && (useTimeZoneServer->asBool()) && (!timeZoneServer->equalsString(""))) {
        String request = timeZoneServer->c_str();
        network()->debug(F("Time zone update via '%s'"), request.c_str());
        HTTPClient http;
        WiFiClient wifiClient;
        http.begin(wifiClient, request);
        int httpCode = http.GET();
        if (httpCode > 0) {
          WJsonParser parser;
          this->timeZone->readOnly(false);
          this->rawOffset->readOnly(false);
          this->dstOffset->readOnly(false);
          WProperty* property = parser.parse(http.getString().c_str(), this);
          this->timeZone->readOnly(true);
          this->rawOffset->readOnly(true);
          this->dstOffset->readOnly(true);
          if (property != nullptr) {
            failedTimeZoneSync = 0;
            lastTimeZoneSync = millis();
            validTime->asBool(true);
            network()->debug(F("Time zone evaluated. Current local time: %s"), _epochTimeFormatted->c_str());
            timeUpdated = true;
          } else {
            failedTimeZoneSync++;
            network()->error(F("Time zone update failed. (%d. attempt): Wrong html response."), failedTimeZoneSync);
          }
        } else {
          failedTimeZoneSync++;
          network()->error(F("Time zone update failed (%d. attempt): http code %d"), failedTimeZoneSync, httpCode);
        }
        http.end();
        if (failedTimeZoneSync == 3) {
          failedTimeZoneSync = 0;
          lastTimeZoneSync = millis();
        }
      }
      // check nightMode
      if ((validTime) && (this->enableNightMode) && (this->enableNightMode->asBool())) {
        this->nightMode->asBool(this->isTimeBetween(this->nightSwitches->byteArrayValue(0), this->nightSwitches->byteArrayValue(1),
                                                    this->nightSwitches->byteArrayValue(2), this->nightSwitches->byteArrayValue(3)));
      }
      if (timeUpdated) {
        _notifyOnTimeUpdate();
      } else {
        _notifyOnMinuteUpdate();
      }
      lastTry = millis();
    }
  }

  void setOnTimeUpdate(THandlerFunction onTimeUpdate) {
    _onTimeUpdate = onTimeUpdate;
  }

  void setOnMinuteTrigger(THandlerFunction onMinuteTrigger) {
    _onMinuteTrigger = onMinuteTrigger;
  }

  unsigned long epochTime() {
    return _epochTime(true);
  }

  byte weekDay() {
    return weekDayOf(epochTime());
  }

  static byte weekDayOf(unsigned long epochTime) {
    // weekday from 0 to 6, 0 is Sunday
    return (((epochTime / 86400L) + 4) % 7);
  }

  const char* weekDayNameShort() {
    return weekDayNameShortOf(weekDay());
  }

  const char* weekDayNameShortOf(byte weekDay) {
    switch (weekDay) {
      case 1:
        return PSTR("Mo");
      case 2:
        return PSTR("Di");
      case 3:
        return PSTR("Mi");
      case 4:
        return PSTR("Do");
      case 5:
        return PSTR("Fr");
      case 6:
        return PSTR("Sa");
      case 0:
        return PSTR("So");
      default:
        return PSTR("n.a.");
    }
  }

  byte hours() {
    return hoursOf(epochTime());
  }

  static byte hoursOf(unsigned long epochTime) {
    return ((epochTime % 86400L) / 3600);
  }

  byte minutes() {
    return minutesOf(epochTime());
  }

  static byte minutesOf(unsigned long epochTime) {
    return ((epochTime % 3600) / 60);
  }

  byte seconds() {
    return secondsOf(epochTime());
  }

  static byte secondsOf(unsigned long epochTime) {
    return (epochTime % 60);
  }

  int yearOf() {
    return yearOf(epochTime());
  }

  static int yearOf(unsigned long epochTime) {
    return year(epochTime);
  }

  byte monthOf() {
    return monthOf(epochTime());
  }

  static byte monthOf(unsigned long epochTime) {
    // month from 1 to 12
    return month(epochTime);
  }

  byte dayOf() {
    return dayOf(epochTime());
  }

  static byte dayOf(unsigned long epochTime) {
    // day from 1 to 31
    return day(epochTime);
  }

  static bool isTimeLaterThan(byte epochTimeHours, byte epochTimeMinutes, byte hours, byte minutes) {
    return ((epochTimeHours > hours) || ((epochTimeHours == hours) && (epochTimeMinutes >= minutes)));
  }

  bool isTimeLaterThan(byte hours, byte minutes) {
    return isTimeLaterThan(this->hours(), this->minutes(), hours, minutes);
  }

  bool isTimeEarlierThan(byte hours, byte minutes) {
    return ((this->hours() < hours) || ((this->hours() == hours) && (this->minutes() < minutes)));
  }

  bool isTimeBetween(byte fromHours, byte fromMinutes, byte toHours, byte toMinutes) {
    if (isTimeLaterThan(fromHours, fromMinutes, toHours, toMinutes)) {
      // e.g. 22:00-06:00
      return ((isTimeLaterThan(fromHours, fromMinutes)) || (isTimeEarlierThan(toHours, toMinutes)));
    } else {
      // e.g. 06:00-22:00
      return ((isTimeLaterThan(fromHours, fromMinutes)) && (isTimeEarlierThan(toHours, toMinutes)));
    }
  }

  void updateFormattedTime() {
    WStringStream* stream = updateFormattedTime(epochTime());
    _epochTimeFormatted->asString(stream->c_str());
    delete stream;
  }

  static WStringStream* updateFormattedTime(unsigned long rawTime) {
    // Format YY-MM-DD hh:mm:ss
    WStringStream* stream = new WStringStream(19);
    char buffer[20];
    snprintf(buffer, 20, "%02d-%02d-%02d %02d:%02d:%02d",
             yearOf(rawTime), monthOf(rawTime), dayOf(rawTime),
             ((rawTime % 86400L) / 3600), ((rawTime % 3600) / 60), rawTime % 60);
    stream->print(buffer);
    return stream;
  }

  bool isValidTime() {
    return validTime->asBool();
  }

  bool isClockSynced() {
    return ((lastNtpSync > 0) && (lastTimeZoneSync > 0));
  }

  int getRawOffset() {
    return rawOffset->asInt();
  }

  int getDstOffset() {
    return (useTimeZoneServer->asBool() || isDaySavingTime() ? dstOffset->asInt() : 0);
  }

  void printConfigPage(WPage* page) {
    HTTP_CONFIG_PAGE_BEGIN(page->stream(), id());
    page->stream()->printf(HTTP_TOGGLE_GROUP_STYLE, "ga", (useTimeZoneServer->asBool() ? HTTP_BLOCK : HTTP_NONE), "gb", (useTimeZoneServer->asBool() ? HTTP_NONE : HTTP_BLOCK));
    page->stream()->printf(HTTP_TOGGLE_GROUP_STYLE, "gd", (useDaySavingTimes->asBool() ? HTTP_BLOCK : HTTP_NONE), "ge", HTTP_NONE);
    if (this->enableNightMode) {
      page->stream()->printf(HTTP_TOGGLE_GROUP_STYLE, "gn", (enableNightMode->asBool() ? HTTP_BLOCK : HTTP_NONE), "gm", HTTP_NONE);
    }
    // NTP Server
    page->stream()->printf(HTTP_TEXT_FIELD, "NTP server:", "ntp", "32", ntpServer->c_str());

    page->div();    
    page->stream()->printf(HTTP_RADIO_OPTION, "sa", "sa", HTTP_TRUE, (useTimeZoneServer->asBool() ? HTTP_CHECKED : ""), "tg()", "Get time zone via internet");
    page->stream()->printf(HTTP_RADIO_OPTION, "sb", "sa", HTTP_FALSE, (useTimeZoneServer->asBool() ? "" : HTTP_CHECKED), "tg()", "Use fixed offset to UTC time");
    page->divEnd();

    page->div("ga");    
    page->stream()->printf(HTTP_TEXT_FIELD, "Time zone server:", "tz", "64", timeZoneServer->c_str());
    page->divEnd();
    page->div("gb");    
    page->stream()->printf(HTTP_TEXT_FIELD, "Fixed offset to UTC in minutes:", "ro", "5", String(rawOffset->asInt() / 60).c_str());

    page->stream()->printf(HTTP_CHECKBOX_OPTION, "sd", "sd", (useDaySavingTimes->asBool() ? HTTP_CHECKED : ""), "td()", "Calculate day saving time (summer time)");    
    page->div("gd");
    page->stream()->print(F("<table  class='st'>"));
    page->stream()->print(F("<tr>"));
    page->stream()->print(F("<th></th>"));
    page->stream()->print(F("<th>Standard time</th>"));
    page->stream()->print(F("<th>Day saving time<br>(summer time)</th>"));
    page->stream()->print(F("</tr>"));
    page->stream()->print(F("<tr>"));
    page->stream()->print(F("<td>Offset to standard time in minutes</td>"));
    page->stream()->print(F("<td></td>"));
    page->stream()->print(F("<td>"));
    page->stream()->printf(HTTP_INPUT_FIELD, "do", "5", String(dstOffset->asInt() / 60).c_str());
    page->stream()->print(F("</td>"));
    page->stream()->print(F("</tr>"));
    page->stream()->print(F("<tr>"));
    page->stream()->print(F("<td>Month [1..12]</td>"));
    page->stream()->print(F("<td>"));
    page->stream()->printf(HTTP_INPUT_FIELD, "rm", "2", String(dstRule->byteArrayValue(STD_MONTH)).c_str());
    page->stream()->print(F("</td>"));
    page->stream()->print(F("<td>"));
    page->stream()->printf(HTTP_INPUT_FIELD, "dm", "2", String(dstRule->byteArrayValue(DST_MONTH)).c_str());
    page->stream()->print(F("</td>"));
    page->stream()->print(F("</tr>"));
    page->stream()->print(F("<tr>"));
    page->stream()->print(F("<td>Week [0: last week of month; 1..4]</td>"));
    page->stream()->print(F("<td>"));
    page->stream()->printf(HTTP_INPUT_FIELD, "rw", "1", String(dstRule->byteArrayValue(STD_WEEK)).c_str());
    page->stream()->print(F("</td>"));
    page->stream()->print(F("<td>"));
    page->stream()->printf(HTTP_INPUT_FIELD, "dw", "1", String(dstRule->byteArrayValue(DST_WEEK)).c_str());
    page->stream()->print(F("</td>"));
    page->stream()->print(F("</tr>"));
    page->stream()->print(F("<tr>"));
    page->stream()->print(F("<td>Weekday [0:sunday .. 6:saturday]</td>"));
    page->stream()->print(F("<td>"));
    page->stream()->printf(HTTP_INPUT_FIELD, "rd", "1", String(dstRule->byteArrayValue(STD_WEEKDAY)).c_str());
    page->stream()->print(F("</td>"));
    page->stream()->print(F("<td>"));
    page->stream()->printf(HTTP_INPUT_FIELD, "dd", "1", String(dstRule->byteArrayValue(DST_WEEKDAY)).c_str());
    page->stream()->print(F("</td>"));
    page->stream()->print(F("</tr>"));
    page->stream()->print(F("<tr>"));
    page->stream()->print(F("<td>Hour [0..23]</td>"));
    page->stream()->print(F("<td>"));
    page->stream()->printf(HTTP_INPUT_FIELD, "rh", "2", String(dstRule->byteArrayValue(STD_HOUR)).c_str());
    page->stream()->print(F("</td>"));
    page->stream()->print(F("<td>"));
    page->stream()->printf(HTTP_INPUT_FIELD, "dh", "2", String(dstRule->byteArrayValue(DST_HOUR)).c_str());
    page->stream()->print(F("</td>"));
    page->stream()->print(F("</tr>"));
    page->stream()->print(F("</table>"));
    page->divEnd();
    page->divEnd();
    if (this->enableNightMode) {
      // nightMode
      page->stream()->printf(HTTP_CHECKBOX_OPTION, "sn", "sn", (enableNightMode->asBool() ? HTTP_CHECKED : ""), "tn()", "Enable support for night mode");
      page->div("gn");
      page->stream()->print(F("<table  class='settingstable'>"));
      page->stream()->print(F("<tr>"));
      char timeFrom[6];
      snprintf(timeFrom, 6, "%02d:%02d", this->nightSwitches->byteArrayValue(0), this->nightSwitches->byteArrayValue(1));
      page->stream()->print(F("<td>from"));
      page->stream()->printf(HTTP_INPUT_FIELD, "nf", "5", timeFrom);
      page->stream()->print(F("</td>"));
      char timeTo[6];
      snprintf(timeTo, 6, "%02d:%02d", this->nightSwitches->byteArrayValue(2), this->nightSwitches->byteArrayValue(3));
      page->stream()->print(F("<td>to"));
      page->stream()->printf(HTTP_INPUT_FIELD, "nt", "5", timeTo);
      page->stream()->print(F("</td>"));
      page->stream()->print(F("</tr>"));
      page->stream()->print(F("</table>"));
      page->divEnd();
      page->stream()->printf(HTTP_TOGGLE_FUNCTION_SCRIPT, "tn()", "sn", "gn", "gm");
    }
    page->stream()->printf(HTTP_TOGGLE_FUNCTION_SCRIPT, "tg()", "sa", "ga", "gb");
    page->stream()->printf(HTTP_TOGGLE_FUNCTION_SCRIPT, "td()", "sd", "gd", "ge");
    page->stream()->print(FPSTR(HTTP_CONFIG_SAVE_BUTTON));
  }

  void submitConfigPage(AsyncWebServerRequest* request) {
    this->ntpServer->asString(request->arg("ntp").c_str());
    this->timeZoneServer->asString(request->arg("tz").c_str());
    this->useTimeZoneServer->asBool(request->arg("sa") == HTTP_TRUE);
    this->useDaySavingTimes->asBool(request->arg("sd") == HTTP_TRUE);
    this->rawOffset->asInt(atol(request->arg("ro").c_str()) * 60);
    this->dstOffset->asInt(atol(request->arg("do").c_str()) * 60);
    this->dstRule->byteArrayValue(STD_MONTH, atoi(request->arg("rm").c_str()));
    this->dstRule->byteArrayValue(STD_WEEK, atoi(request->arg("rw").c_str()));
    this->dstRule->byteArrayValue(STD_WEEKDAY, atoi(request->arg("rd").c_str()));
    this->dstRule->byteArrayValue(STD_HOUR, atoi(request->arg("rh").c_str()));
    this->dstRule->byteArrayValue(DST_MONTH, atoi(request->arg("dm").c_str()));
    this->dstRule->byteArrayValue(DST_WEEK, atoi(request->arg("dw").c_str()));
    this->dstRule->byteArrayValue(DST_WEEKDAY, atoi(request->arg("dd").c_str()));
    this->dstRule->byteArrayValue(DST_HOUR, atoi(request->arg("dh").c_str()));
    if (this->enableNightMode) {
      this->enableNightMode->asBool(request->arg("sn") == HTTP_TRUE);
      processNightModeTime(0, request->arg("nf").c_str());
      processNightModeTime(2, request->arg("nt").c_str());
    }
  }

  WProperty* epochTimeFormatted() {
    return _epochTimeFormatted;
  }

  WProperty* nightMode;

 private:
  THandlerFunction _onTimeUpdate, _onMinuteTrigger;
  unsigned long lastTry, lastNtpSync, lastTimeZoneSync, ntpTime;
  unsigned long dstStart, dstEnd;
  byte failedTimeZoneSync;
  WProperty* _epochTimeFormatted;
  WProperty* validTime;
  WProperty* ntpServer;
  WProperty* useTimeZoneServer;
  WProperty* timeZoneServer;
  WProperty* timeZone;
  WProperty* rawOffset;
  WProperty* dstOffset;
  WProperty* useDaySavingTimes;
  WProperty* dstRule;
  WProperty* enableNightMode;
  WProperty* nightSwitches;

  void _notifyOnTimeUpdate() {
    if (_onTimeUpdate) {
      _onTimeUpdate();
    }
    _notifyOnMinuteUpdate();
  }

  void _notifyOnMinuteUpdate() {
    if (_onMinuteTrigger) {
      _onMinuteTrigger();
    }
  }

  unsigned long _epochTime(bool useDstOffset) {
    return (lastNtpSync > 0 ? ntpTime + getRawOffset() + (useDstOffset ? getDstOffset() : 0) + ((millis() - lastNtpSync) / 1000) : 0);
  }

  unsigned long getEpochTime(int year, byte month, byte week, byte weekday, byte hour) {
    tmElements_t ds;
    ds.Year = (week == 0 && month == 12 ? year + 1 : year) - 1970;
    ds.Month = (week == 0 ? (month == 12 ? 1 : month + 1) : month);
    ds.Day = 1;
    ds.Hour = hour;
    ds.Minute = 0;
    ds.Second = 0;
    unsigned long tt = makeTime(ds);
    byte iwd = weekDayOf(tt);
    if (week == 0) {
      // last week of last month
      short diffwd = iwd - weekday;
      diffwd = (diffwd <= 0 ? diffwd + 7 : diffwd);
      tt = tt - (diffwd * 60 * 60 * 24);
    } else {
      short diffwd = weekday - iwd;
      diffwd = (diffwd < 0 ? diffwd + 7 : diffwd);
      tt = tt + (((7 * (week - 1)) + diffwd) * 60 * 60 * 24);
    }
    return tt;
  }

  void calculateDstStartAndEnd() {
    if ((!this->useTimeZoneServer->asBool()) && (this->useDaySavingTimes->asBool())) {
      int year = WClock::yearOf(_epochTime(false));
      dstStart = getEpochTime(year, dstRule->byteArrayValue(DST_MONTH), dstRule->byteArrayValue(DST_WEEK), dstRule->byteArrayValue(DST_WEEKDAY), dstRule->byteArrayValue(DST_HOUR));
      WStringStream* stream = updateFormattedTime(dstStart);
      // network()->debug(F("DST start is: %s"), stream->c_str());
      delete stream;
      dstEnd = getEpochTime(year, dstRule->byteArrayValue(STD_MONTH), dstRule->byteArrayValue(STD_WEEK), dstRule->byteArrayValue(STD_WEEKDAY), dstRule->byteArrayValue(STD_HOUR));
      stream = updateFormattedTime(dstEnd);
      // network()->debug(F("STD start is: %s"), stream->c_str());
      delete stream;
    }
  }

  bool isDaySavingTime() {
    if ((!this->useTimeZoneServer->asBool()) && (this->useDaySavingTimes->asBool())) {
      if ((this->dstStart != 0) && (this->dstEnd != 0)) {
        unsigned long now = _epochTime(false);
        if (yearOf(now) != yearOf(dstStart)) {
          calculateDstStartAndEnd();
        }
        if (dstStart < dstEnd) {
          return ((now >= dstStart) && (now < dstEnd));
        } else {
          return ((now < dstEnd) || (now >= dstStart));
        }
      } else {
        return false;
      }
    } else {
      return (dstOffset->asInt() != 0);
    }
  }

  void processNightModeTime(byte arrayIndex, String timeStr) {
    timeStr = (timeStr.length() == 4 ? "0" + timeStr : timeStr);
    if (timeStr.length() == 5) {
      byte hh = timeStr.substring(0, 2).toInt();
      byte mm = timeStr.substring(3, 5).toInt();
      this->nightSwitches->byteArrayValue(arrayIndex, hh);
      this->nightSwitches->byteArrayValue(arrayIndex + 1, mm);
    }
  }
};

#endif
