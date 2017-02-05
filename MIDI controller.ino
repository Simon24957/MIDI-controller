#include <Wire.h>                            
#include <math.h>
#include <Streaming.h>
#include <EEPROM.h>
#include <LiquidCrystal_I2C.h>


//----------------------------  adresses i2c
#define KNOB_EXPANDER 0x24                     // KNOB_EXPANDER : (A0,A1->0V , A2->+5V)
#define EXP_BTNS 0x27
#define LCD 0x21
// LEDS
//----------------------------  communication série
#define NBADRESSES 24
#define OUTPUT_MIDI 0
#define OUTPUT_USB 1
//----------------------------
#define SIZETAB 8


                         
volatile byte flagKnob = 0;                           //   Les variables manipulées au sein de la routine d'interruption (ISR) doivent etre définient comme volatile
volatile byte val_btns = 0;                         
volatile char flagBtns = 0;
volatile byte val_knob = 0; 
volatile byte old_knob = B00000000; 
volatile byte modif_knob = B00000000;
volatile unsigned int duration_interrupt=0;
 
byte rising_knob = B00000000;




 
//----------------------------
struct menuState{                                // Structure dédiée au parcours du menu : 3 niveaux de menu ( context, object, value). 
  int context;                                   // cursorPosition indique à quel niveau du menu se trouve l'utilisateur
  int object;
  int value;
  int cursorPosition;
};



struct midiSettings{                            // répertorie les settings courants (adresses, canal, ...)
  byte midiAdresses[NBADRESSES]={1,2,3,4,5,6,7,8,11,12,13,14,15,16,17,18,21,22,23,24,25,26,27,28};
  byte canal=1;
  byte output=OUTPUT_MIDI;
};

//--------------------------------
midiSettings currentSettings;
LiquidCrystal_I2C lcd(LCD,16,2);              // set the LCD address to 0x20 for a 16 chars and 2 line display




/////////////////////////////////////////////////    SETUP    ////////////////////////////////////////////////////

void setup()
{
  analogReference(EXTERNAL);                   //  !!! Si tension sur l'External Reference. ecrire INMPERATIVEMENT cette instruction !!!
 
  //------------------------------ 
  EIMSK &= B10111111;                        // Desactive int6. nécessaire pour que l'affectation d'une nouvelle valeur dans EICRB ne déclenche pas d'interruption
  EICRB |= B00100000;                        // interruption INT6 sur front descendant
  EICRB &= B11101111;                        //    ...
  EIMSK|= B01000000;                         // active int6
 
  //------------------------------
  Serial.begin(115200);                        // Initie la communication série via USB
  Serial1.begin(31250);                        // Initie la communication série via pins TX/RX
  
  Wire.begin();                             // join i2c bus (address optional for master)
  Wire.beginTransmission(KNOB_EXPANDER);    //  Initie les opérations i2c avec le KNOB_EXPANDER
                                            //  Les opérations de lecture (utilisation du KNOB_EXPANDER en input) nécéssitent une initialisation des I/O à l'état haut. 
  Wire.write(255);                          //  Cette instruction ecrit un octet dans le buffer(logiciel) i2c en vue de la transmission avec l'instruction Wire.endTransmission(); 
  Wire.endTransmission();                   //  Les octets contenus dans le buffer(logiciel) i2c sont transmis. 255 --> un état haut est écrit sur toutes les I/O.

  Wire.begin();                             // join i2c bus (address optional for master)
  Wire.beginTransmission(EXP_BTNS);         //  Initie les opérations i2c avec le KNOB_EXPANDER
                                            //  Les opérations de lecture (utilisation du KNOB_EXPANDER en input) nécéssitent une initialisation des I/O à l'état haut. 
  Wire.write(255);                          //  Cette instruction ecrit un octet dans le buffer(logiciel) i2c en vue de la transmission avec l'instruction Wire.endTransmission(); 
  Wire.endTransmission();                   //  Les octets contenus dans le buffer(logiciel) i2c sont transmis. 255 --> un état haut est écrit sur toutes les I/O.
   

  pinMode(5, INPUT);
  pinMode(13, OUTPUT);
  
  //-------------------------------
  lcd.init();                   
  lcd.backlight();
  
  //-------------------------------
  menuState initMenu = {0,0,0,0};                      // cette structure est créée dans le scope du setup uniquement pour l'initialisation de l'affichage
  displayMenu(&initMenu);                  // initialisation de l'affichage
  
  digitalWrite(13,HIGH);
}









//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                                        FONCTIONS                                             //
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define NB_CONTEXT 4
#define NB_OBJECT 24
#define MAX_CANAL 16
#define MAX_ADRESS 127

#define KNOB_ROT_CCW  0
#define KNOB_ROT_CW   1
#define KNOB_CLIC     2
#define KNOB_NO_ACTION 3


////////////////////////////////////////////    UPDATE DE L'AFFICHAGE   /////////////////////////////////////////
void displayMenu(struct menuState* p_menu){
    
  #define DEBUG_DISPLAYMENU 0
  #define LCD_ACTIVE 1
    
  static menuState last_displayed_menu = {3,22,127,1};  // initialisé de manière à ce que toutes les valeurs changent au premier appel. de cette manière, tout est mis a jour.
    
  unsigned int duration_fdisplay=0;  
  unsigned int duration_fdisplay_object = 0;
  unsigned int duration_fdisplay_object_print = 0;
  unsigned int duration_fdisplay_object_print2 = 0;
      
  EIMSK &= B10111111;
  duration_fdisplay = micros();

  
  #if DEBUG_DISPLAYMENU  
  Serial << "last_displayed" << "Context: " << last_displayed_menu.context << " Object: " << last_displayed_menu.object << " Value: " << last_displayed_menu.value << " Cursor: " << last_displayed_menu.cursorPosition << endl;
  Serial << "p_menu" << "Context: " << p_menu->context << " Object: " << p_menu->object << " Value: " << p_menu->value << " Cursor: " << p_menu->cursorPosition << endl;
  if (p_menu->cursorPosition==0 && last_displayed_menu.cursorPosition!=0) // si on repasse au niveau context
    Serial << "niveau context "<< endl; 
  if (p_menu->context!=last_displayed_menu.context)  
    Serial << "context change "<< endl;
  if ((p_menu->object!=last_displayed_menu.object) || (p_menu->cursorPosition==1 && last_displayed_menu.cursorPosition!=1)) // si l'objet change
    Serial << "object change "<< endl;
  if ((p_menu->value!=last_displayed_menu.value) || (p_menu->cursorPosition==2 && last_displayed_menu.cursorPosition!=2))// si la value change
     Serial << "value change "<< endl; 
  #endif



  #if LCD_ACTIVE
    
  static char* contextString[]={"Adresses midi", "Canal        ", "Load Settings", "Save Settings", "Output       "};
  static char* objectString[]={"But1 On","But2 On","But3 On","But4 On","But5 On","But6 On","But7 On","But8 On",
                               "But1 Off","But2 Off","But3 Off","But4 Off","But5 Off","But6 Off","But7 Off","But8 Off",
                               "Fader1","Fader2","Fader3","Fader4","Fader5","Fader6","Fader7","Fader8"};
  char printString[12]={"############"};
  int index_compar = 0;
  byte terminated = B00000000;
  byte i=0;

  /*if (p_menu->cursorPosition==0 && last_displayed_menu.cursorPosition!=0){ // si on repasse au niveau context
    lcd.clear();
    lcd.setCursor(1,0);
    lcd.print(contextString[p_menu->context]);     
  }     */
  //-----------------------------------------------------------------------------------------------------------------------  CONTEXT change  ---------------------------------
  if (p_menu->context!=last_displayed_menu.context){  // si le context change
    lcd.setCursor(1,0);
    lcd.print(contextString[p_menu->context]);// actualiser context
  } 
  //-----------------------------------------------------------------------------------------------------------------------  OBJECT change  ---------------------------------  
  if ((p_menu->object!=last_displayed_menu.object) || (p_menu->cursorPosition==1 && last_displayed_menu.cursorPosition!=1)) {// si l'objet change ou si on passe dans le niveau object
    duration_fdisplay_object = micros();
    i=0;
    do{
      if (*(objectString[last_displayed_menu.object]+index_compar) == '\0') {terminated |= B00000001;}
      if (*(objectString[p_menu->object]+index_compar) =='\0' ) {terminated |=B00000010;}
 
      if (terminated == 3 && i){// lorsque les deux chaines sont terminées,et si le(s) dernier(s) caractère(s) consécutif(s) sont à remplacer.
        printString[i] = '\0';
        duration_fdisplay_object_print = micros();
        lcd.setCursor(1+index_compar-i,1);
        lcd.print(printString);
        duration_fdisplay_object_print = micros() - duration_fdisplay_object_print;
        i=0;
      }
      else if ((*(objectString[p_menu->object]+index_compar) == *(objectString[last_displayed_menu.object]+index_compar)) && i){// lorsqu'un caractère est identique à l'ancien, et si les caractères précédents sont à remplacer
        printString[i] = '\0';
        duration_fdisplay_object_print2 = micros();
        lcd.setCursor(1+index_compar-i,1);
        lcd.print(printString);
        duration_fdisplay_object_print2 = micros() - duration_fdisplay_object_print2;
        i=0;
        }
      else if (*(objectString[p_menu->object]+index_compar) != *(objectString[last_displayed_menu.object]+index_compar)){//Si un caractère est différent de l'ancien, on le signale au prochain cycle de la boucle en incrémentant i. 
        if (terminated <=1) {printString[i] = *(objectString[p_menu->object]+index_compar);} // aucune chaine "object" terminée, ou ancienne terminée mais pas la nouvelle ==> ajout du nouveau caractère à printString
        if (terminated ==2) {printString[i] = (' ');}// nouvelle chaine terminée mais pas l'ancienne ==> ' '   
        i++;
      }
      index_compar++;    
    }while(terminated !=3 && index_compar < 11);  
    /*
    // ancienne version ( displayMenu V2) plus simple
    duration_fdisplay_object_print = micros();
    lcd.setCursor(1,1);
    lcd.print(objectString[p_menu->object]);
    duration_fdisplay_object_print = micros() - duration_fdisplay_object_print;*/
    duration_fdisplay_object_print2 = duration_fdisplay_object_print + duration_fdisplay_object_print2;     
    duration_fdisplay_object = micros()-duration_fdisplay_object;  
  } 
    
  //-----------------------------------------------------------------------------------------------------------------------  VALUE change  --------------------------------- 
  if ((p_menu->value!=last_displayed_menu.value) || (p_menu->cursorPosition==2 && last_displayed_menu.cursorPosition!=2)) {// si la value change ou si on passe dans le niveau value
    lcd.setCursor(13,1);
    lcd.print(p_menu->value);// actualiser la value
    if ((last_displayed_menu.value >= 100) && (p_menu->value < 100)) {lcd.setCursor(15,1); lcd.print(" ");}
    if ((last_displayed_menu.value >= 10) && (p_menu->value < 10)) {lcd.setCursor(14,1); lcd.print(" ");}
  } 

  //-----------------------------------------------------------------------------------------------------------------------  CURSORPOSITION change  ------------------------
  if(p_menu->cursorPosition==0 && last_displayed_menu.cursorPosition!=0){
    lcd.setCursor(0,0);
    lcd.print(">"); 
    lcd.setCursor(0,1);
    lcd.print(" ");
    lcd.setCursor(12,1);
    lcd.print(" ");
  }
  if(p_menu->cursorPosition==1 && last_displayed_menu.cursorPosition!=1){
    lcd.setCursor(0,0);
    lcd.print(" "); 
    lcd.setCursor(0,1);
    lcd.print(">");
    lcd.setCursor(12,1);
    lcd.print(" ");
  }
  if(p_menu->cursorPosition==2 && last_displayed_menu.cursorPosition!=2){
    lcd.setCursor(0,0);
    lcd.print(" "); 
    lcd.setCursor(0,1);
    lcd.print(" ");
    lcd.setCursor(12,1);
    lcd.print(">");
  }
  #endif

  last_displayed_menu = *p_menu;


  duration_fdisplay = micros()-duration_fdisplay;
  Serial << "time to print on lcd          : " << duration_fdisplay_object_print2  << endl;
  Serial << "time through object category  : " << duration_fdisplay_object << endl;     
  Serial << "time through display function : " << duration_fdisplay << endl << endl;     
  EIMSK |= B01000000;//reactivate interruption
}











///////////////////////////////////////////////    UPDATE DU MENU    //////////////////////////////////////////
void updateMenu(struct menuState* p_menu, int action){

    
  byte maxValue;
          
  if(action==KNOB_CLIC){
    p_menu->cursorPosition++;
      
    //Context 1(Canal) and 4(Output) have no object, we load the value and place the cursor directly on value
    if(p_menu->context==1 && p_menu->cursorPosition==1){
      p_menu->cursorPosition=2;
      p_menu->value=currentSettings.canal;
    }
    if(p_menu->context==4 && p_menu->cursorPosition==1){
      p_menu->cursorPosition=2;
      p_menu->value=currentSettings.output;
    }
          
    // quand on parametre les adresse midi, un clic valide et incrémente l'object. le curseur reste sur la value
    if(p_menu->cursorPosition==3){                            
      if(p_menu->context==0 && p_menu->object<NB_OBJECT-1){ 
        //save the selected adress
        currentSettings.midiAdresses[p_menu->object]=p_menu->value;
        //Prepare for the next setting
        p_menu->object++;
        p_menu->value=currentSettings.midiAdresses[p_menu->object];
        p_menu->cursorPosition=2;                       
      }
      // pour le dernier object du context adresse midi, le curseur retourne sur 0 (context)
      else if(p_menu->context==0){
        //save the selected adress
        currentSettings.midiAdresses[p_menu->object]=p_menu->value;
        //This was the last setting, return to main screen
        p_menu->cursorPosition=0;
        p_menu->object=0;
        p_menu->value=0;
      }
      else if(p_menu->context==1){
        //save the selected adress
        currentSettings.canal=p_menu->value;
        //This was the last setting, return to main screen
        p_menu->cursorPosition=0;
        p_menu->object=0;
        p_menu->value=0;
      }
      else{
        //return to main screen
        p_menu->cursorPosition=0;
        p_menu->object=0;
        p_menu->value=0;
      }
    }
  }
  else{
    //Knob was turned
    switch(p_menu->cursorPosition){
      case 0: //Context selection
          if(action==KNOB_ROT_CW && p_menu->context<NB_CONTEXT-1) p_menu->context++;
          if(action==KNOB_ROT_CCW && p_menu->context >0) p_menu->context--;
          break;
      case 1: //Object selection
          if(action==KNOB_ROT_CW && p_menu->object<NB_OBJECT-1) p_menu->object++;
          if(action==KNOB_ROT_CCW && p_menu->object>0) p_menu->object--;
          break;
      case 2: //Value selection
          if(p_menu->context==0) maxValue = MAX_ADRESS;
          if(p_menu->context==1) maxValue = MAX_CANAL;
          //if(context==2)
          if(action==KNOB_ROT_CW && p_menu->value<maxValue) p_menu->value++;
          if(action==KNOB_ROT_CCW && p_menu->value>0) p_menu->value--;
          break;
    }
  }
  
  //Load value when appropriate
  if(p_menu->context==0 && p_menu->cursorPosition==1) p_menu->value=currentSettings.midiAdresses[p_menu->object];
  if(p_menu->context==1 && p_menu->cursorPosition==1) p_menu->value=currentSettings.canal;
 
}




///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                                           //
//                                                MAIN                                                       //
//                                                                                                           //
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
void loop(){

  static menuState menu;
  int action=KNOB_NO_ACTION;
  //EIMSK|= B01000000; // autorise l'int6


  if (flagKnob){
    
    flagKnob=0;
    rising_knob = modif_knob & val_knob;
    old_knob = val_knob;
    action=KNOB_NO_ACTION;
    
    if(rising_knob & B10000000){                                        // tourne encodeur
      if ((val_knob & B01000000) == 64) {action=KNOB_ROT_CW; /*Serial << "knob rot1  ";*/ }               // la pinB de l'encodeur est elle en avance ...
      else if ((val_knob & B01000000)== 0) {action=KNOB_ROT_CCW;/*Serial << "knob rot2  ";*/}            // ... ou en retard de phase ?
    }
    if(rising_knob & B00100000) action=KNOB_CLIC; // clic
    
    if(action!=KNOB_NO_ACTION){                                           // si action sur le bouton, actualiser le menu
      //Serial << "Action : " << action << endl;
      updateMenu(&menu, action);
      displayMenu(&menu);
    }
  }  
}


//////////////////////////////////    INTERRUPTION - EVENEMENTS KNOB/BOUTONS    ////////////////////////////////

ISR(INT6_vect){  
   
  #define DEBUG_ISR 0
  
  SREG |=B10000000;                       //Par défaut le déclenchement des interruptions est désactivé dans les ISR. En passant le bit 7 du registre SREG à 1, on rend possible le déclenchement d'interruption au sein même de l'ISR. Cela est nécessaire pour les requêtes i2c, la communication série avec l'ordi etc...            
  EIMSK &= B10111111;                     //Eternal Interrupt Mask Register(EIMSK) : les interruption sont autorisées lorsque leur bit associé dans le registre EIMSK est à 1. Ici on interdit le déclenchement de l'interruption lorsque le flag INTF6 passe à 1. Cette ISR est déclenché par INTF6, on interdit son redéclenchement au sein de cette routine afin d'éviter les enmerdes ;) 
  
  //#if DEBUG_ISR
  duration_interrupt=micros();
  Serial << "int  " << endl;
  //#endif
  
  Wire.requestFrom(KNOB_EXPANDER,1);      // Demande de lecture de 1 octet à l'adresse "KNOB_EXPANDER". L'opération de lecture réinitialise la sortie "interruption" du PCF8574P
  while(!Wire.available());               // attente de l'arrivée d'un octet dans le registre de réception i2c
  val_knob = Wire.read();
  val_knob&=B11100000;                    // masquage des 5 bits de poids faibles affectés de valeur constante (5V ou 0V du circuit)
  modif_knob = val_knob ^ old_knob;       // old_knob est mis à jour dans la void loop par l'opération old_knob = val_knob. Après tests, la mise à jour dans la void lood permet d'éviter des erreurs du type lancement d'une requête EXP_BTNS lorsque le knob est tourné.


  // pour déterminer d'ou provient l'interruption (knob ou boutons), 4 cas à gérer :
  //....._____________________
  //.....|modif_knob| val_knob |
  //.....|__________|__________|
  //.....|   0      |    0     |  --> EXP_BTNS
  //.....|   0      |    1     |  --> cas qui ne devrait pas se produire. pour un raison inconnue l'interruption semble se redéclencher un deuxième fois lorsque l'on tourne rapidement l'encodeur
  //.....|   1      |    0     |  --> EXP_KNOB
  //.....|   1      |    1     |  --> EXP_KNOB


  if(modif_knob){                        // si une modification a eu lieu sur l'octet val_knob
    flagKnob = 1;
    #if DEBUG_ISR
    Serial << "knob = " << val_knob << endl;
    Serial << "modif_knob = " << modif_knob << endl;
    #endif
  }
  else if((!modif_knob)&(!val_knob)){     // si pas de modif et que val_knob est égale à 0, c'est donc l'EXP_BTNS qui a déclenché l'interruption.
    flagBtns = 1;
    Wire.requestFrom(EXP_BTNS,1);           // Demande de lecture de 1 octet à l'adresse "EXP_BTNS". L'opération de lecture réinitialise la sortie "interruption" du PCF8574P
    while(!Wire.available());
    val_btns = ~Wire.read();
    #if DEBUG_ISR
    Serial << "VAL_BTNS = " << val_btns << endl;    
    Serial << "knob = " << val_knob << endl;
    Serial << "modif_knob = " << modif_knob << endl;
    #endif
  }
  #if DEBUG_ISR
  else{                                   // dernier cas, qui ne devrait pas se produire : modif_knob ==0 et val_knob !=0 . au repos val_knob doit être a 0, si action sur le knob, alors il devrait y avoir une modif.
    Serial << "          BUUUUUUUUUUUUUUUUUUUUG" << endl;
    Serial << "          knob = " << val_knob << endl;
    Serial << "          modif_knob = " << modif_knob << endl;
  }
  #endif

  //#if DEBUG_ISR
  duration_interrupt = micros() - duration_interrupt;
  Serial << "duration_interrupt = " << duration_interrupt << endl; 
  //#endif
  //TWCR &= B11111110;                    // je ne sais plus pourquoi j'avais mis çà. çà a l'air de marcher sans. le bit 0 de ce registre est censé autoriser les interruption liées à la lecture i2c ...? 
  SREG &=B01111111;
  EIMSK|= B01000000;                      // La modification du registre EIMSK peut provoquer une interruption non souhaitée. on désactive puis réactive l'interruption avec SREG pour eviter ce phénomème.
  SREG |=B10000000;
}



