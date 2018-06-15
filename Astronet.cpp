


#include "Astronet_config.h"
#include "Astronet.h"
#include "RF24.h"
#include <stddef.h>
#include <stdint.h>
#include <EEPROM.h>


int Astronet::id = 0;
int Astronet::success=0;
int Astronet::failed=0;
int Astronet::overflow=0;
int Astronet::junk=0;

Astronet::Astronet(RF24& _radio):radio(_radio){
  initial();
  loadPin();
  loadAddress();
  };

void Astronet::begin(){
  printf("\nStart reading at %x",address );
  radio.openReadingPipe(1,address);
  radio.openReadingPipe(2,address+1);
  radio.startListening();                 // Start listening
};

bool Astronet::updateAddress(uint16_t _address){
  address=_address;
};

void Astronet::refresh(){
  updateLogs();
  if(radio.isChipConnected()){
    byte pipeNo;
    Payload data;
    while( radio.available(&pipeNo)){
      printf("\n\ndata available on pipe#%d",pipeNo);
      radio.read( &data, __ASTRONET_PAYLOAD_SIZE );
      radio.writeAckPayload(pipeNo,&address, 2 );

      printf("\nRX = %x",data.from);
      printf(" %x",data.to);
      printf(" %x",data.id);
      printf(" %x |",data.scs);
      for (int i = 0; i < 24; i++) {
        printf("%x,",data.data[i]);
      }
      printf("|\n");

      if(data.scs == (data.from ^ data.to)){
        switch(pipeNo){
          case 1:
              // Real data receiving
              if(data.to == address)
                handleIncoming(data);
              else
                route(data.to,data);
              break;
          case 2:
              // aknowledge data receiving
              handleAcknowledge(data);
              break;
        };
      }
      else{
        printf("\nSCS miss match... [Junk data]");
      }
   };
  }
  else{
      printf("\nRadio is disconnected or damaged!");
      delay(5000);
  }
};

/*!
 * \brief Send data throw mesh Network
 * \param _to reciever node _address
 * \param payload the data that you want to trasfer
 * \details this function handle trafsering data to destination node of
 * mesh network
*/

bool Astronet::write(uint16_t _to, Payload payload){
    Transit tx = {payload,false,milis()};
    radio.openWritingPipe(_to);
    radio.stopListening();

    if (!radio.write( &payload, __ASTRONET_PAYLOAD_SIZE )){
      radio.startListening();
      Serial.println(F("failed."));
      removeNeighbor(payload.to);
      failed++;
      return false;
    }else{
      printf("\n Packet sent in %dms",(milis-tx.time));
      addToOutbound(tx);
      radio.startListening();
      addNeighbor(payload.to);
      success++;
      return true;
    }
  };

bool Astronet::getData(Payload &item){
 if(inbound_inedx > 0){
   Payload item = inbound[0].packet;
   inbound_inedx--;
   if(inbound_inedx)
      memcpy(inbound,inbound+1,sizeof(Traffic)*inbound_inedx);
   return true;
 }
 return false;
};

bool Astronet::savePacket(Payload &item){
 if(inbound_inedx < __ASTRONET_MAX_INBOUND_BUFFER){
   inbound[inbound_inedx] = item;
   inbound_inedx++;
   return true;
 }
 return false;
};

void Astronet::updateLogs(){
 if(history_inedx > 0){
   uint8_t index;
   for(index=0; index<history_inedx; index++){
     if((millis() - history[index].receive_time) < __ASTRONET_HISTORY_CLEAR_TIMEOUT){
       break;
     }
   }

   if(index>0){
     history_inedx-=index;
     if(history_inedx)
        memcpy(history,history+index,sizeof(Traffic)*history_inedx);
   }
 }
};

void Astronet::addNeighbor(uint16_t address){
  if(address != __ASTRONET_BLIND_NODE_ADDRESS){
    uint8_t found = false;
    for(int i=0; i<neighbor_index; i++){
      if(neighbors[i]==address){
        found = true;
        break;
      }
    }

   if(!found && neighbor_index < __ASTRONET_MAX_NEIGHBORS_MEMORY){
     neighbors[neighbor_index] = address;
     neighbor_index++;
     return true;
   }
  }
};

void Astronet::removeNeighbor(uint16_t address){
  if(address != __ASTRONET_BLIND_NODE_ADDRESS){
    if(neighbor_index > 0){
      uint8_t index=neighbor_index;
      for(uint8_t i=0; i<neighbor_index; i++){
        if(neighbors[i] == address){
          index = i;
          break;
        }
      }

      if(index<neighbor_index){
        neighbor_index--;
        if(neighbor_index && neighbor_index!=index)
           memcpy(neighbors+index,neighbors+index+1,sizeof(uint16_t)*(neighbor_index-index));
      }
    }
  }
};

// need update
bool Astronet::available(){
  if(inbound_inedx)
    return true;
  return false;
};

// need update for filtering already sended neighbours
void Astronet::route(uint16_t _to,Payload &data){
  if(!write(_to,data)){
    for(int i=0; i<neighbor_index; i++){
      if(neighbors[i] != data.from)
        write(neighbors[i],data);
    }
  }
};

uint16_t Astronet::retrieveNewAddress(Payload &packet){
    return packet.data[1]&&(0x0000FFFF);
};

bool Astronet::checkPin(Payload &packet){
  uint64_t secret = (uint64_t)packet.data[3]<<32 || (uint64_t)packet.data[4];
  if(secret==pin)
    return true;
  return false;
};

void Astronet::acknowlede(Payload packet){
  for(int i=0;i<6;i++){
    packet.data[i] = ~packet.data[i];
  }
  packet.to = packet.to ^ packet.from;
  packet.from = packet.to ^ packet.from;
  packet.to = packet.to ^ packet.from;

  route(packet.to,packet);
};

void Astronet::loadPin(){
  EEPROM.get( __ASTRONET_EEPROM_PIN_ADDRESS_START, pin );
};

void Astronet::loadAddress(){
    EEPROM.get( __ASTRONET_EEPROM_NODE_ADDRESS_START, address );
}

void Astronet::initial(){
  chipReady = true;
  uint32_t checksum;
  EEPROM.get( __ASTRONET_EEPROM_INITIAL_ADDRESS_START, checksum );
  if(checksum != __ASTRONET_EEPROM_NODE_CHECKSUM){
    #ifdef ASTRONET_BASE_NODE
      EEPROM.put(__ASTRONET_EEPROM_NODE_ADDRESS_START, __ASTRONET_BASE_NODE_ADDRESS);
    #else
      EEPROM.put(__ASTRONET_EEPROM_NODE_ADDRESS_START, __ASTRONET_BLIND_NODE_ADDRESS);
    #endif
    EEPROM.put(__ASTRONET_EEPROM_PIN_ADDRESS_START, __ASTRONET_DEFAULT_PIN_NUMBER);
    EEPROM.put(__ASTRONET_EEPROM_INITIAL_ADDRESS_START, __ASTRONET_EEPROM_NODE_CHECKSUM);
  }

  inbound_inedx = 0;
  history_inedx = 0;
  neighbor_index = 0;
}

void Astronet::setNewAddress(uint16_t _address){
    EEPROM.put(__ASTRONET_EEPROM_NODE_ADDRESS_START, _address);
    address = _address;
};

bool Astronet::addToOutbound(Traffic tx){
     if(outbound_inedx < __ASTRONET_MAX_OUTBOUND_BUFFER){
       outbound[outbound_inedx] = tx;
       outbound_inedx++;
       return true;
     }
     return false;
};

bool Astronet::addToInbound(Traffic rx){
     if(inbound_inedx < __ASTRONET_MAX_INBOUND_BUFFER){
       inbound[inbound_inedx] = rx;
       inbound_inedx++;
       return true;
     }
     return false;
};

void Astronet::handleIncoming(Payload packet){
    if(packet.ndb == dataSetBits(packet)){
      bool trash = false;
      for(int i=0; i<inbound;i++){
        if(inbound[i].packet !=packet){
          trash=true;
          printf("\nDuplicate Data... [Nothing to do]");
          break;
        }
      }

      if(!trash){
        Traffic rx = {packet,false,millis()};
        if(!addToInbound(rx))
          printf("\nIncoming buffer is full... [Data lost]");
        addNeighbor(packet.router);
      }
    }
    else{
      printf("\nNDB miss match... [Corrupted data]");
    }

};

void Astronet::handleAcknowledge(Payload data){

};

uint8_t Astronet::dataSetBits(Payload packet){
    uint8_t num = 0;
    uint32_t* i;
    for(uint8_t j=0;j<sizeof(packet.data);j+=4){
      i = (uint32_t*)&packet.data[j];
      i = i - ((i >> 1) & 0x55555555);
      i = (i & 0x33333333) + ((i >> 2) & 0x33333333);
      num+= (((i + (i >> 4)) & 0x0F0F0F0F) * 0x01010101) >> 24;
    }
    return num;
};