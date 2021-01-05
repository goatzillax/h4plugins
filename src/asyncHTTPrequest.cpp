//#include<H4P_WiFiSelect.h>
#include "asyncHTTPrequest.h"

//**************************************************************************************************************
asyncHTTPrequest::asyncHTTPrequest()
    : _readyState(readyStateUnsent)
    , _HTTPcode(0)
    , _chunked(false)
    , _debug(DEBUG_IOTA_HTTP_SET)
    , _timeout(DEFAULT_RX_TIMEOUT)
    , _lastActivity(0)
    , _requestStartTime(0)
    , _requestEndTime(0)
    , _URL(nullptr)
    , _connectedHost(nullptr)
    , _connectedPort(-1)
    , _client(nullptr)
    , _contentLength(0)
    , _contentRead(0)
    , _readyStateChangeCB(nullptr)
    , _readyStateChangeCBarg(nullptr)
    , _onDataCB(nullptr)
    , _onDataCBarg(nullptr)
    , _request(nullptr)
    , _response(nullptr)
    , _chunks(nullptr)
    , _headers(nullptr)
{}
    //DEBUG_HTTP("New request."); 

//**************************************************************************************************************
asyncHTTPrequest::~asyncHTTPrequest(){
    if(_client) _client->close(true);
    delete _URL;
    delete _headers;
    delete _request;
    delete _response;
    delete _chunks;
    delete[] _connectedHost;
}

//**************************************************************************************************************
void    asyncHTTPrequest::setDebug(bool debug){
    if(_debug || debug) {
        _debug = true;
        //DEBUG_HTTP("setDebug(%s) version %s\r\n", debug ? "on" : "off", asyncHTTPrequest_h);
    }
	_debug = debug;
}

//**************************************************************************************************************
bool    asyncHTTPrequest::debug(){
    return(_debug);
}

//**************************************************************************************************************
bool	asyncHTTPrequest::open(const char* method, const char* URL){
    //DEBUG_HTTP("open(%s, %.32s)\r\n", method, URL);
    if(_readyState != readyStateUnsent && _readyState != readyStateDone) {return false;}
    _requestStartTime = millis();
    delete _URL;
    delete _headers;
    delete _request;
    delete _response;
    delete _chunks;
    _URL = nullptr;
    _headers = nullptr;
    _response = nullptr;
    _request = nullptr;
    _chunks = nullptr;
    _chunked = false;
    _contentRead = 0;
    _readyState = readyStateUnsent;

    if (strcmp(method, "GET") == 0) {
        _HTTPmethod = HTTPmethodGET;
    } else if (strcmp(method, "POST") == 0) {
        _HTTPmethod = HTTPmethodPOST;
    } else
        return false;

    if (!_parseURL(URL)) {
        return false;}
    if( _client && _client->connected() && 
      (strcmp(_URL->host, _connectedHost) != 0 || _URL->port != _connectedPort)){return false;}
    char* hostName = new char[strlen(_URL->host)+10];
    sprintf(hostName,"%s:%d", _URL->host, _URL->port);  
    _addHeader("host",hostName);
    delete[] hostName;
    _lastActivity = millis();
	return _connect();
}
//**************************************************************************************************************
void    asyncHTTPrequest::onReadyStateChange(readyStateChangeCB cb, void* arg){
    _readyStateChangeCB = cb;
    _readyStateChangeCBarg = arg;
}

//**************************************************************************************************************
void	asyncHTTPrequest::setTimeout(int seconds){
    //DEBUG_HTTP("setTimeout(%d)\r\n", seconds);
    _timeout = seconds;
}

//**************************************************************************************************************
bool	asyncHTTPrequest::send(){
    //DEBUG_HTTP("send()\r\n");
    if( ! _buildRequest()) return false;
    _send();
    return true;
}

//**************************************************************************************************************
bool    asyncHTTPrequest::send(String body){
    //DEBUG_HTTP("send(String) %s... (%d)\r\n", body.substring(0,16).c_str(), body.length());
    _addHeader("Content-Length", String(body.length()).c_str());
    if( ! _buildRequest()) return false;
    _request->write(body);
    _send();
    return true;
}

//**************************************************************************************************************
bool	asyncHTTPrequest::send(const char* body){
    //DEBUG_HTTP("send(char*) %s.16... (%d)\r\n",body, strlen(body));
    _addHeader("Content-Length", String(strlen(body)).c_str());
    if( ! _buildRequest()) return false;
    _request->write(body);
   _send();
    return true;
}

//**************************************************************************************************************
bool	asyncHTTPrequest::send(const uint8_t* body, size_t len){
    //DEBUG_HTTP("send(char*) %s.16... (%d)\r\n",(char*)body, len);
    _addHeader("Content-Length", String(len).c_str());
    if( ! _buildRequest()) return false;
    _request->write(body, len);
    _send();
    return true;
}

//**************************************************************************************************************
bool	asyncHTTPrequest::send(xbuf* body, size_t len){
    //DEBUG_HTTP("send(char*) %s.16... (%d)\r\n", body->peekString(16).c_str(), len);
    _addHeader("Content-Length", String(len).c_str());
    if( ! _buildRequest()) return false;
    _request->write(body, len);
    _send();
    return true;
}

//**************************************************************************************************************
void    asyncHTTPrequest::abort(){
    //DEBUG_HTTP("abort()\r\n");
    if(! _client) return;
    _client->abort();
}
//**************************************************************************************************************
int		asyncHTTPrequest::readyState(){
    return _readyState;
}

//**************************************************************************************************************
int	asyncHTTPrequest::responseHTTPcode(){
    return _HTTPcode;
}

//**************************************************************************************************************
String	asyncHTTPrequest::responseText(){
    //DEBUG_HTTP("responseText() ");
    if( ! _response || _readyState < readyStateLoading || ! available()){
        //DEBUG_HTTP("responseText() no data\r\n");
        return String(); 
    }       
    String localString;
    size_t avail = available();
    if( ! localString.reserve(avail)) {
        //DEBUG_HTTP("!responseText() no buffer\r\n")
        _HTTPcode = HTTPCODE_TOO_LESS_RAM;
        _client->abort();
        return String();
    }
    localString = _response->readString(avail);
    _contentRead += localString.length();
    //DEBUG_HTTP("responseText() %s... (%d)\r\n", localString.substring(0,16).c_str() , avail);
    return localString;
}

//**************************************************************************************************************
size_t  asyncHTTPrequest::responseRead(uint8_t* buf, size_t len){
    if( ! _response || _readyState < readyStateLoading || ! available()){
        //DEBUG_HTTP("responseRead() no data\r\n");
        return 0;
    } 
    size_t avail = available() > len ? len : available();
    _response->read(buf, avail);
    //DEBUG_HTTP("responseRead() %.16s... (%d)\r\n", (char*)buf , avail);
    _contentRead += avail;
    return avail;
}

//**************************************************************************************************************
size_t	asyncHTTPrequest::available(){
    if(_readyState < readyStateLoading) return 0;
    if(_chunked && (_contentLength - _contentRead) < _response->available()){
        return _contentLength - _contentRead;
    }
    return _response->available();
}

//**************************************************************************************************************
size_t	asyncHTTPrequest::responseLength(){
    if(_readyState < readyStateLoading) return 0;
    return _contentLength;
}

//**************************************************************************************************************
void	asyncHTTPrequest::onData(onDataCB cb, void* arg){
    //DEBUG_HTTP("onData() CB set\r\n");
    _onDataCB = cb;
    _onDataCBarg = arg;
}

//**************************************************************************************************************
uint32_t asyncHTTPrequest::elapsedTime(){
    if(_readyState <= readyStateOpened) return 0;
    if(_readyState != readyStateDone){
        return millis() - _requestStartTime;
    }
    return _requestEndTime - _requestStartTime;
}

//**************************************************************************************************************
String asyncHTTPrequest::version(){
    return String(asyncHTTPrequest_h);
}

/*______________________________________________________________________________________________________________

               PPPP    RRRR     OOO    TTTTT   EEEEE    CCC    TTTTT   EEEEE   DDDD
               P   P   R   R   O   O     T     E       C   C     T     E       D   D
               PPPP    RRRR    O   O     T     EEE     C         T     EEE     D   D
               P       R  R    O   O     T     E       C   C     T     E       D   D
               P       R   R    OOO      T     EEEEE    CCC      T     EEEEE   DDDD
_______________________________________________________________________________________________________________*/

//**************************************************************************************************************
bool  asyncHTTPrequest::_parseURL(const char* url){
    return _parseURL(String(url));
}

//**************************************************************************************************************
bool  asyncHTTPrequest::_parseURL(String url){
    delete _URL;
    int hostBeg = 0;
    _URL = new URL;
    _URL->scheme = new char[8];
    strcpy(_URL->scheme, "HTTP://");
    if(url.substring(0,7).equalsIgnoreCase("HTTP://")){
        hostBeg += 7; 
    }
    else if(url.substring(0,8).equalsIgnoreCase("HTTPS://")){
        return false;
    }
    
    int pathBeg = url.indexOf('/', hostBeg);
    if(pathBeg < 0) return false;
    int hostEnd = pathBeg;
    int portBeg = url.indexOf(':',hostBeg);
    if(portBeg > 0 && portBeg < pathBeg){
        _URL->port = url.substring(portBeg+1, pathBeg).toInt();
        hostEnd = portBeg;
    }
    _URL->host = new char[hostEnd - hostBeg + 1];
    strcpy(_URL->host, url.substring(hostBeg, hostEnd).c_str());
    int queryBeg = url.indexOf('?');
    if(queryBeg < 0) queryBeg = url.length();
    _URL->path = new char[queryBeg - pathBeg + 1];
    strcpy(_URL->path, url.substring(pathBeg, queryBeg).c_str());
    _URL->query = new char[url.length() - queryBeg + 1];
    strcpy(_URL->query, url.substring(queryBeg).c_str());
    //DEBUG_HTTP("_parseURL() %s%s:%d%s%.16s\r\n", _URL->scheme, _URL->host, _URL->port, _URL->path, _URL->query);
    return true;
}

//**************************************************************************************************************
bool  asyncHTTPrequest::_connect(){
    //DEBUG_HTTP("_connect()\r\n");
    if( ! _client){
        _client = new AsyncClient();
    }
    delete[] _connectedHost;	
    _connectedHost = new char[strlen(_URL->host) + 1];
    strcpy(_connectedHost, _URL->host);
    _connectedPort = _URL->port;
    _client->onConnect([](void *obj, AsyncClient *client){((asyncHTTPrequest*)(obj))->_onConnect(client);}, this);
    _client->onDisconnect([](void *obj, AsyncClient* client){((asyncHTTPrequest*)(obj))->_onDisconnect(client);}, this);
    _client->onPoll([](void *obj, AsyncClient *client){((asyncHTTPrequest*)(obj))->_onPoll(client);}, this);
    _client->onError([](void *obj, AsyncClient *client, uint32_t error){((asyncHTTPrequest*)(obj))->_onError(client, error);}, this);
    if( ! _client->connected()){
        if( ! _client->connect(_URL->host, _URL->port)) {
            //DEBUG_HTTP("!client.connect(%s, %d) failed\r\n", _URL->host, _URL->port);
            _HTTPcode = HTTPCODE_NOT_CONNECTED;
            _setReadyState(readyStateDone);
            return false;
        }
    }
    else {
        _onConnect(_client);
    }
    _lastActivity = millis();
    return true;
}

//**************************************************************************************************************
bool   asyncHTTPrequest::_buildRequest(){
    //DEBUG_HTTP("_buildRequest()\r\n");
    
        // Build the header.

    if( ! _request) _request = new xbuf;
    _request->write(_HTTPmethod == HTTPmethodGET ? "GET " : "POST ");
   _request->write(_URL->path);
    _request->write(_URL->query);
   _request->write(" HTTP/1.1\r\n");
    delete _URL;
    _URL = nullptr;
    header* hdr = _headers;
    while(hdr){
        _request->write(hdr->name);
        _request->write(':');
        _request->write(hdr->value);
        _request->write("\r\n");
        hdr = hdr->next;
    }
    delete _headers;
    _headers = nullptr;
    _request->write("\r\n");

    return true;
}

//**************************************************************************************************************
size_t  asyncHTTPrequest::_send(){
    if( ! _request) return 0;
    //DEBUG_HTTP("_send() %d\r\n", _request->available());
    if( ! _client->connected() || ! _client->canSend()){
        //DEBUG_HTTP("*can't send\r\n");
        return 0;
    }
    size_t supply = _request->available();
    size_t demand = _client->space();
    if(supply > demand) supply = demand;
    size_t sent = 0;
    uint8_t* temp = new uint8_t[100];
    while(supply){
        size_t chunk = supply < 100 ? supply : 100;
        supply -= _request->read(temp, chunk);
        sent += _client->add((char*)temp, chunk);
    }
    delete temp;
    if(_request->available() == 0){
        delete _request;
        _request = nullptr;
    }
    _client->send();
    //DEBUG_HTTP("*sent %d\r\n", sent);
    _lastActivity = millis(); 
    return sent;
}

//**************************************************************************************************************
void  asyncHTTPrequest::_setReadyState(readyStates newState){
    if(_readyState != newState){
        _readyState = newState;          
        //DEBUG_HTTP("_setReadyState(%d)\r\n", _readyState);
        if(_readyStateChangeCB){
            _readyStateChangeCB(_readyStateChangeCBarg, this, _readyState);
        }
    } 
}

//**************************************************************************************************************
void  asyncHTTPrequest::_processChunks(){
    while(_chunks->available()){
        //DEBUG_HTTP("_processChunks() %.16s... (%d)\r\n", _chunks->peekString(16).c_str(), _chunks->available());
        size_t _chunkRemaining = _contentLength - _contentRead - _response->available();
        _chunkRemaining -= _response->write(_chunks, _chunkRemaining);
        if(_chunks->indexOf("\r\n") == -1){
            return;
        }
        String chunkHeader = _chunks->readStringUntil("\r\n");
        //DEBUG_HTTP("*getChunkHeader %.16s... (%d)\r\n", chunkHeader.c_str(), chunkHeader.length());
        size_t chunkLength = strtol(chunkHeader.c_str(),nullptr,16);
        _contentLength += chunkLength;
        if(chunkLength == 0){
            char* connectionHdr = respHeaderValue("connection");
            if(connectionHdr && (strcasecmp_P(connectionHdr,PSTR("disconnect")) == 0)){
                //DEBUG_HTTP("*all chunks received - closing TCP\r\n");
                _client->close();
            }
            else {
                //DEBUG_HTTP("*all chunks received - no disconnect\r\n"); 
            }
            _requestEndTime = millis();
            _lastActivity = 0;
            _timeout = 0;
            _setReadyState(readyStateDone);
            return;
        }
    }
}

/*______________________________________________________________________________________________________________

EEEEE   V   V   EEEEE   N   N   TTTTT         H   H    AAA    N   N   DDDD    L       EEEEE   RRRR     SSS
E       V   V   E       NN  N     T           H   H   A   A   NN  N   D   D   L       E       R   R   S 
EEE     V   V   EEE     N N N     T           HHHHH   AAAAA   N N N   D   D   L       EEE     RRRR     SSS
E        V V    E       N  NN     T           H   H   A   A   N  NN   D   D   L       E       R  R        S
EEEEE     V     EEEEE   N   N     T           H   H   A   A   N   N   DDDD    LLLLL   EEEEE   R   R    SSS 
_______________________________________________________________________________________________________________*/

//**************************************************************************************************************
void  asyncHTTPrequest::_onConnect(AsyncClient* client){
    //DEBUG_HTTP("_onConnect handler\r\n");
    _client = client;
    _setReadyState(readyStateOpened);
    _response = new xbuf;
    _contentLength = 0;
    _contentRead = 0;
    _chunked = false;
    _client->onAck([](void* obj, AsyncClient* client, size_t len, uint32_t time){((asyncHTTPrequest*)(obj))->_send();}, this);
    _client->onData([](void* obj, AsyncClient* client, void* data, size_t len){((asyncHTTPrequest*)(obj))->_onData(data, len);}, this);
    if(_client->canSend()){
        _send();
    }
    _lastActivity = millis();
}

//**************************************************************************************************************
void  asyncHTTPrequest::_onPoll(AsyncClient* client){
    if(_timeout && (millis() - _lastActivity) > (_timeout * 1000)){
        _client->close();
        _HTTPcode = HTTPCODE_TIMEOUT;
        //DEBUG_HTTP("_onPoll timeout\r\n");
    }
    if(_onDataCB && available()){
        _onDataCB(_onDataCBarg, this, available());
    }      
}

//**************************************************************************************************************
void  asyncHTTPrequest::_onError(AsyncClient* client, int8_t error){
    //DEBUG_HTTP("_onError handler error=%d\r\n", error);
    _HTTPcode = error;
}

//**************************************************************************************************************
void  asyncHTTPrequest::_onDisconnect(AsyncClient* client){
    //DEBUG_HTTP("_onDisconnect handler\r\n");
    if(_readyState < readyStateOpened){
        _HTTPcode = HTTPCODE_NOT_CONNECTED;
    }
    else if (_HTTPcode > 0 && 
            (_readyState < readyStateHdrsRecvd || (_contentRead + _response->available()) < _contentLength)) {
        _HTTPcode = HTTPCODE_CONNECTION_LOST;
    }
    delete _client;
    _client = nullptr;
    delete[] _connectedHost;
    _connectedHost = nullptr;
    _connectedPort = -1;
    _requestEndTime = millis();
    _lastActivity = 0;
    _setReadyState(readyStateDone);
}

//**************************************************************************************************************
void  asyncHTTPrequest::_onData(void* Vbuf, size_t len){
    //DEBUG_HTTP("_onData handler %.16s... (%d)\r\n",(char*) Vbuf, len);
    _lastActivity = millis();
    
                // Transfer data to xbuf

    if(_chunks){
        _chunks->write((uint8_t*)Vbuf, len);
        _processChunks();
    }
    else {
        _response->write((uint8_t*)Vbuf, len);                
    }

                // if headers not complete, collect them.
                // if still not complete, just return.

    if(_readyState == readyStateOpened){
        if( ! _collectHeaders()) return;
    }

                // If there's data in the buffer and not Done,
                // advance readyState to Loading.

    if(_response->available() && _readyState != readyStateDone){
        _setReadyState(readyStateLoading);
    }

                // If not chunked and all data read, close it up.

    if( ! _chunked && (_response->available() + _contentRead) >= _contentLength){
        char* connectionHdr = respHeaderValue("connection");
        if(connectionHdr && (strcasecmp_P(connectionHdr,PSTR("disconnect")) == 0)){
            //DEBUG_HTTP("*all data received - closing TCP\r\n");
            _client->close();
        }
        else {
            //DEBUG_HTTP("*all data received - no disconnect\r\n");
        }
        _requestEndTime = millis();
        _lastActivity = 0;
        _timeout = 0;
        _setReadyState(readyStateDone);        
    }

                // If onData callback requested, do so.

    if(_onDataCB && available()){
        _onDataCB(_onDataCBarg, this, available());
    }            
          
}

//**************************************************************************************************************
bool  asyncHTTPrequest::_collectHeaders(){
    //DEBUG_HTTP("_collectHeaders()\r\n");

            // Loop to parse off each header line.
            // Drop out and return false if no \r\n (incomplete)

    do {
        String headerLine = _response->readStringUntil("\r\n");

            // If no line, return false.

        if( ! headerLine.length()){
            return false;
        }  

            // If empty line, all headers are in, advance readyState.
           
        if(headerLine.length() == 2){
            _setReadyState(readyStateHdrsRecvd);
        }

            // If line is HTTP header, capture HTTPcode.

        else if(headerLine.substring(0,7) == "HTTP/1."){
            _HTTPcode = headerLine.substring(9, headerLine.indexOf(' ', 9)).toInt();
        }

            // Ordinary header, add to header list.

        else {
            int colon = headerLine.indexOf(':');
            if(colon != -1){
                String name = headerLine.substring(0, colon);
                name.trim();
                String value = headerLine.substring(colon+1);
                value.trim();
                _addHeader(name.c_str(), value.c_str());
            }   
        } 
    } while(_readyState == readyStateOpened); 

            // If content-Length header, set _contentLength

    header *hdr = _getHeader("Content-Length");
    if(hdr){
        _contentLength = strtol(hdr->value,nullptr,10);
    }

            // If chunked specified, try to set _contentLength to size of first chunk

    hdr = _getHeader("Transfer-Encoding"); 
    if(hdr && strcasecmp_P(hdr->value, PSTR("chunked")) == 0){
        //DEBUG_HTTP("*transfer-encoding: chunked\r\n");
        _chunked = true;
        _contentLength = 0;
        _chunks = new xbuf;
        _chunks->write(_response, _response->available());
        _processChunks();
    }         

    
    return true;
}      
        

/*_____________________________________________________________________________________________________________

                        H   H  EEEEE   AAA   DDDD   EEEEE  RRRR    SSS
                        H   H  E      A   A  D   D  E      R   R  S   
                        HHHHH  EEE    AAAAA  D   D  EEE    RRRR    SSS
                        H   H  E      A   A  D   D  E      R  R       S
                        H   H  EEEEE  A   A  DDDD   EEEEE  R   R   SSS
______________________________________________________________________________________________________________*/

//**************************************************************************************************************
void	asyncHTTPrequest::setReqHeader(const char* name, const char* value){
    if(_readyState <= readyStateOpened && _headers){
        _addHeader(name, value);
    }
}

//**************************************************************************************************************
void	asyncHTTPrequest::setReqHeader(const char* name, int32_t value){
    if(_readyState <= readyStateOpened && _headers){
        setReqHeader(name, String(value).c_str());
    }
}

//**************************************************************************************************************
int		asyncHTTPrequest::respHeaderCount(){
    if(_readyState < readyStateHdrsRecvd) return 0;                                            
    int count = 0;
    header* hdr = _headers;
    while(hdr){
        count++;
        hdr = hdr->next;
    }
    return count;
}

//**************************************************************************************************************
char*   asyncHTTPrequest::respHeaderName(int ndx){
    if(_readyState < readyStateHdrsRecvd) return nullptr;      
    header* hdr = _getHeader(ndx);
    if ( ! hdr) return nullptr;
    return hdr->name;
}

//**************************************************************************************************************
char*   asyncHTTPrequest::respHeaderValue(const char* name){
    if(_readyState < readyStateHdrsRecvd) return nullptr;      
    header* hdr = _getHeader(name);
    if( ! hdr) return nullptr;
    return hdr->value;
}

//**************************************************************************************************************
char*   asyncHTTPrequest::respHeaderValue(int ndx){
    if(_readyState < readyStateHdrsRecvd) return nullptr;      
    header* hdr = _getHeader(ndx);
    if ( ! hdr) return nullptr;
    return hdr->value;
}

//**************************************************************************************************************
bool	asyncHTTPrequest::respHeaderExists(const char* name){
    if(_readyState < readyStateHdrsRecvd) return false;      
    header* hdr = _getHeader(name);
    if ( ! hdr) return false;
    return true;
}

//**************************************************************************************************************
String  asyncHTTPrequest::headers(){
    String _response = "";
    header* hdr = _headers;
    while(hdr){
        _response += hdr->name;
        _response += ':';
        _response += hdr->value;
        _response += "\r\n";
        hdr = hdr->next;
    }
    _response += "\r\n";
    return _response;
}

//**************************************************************************************************************
asyncHTTPrequest::header*  asyncHTTPrequest::_addHeader(const char* name, const char* value){
    header* hdr = (header*) &_headers;
    while(hdr->next) {
        if(strcasecmp(name, hdr->next->name) == 0){
            header* oldHdr = hdr->next;
            hdr->next = hdr->next->next;
            oldHdr->next = nullptr;
            delete oldHdr;
        }
        else {
            hdr = hdr->next;
        }
    }
    hdr->next = new header;
    hdr->next->name = new char[strlen(name)+1];
    strcpy(hdr->next->name, name);
    hdr->next->value = new char[strlen(value)+1];
    strcpy(hdr->next->value, value);
    return hdr->next;
}

//**************************************************************************************************************
asyncHTTPrequest::header* asyncHTTPrequest::_getHeader(const char* name){
    header* hdr = _headers;
    while (hdr) {
        if(strcasecmp(name, hdr->name) == 0) break;
        hdr = hdr->next;
    }
    return hdr;
}

//**************************************************************************************************************
asyncHTTPrequest::header* asyncHTTPrequest::_getHeader(int ndx){
    header* hdr = _headers;
    while (hdr) {
        if( ! ndx--) break;
        hdr = hdr->next; 
    }
    return hdr;
}
