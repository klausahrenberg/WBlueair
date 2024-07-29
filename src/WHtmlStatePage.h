#ifndef W_STATE_HTML_PAGE_H
#define W_STATE_HTML_PAGE_H

#include "WDevice.h"
#include "html/WPage.h"

class WHtmlStatePage : public WPage {
 public:
  WHtmlStatePage(WNetwork* network, WPurifierDevice* purifier)
      : WPage(network, "state", "Luftreiniger BlueAir 480i") {
    _purifier = purifier;
    // printPage(std::bind(&WHtmlStatePage::_printConfigPage, this, std::placeholders::_1));
    // submittedPage(std::bind(&WHtmlStatePage::_saveConfigPage, this, std::placeholders::_1, std::placeholders::_2));
    network->addCustomPage(this);
  }

  void printPage() {
    configPageBegin(id());

    table("tt");
    tr(); th(2); print(F("Messung im Raum")); thEnd(); trEnd();
    tr(); 
      td(); print(F("AQI")); tdEnd(); 
      td(); _printAqi(_purifier->pms()->aqi()->asInt()); tdEnd();
    trEnd();
    tr();
      td(2); print(_purifier->pms()->lastUpdate()->asString()); tdEnd();
    trEnd();
    tr(); trEnd();
    tr(); th(2); print(_purifier->outsideAqi()->locale()->asString()); thEnd(); trEnd();
    tr();
      td(); print(F("AQI outside")); tdEnd();
      td(); _printAqi(_purifier->outsideAqi()->aqi()->asInt()); tdEnd();
    trEnd();
    tr();
      td(2); print(_purifier->outsideAqi()->updateTime()->asString()); tdEnd();
    trEnd();
    tableEnd();

    const static char HTTP_STYLE[] PROGMEM = R"=====(
      <label class='switch'>
        <input type='checkbox' onchange='toggleCheckbox(this)' id='output' checked>
        <span class='slider'></span>
      </label>"
    )=====";  
                       
    //stream()->printf("<label class=\"switch\"><input type=\"checkbox\" onchange=\"toggleCheckbox(this)\" id=\"output\" checked><span class=\"slider\"></span></label>", "");
    
  }

  void submitPage(AsyncWebServerRequest* request) {
    // network()->notice(F("Save config page"));
    // this->showAsWebthingDevice->asBool(request->arg("sa") == HTTP_TRUE);
    // this->stationIndex->asString(request->arg("si").c_str());
    // this->apiToken->asString(request->arg("at").c_str());
  }

 private:
  WPurifierDevice* _purifier;

  void _printAqi(int aqi) {
    int c = _statusColor(aqi);
    String cS = String(c, HEX);
    String cS2 = "background-color:#" + cS;
    div(b_class, "vd", b_style, cS2.c_str());
    print(String(aqi).c_str());
    divEnd();
  }

  int _statusColor(int aqi) {
    byte r = 0;
    byte g = 0;
    byte b = 0;
    float m = 0.05;
    if (aqi < 20) {
      b = 255 - round(aqi * 255 * 0.05);
      g = round(aqi * 255 * 0.05);
    }
    if ((aqi >= 20) && (aqi < 60)) {
      g = 255;
    } else if ((aqi >= 60) && (aqi < 80)) {
      g = 255 - round((aqi - 60) * 255 * 0.05);
    }
    if ((aqi >= 30) && (aqi < 50)) {
      r = round((aqi - 30) * 255 * 0.05);
    } else if ((aqi >= 50) && (aqi < 90)) {
      r = 255;
    } else if ((aqi >= 90) && (aqi < 110)) {
      r = 255 - round((aqi - 90) * 255 * 0.025);
    } else if (aqi >= 110) {
      r = 128;
    }
    int rgb = r;

    rgb = (rgb << 8) + g;
    rgb = (rgb << 8) + b;
    return rgb;
  }

};

#endif