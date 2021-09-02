#include <Arduino.h>
#include <BleGamepad.h>   // https://github.com/lemmingDev/ESP32-BLE-Gamepad
#include <ESP32Encoder.h> // https://github.com/madhephaestus/ESP32Encoder/
#include <Wire.h>
#include <SSD1306Wire.h>
#include <Preferences.h>

#define totalButtons 30

#define latchPin 18 
#define clockPin 19
#define dataPinL 5
#define dataPinR 4
#define totalShiftInputs 12 // max number of inputs on one set of shift reg

#define clutchMstrPin 13
#define clutchSlvPin 14

#define MAXENC 2
#define HOLDOFFTIME 160   // TO PREVENT MULTIPLE ROTATE "CLICKS" WITH CHEAP ENCODERS WHEN ONLY ONE CLICK IS INTENDED
#define KEEPHIGH 150

#define SDA 21
#define SCL 22

#define conectionLED 12

// all the array pos not pyhsical pos (pychsical - 1)
#define UP_BUTTON_POS 3
#define LEFT_BUTTON_POS 4
#define DOWN_BUTTON_POS 5
#define RIGHT_BUTTON_POS 6
#define ENTER_BUTTON_POS 7
#define CENTRE_BUTTON_POS 15 

#define SCREEN_OFF 0
#define MAIN_MENU 1
#define CLUTCH_SET 2
#define CAR_SELECT 3
#define NEW_CLUTCH 4
#define EXIT 5

#define ENTER_ACTION 0
#define UP_ACTION 1           
#define DOWN_ACTION 2         
#define LEFT_ACTION 3
#define RIGHT_ACTION 4

#define MAX_CARS 10
#define MAX_NAME_LENGTH 10

#define MAX_ADJUST 10
#define MIN_ADJUST 0.01f

#define NUM_CLUTCH_AVGS 3


// ALL SCREEN STUFF IS SWITCHED TO SERIAL

// Initialize the OLED display using Wire library
SSD1306Wire  display(0x3c, SDA, SCL, GEOMETRY_128_32);
Preferences preferences;

const char* keys[10][2] = {
                            {"A","B"},
                            {"C","D"},
                            {"E","F"},
                            {"G","H"},
                            {"I","J"},
                            {"K","L"},
                            {"M","N"},
                            {"O","P"},
                            {"Q","R"},
                            {"S","T"},
                          };

struct car
{
  char name[9];
  float clSet; 
};

struct car cars[MAX_CARS] = {0};

int screenState = SCREEN_OFF;
int menuPos = CLUTCH_SET;
int selectedCar = 0;
int targetCar = 0;
int totalCars = 0; 
int nameConfirm = 0;
int screenRefreshCount = 0;

float adjust = 0.1;

char menuNames[4][11] = {
                          "Clutch Set",
                          "Car Select",
                          "New Clutch",
                          "Exit"
                        };

char editName[10] = "         ";
int cursor = 0;
int currentChar = 32;// 32,48-57,65-90 

char displayBuffer[2][20];

BleGamepad bleGamepad;

uint8_t uppPin[MAXENC] = {32, 27};
uint8_t dwnPin[MAXENC] = {33, 26};
uint8_t prsPin[MAXENC] = {35, 25};
uint8_t encoderUpp[MAXENC] = {(totalShiftInputs * 2) + 2, (totalShiftInputs * 2) + 4};
uint8_t encoderDwn[MAXENC] = {(totalShiftInputs * 2) + 3, (totalShiftInputs * 2) + 5};
uint8_t encoderPrs[MAXENC] = {(totalShiftInputs * 2), (totalShiftInputs * 2) + 1};
ESP32Encoder encoder[MAXENC];
unsigned long holdoff[MAXENC] = {0,0};
unsigned long lastEncoderUpp[MAXENC] = {0,0};
unsigned long lastEncoderDwn[MAXENC] = {0,0};
int32_t prevenccntr[MAXENC] = {0,0};
bool prevprs[MAXENC] = {0,0};


int previousButtonStates[totalButtons] = {0};
int currentButtonStates[totalButtons] = {0};

int buttonRisingL = true;
int buttonRisingR = true;

unsigned long lastTimeRotary[4] = {0}; // 0 - A clock, 1 - A antiClock, 2 - B clock, 3 - B antiClock
unsigned long lastTimeMenu = 0;
unsigned long lastCursorDisplay = 0;

unsigned long lastLedSwitch = 0;
int ledState = LOW;



void readButtonInputs()
{
  //update all the values from the shift reg and the paddles to currentButtonStates

  digitalWrite(latchPin, 0); // get the shift regs ready to pull current values through
  digitalWrite(clockPin, 0);
  digitalWrite(clockPin, 1);
  digitalWrite(latchPin, 1);

  for(int j = 0; j < (totalShiftInputs); j++)
  {
    if(j == 8) // miss 4 shift inputs that have no data
    {
      for(int k = 0; k < 4; k++)
      {
        digitalWrite(clockPin, LOW);
        digitalWrite(clockPin, HIGH);
      }
    }
    
    currentButtonStates[j] = digitalRead(dataPinL); // move current shift reg values into button states
    currentButtonStates[j + totalShiftInputs] = digitalRead(dataPinR);

    digitalWrite(clockPin, LOW);
    digitalWrite(clockPin, HIGH);
    
  }

  unsigned long now = millis();

  // -- ROTARY ENCODERS : ROTATION -- //

  for (uint8_t i=0; i<MAXENC; i++) 
  {
    int32_t cntr = encoder[i].getCount();
    if (cntr!=prevenccntr[i]) 
    {
      if (!holdoff[i]) 
      {
        if (cntr>prevenccntr[i]) 
        { 
          /*
          bleGamepad.press(encoderUpp[i]);
          delay(150);
          bleGamepad.release(encoderUpp[i]);
          */
          currentButtonStates[encoderUpp[i]] = 1;
          lastEncoderUpp[i] = millis();
        }
        if (cntr<prevenccntr[i]) 
        {
          /*
          bleGamepad.press(encoderDwn[i]);
          delay(150);
          bleGamepad.release(encoderDwn[i]);
          */
          currentButtonStates[encoderDwn[i]] = 1;
          lastEncoderDwn[i] = millis();
        }
        holdoff[i] = now;
        if (holdoff[i]==0) holdoff[i] = 1;  // SAFEGUARD WRAP AROUND OF millis() (WHICH IS TO 0) SINCE holdoff[i]==0 HAS A SPECIAL MEANING ABOVE
      }
      else if (now-holdoff[i] > HOLDOFFTIME) 
      {
        prevenccntr[i] = encoder[i].getCount();
        holdoff[i] = 0;
      }
    }
    
    if(millis() - lastEncoderUpp[i] < KEEPHIGH)
    {
      currentButtonStates[encoderUpp[i]] = 1;
    }

    if(millis() - lastEncoderDwn[i] < KEEPHIGH)
    {
      currentButtonStates[encoderDwn[i]] = 1;
    }
    

    
  // -- ROTARY ENCODERS : PUSH SWITCH -- //

    bool pressed = !digitalRead(prsPin[i]);
    if (pressed != prevprs[i]) 
    {
      if (pressed) 
      {  // PRESSED
        currentButtonStates[encoderPrs[i]] = 0;  // push buttons are incorrently pulled up or something idk but this sorts it out (fix this at some point Jono please)
      }
      else 
      {  // RELEASED
        currentButtonStates[encoderPrs[i]] = 1;
      }
      prevprs[i] = !prevprs[i];
    }
  }
}



int getClutch()
{
  float mstrValue = 0;
  float slvValue = 0;

  for (int i = 0; i < NUM_CLUTCH_AVGS; i++)
  {
    mstrValue = mstrValue + analogRead(clutchMstrPin);
    slvValue = slvValue + analogRead(clutchSlvPin);
    delay(1);
  }

  float avgMstrValue = mstrValue / NUM_CLUTCH_AVGS;
  float avgSlvValue = slvValue / NUM_CLUTCH_AVGS;

  avgMstrValue = map(avgMstrValue, 1400, 2950, 4095 , 0); 
  avgSlvValue = map(avgSlvValue, 1500, 2950, 4095 , 0);

  if(avgMstrValue > 4095)
  {
    avgMstrValue = 4095;
  }
  else if(avgMstrValue < 0)
  {
    avgMstrValue = 0;
  }

  if(avgSlvValue > 4095)
  {
    avgSlvValue = 4095;
  }
  else if(avgSlvValue < 0)
  {
    avgSlvValue = 0;
  }

  avgSlvValue = avgSlvValue * (cars[selectedCar].clSet / 100);

  //Serial.printf("%f    %f    %f\n", avgMstrValue, avgSlvValue, cars[selectedCar].clSet);

  if(avgSlvValue > avgMstrValue)
  {
    return (int) map(avgSlvValue, 0, 4095, 32737, -32737);
  }
  else
  {
    return (int) map(avgMstrValue, 0, 4095, 32737, -32737);
  }
  
}



void storeButtons()
{
  int i;
  for(i = 0; i < totalButtons; i++)
  {
    previousButtonStates[i] = currentButtonStates[i];
    if(i != (totalShiftInputs * 2) && i != ((totalShiftInputs * 2)+1)) // check for rotary buttons, they don't replenish themselves
    {
      currentButtonStates[i] = 0;
    }  

  }

}


void displayCursorPos()
{
  if(millis() - lastCursorDisplay < 500)
  {
    editName[cursor] = 42;
  }
  else if(millis() - lastCursorDisplay > 1500)
  {
    editName[cursor] = 42;
    lastCursorDisplay = millis();
  }
  else
  {
    editName[cursor] = currentChar;
  }
}



void pushBuffer()
{
  display.clear();
  
  if(screenState == MAIN_MENU || screenState == CAR_SELECT)
  {
    display.setTextAlignment(TEXT_ALIGN_CENTER_BOTH);
    display.setFont(ArialMT_Plain_24);
    display.drawString(64,16,displayBuffer[0]);
    //Serial.println(displayBuffer[0]);
  }
  else
  {
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setFont(ArialMT_Plain_16);
    display.drawString(64,0,displayBuffer[0]);
    //Serial.println(displayBuffer[0]);
    display.drawString(64,17,displayBuffer[1]);
    //Serial.println(displayBuffer[1]);
  }
  display.display();
}



void setTotalCars()// pulls all the data for cars out of mem, makes them and counts them
{
  int i;

  for(i = 0; i <= MAX_CARS - 1; i++)
  {
    if(preferences.isKey(keys[i][0]))
    {
      struct car newCar;
      strcpy(newCar.name, preferences.getString(keys[i][0]).c_str());
      newCar.clSet = preferences.getFloat(keys[i][1]);

      cars[totalCars] = newCar;
      totalCars = i;
    }
    else
    {
      break;
    }
  }
}



void incrementChar()// move through the character list fowards
{
  if(currentChar == 32) // go to A from space
  {
    currentChar = 65;
  }
  else if(currentChar == 90)// go to 0 from Z
  {
    currentChar = 48;
  }
  else if(currentChar == 57) // go to space from 9
  {
    currentChar = 32;
  }
  else
  {
    currentChar++;
  }

  editName[cursor] = currentChar;
}



void decrementChar()// move through the character list backwards
{
  if(currentChar == 32)// go to 9 from space
  {
    currentChar = 57;
  }
  else if(currentChar == 48) // go to Z from 0
  {
    currentChar = 90;
  }
  else if(currentChar == 65) // go to space from A
  {
    currentChar = 32;
  }
  else
  {
    currentChar--;
  }

  editName[cursor] = currentChar;
}



void incrementCursor() //move to eidt next char
{
  editName[cursor] = currentChar;

  if(cursor < MAX_NAME_LENGTH - 1)
  {
    cursor++;
  }
  else
  {
    cursor = 0;
  }
  currentChar = editName[cursor];
}



void decrementCursor()
{
  editName[cursor] = currentChar;

  if (cursor == 0)
  {
    cursor = MAX_NAME_LENGTH - 1;
  }
  else
  {
    cursor--;
  }
  currentChar = editName[cursor];
}



void screenOff(void)  //turn the screen off and shutdown menus and store data
{
  //Log all changed data
  int i;

  for(i = 0; i <= totalCars - 1; i++)
  {
    if(preferences.getString(keys[i][0]).c_str() != cars[i].name)
    {
      preferences.putString(keys[i][0], cars[i].name);
      preferences.putFloat(keys[i][1], cars[i].clSet);
    }
    else if(preferences.getFloat(keys[i][1]) != cars[i].clSet)
    {
      preferences.putFloat(keys[i][1], cars[i].clSet);
    }
  }
  
  preferences.end();
  display.displayOff();
  menuPos = CLUTCH_SET;
  //Serial.println("*OFF*");
  screenState = SCREEN_OFF;
}



void mainMenu() //move to main menu
{
  if (screenState == SCREEN_OFF) //Check that the screen is on and if not turn it on
  {
    display.displayOn();
    preferences.begin("clutches", false);
    setTotalCars();  
  }

  //put inital state on screen
  
  screenState = MAIN_MENU;
  //Serial.println(menuNames[menuPos - 2]);  
  strcpy(displayBuffer[0], menuNames[menuPos - 2]); // set buffer with menu text then push buffer to screen
  pushBuffer();

}



void clutchSet(void) //move to clutch set screen
{
  //check that there are cars 
  if(cars[0].clSet == 0)
  {
    //Serial.println("No cars");
    strcpy(displayBuffer[0], "No cars");
  }
  else // write state on screen
  {
    //Serial.println(cars[selectedCar].name);
    //Serial.printf("%4.2f%% ± %4.2f%% \n", cars[selectedCar].clSet, adjust);
    strcpy(displayBuffer[0],cars[selectedCar].name);
    sprintf(displayBuffer[1],"%4.2f%% ± %4.2f%% \n", cars[selectedCar].clSet, adjust);
  }

  screenState = CLUTCH_SET;
  pushBuffer();
}



void carSelect(void) //move to car select screen
{
  //put inital state on screen
  if(cars[0].clSet == 0)
  {
    //Serial.println("No cars");
    strcpy(displayBuffer[0], "No Cars");
  }
  else //write state on screen
  {
    //Serial.println(cars[targetCar].name);
    strcpy(displayBuffer[0], cars[targetCar].name);
  }
  screenState = CAR_SELECT;
  pushBuffer();
}



void newClutch(void) //move to new clutch screen
{
  //put inital state on screen
  if(nameConfirm)
  {
    //Serial.println("Confirm Name");
    strcpy(displayBuffer[0], "Confirm Name");  
  }
  else
  {
    //Serial.println("Set Name");
    strcpy(displayBuffer[0], "Set Name");
    displayCursorPos();
  }

  //Serial.println(editName);
  screenState = NEW_CLUTCH;
  strcpy(displayBuffer[1], editName);
  pushBuffer();
}



void setName()
{
  if(nameConfirm)
  {
    struct car newCar;
    strcpy(newCar.name, editName);
    newCar.clSet = 40;

    cars[totalCars] = newCar;
    selectedCar = totalCars;
    totalCars++;
    nameConfirm = 0;
    strcpy(editName,"         ");
    cursor = 0;
    clutchSet();
  }
  else
  {
    nameConfirm = 1;
  }
}



void mainMenuInput(int buttonNum) //decide what needs to happen to the main menu with the input
{
  //show scrollable list of all the screens
  //if enter is pressed while on one of the screens move into that menu

  if(screenState == SCREEN_OFF)
  {
    mainMenu();
  }
  else if(buttonNum == ENTER_ACTION) // enter menu item
  {
    if(menuPos == EXIT)
    {
      screenOff();
    }
    else if(menuPos == CLUTCH_SET)
    {
      clutchSet();
    }
    else if(menuPos == CAR_SELECT)
    {
      carSelect();
    }
    else if(menuPos == NEW_CLUTCH)
    {
      newClutch();
    }
  }
  else if(buttonNum == DOWN_ACTION) //go down the menu 
  {
    if(menuPos == EXIT) //check if at the bottom and if so wrap
    {
      menuPos = CLUTCH_SET;
    }
    else
    {
      menuPos++;
    }

    mainMenu();
  }
  else if(buttonNum == UP_ACTION) //go up the menu
  {
    if(menuPos == CLUTCH_SET) //check if at the top and if so wrap
    {
      menuPos = EXIT;
    }
    else
    {
      menuPos--;
    }
  
    mainMenu();
  }
}



void clutchSetInput(int buttonNum) //decide what needs to happen to the clutch set menu with the input
{
  if(buttonNum == ENTER_ACTION)//back to main menu
  {
    adjust = 1;
    mainMenu();
  }
  else if(buttonNum == DOWN_ACTION) //decrease % by adjust
  {
    cars[selectedCar].clSet = cars[selectedCar].clSet - adjust;
    if(cars[selectedCar].clSet <= 0)
    {
      cars[selectedCar].clSet = 0;
    }
    clutchSet(); //redraw
  }
  else if(buttonNum == UP_ACTION) //increase % by adjust
  {
    cars[selectedCar].clSet = cars[selectedCar].clSet + adjust;
    if(cars[selectedCar].clSet >= 100)
    {
      cars[selectedCar].clSet = 100;
    }
    clutchSet(); //redraw
  }
  else if(buttonNum == LEFT_ACTION) //increase adjust
  {
    if(adjust != MAX_ADJUST)
    {
      adjust = adjust * 10;
    }
    clutchSet();
  }
  else if(buttonNum == RIGHT_ACTION) //drcrease adjust
  {
    if(adjust != MIN_ADJUST)
    {
      adjust = adjust / 10;
    }
    clutchSet();
  }
}



void carSeletInput(int buttonNum) //decide what needs to happen to the car select menu with the input
{
  if(buttonNum == ENTER_ACTION) //select target car and go back to menu
  {
    selectedCar = targetCar;
    targetCar = 0;
    mainMenu();
  }
  else if(buttonNum == DOWN_ACTION) //go down car list
  {
    if(targetCar + 1 == totalCars) //check if at the bottom and if so wrap
    {
      targetCar = 0;
    }
    else{
      targetCar++;
    }
    carSelect();
  }
  else if(buttonNum == UP_ACTION) //go up car list
  {
    if(targetCar == 0) //check if at the top and if so wrap
    {
      targetCar = totalCars -1;
    }
    else
    {
      targetCar--;
    }
    carSelect();
  }    
}



void newClutchInput(int buttonNum) // decide what needs to happen to the new clutch menu with the input
{
  if(buttonNum == ENTER_ACTION)// confirm and set name
  {
    setName();    
  }
  else if(buttonNum == DOWN_ACTION) // next char
  {
    nameConfirm = 0;
    incrementChar();
  }
  else if(buttonNum == UP_ACTION) // previous char
  {
    nameConfirm = 0;
    decrementChar();
  }
  else if(buttonNum == LEFT_ACTION) // move cursor left
  {
    nameConfirm = 0;
    decrementCursor();
  }
  else if(buttonNum == RIGHT_ACTION) // move cursor right
  {
    nameConfirm = 0;
    incrementCursor();
  }
  
  if(buttonNum != ENTER_ACTION || nameConfirm == 1)
  {
    newClutch();
  }

  screenRefreshCount = 0;
  
}



void screenSet(int buttonNum) //call curent state with new input
{
  switch(screenState)
  {
    case SCREEN_OFF:
        mainMenuInput(buttonNum);
      break;

    case MAIN_MENU:
        mainMenuInput(buttonNum);
      break;

    case CLUTCH_SET:
        clutchSetInput(buttonNum);
      break;

    case CAR_SELECT:
        carSeletInput(buttonNum);
      break;

    case NEW_CLUTCH:
        newClutchInput(buttonNum);
      break;
  }
}



void setup() 
{
  delay(100);
  // configure shift regs pins
  pinMode(latchPin, OUTPUT);
  pinMode(dataPinL, INPUT);  
  pinMode(dataPinR, INPUT);
  pinMode(clockPin, OUTPUT);

  pinMode(clutchMstrPin, INPUT);
  pinMode(clutchSlvPin, INPUT);

  pinMode(conectionLED, OUTPUT);

  display.init();
  
  display.setContrast(255);

  bleGamepad.begin( 32, 0, true, false, false, false, false, false, false, false, false, false, false, false, false);
  
  Serial.begin(115200);

  // configure encoders
  for (uint8_t i=0; i<MAXENC; i++) {
    encoder[i].clearCount();
    encoder[i].attachSingleEdge(dwnPin[i], uppPin[i]);
    pinMode(prsPin[i], INPUT);
  }

}



void loop() 
{
  if(bleGamepad.isConnected()) 
  {

    readButtonInputs();

    bleGamepad.setX(getClutch());

    if(millis() - lastTimeMenu > 400)
    {
      if (currentButtonStates[ENTER_BUTTON_POS] && currentButtonStates[CENTRE_BUTTON_POS]) // detect screen on command
      {
        mainMenuInput(ENTER_ACTION);
        currentButtonStates[ENTER_BUTTON_POS] = 0; // clear button commands so they don't get sent to pc
        currentButtonStates[CENTRE_BUTTON_POS] = 0;
        lastTimeMenu = millis();
      }

      if(screenState != SCREEN_OFF)  //if screen is on let me push buttons
      {
        
        if(currentButtonStates[UP_BUTTON_POS])
        {
          screenSet(UP_ACTION);
          currentButtonStates[UP_BUTTON_POS] = 0;
          lastTimeMenu = millis();
        }
        else if(currentButtonStates[DOWN_BUTTON_POS])
        {
          screenSet(DOWN_ACTION);
          currentButtonStates[DOWN_BUTTON_POS] = 0;
          lastTimeMenu = millis();
        }
        else if(currentButtonStates[LEFT_BUTTON_POS])
        {
          screenSet(LEFT_ACTION);
          currentButtonStates[LEFT_BUTTON_POS] = 0;
          lastTimeMenu = millis();
        }
        else if(currentButtonStates[RIGHT_BUTTON_POS])
        {
          screenSet(RIGHT_ACTION);
          currentButtonStates[RIGHT_BUTTON_POS] = 0;
          lastTimeMenu = millis();
        }
        else if(currentButtonStates[ENTER_BUTTON_POS])
        {
          screenSet(ENTER_ACTION);
          currentButtonStates[ENTER_BUTTON_POS] = 0;
          lastTimeMenu = millis();
        }
        
      }

    }
    else if(screenState != SCREEN_OFF || millis() - lastTimeMenu < 400) //stop interaction to quick 
    {
      currentButtonStates[UP_BUTTON_POS] = 0;
      currentButtonStates[LEFT_BUTTON_POS] = 0;
      currentButtonStates[DOWN_BUTTON_POS] = 0;
      currentButtonStates[RIGHT_BUTTON_POS] = 0;
      currentButtonStates[ENTER_BUTTON_POS] = 0;
    }

    if(screenState == NEW_CLUTCH && screenRefreshCount == 20)
    {
      newClutch();
      screenRefreshCount = 0;
      Serial.println();
    }

    screenRefreshCount++;

    int i;
    for(i = 1; i < (totalButtons + 1); i++) // set button commands to be sent
    {
      if (currentButtonStates[i - 1] != previousButtonStates[i - 1])
      {
        if(currentButtonStates[i - 1] == HIGH)
        {
          bleGamepad.press(i);
        }
        else
        {
          bleGamepad.release(i);
        }
      }
    }

    storeButtons(); // make current previous
    //bleGamepad.sendReport();

    unsigned long currentMillis = millis();
    if(currentMillis - lastLedSwitch > 1000)
    {
      lastLedSwitch = currentMillis;
      ledState = not(ledState);
      digitalWrite(conectionLED, ledState);
    }

  }
  else 
  {
    digitalWrite(conectionLED, LOW);
  }
  delay(10);
}

