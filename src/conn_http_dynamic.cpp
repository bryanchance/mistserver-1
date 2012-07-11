/// \file conn_http_dynamic.cpp
/// Contains the main code for the HTTP Dynamic Connector

#include <iostream>
#include <queue>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <getopt.h>
#include <ctime>
#include <mist/socket.h>
#include <mist/http_parser.h>
#include <mist/json.h>
#include <mist/dtsc.h>
#include <mist/flv_tag.h>
#include <mist/base64.h>
#include <mist/amf.h>
#include <mist/mp4.h>

/// Holds everything unique to HTTP Dynamic Connector.
namespace Connector_HTTP_Dynamic{

  /// Returns AMF-format metadata
  std::string GetMetaData( ) {
    /// \todo Make this actually do what it should - even though it seems to be ignored completely by all media players.
    AMF::Object amfreply("container", AMF::AMF0_DDV_CONTAINER);
    amfreply.addContent(AMF::Object("onMetaData",AMF::AMF0_STRING));
    amfreply.addContent(AMF::Object("",AMF::AMF0_ECMA_ARRAY));
    amfreply.getContentP(1)->addContent(AMF::Object("trackinfo", AMF::AMF0_STRICT_ARRAY));
    amfreply.getContentP(1)->getContentP(0)->addContent(AMF::Object("arrVal"));
    //amfreply.getContentP(1)->getContentP(0)->getContentP(0)->addContent(AMF::Object("timescale",(double)1000));
    //amfreply.getContentP(1)->getContentP(0)->getContentP(0)->addContent(AMF::Object("length",(double)59641700));
    amfreply.getContentP(1)->getContentP(0)->getContentP(0)->addContent(AMF::Object("language","eng"));
    amfreply.getContentP(1)->getContentP(0)->getContentP(0)->addContent(AMF::Object("sampledescription", AMF::AMF0_STRICT_ARRAY));
    amfreply.getContentP(1)->getContentP(0)->getContentP(0)->getContentP(1)->addContent(AMF::Object("arrVal"));
    amfreply.getContentP(1)->getContentP(0)->getContentP(0)->getContentP(1)->getContentP(0)->addContent(AMF::Object("sampletype","avc1"));
    amfreply.getContentP(1)->getContentP(0)->addContent(AMF::Object("arrVal"));
    //amfreply.getContentP(1)->getContentP(0)->getContentP(1)->addContent(AMF::Object("timescale",(double)44100));
    //amfreply.getContentP(1)->getContentP(0)->getContentP(1)->addContent(AMF::Object("length",(double)28630000));
    amfreply.getContentP(1)->getContentP(0)->getContentP(1)->addContent(AMF::Object("language","eng"));
    amfreply.getContentP(1)->getContentP(0)->getContentP(1)->addContent(AMF::Object("sampledescription", AMF::AMF0_STRICT_ARRAY));
    amfreply.getContentP(1)->getContentP(0)->getContentP(1)->getContentP(1)->addContent(AMF::Object("arrVal"));
    amfreply.getContentP(1)->getContentP(0)->getContentP(1)->getContentP(1)->getContentP(0)->addContent(AMF::Object("sampletype","mp4a"));
    amfreply.getContentP(1)->addContent(AMF::Object("audiochannels",(double)2));
    amfreply.getContentP(1)->addContent(AMF::Object("audiosamplerate",(double)44100));
    amfreply.getContentP(1)->addContent(AMF::Object("videoframerate",(double)25));
    amfreply.getContentP(1)->addContent(AMF::Object("aacaot",(double)2));
    amfreply.getContentP(1)->addContent(AMF::Object("avclevel",(double)12));
    amfreply.getContentP(1)->addContent(AMF::Object("avcprofile",(double)77));
    amfreply.getContentP(1)->addContent(AMF::Object("audiocodecid","mp4a"));
    amfreply.getContentP(1)->addContent(AMF::Object("videocodecid","avc1"));
    amfreply.getContentP(1)->addContent(AMF::Object("width",(double)1280));
    amfreply.getContentP(1)->addContent(AMF::Object("height",(double)720));
    amfreply.getContentP(1)->addContent(AMF::Object("frameWidth",(double)1280));
    amfreply.getContentP(1)->addContent(AMF::Object("frameHeight",(double)720));
    amfreply.getContentP(1)->addContent(AMF::Object("displayWidth",(double)1280));
    amfreply.getContentP(1)->addContent(AMF::Object("displayHeight",(double)720));
    //amfreply.getContentP(1)->addContent(AMF::Object("moovposition",(double)35506700));
    //amfreply.getContentP(1)->addContent(AMF::Object("duration",(double)596.458));
    return amfreply.Pack( );
  }//getMetaData

  /// Returns a F4M-format manifest file
  std::string BuildManifest(std::string MovieId) {
    std::string Result="<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
      "<manifest xmlns=\"http://ns.adobe.com/f4m/1.0\">\n"
      "<id>" + MovieId + "</id>\n"
      "<mimeType>video/mp4</mimeType>\n"
      "<streamType>live</streamType>\n"
      "<deliveryType>streaming</deliveryType>\n"
      "<bootstrapInfo profile=\"named\" id=\"bootstrap1\">" + Base64::encode(MP4::GenerateLiveBootstrap(1)) + "</bootstrapInfo>\n"
      "<media streamId=\"1\" bootstrapInfoId=\"bootstrap1\" url=\"" + MovieId + "/\">\n"
      "<metadata>" + Base64::encode(GetMetaData()) + "</metadata>\n"
      "</media>\n"
      "</manifest>\n";
    return Result;
  }//BuildManifest

  /// Main function for Connector_HTTP_Dynamic
  int Connector_HTTP_Dynamic(Socket::Connection conn){
    std::string FlashBuf;
    FLV::Tag tmp;//temporary tag, for init data

    std::queue<std::string> Flash_FragBuffer;//Fragment buffer
    DTSC::Stream Strm;//Incoming stream buffer.
    HTTP::Parser HTTP_R, HTTP_S;//HTTP Receiver en HTTP Sender.

    bool ready4data = false;//Set to true when streaming is to begin.
    bool inited = false;
    Socket::Connection ss(-1);
    std::string streamname;
    FLV::Tag tag;///< Temporary tag buffer.
    std::string recBuffer = "";

    std::string Movie;
    std::string Quality;
    int Segment = -1;
    int ReqFragment = -1;
    int temp;
    int Flash_RequestPending = 0;
    unsigned int lastStats = 0;
    conn.setBlocking(false);//do not block on conn.spool() when no data is available

    while (conn.connected()){
      if (conn.spool()){
        if (HTTP_R.Read(conn.Received())){
          #if DEBUG >= 4
          std::cout << "Received request: " << HTTP_R.url << std::endl;
          #endif
          if (HTTP_R.url.find("f4m") == std::string::npos){
            Movie = HTTP_R.url.substr(1);
            Movie = Movie.substr(0,Movie.find("/"));
            Quality = HTTP_R.url.substr( HTTP_R.url.find("/",1)+1 );
            Quality = Quality.substr(0, Quality.find("Seg"));
            temp = HTTP_R.url.find("Seg") + 3;
            Segment = atoi( HTTP_R.url.substr(temp,HTTP_R.url.find("-",temp)-temp).c_str());
            temp = HTTP_R.url.find("Frag") + 4;
            ReqFragment = atoi( HTTP_R.url.substr(temp).c_str() );
            #if DEBUG >= 4
            printf( "Quality: %s, Seg %d Frag %d\n", Quality.c_str(), Segment, ReqFragment);
            #endif
            Flash_RequestPending++;
          }else{
            Movie = HTTP_R.url.substr(1);
            Movie = Movie.substr(0,Movie.find("/"));
            HTTP_S.Clean();
            HTTP_S.SetHeader("Content-Type","text/xml");
            HTTP_S.SetHeader("Cache-Control","no-cache");
            std::string manifest = BuildManifest(Movie);
            HTTP_S.SetBody(manifest);
            conn.Send(HTTP_S.BuildResponse("200", "OK"));
            #if DEBUG >= 3
            printf("Sent manifest\n");
            #endif
          }
          ready4data = true;
          HTTP_R.Clean(); //clean for any possible next requests
        }else{
          #if DEBUG >= 3
          fprintf(stderr, "Could not parse the following:\n%s\n", conn.Received().c_str());
          #endif
        }
      }
      if (ready4data){
        if (!inited){
          //we are ready, connect the socket!
          ss = Socket::getStream(streamname);
          if (!ss.connected()){
            #if DEBUG >= 1
            fprintf(stderr, "Could not connect to server!\n");
            #endif
            ss.close();
            HTTP_S.Clean();
            HTTP_S.SetBody("No such stream is available on the system. Please try again.\n");
            conn.Send(HTTP_S.BuildResponse("404", "Not found"));
            ready4data = false;
            continue;
          }
          #if DEBUG >= 3
          fprintf(stderr, "Everything connected, starting to send video data...\n");
          #endif
          inited = true;
        }
        if ((Flash_RequestPending > 0) && !Flash_FragBuffer.empty()){
          HTTP_S.Clean();
          HTTP_S.SetHeader("Content-Type","video/mp4");
          HTTP_S.SetBody(MP4::mdatFold(Flash_FragBuffer.front()));
          Flash_FragBuffer.pop();
          conn.Send(HTTP_S.BuildResponse("200", "OK"));
          Flash_RequestPending--;
          #if DEBUG >= 3
          fprintf(stderr, "Sending a video fragment. %i left in buffer, %i requested\n", (int)Flash_FragBuffer.size(), Flash_RequestPending);
          #endif
        }
        unsigned int now = time(0);
        if (now != lastStats){
          lastStats = now;
          ss.Send("S "+conn.getStats("HTTP_Dynamic"));
        }
        if (ss.spool() || ss.Received() != ""){
          if (Strm.parsePacket(ss.Received())){
            tag.DTSCLoader(Strm);
            if (Strm.getPacket(0).getContentP("keyframe")){
              if (FlashBuf != ""){
                Flash_FragBuffer.push(FlashBuf);
                while (Flash_FragBuffer.size() > 2){
                  Flash_FragBuffer.pop();
                }
                #if DEBUG >= 4
                fprintf(stderr, "Received a fragment. Now %i in buffer.\n", (int)Flash_FragBuffer.size());
                #endif
              }
              FlashBuf.clear();
              //fill buffer with init data, if needed.
              if (Strm.metadata.getContentP("audio") && Strm.metadata.getContentP("audio")->getContentP("init")){
                tmp.DTSCAudioInit(Strm);
                FlashBuf.append(tmp.data, tmp.len);
              }
              if (Strm.metadata.getContentP("video") && Strm.metadata.getContentP("video")->getContentP("init")){
                tmp.DTSCVideoInit(Strm);
                FlashBuf.append(tmp.data, tmp.len);
              }
            }
            FlashBuf.append(tag.data, tag.len);
          }
        }
      }
    }
    conn.close();
    ss.close();
    #if DEBUG >= 1
    if (FLV::Parse_Error){fprintf(stderr, "FLV Parser Error: %s\n", FLV::Error_Str.c_str());}
    fprintf(stderr, "User %i disconnected.\n", conn.getSocket());
    if (inited){
      fprintf(stderr, "Status was: inited\n");
    }else{
      if (ready4data){
        fprintf(stderr, "Status was: ready4data\n");
      }else{
        fprintf(stderr, "Status was: connected\n");
      }
    }
    #endif
    return 0;
  }//Connector_HTTP_Dynamic main function

};//Connector_HTTP_Dynamic namespace

// Load http setup file with the correct settings for this HTTP connector
#define MAINHANDLER Connector_HTTP_Dynamic::Connector_HTTP_Dynamic
#define CONNECTOR "dynamic"
#include "server_setup_http.h"
