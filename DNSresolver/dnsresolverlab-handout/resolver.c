#include<arpa/inet.h>
#include<netinet/in.h>
#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<sys/types.h>
#include<sys/socket.h>
#include <time.h>
#include <stdbool.h>
#include <unistd.h>

typedef unsigned int dns_rr_ttl;
typedef unsigned short dns_rr_type;
typedef unsigned short dns_rr_class;
typedef unsigned short dns_rdata_len;
typedef unsigned short dns_rr_count;
typedef unsigned short dns_query_id;
typedef unsigned short dns_flags;

typedef struct {
   char *name;
   dns_rr_type type;
   dns_rr_class class;
   dns_rr_ttl ttl;
   dns_rdata_len rdata_len;
   unsigned char *rdata;
} dns_rr;

struct dns_answer_entry;
struct dns_answer_entry {
   char *value;
   struct dns_answer_entry *next;
};
typedef struct dns_answer_entry dns_answer_entry;

#define MAX_LENGTH 2048

void print_bytes(unsigned char *bytes, int byteslen) {
   int i, j, byteslen_adjusted;
   
   if (byteslen % 8) {
      byteslen_adjusted = ((byteslen / 8) + 1) * 8;
   } else {
      byteslen_adjusted = byteslen;
   }
   for (i = 0; i < byteslen_adjusted + 1; i++) {
      if (!(i % 8)) {
         if (i > 0) {
            for (j = i - 8; j < i; j++) {
               if (j >= byteslen_adjusted) {
                  printf("  ");
               } else if (j >= byteslen) {
                  printf("  ");
               } else if (bytes[j] >= '!' && bytes[j] <= '~') {
                  printf(" %c", bytes[j]);
               } else {
                  printf(" .");
               }
            }
         }
         if (i < byteslen_adjusted) {
            printf("\n%02X: ", i);
         }
      } else if (!(i % 4)) {
         printf(" ");
      }
      if (i >= byteslen_adjusted) {
         continue;
      } else if (i >= byteslen) {
         printf("   ");
      } else {
         printf("%02X ", bytes[i]);
      }
   }
   printf("\n");
}

void canonicalize_name(char *name) {
   /*
    * Canonicalize name in place.  Change all upper-case characters to
    * lower case and remove the trailing dot if there is any.  If the name
    * passed is a single dot, "." (representing the root zone), then it
    * should stay the same.
    *
    * INPUT:  name: the domain name that should be canonicalized in place
    */
   
   int namelen, i;
   
   // leave the root zone alone
   if (strcmp(name, ".") == 0) {
      return;
   }
   
   namelen = strlen(name);
   // remove the trailing dot, if any
   if (name[namelen - 1] == '.') {
      name[namelen - 1] = '\0';
   }
   
   // make all upper-case letters lower case
   for (i = 0; i < namelen; i++) {
      if (name[i] >= 'A' && name[i] <= 'Z') {
         name[i] += 32;
      }
   }
}

int name_ascii_to_wire(char *name, unsigned char *wire) {
   /*
    * Convert a DNS name from string representation (dot-separated labels)
    * to DNS wire format, using the provided byte array (wire).  Return
    * the number of bytes used by the name in wire format.
    *
    * INPUT:  name: the string containing the domain name
    * INPUT:  wire: a pointer to the array of bytes where the
    *              wire-formatted name should be constructed
    * OUTPUT: the length of the wire-formatted name.
    */
   const char separator[2] = "."; //Size 2 since it has a char and it needs to be null terminated
   
   int wireLength = 0; //Keeps track of length of the wire array
   
   char *token; //holds pointer to beginning of tokens in string
   
   token = strtok(name, separator); //Takes str name and separes tokens by a '.'
   
   while (token) { //adds token to wire and then gets the rest to add to wire till token is NULL
      unsigned char tokenLength = (unsigned char)strlen(token); //gets the length of the token
      *wire = tokenLength; //sets the pointer index of the wire to the length of token to add
      wire++; //add 1 to array so we are now pointing at new area to add to
      wireLength++; //add 1 to overall length of wire-formatted name
      
      for (unsigned char i = 0; i < tokenLength; i++) { //add chars to the wire array
         *wire = (unsigned char)token[i]; //adds char in token at i to index *wire is pointing to
         wire++; //add 1 to array so we are now pointing at new area to add to
         wireLength++; //add 1 to overall length of wire-formatted name
      }
      
      token = strtok(NULL, separator); //get next token, NULL is used since tokens are coming from name string still
   }
   
   return wireLength;
}

//Wrapper function used to convert next two unsigned chars in network order in wire array to shorts in host order
unsigned short NtoHs(unsigned char *wire, int byteOffset) {
   unsigned short numOfBytes = 2;
   unsigned char next2Bytes[] = {wire[byteOffset++], wire[byteOffset]}; //array with chars in big-endian order
   unsigned short netShort; //holds pointer to memcpy area
   
   memcpy(&netShort, next2Bytes, numOfBytes); //copies 2 bytes from next2Chars to *shortConversion
   
   return ntohs(netShort); //converts unsigend short netShort from network byte order to host byte order
}

char *name_ascii_from_wire(unsigned char *wire, int *indexp) {
   /*
    * Extract the wire-formatted DNS name at the offset specified by
    * *indexp in the array of bytes provided (wire) and return its string
    * representation (dot-separated labels) in a char array allocated for
    * that purpose.  Update the value pointed to by indexp to the next
    * value beyond the name.
    *
    * INPUT:  wire: a pointer to an array of bytes
    * INPUT:  indexp, a pointer to the index in the wire where the
    *              wire-formatted name begins
    * OUTPUT: a string containing the string representation of the name,
    *              allocated on the heap.
    */
   unsigned int sizeOfNameBuffer = 200; //how much memory is needed to hold the string representation of the name on the heap
   char *name = (char*)malloc(sizeOfNameBuffer); //puts name array on the heap
   int nameIndex = 0; //holds the index for name array
   int c0hexToInt = 192;
   
   while (wire[*indexp]) { //while the value in wire at indexp points to something that isn't NULL
      /*192 in hex is c0 which means that the name is compressed and needs to be found based on the next
       byte that points to where the name starts in the query part of the wire. */
      if (wire[*indexp] >= c0hexToInt) {
         (*indexp)++; //now we are pointing to the next byte in the response
         unsigned char beginIndexOfName = wire[*indexp]; //get the index of where the name starts
         
         while (wire[beginIndexOfName]) { //loop to get whole domain query name from compression
            if (wire[beginIndexOfName] >= c0hexToInt) { //it is still compressed and we need to find where to look
               beginIndexOfName++; //look at next byte to see where offset of name will be
               int nameOffset = (int)wire[beginIndexOfName]; //grab name offset
               char *compressedName = name_ascii_from_wire(wire, &nameOffset); //recursively call till name is found if compressed alot
               int lengthOfCompressedName = strlen(compressedName); //get length of name string
               
               for (int i = 0; i < lengthOfCompressedName; i++) { //iterate through string and add it to name on heap
                  name[nameIndex++] = compressedName[i]; //assign char at i to index in name and increment the nameIndex by 1
               }
               break; //name has been found and don't need to look anymore
            }
            else { //it isn't compressed more so we can just add the to name array on heap
               char charLength = wire[beginIndexOfName++]; //grab how big char array is
               for (int i = 0; i < charLength; i++) {
                  //add char from wire at beginIndexOfName to name at nameIndex and increment both indecies by 1
                  name[nameIndex++] = wire[beginIndexOfName++];
               }
               name[nameIndex++] = '.'; //found end of word append a dot to it
            }
         }
         break; //compression is done name has been found
      }
      else { //name is not compressed
         char charLength = wire[(*indexp)++]; //section length is right what indexp is pointing to since no compression
         for (int i = 0; i < charLength; i++) {
            //add char from wire at *indexp to name at nameIndex and increment both indecies by 1
            name[nameIndex++] = wire[(*indexp)++];
         }
         name[nameIndex++] = '.'; //found end of the word append a dot to it
      }
   }
   (*indexp)++; //increment indexp to point to the next thing in the wire array
   
   return name;
}

dns_rr rr_from_wire(unsigned char *wire, int *indexp, int query_only) {
   /*
    * Extract the wire-formatted resource record at the offset specified by
    * *indexp in the array of bytes provided (wire) and return a
    * dns_rr (struct) populated with its contents. Update the value
    * pointed to by indexp to the next value beyond the resource record.
    *
    * INPUT:  wire: a pointer to an array of bytes
    * INPUT:  indexp: a pointer to the index in the wire where the
    *              wire-formatted resource record begins
    * INPUT:  query_only: a boolean value (1 or 0) which indicates whether
    *              we are extracting a full resource record or only a
    *              query (i.e., in the question section of the DNS
    *              message).  In the case of the latter, the ttl,
    *              rdata_len, and rdata are skipped.
    * OUTPUT: the resource record (struct)
    */
   dns_rr resourceRecord; //holds the dns_rr resouce record
   unsigned int timeToLiveSize = 4; //time to live is 4 bytes here but can be up to 8 bytes
   unsigned char TTLBytes[timeToLiveSize]; //holds the bytes for the time to live in each index
   int moveIndex = 2; //move the index by two since type and class are only two bytes here
   int ttl; //holds the pointer to 4 bytes of time to live
   dns_rr_type type1 = 1;
   dns_rr_type type5 = 5;
   
   resourceRecord.name = name_ascii_from_wire(wire, indexp); //get DNS name of and set as resource name
   resourceRecord.type = NtoHs(wire, *indexp); //set the type of the record and do it in host order
   *indexp += moveIndex; //move the indexp in wire to the class
   resourceRecord.class = NtoHs(wire, *indexp); //set the class of the record and do it in host order
   *indexp += moveIndex; //move the indexp in wire to time to live
   if (!query_only) {
      for (int i = 0; i < timeToLiveSize; i++) { //this loop sets each byte of the TTL from the wire
         TTLBytes[i] = wire[(*indexp)++]; //store what is pointed to by indexp in wire in TTL[i] and then increment indexp by 1
      }
   
      memcpy(&ttl, TTLBytes, timeToLiveSize); //copies 4 bytes from TTL array and stores it in ttl
   
      resourceRecord.ttl = ntohl(ttl); //converts ttl from network byte order to host byte order
   
      resourceRecord.rdata_len = NtoHs(wire, *indexp); //grab the size of the answer which is two bytes
      *indexp += moveIndex; //since Rdata Length is 2 bytes move two bits over
   
      unsigned char *rData = (unsigned char *)malloc(resourceRecord.rdata_len); //make byte array that is rdata_len on the heap
   
      if (resourceRecord.type == type1) { // type 1 IPv4 address so answer is right after rdata_len
         for (int i = 0; i < resourceRecord.rdata_len; i++) {
            //Store in rData[i] what is in the wire at indexp then increment by 1
            rData[i] = wire[(*indexp)++];
         }
      }
      else if (resourceRecord.type == type5) { //type 5 alias so answer is in compression form and needs to be found
         rData = (unsigned char *) name_ascii_from_wire(wire, indexp); //get name (bytes) of answer from wire
      }
   
      resourceRecord.rdata = rData; //set the RData in resource record
   }
   return resourceRecord;
}


int rr_to_wire(dns_rr rr, unsigned char *wire, int query_only) {
   /*
    * Convert a DNS resource record struct to DNS wire format, using the
    * provided byte array (wire).  Return the number of bytes used by the
    * name in wire format.
    *
    * INPUT:  rr: the dns_rr struct containing the rr record
    * INPUT:  wire: a pointer to the array of bytes where the
    *             wire-formatted resource record should be constructed
    * INPUT:  query_only: a boolean value (1 or 0) which indicates whether
    *              we are constructing a full resource record or only a
    *              query (i.e., in the question section of the DNS
    *              message).  In the case of the latter, the ttl,
    *              rdata_len, and rdata are skipped.
    * OUTPUT: the length of the wire-formatted resource record.
    *
    */
   int lengthOfResourceRecord = 0; //keeps track of bytes used
   
   if (query_only) {
      dns_rr_class class = rr.class; //set class of dns_rr
      unsigned char typeByte1 = 0x00; //first type byte 00
      unsigned char typeByte2 = 0x01; //second type byte 01
      
      unsigned char classByte1 = *((unsigned char *)&class); //first class byte
      unsigned char classByte2 = *((unsigned char *)&class + 1); //move by 1 in class and grav second class byte
      
      *wire = typeByte1; //store 00 as first byte on wire
      lengthOfResourceRecord++; //added a byte
      wire++; //move over one to store next byte
      *wire = typeByte2; //store 01 as second byte on wire for query to make type 00 01 which is IPv4 type query
      lengthOfResourceRecord++; //added a byte
      wire++; //move over one to store next byte
      
      *wire = classByte1; //store first class byte on wire
      lengthOfResourceRecord++; //added a byte
      wire++; //move over one to store next byte
      *wire = classByte2; //store second class byte on wire
      lengthOfResourceRecord++; //added a byte
      
      return lengthOfResourceRecord; //number of bytes added
   }
   else { //called on not a query which shouldn't happen
      fprintf(stderr, "rr_to_wire was called for a non-query RR.\n");
      return 0;
   }
}

unsigned short create_dns_query(char *qname, dns_rr_type qtype, unsigned char *wire) {
   /*
    * Create a wire-formatted DNS (query) message using the provided byte
    * array (wire).  Create the header and question sections, including
    * the qname and qtype.
    *
    * INPUT:  qname: the string containing the name to be queried
    * INPUT:  qtype: the integer representation of type of the query (type A == 1)
    * INPUT:  wire: the pointer to the array of bytes where the DNS wire
    *               message should be constructed
    * OUTPUT: the length of the DNS wire message
    */
   dns_rr_class RRClass = htons(0x0001); //converts host order to network order holds resource record class
   dns_flags flags = htons(0x0100); //converts host order to network order will set the RD bit as 1 and others will be 0
   unsigned short lengthOfMessage = 0;
   unsigned char hex0 = 0x00;
   unsigned char hex1 = 0x01;
   
   srand(time(NULL));
   dns_query_id queryID = (unsigned short)rand(); //makes random query ID 2 bytes long
   
   unsigned char IDbyte1 = *((unsigned char *)&queryID); //holds the first byte of query ID
   unsigned char IDbyte2 = *((unsigned char *)&queryID + 1); //holds the second byte of the query ID
   unsigned char flagsByte1 = *((unsigned char *)&flags); //holds first byte of the flags
   unsigned char flagsByte2 = *((unsigned char *)&flags + 1); //holds second byte of the flags
   
   //make wire to hold query. Start with queryID
   *wire = IDbyte1;
   wire++; //move pointer over by one
   lengthOfMessage++; //increment by 1 for total length of DNS wire message
   *wire = IDbyte2;
   wire++; //move pointer over by one
   lengthOfMessage++; //increment by 1 for total length of DNS wire message
   
   //add flags to query
   *wire = flagsByte1;
   wire++; //move pointer over by one
   lengthOfMessage++; //increment by 1 for total length of DNS wire message
   *wire = flagsByte2;
   wire++; //move pointer over by one
   lengthOfMessage++; //increment by 1 for total length of DNS wire message
   
   //add total questions which is one
   *wire = hex0; //first byte of total questions
   wire++; //move pointer over by one
   lengthOfMessage++; //increment by 1 for total length of DNS wire message
   *wire = hex1; //second byte of total questions
   wire++; //move pointer over by one
   lengthOfMessage++; //increment by 1 for total length of DNS wire message
   
   //No Answer, Authority, or Additional RRs in header these are each 2 bytes long so a
   // total of 6 bytes that are just 0x0
   int numberOfRRBytes = 6; //total bytes from all RRs in header
   for (int i = 0; i < numberOfRRBytes; i++) {
      *wire = hex0; //byte is 0
      wire++; //move pointer over by one
      lengthOfMessage++; //increment by 1 for total length of DNS wire message
   }
   
   //Make query name be an unsigned char[]
   int numOfNameBytes = name_ascii_to_wire(qname, wire); //get length of the wire name
   
   if (!numOfNameBytes) { //if numOfNameBytes is 0 there was a problem so exit
      exit(EXIT_FAILURE);
   }
   
   lengthOfMessage += (unsigned short)numOfNameBytes; //adding the length in bytes of the name to total length of query
   wire += numOfNameBytes; //move pointer in wire to next empty part of the array
   
   *wire = hex0; //reach end of name so append 00 so we know name is done
   wire++; //move pointer over by one
   lengthOfMessage++; //increment by 1 for total length of DNS wire message
   
   //add type and class to query
   dns_rr resourceRecord;
   resourceRecord.class = RRClass; //class is 1
   resourceRecord.type = qtype; //RR type
   
   int rrBytes = rr_to_wire(resourceRecord, wire, true); //make resource record and get back how many bytes it is
   
   if (!rrBytes) { //if rrBytes is 0 there was a problem so exit
      exit(EXIT_FAILURE);
   }
   
   lengthOfMessage += (unsigned short)rrBytes; //adding the length in bytes of the resource record to total length of query
   wire += rrBytes; //move pointer in wire to next empty part of the array
   
   return lengthOfMessage;
}

dns_answer_entry *get_answer_address(char *qname, dns_rr_type qtype, unsigned char *wire) {
   /*
    * Extract the IPv4 address from the answer section, following any
    * aliases that might be found, and return the string representation of
    * the IP address.  If no address is found, then return NULL.
    *
    * INPUT:  qname: the string containing the name that was queried
    * INPUT:  qtype: the integer representation of type of the query (type A == 1)
    * INPUT:  wire: the pointer to the array of bytes representing the DNS wire message
    * OUTPUT: a linked list of dns_answer_entrys the value member of each
    * reflecting either the name or IP address.  If
    */
   int byteOffset = 0; //will be used to move through bytes in the wire
   unsigned char responseHeaderByte1 = 0x81; //usual response header byte
   unsigned char responseHeaderByte2 = 0x80; //usual response header byte
   unsigned char numberOfQsByte1 = 0x00; //first byte of number of questions
   unsigned char numberOfQsByte2 = 0x01; //second byte, question will just be 1 here always
   unsigned char endOfQuery = 0x00; //question always ends with 0
   int skipToCompressionName = 5; //skip to the compression in the answer
   dns_rr_type typeIsAlias = 5; //RR type is an alias
   int incrBy2 = 2;
   byteOffset += incrBy2; //don't need the query ID so move by two since ID is two bytes
   
   if (wire[byteOffset++] != responseHeaderByte1) { //check first header byte and move by one in wire
      //fprintf(stderr, "Wrong header byte 1\n"); for debugging
   }
   
   if (wire[byteOffset++] != responseHeaderByte2) { //check second header byte and move by one in wire
      //fprintf(stderr, "Wrong header byte 2\n"); for debugging
   }
   
   if (wire[byteOffset++] != numberOfQsByte1) { //check first byte of num of Qs and move by one in wire
      //fprintf(stderr, "Wrong first byte in number of questions\n"); for debugging
   }
   
   if (wire[byteOffset++] != numberOfQsByte2) { //check second byte of num of Qs and move by one in wire
      //fprintf(stderr, "Wrong second byte in number of questions\n"); for debugging
   }
   
   //now get the number of responses/resoure record count
   unsigned short RRCount = NtoHs(wire, byteOffset); //store num of responses in responseCount to host order
   byteOffset += incrBy2; //increment by 2 since NtoHs reads two bytes at a time
   unsigned short totalAuthorityRRs = NtoHs(wire, byteOffset); //store num of authority records in host order
   byteOffset += incrBy2; //increment by 2 since NtoHs reads two bytes at a time
   unsigned short totalAdditionalRRs = NtoHs(wire, byteOffset); //store num of additional records in host order
   byteOffset += incrBy2; //increment by 2 since NtoHs reads two bytes at a time
   
   unsigned short totalRRs = RRCount + totalAuthorityRRs + totalAdditionalRRs; //get total number of RRs
   
   if (!totalRRs) { //if total count is 0 then no address found and return NULL
      return NULL;
   }
   
   //this while loop skips over the question since it isn't needed
   while (wire[byteOffset] != endOfQuery) { //keep iterating byteOffset till find end of question
      unsigned char sectionLength = wire[byteOffset++]; //find how many chars there are in this line
      byteOffset += sectionLength; //this speeds up the loop to move the index in the wire to the end of a word
   }
   
   /*Now we are at the end of the characters and have the type, class and first byte of the compression
    so since type and class are always 1 for a query just skip them, also we skip over the first byte since
    we look at if it is compressed else where in the code*/
   byteOffset += skipToCompressionName; //now we are looking at the start of the name which will be looked in rr_from_wire
   
   //Now we can start extracting RRs and making those structs
   dns_rr RRarray[totalRRs]; //make array that will have all RRs in it
   int arrayIndex = 0; //index to the RRarray
   dns_answer_entry *answerEntries = NULL; //used to point to linked list of answer enteries
   dns_answer_entry *nextEntry = NULL; //used to hold the next part of the linked list so answerEntries can point to nextEntry in its *next variable
   
   //Get all of the Resoure records
   for (; arrayIndex < totalRRs; arrayIndex++) {
      //get RRs from wire starting at byteOffset starting at compressed name and what whole recond since it isn't a query
      RRarray[arrayIndex] = rr_from_wire(wire, &byteOffset, false);
   }
   
   //Use RRarray to create linked list
   for (int i = 0; i < arrayIndex; i++) {
      if (RRarray[i].type == qtype || RRarray[i].type == typeIsAlias) {
         if (i == 0) { //we are at the beginning of the linked list so initialize first pointer of answerEntries
            nextEntry = (dns_answer_entry *)malloc(sizeof(dns_answer_entry));
            //store the next entry pointer
            answerEntries = nextEntry;
         }
         else {
            nextEntry->next = (dns_answer_entry *)malloc(sizeof(dns_answer_entry)); //create next entry pointer
            nextEntry = nextEntry->next; //change the nextEntry to be the next in the list
            nextEntry->next = NULL; //have the nextEntry value of next be null the end of the list
         }
         
         if (RRarray[i].type == qtype) { //type is qtype
            nextEntry->value = (char *)malloc(INET_ADDRSTRLEN); //allocate the value record to the right length IPv4
            /*This converts the netword address RRarray[i].rdata to AF_INET (IPv4)
             into a char string. The string is copied to nextEntry->value,
             INET_ADDRSTRLEN says the number of bytes available in nextEntry->value
             It converts struct in_addr to a dotted-decimal format*/
            inet_ntop(AF_INET, RRarray[i].rdata, nextEntry->value, INET_ADDRSTRLEN);
         }
         else if (RRarray[i].type == typeIsAlias) {
            canonicalize_name((char *)RRarray[i].rdata); //changes the name
            nextEntry->value = (char *)RRarray[i].rdata; //sets value in nextEntry
         }
      }
   }
   
   return answerEntries;
}

int send_recv_message(unsigned char *request, int requestlen, unsigned char *response, char *server, unsigned short port) {
   /*
    * Send a message (request) over UDP to a server (server) and port
    * (port) and wait for a response, which is placed in another byte
    * array (response).  Create a socket, "connect()" it to the
    * appropriate destination, and then use send() and recv();
    *
    * INPUT:  request: a pointer to an array of bytes that should be sent
    * INPUT:  requestlen: the length of request, in bytes.
    * INPUT:  response: a pointer to an array of bytes in which the
    *             response should be received
    * OUTPUT: the size (bytes) of the response received
    */
   struct sockaddr_in ip4addr; //struct that holds the ipv4 address and its info
   ip4addr.sin_family = AF_INET; //protocol family is IPv4 of the struct
   ip4addr.sin_port = htons(port); //converts from host order to network order
   
   /* Converts the char string server into a network address struct
    in the AF_INET family and the ncopies the network addr to ip4addr.sin_addr*/
   inet_pton(AF_INET, server, &ip4addr.sin_addr);
   
   /* Creates a socket the has IPv4 protocols, and supports unreliable datagrams,
    0 is from the type since type doesn't change here it will always be 0*/
   int serverfd = socket(AF_INET, SOCK_DGRAM, 0);
   
   /* Connects serverfd to the ip4addr adress that has length sockaddr_in*/
   if (connect(serverfd, (struct sockaddr *)&ip4addr, sizeof(struct sockaddr_in)) < 0) { //-1 if this system call fails
      fprintf(stderr, "Can't connect\n");
      exit(EXIT_FAILURE);
   }
   /* Writes up to requestlen from the request buffer to the
    socket serverfd it returns number of bytes written so we check to ensure all were written */
   if (write(serverfd, request, requestlen) != requestlen) {
      fprintf(stderr, "Unable to transfer full request to server\n");
      exit(EXIT_FAILURE);
   }
   
   //Reads up to MAX_LENGTH from the serverfd into the response buffer
   int readBytes = read(serverfd, response, MAX_LENGTH);
   
   if (readBytes == -1) { //the read call failed
      fprintf(stderr, "Read error!\n");
      exit(EXIT_FAILURE);
   }
   
   return readBytes;
}

dns_answer_entry *resolve(char *qname, char *server) {
   unsigned char queryWireBegin[MAX_LENGTH]; //make a beginning query wire of max_size
   dns_rr_type qtype = 1; //type for a query will ALWAYS be 1
   unsigned short dnsPort = 53; //dns well-known port is 53
   
   unsigned short wireLength = create_dns_query(qname, qtype, queryWireBegin); //makes DNS query and store in queryWireBegin and returns length of query
   
   unsigned char queryWireFinal[wireLength]; //create a wire array that is the size of the whole query so you don't have extra uneeded space in wire
   
   for (int i = 0; i < wireLength; i++) {
      //go through length of queryWireFinal and add all the bytes from the queryWireBegin
      queryWireFinal[i] = queryWireBegin[i];
   }
   
   //Send query and store response in responseWire
   unsigned char responseWire[MAX_LENGTH]; //don't know how long this will be
   
   //Receives messages from server and stores it in responseWire DNS well known port is 53
   send_recv_message(queryWireFinal, wireLength, responseWire, server, dnsPort);
   
   return get_answer_address(qname, qtype, responseWire); //returns the all the RR entries as a linked list
}

int main(int argc, char *argv[]) {
   dns_answer_entry *ans;
   if (argc < 3) {
      fprintf(stderr, "Usage: %s <domain name> <server>\n", argv[0]);
      exit(1);
   }
   ans = resolve(argv[1], argv[2]);
   while (ans != NULL) {
      printf("%s\n", ans->value);
      ans = ans->next;
   }

   while (ans != NULL) { //cleans up address list
   	free(ans->value);
   	ans = ans->next;
   	free(ans->next);
   }
}
