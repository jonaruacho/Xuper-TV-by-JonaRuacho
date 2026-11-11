#include "service.hpp"
#include "codec.hpp"
#include "ranger_codec.hpp"
#include "trace.hpp"
#include "installation.hpp"
#include <curl/curl.h>
#define JSON_NOEXCEPTION 1
#include "json.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <climits>
#include <cstdlib>
#include <ctime>
#include <chrono>
#include <map>
#include <set>
#include <mbedtls/md5.h>
#ifdef __SWITCH__
#include <switch.h>
#else
#include <random>
#endif

namespace xs {
using Json=nlohmann::json;
static std::string text(const Json& object,const char* key,const std::string& fallback="") {
    if(!object.is_object())return fallback;
    auto it=object.find(key);if(it==object.end())return fallback;
    return it->is_string()?it->get<std::string>():it->is_number()?it->dump():fallback;
}
struct Buffer {std::string data;bool too_large=false;};
static size_t append(char* p,size_t size,size_t count,void* user) {
    auto* buffer=static_cast<Buffer*>(user);
    if(size&&count>16*1024*1024/size){buffer->too_large=true;return 0;}
    const size_t bytes=size*count;
    if(buffer->data.size()+bytes>16*1024*1024){buffer->too_large=true;return 0;}
    buffer->data.append(p,bytes);return bytes;
}
static bool request(const std::string& url,const std::string* post,const Json& config,std::string& body,std::string& error,bool follow_get=true,const std::string& ranger_id="") {
    // Only the API-selected Ranger resolver may explicitly use HTTP. Account
    // requests and subtitles retain HTTPS; resolver redirects remain disabled.
    const bool resolver=!post&&!follow_get&&!ranger_id.empty();
    if(!valid_source(url)||(url.rfind("https://",0)!=0&&!(resolver&&url.rfind("http://",0)==0))){error="El adaptador requiere HTTPS valido.";return false;}
    if(resolver)trace(url.rfind("https://",0)==0?"ranger:transport_https":"ranger:transport_http");
    CURL* curl=curl_easy_init();if(!curl){error="No se pudo iniciar HTTP.";return false;}
    Buffer response;curl_slist* headers=nullptr;
    curl_easy_setopt(curl,CURLOPT_URL,url.c_str());
    curl_easy_setopt(curl,CURLOPT_ACCEPT_ENCODING,"");
    curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,10L);curl_easy_setopt(curl,CURLOPT_TIMEOUT,25L);
    curl_easy_setopt(curl,CURLOPT_SSL_VERIFYPEER,1L);curl_easy_setopt(curl,CURLOPT_SSL_VERIFYHOST,2L);
    // Never forward sign-in requests or credentials to a redirected host.
    curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,post||!follow_get?0L:1L);
    curl_easy_setopt(curl,CURLOPT_MAXREDIRS,3L);
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(curl,CURLOPT_PROTOCOLS_STR,resolver?"http,https":"https");
    curl_easy_setopt(curl,CURLOPT_REDIR_PROTOCOLS_STR,resolver?"http,https":"https");
#else
    curl_easy_setopt(curl,CURLOPT_PROTOCOLS,resolver?(CURLPROTO_HTTP|CURLPROTO_HTTPS):CURLPROTO_HTTPS);
    curl_easy_setopt(curl,CURLOPT_REDIR_PROTOCOLS,resolver?(CURLPROTO_HTTP|CURLPROTO_HTTPS):CURLPROTO_HTTPS);
#endif
    curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,append);curl_easy_setopt(curl,CURLOPT_WRITEDATA,&response);
    const std::string ca=text(config,"ca_file");
    if(!ca.empty())curl_easy_setopt(curl,CURLOPT_CAINFO,ca.c_str());
    if(post) {
        headers=curl_slist_append(headers,"Content-Type: application/json;charset=utf-8");
        for(const auto& pair:{std::pair<const char*,std::string>{"apk",text(config,"app_id")},
                             {"apkVer",text(config,"apk_version")},{"spkgVer","Horizon homebrew"}}) {
            if(pair.second.find_first_of("\r\n")==std::string::npos)
                headers=curl_slist_append(headers,(std::string(pair.first)+": "+pair.second).c_str());
        }
        curl_easy_setopt(curl,CURLOPT_POSTFIELDS,post->data());
        curl_easy_setopt(curl,CURLOPT_POSTFIELDSIZE_LARGE,static_cast<curl_off_t>(post->size()));
    }
    // Native Ranger RequestSLB sets this header even though its request has no body.
    if(!ranger_id.empty()&&!post){
        headers=curl_slist_append(headers,"Content-Type: application/octet-stream");
        for(const auto& pair:{std::pair<const char*,std::string>{"App",text(config,"app_id")},
                             {"App-Version",text(config,"apk_version")},{"Ranger-Id",ranger_id}}){
            if(pair.second.find_first_of("\r\n")==std::string::npos)
                headers=curl_slist_append(headers,(std::string(pair.first)+": "+pair.second).c_str());
        }
        curl_easy_setopt(curl,CURLOPT_HTTP_VERSION,CURL_HTTP_VERSION_1_1);
    }
    if(headers)curl_easy_setopt(curl,CURLOPT_HTTPHEADER,headers);
    const CURLcode rc=curl_easy_perform(curl);long status=0;curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status);
    if(!ranger_id.empty()){
        // Structural diagnostics only: never URLs, headers, identifiers or response text.
        const auto first=response.data.find_first_not_of(" \r\n\t");
        const char* shape=first==std::string::npos?"empty":response.data[first]=='<'?"html":
            response.data[first]=='{'||response.data[first]=='['?"json":"other";
        const char* edge=response.data.find("cloudflare")!=std::string::npos?"cloudflare":
            response.data.find("nginx")!=std::string::npos?"nginx":"unspecified";
        char stage[160];std::snprintf(stage,sizeof(stage),"ranger:http status=%ld curl=%d bytes=%zu shape=%s edge=%s",status,static_cast<int>(rc),response.data.size(),shape,edge);
        trace(stage);
    }
    curl_slist_free_all(headers);curl_easy_cleanup(curl);
    if(rc!=CURLE_OK){error=response.too_large?"Respuesta demasiado grande.":std::string("Conexion: ")+curl_easy_strerror(rc);return false;}
    if(status!=200){error="Servidor respondio HTTP "+std::to_string(status);return false;}
    body=std::move(response.data);return true;
}
LoginAttempt service_login(const std::string& path,const std::string& username,const std::string& password) {
    LoginAttempt out;std::ifstream file(path);
    if(!file){out.status="Falta service.json junto al NRO.";return out;}
    const std::string config_bytes((std::istreambuf_iterator<char>(file)),{});
    Json config=Json::parse(config_bytes,nullptr,false);
    if(config.is_discarded()||!config.is_object()){out.status="service.json no es valido.";return out;}
    const std::string key=text(config,"key_base64");
    const auto hosts=config.find("portal_hosts");
    if(hosts==config.end()||!hosts->is_array()||hosts->empty()||key.empty()){out.status="Configuracion del servicio incompleta.";return out;}
    std::string prepared;
    if(!prepare_password(password,text(config,"password_salt"),text(config,"password_aes_key"),prepared)) {
        out.status="Actualiza service.json del paquete 0.3; no se pudo preparar la clave.";return out;
    }
    Json payload={ {"accountType",account_type(username)},{"userName",username},
        {"password",prepared},{"type","1"},{"macAddr",text(config,"device_id")},
        {"areaCode",""},{"verificationCode",""},{"verificationToken",""},
        {"matadata",""},{"signdata",""},{"channel","default"},
        {"loginType",text(config,"login_type","2")},{"appLanguage",text(config,"language","es")},
        {"apkVersion",text(config,"apk_version")},{"sysVersion","Horizon"},
        {"appId",text(config,"app_id")},{"hardwareInfo","Tegra X1"},
        {"model","Nintendo Switch"},{"product","Horizon homebrew"},{"cpu","arm64-v8a"},
        {"B29",""},{"reserve1",""},{"portalCode",""},{"deviceToken",""},
        {"sn",""},{"drmId",""},{"sdkVer",0} };
    if(text(payload,"macAddr").empty()){out.status="Falta identificador de instalacion en service.json.";return out;}
    std::string encrypted;
    if(!encode_request(payload.dump(),key,encrypted)){out.status="No se pudo codificar la solicitud.";return out;}
    payload.clear();std::fill(prepared.begin(),prepared.end(),'\0');
    if(curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK){out.status="No se pudo iniciar cURL.";return out;}
    struct CurlExit {~CurlExit(){curl_global_cleanup();}} cleanup;
    std::string body,error;bool connected=false;
    // Retry a second portal only after a transport/HTTP error. A rejected account
    // is never retried automatically.
    size_t attempts=0;
    for(const auto& host:*hosts) {
        if(!host.is_string()||attempts++>=2)break;
        std::string base=host.get<std::string>();
        if(base.rfind("https://",0)!=0)base="https://"+base;
        while(!base.empty()&&base.back()=='/')base.pop_back();
        if(request(base+"/api/portalCore/v8/login",&encrypted,config,body,error)){connected=true;break;}
    }
    std::fill(encrypted.begin(),encrypted.end(),'\0');
    if(!connected){out.status=error.empty()?"No hay servidores validos configurados.":error;return out;}
    Json response=Json::parse(body,nullptr,false);
    if(response.is_discarded()||!response.is_object()){out.status="Respuesta de login no reconocida.";return out;}
    if(text(response,"returnCode")!="0"){
        std::string code=text(response,"returnCode","desconocido");
        for(char& c:code)if(static_cast<unsigned char>(c)<32)c=' ';
        out.status=code=="portal200001"?"El servicio indica version fuera de servicio (portal200001).":
            "Acceso rechazado por el servicio. Codigo: "+code.substr(0,80);return out;
    }
    auto data=response.find("data");
    if(data!=response.end()&&data->is_string()) {
        std::string plain;
        if(!decode_response_data(data->get<std::string>(),key,plain)){out.status="No se pudo decodificar data.";return out;}
        response["data"]=Json::parse(plain,nullptr,false);data=response.find("data");
    }
    if(data==response.end()||!data->is_object()){out.status="Login sin datos de sesion reconocidos.";return out;}
    out.authenticated=true;
    const std::string playlist=text(*data,"playlistUrl");
    if(playlist.empty()){out.status="Acceso aceptado; el servicio no devolvio una lista M3U. Catalogo propietario pendiente.";return out;}
    if(!request(playlist,nullptr,config,body,error)){out.status="Acceso aceptado. Lista: "+error;return out;}
    std::istringstream input(body);out.catalog=read_m3u(input);
    if(out.catalog.entries.empty())out.status="Acceso aceptado; playlistUrl no contiene una lista compatible.";
    else out.status="Acceso aceptado. Lista cargada; reproduccion pendiente de comprobar.";
    return out;
}
struct ServiceSession {Json config;Json common;Json identity;std::string playlist;std::map<int,Catalog> channels;std::vector<int> live_categories;};
struct CurlScope {
    bool ready=curl_global_init(CURL_GLOBAL_DEFAULT)==CURLE_OK;
    ~CurlScope(){if(ready)curl_global_cleanup();}
};
static bool api_post(const Json& config,const Json& common,const char* route,Json payload,Json& data,std::string& status) {
        for(auto it=common.begin();it!=common.end();++it)payload[it.key()]=it.value();
        std::string encoded;if(!encode_request(payload.dump(),text(config,"key_base64"),encoded)){status="No se pudo preparar la solicitud.";return false;}
        std::string body,error;bool connected=false;size_t attempts=0;
        for(const auto& h:config["portal_hosts"]){
            if(!h.is_string()||attempts++>=2)break;
            std::string base=h.get<std::string>();if(base.rfind("https://",0)!=0)base="https://"+base;
            while(!base.empty()&&base.back()=='/')base.pop_back();
            if(request(base+route,&encoded,config,body,error)){connected=true;break;}
        }
        if(!connected){status=error.empty()?"Sin servidor configurado.":error;return false;}
        Json response=Json::parse(body,nullptr,false);
        if(!response.is_object()){status="Respuesta del servicio no reconocida.";return false;}
        const std::string code=text(response,"returnCode","desconocido");
        if(code!="0"){
            if(code=="portal100024")status="Restriccion regional del servicio (portal100024).";
            else if(code=="portal200001")status="El servicio indica version fuera de servicio (portal200001).";
            else {std::string safe=code.substr(0,80);for(char& c:safe)if(static_cast<unsigned char>(c)<32)c=' ';
                status=std::string("Servicio")+": rechazo "+safe;}
            return false;
        }
        auto d=response.find("data");
        if(d==response.end()){status="Respuesta sin data.";return false;}
        if(d->is_string()){
            std::string plain;if(!decode_response_data(d->get<std::string>(),text(config,"key_base64"),plain)){
                status="No se pudo decodificar data.";return false;}
            data=Json::parse(plain,nullptr,false);
        }else data=*d;
        if(!data.is_object()){status="Datos de respuesta no reconocidos.";return false;}
        return true;
    }
static Json identity(const ServiceSession& s) {
    return {{"userId",text(s.identity,"userId")},{"userToken",text(s.identity,"userToken")},
            {"portalCode",text(s.common,"portalCode")}};
}

static std::string query_value(const std::string& input,const std::string& key) {
    size_t start=input.find('?');start=start==std::string::npos?0:start+1;
    while(start<input.size()){
        const size_t end=input.find('&',start),equals=input.find('=',start);
        if(equals!=std::string::npos&&(end==std::string::npos||equals<end)&&input.substr(start,equals-start)==key)
            return input.substr(equals+1,end==std::string::npos?end:end-equals-1);
        if(end==std::string::npos)break;
        start=end+1;
    }return {};
}
static bool header_value(const std::string& value) {
    return !value.empty()&&value.size()<=16384&&std::none_of(value.begin(),value.end(),[](unsigned char c){return c<32||c==127;});
}
static std::string slb_url(std::string base,RangerMode mode,const std::string& encoded) {
    if(base.find("://")==std::string::npos)base="https://"+base;
    if(!valid_source(base)||(base.rfind("https://",0)!=0&&base.rfind("http://",0)!=0))return {};
    CURLU* url=curl_url();if(!url)return {};
    const std::string query="auth="+encoded;char* result=nullptr;
    bool ok=curl_url_set(url,CURLUPART_URL,base.c_str(),0)==CURLUE_OK&&
        curl_url_set(url,CURLUPART_PATH,mode==RangerMode::Live?"/slb/v9/live":"/slb/v11/vod",0)==CURLUE_OK&&
        curl_url_set(url,CURLUPART_QUERY,query.c_str(),0)==CURLUE_OK&&
        curl_url_set(url,CURLUPART_FRAGMENT,nullptr,0)==CURLUE_OK&&
        curl_url_get(url,CURLUPART_URL,&result,0)==CURLUE_OK;
    std::string output=ok&&result?result:"";curl_free(result);curl_url_cleanup(url);return output;
}
static SourceResult resolve_ranger(const std::shared_ptr<ServiceSession>& s,RangerMode mode,
                                  const std::string& code,const std::string& license,
                                  const std::string& tag,const std::string& cdn_type,const std::string& format="") {
    SourceResult out;
    if(code.empty()||code.size()>8192||!header_value(license)){
        out.status="Ranger: fuente sin identificador o licencia valida.";return out;
    }
    Json payload=identity(*s); // Defined below; only session fields enter the request.
    payload.update({{"hasPay",text(s->identity,"hasPay")},{"userIdentity",text(s->identity,"userIdentity")},
        {"type","merge"},{"appVer",text(s->config,"apk_version")},{"lang",text(s->config,"language","es")},
        {"encMediaSupported",1},{"liveCodeList",Json::array({text(s->common,"portalCode")+"_live"})},
        {"appParams",""},{"pipFlag","0"}});
    Json metadata;
    if(!api_post(s->config,s->common,"/api/portalCore/v15/getSlbInfo",payload,metadata,out.status))return out;
    if(text(metadata,"error_code")!="0"){out.status="Ranger: el servicio rechazo la configuracion CDN.";return out;}
    const auto cdns=metadata.find("cdn_list");
    if(cdns==metadata.end()||!cdns->is_array()){out.status="Ranger: no se recibieron servidores CDN.";return out;}
    const std::string business=mode==RangerMode::Live?"live":"vod";
    for(const auto& cdn:*cdns){
        if(text(cdn,"tag")!=business||(!cdn_type.empty()&&text(cdn,"cdn_type")!=cdn_type))continue;
        const auto auths=cdn.find("url_list");if(auths==cdn.end()||!auths->is_array())continue;
        for(const auto& entry:*auths){
            if(text(entry,"tag")!=tag)continue;
            const std::string auth=text(entry,"url");if(!header_value(auth))continue;
            if(mode==RangerMode::Vod&&(query_value(auth,"app_id")!=query_value(license,"app_id")||
                query_value(auth,"tag")!=query_value(license,"tag")))continue;
            std::string plain=auth;
            if(mode==RangerMode::Vod)plain=Json::array({{{"auth",auth},{"license",license},{"media_code",code}}}).dump();
            std::string encoded;
            if(!ranger_encode(text(s->config,"key_base64"),mode,plain,false,encoded)){
                out.status="Ranger: no se pudo preparar la solicitud CDN.";return out;}
            const std::string endpoint=slb_url(text(cdn,"main_addr"),mode,encoded);
            if(endpoint.empty()){out.status="Ranger: servidor de resolucion incompatible (HTTP/HTTPS requerido).";return out;}
            std::string body,error;
            // Auth tokens are sent only to the selected API-provided CDN host;
            // a redirect is never followed with this encrypted authorization.
            uint32_t nonce=0;
#ifdef __SWITCH__
            randomGet(&nonce,sizeof(nonce));
#else
            nonce=std::random_device{}();
#endif
            // Bootstrap registers no Android ID. Keep that value (zero), without
            // inventing or rotating a device identity. Native converts user_id with atoll.
            const std::string uid=text(s->identity,"userId");
            const auto id=ranger_request_id(static_cast<uint32_t>(std::strtoull(uid.c_str(),nullptr,10)),
                                            0,static_cast<uint32_t>(std::time(nullptr)),nonce);
            if(id.empty()){out.status="Ranger: no se pudo preparar el identificador de solicitud.";return out;}
            if(!request(endpoint,nullptr,s->config,body,error,false,id)){out.status="Ranger/CDN: "+error;return out;}
            if(!ranger_decode(text(s->config,"key_base64"),mode,body,true,plain)){
                const std::string chars="jWB7YtC3n9iXbEkUcJl1VxF4STpQoOIaRmh2M-efAgLwPqGr6uyD5vNsdH_Kz0Z8=\r\n";
                size_t invalid=0;for(char c:body)if(chars.find(c)==std::string::npos)++invalid;
                char framing[128];std::snprintf(framing,sizeof(framing),"ranger:decode_failed bytes=%zu mod4=%zu padding=%d invalid_chars=%zu",body.size(),body.size()%4,body.find('=')!=std::string::npos,invalid);trace(framing);
                const auto first=body.find_first_not_of(" \r\n\t");
                out.status=first!=std::string::npos&&(body[first]=='<'||body[first]=='{'||body[first]=='[')?
                    "Ranger: CDN devolvio HTML/JSON en vez de la respuesta cifrada. Revisa diagnostico.log.":
                    "Ranger: cifrado CDN no compatible. Envia diagnostico.log.";
                return out;}
            Json decoded=Json::parse(plain,nullptr,false);Json servers;
            if(mode==RangerMode::Live&&decoded.is_object()){
                const auto found=decoded.find("servers");if(found!=decoded.end())servers=*found;
            }else if(mode==RangerMode::Vod&&decoded.is_object()){
                const auto infos=decoded.find("play_infos");
                if(infos!=decoded.end()&&infos->is_array())for(const auto& info:*infos){
                    if(text(info,"media_code")!=code)continue;
                    if(text(info,"status_code")!="200"){out.status="Ranger: CDN rechazo la reproduccion del video.";return out;}
                    const auto found=info.find("servers");if(found!=info.end())servers=*found;break;
                }
            }
            if(servers.is_array())for(const auto& server:servers){
                const std::string source=text(server,"media_url");
                if(!valid_source(source)||(source.rfind("https://",0)!=0&&source.rfind("http://",0)!=0))continue;
                // Native schemes, local proxy paths and encrypted media need
                // additional implementations; they are never fabricated here.
                CURLU* parsed=curl_url();char* host=nullptr;
                const bool parsed_ok=parsed&&curl_url_set(parsed,CURLUPART_URL,source.c_str(),0)==CURLUE_OK&&curl_url_get(parsed,CURLUPART_HOST,&host,0)==CURLUE_OK;
                const std::string hostname=host?host:"";curl_free(host);if(parsed)curl_url_cleanup(parsed);
                if(!parsed_ok||hostname=="localhost"||hostname=="[::1]"||hostname=="::1"||hostname.rfind("127.",0)==0)continue;
                out.source=source;
                if(mode==RangerMode::Live){
                    if(text(cdn,"cdn_type")=="1"){
                        const std::string path=ranger_slb_live_path(code,format);
                        if(path.empty()){out.source.clear();out.status="Ranger: codigo de canal no compatible con la ruta SLB.";return out;}
                        const auto path_start=source.find_first_of("/?#",source.find("://")+3);
                        const auto suffix=path_start==std::string::npos?std::string::npos:source.find_first_of("?#",path_start);
                        out.source=source.substr(0,path_start)+path+(suffix==std::string::npos?"":source.substr(suffix));
                        if(!valid_source(out.source)){out.source.clear();out.status="Ranger: direccion de canal no valida.";return out;}
                        trace(format=="mp4"?"ranger:live_route_slb_mp4":"ranger:live_route_slb_ts");
                    }
                    const auto start=source.find_first_of("/?#",source.find("://")+3);
                    std::string resource=start==std::string::npos?"/":source.substr(start);
                    if(resource.front()!='/')resource="/"+resource;
                    const auto moment=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                    const std::string content_auth=ranger_live_content_auth(resource,0,static_cast<uint64_t>(moment));
                    if(!header_value(content_auth)){out.source.clear();out.status="Ranger: no se pudo preparar la autorizacion del video.";return out;}
                    out.headers={{"Content-Auth",content_auth},{"Content-License",license},
                        {"App",text(s->config,"app_id")},{"App-Version",text(s->config,"apk_version")},
                        {"Ranger-Id",id},{"Pragma","akamai-x-cache-on"}};
                    trace(resource.find("token=")!=std::string::npos?"ranger:live_media_auth_signed":"ranger:live_media_auth_resource");
                }
                out.ok=true;out.status="Ranger: fuente HTTP autorizada recibida.";return out;
            }
            out.status="Ranger: el CDN requiere un transporte o proxy de video aun no compatible.";return out;
        }
    }
    out.status="Ranger: no hay una ruta CDN compatible con la licencia de esta fuente.";return out;
}

LoginAttempt service_bootstrap(const std::string& path) {
    LoginAttempt out;Json config;
    auto entropy=[](unsigned char* bytes,size_t size){
#ifdef __SWITCH__
        randomGet(bytes,size);
#else
        std::random_device random;for(size_t i=0;i<size;++i)bytes[i]=static_cast<unsigned char>(random());
#endif
        return true;
    };
    if(!load_installation(path,config,out.status,entropy))return out;
    if(!config.is_object()||text(config,"installation_serial").empty()||text(config,"password_salt").empty()){
        out.status="Actualiza todos los archivos del paquete.";return out;
    }
    const auto hosts=config.find("portal_hosts");const std::string key=text(config,"key_base64");
    if(hosts==config.end()||!hosts->is_array()||hosts->empty()||key.empty()){
        out.status="Configuracion del servicio incompleta.";return out;
    }
    if(curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK){out.status="No se pudo iniciar cURL.";return out;}
    struct CurlExit {~CurlExit(){curl_global_cleanup();}} cleanup;
    const std::string state_path=path.substr(0,path.find_last_of("/\\")+1)+"device_state.json";
    Json state=Json::object();
    {std::ifstream previous(state_path);if(previous){std::string saved((std::istreambuf_iterator<char>(previous)),{});
        Json parsed=Json::parse(saved,nullptr,false);if(parsed.is_object())state=std::move(parsed);}}
    unsigned open_count=0;
    auto counter=state.find("open_count");
    if(counter!=state.end()&&counter->is_number_unsigned())open_count=std::min(counter->get<unsigned>(),2147483646u)+1;
    Json common={{"loginType",text(config,"login_type","2")},{"appLanguage",text(config,"language","es")},
        {"apkVersion",text(config,"apk_version")},{"sysVersion","Horizon"},{"appId",text(config,"app_id")},
        {"hardwareInfo","Tegra X1"},{"model","Nintendo Switch"},{"product","Horizon homebrew"},
        {"cpu","arm64-v8a"},{"B29",""},{"reserve1",text(config,"device_id")},{"portalCode",""},
        {"deviceToken",""},{"sn",text(state,"sn")},{"drmId",""},{"sdkVer",0}};
    auto post=[&](const char* route,Json payload,Json& data){
        return api_post(config,common,route,std::move(payload),data,out.status);
    };
    Json device={{"androidId",""},{"wifiMac",text(config,"device_id")},{"etheMac",""},
        {"serialNumber",text(config,"installation_serial")},{"diskInfo",""},{"ramSize",""},
        {"romSize",""},{"gatewayMac",""},{"cpuId",""},{"display","Horizon"},
        {"brand","Nintendo"},{"verId","Horizon"},{"cpuAbi","arm64-v8a"},{"hardware","Tegra X1"},
        {"manufacturer","Nintendo"},{"fingerprint",""},{"host",""},{"device","Nintendo Switch"},
        {"board","nx"},{"tags","homebrew"}};
    Json token_data;
    if(!post("/api/portalCore/v3/snToken",device,token_data))return out;
    const std::string token=text(token_data,"snToken");const bool existing=text(token_data,"isNew")=="1";
    std::string serial=existing?text(token_data,"sn"):"";
    if(serial.empty())serial=derive_device_serial(token,text(config,"password_salt"));
    if(serial.empty()){out.status="El servicio no entrego identidad del dispositivo.";return out;}
    state["sn"]=serial;state["open_count"]=open_count;
    {std::ofstream saved(state_path,std::ios::trunc);saved<<state.dump(2);saved.flush();
        if(!saved){out.status="No se pudo guardar la identidad de esta instalacion.";return out;}}
    common["sn"]=serial;
    Json activation={{"snToken",existing?"":token},{"authVersion",""},{"authCode",""},{"preCode",nullptr},
        {"macAddr",text(config,"device_id")},{"reserve1",text(config,"device_id")},
        {"openNum",open_count},{"channel","default"},{"matadata",""},{"signdata",""}};
    Json session;if(!post("/api/portalCore/v8/active",activation,session))return out;
    auto portals=session.find("portalCodeList");
    if(text(session,"userId").empty()||text(session,"userToken").empty()||portals==session.end()||!portals->is_array()||portals->empty()||text(portals->front(),"portalCode").empty()){
        out.status="Activacion sin sesion completa. No se recibieron permisos del servicio.";return out;
    }
    out.authenticated=true;
    out.session=std::make_shared<ServiceSession>();
    out.session->config=config;out.session->common=common;out.session->identity=session;
    out.session->common["portalCode"]=text(portals->front(),"portalCode");
    out.session->playlist=text(session,"playlistUrl");
    auto root=service_root(out.session);out.catalog=std::move(root.catalog);
    out.status=root.status;return out;
}

static int column_id(const Json& item) {
    const std::string value=text(item,"id");
    if(value.empty()||value.size()>10||value.find_first_not_of("0123456789")!=std::string::npos)return -1;
    char* end=nullptr;const long long n=std::strtoll(value.c_str(),&end,10);
    return end&&!*end&&n>=0&&n<=INT_MAX?static_cast<int>(n):-1;
}
static bool protected_item(const Json& item) {
    for(const char* key:{"restricted","isLock"}) {
        auto field=item.find(key);
        if(field!=item.end()&&((field->is_boolean()&&field->get<bool>())||text(item,key)=="1"))return true;
    }
    return false;
}
static std::string title(const Json& item) {
    std::string result=text(item,"name",text(item,"alias","Sin nombre"));
    for(char& c:result)if(static_cast<unsigned char>(c)<32||c==127)c=' ';
    // Cut on a UTF-8 boundary to keep the renderer's input valid.
    if(result.size()>100){size_t cut=100;while(cut>0&&(static_cast<unsigned char>(result[cut])&0xc0)==0x80)--cut;result.resize(cut);}
    return result.empty()?"Sin nombre":result;
}
static std::string clean_description(std::string value){
    for(char& c:value)if(static_cast<unsigned char>(c)<32)c=' ';
    if(value.size()>1800){size_t n=1800;while(n>0&&(static_cast<unsigned char>(value[n])&0xc0)==0x80)--n;value.resize(n);}return value;
}
static void artwork(const Json& item,Entry& e){
    for(const char* k:{"showIconUrl","posterUrl","showPosterUrl"}){
        const auto u=text(item,k);if(valid_source(u)&&(u.rfind("http://",0)==0||u.rfind("https://",0)==0)){e.artwork=u;break;}
    }
    const auto list=item.find("posterList");
    if(e.artwork.empty()&&list!=item.end()&&list->is_array())for(const auto& poster:*list){
        const auto u=text(poster,"fileUrl");if(valid_source(u)&&(u.rfind("http://",0)==0||u.rfind("https://",0)==0)){e.artwork=u;break;}
    }
    e.artwork_cache=artwork_key(e.artwork);e.description=clean_description(text(item,"description",text(item,"descriptionCompt")));
}
BrowseResult service_root(const std::shared_ptr<ServiceSession>& s) {
    BrowseResult out;if(!s){out.status="Pulsa Y para activar el dispositivo.";return out;}
    CurlScope curl;if(!curl.ready){out.status="No se pudo iniciar cURL.";return out;}
    if(!s->playlist.empty()) {
        std::string body,error;
        if(!request(s->playlist,nullptr,s->config,body,error)){out.status="Lista: "+error;return out;}
        std::istringstream input(body);out.catalog=read_m3u(input);out.ok=!out.catalog.entries.empty();
        out.status=out.ok?"Lista del servicio cargada.":"El servicio devolvio una lista vacia o incompatible.";return out;
    }
    auto payload=identity(*s);
    payload.update({{"columnId",nullptr},{"specialFlag",nullptr},{"pageNum",1},{"pageSize",1000},
                    {"numDisplay",nullptr},{"isAv1","0"}});
    Json root;
    if(!api_post(s->config,s->common,"/api/portalCore/v3/getColumnContents",payload,root,out.status))return out;
    auto children=root.find("childColumnList");
    if(children==root.end()||!children->is_array()){out.status="Catalogo: respuesta sin categorias reconocidas.";return out;}
    s->live_categories.clear();
    out.catalog.title="Inicio";
    int live=-1;const std::string live_code=text(s->common,"portalCode")+"_live";
    for(const auto& item:*children)if(text(item,"code")==live_code&&!protected_item(item)){live=column_id(item);break;}
    if(live>=0){
        payload["columnId"]=live;payload["specialFlag"]="0";
        Json categories;
        if(!api_post(s->config,s->common,"/api/portalCore/v3/getColumnContents",payload,categories,out.status))return out;
        children=categories.find("childColumnList");
        if(children==categories.end()||!children->is_array()){out.status="TV: respuesta sin categorias reconocidas.";return out;}
        for(const auto& item:*children) {
            const int id=column_id(item);
            if(id<0||protected_item(item)){out.catalog.warnings.push_back("Categoria restringida o incompatible omitida.");continue;}
            if(out.catalog.entries.size()>=1000){out.catalog.warnings.push_back("Limite de categorias alcanzado.");break;}
            Entry e;e.title=title(item);e.kind=EntryKind::Category;e.column_id=id;s->live_categories.push_back(id);out.catalog.entries.push_back(std::move(e));
        }
    }
    // Root codes are supplied by the service, not guessed column identifiers.
    const auto roots=root.find("childColumnList");
    for(const auto& item:*roots) {
        const auto code=text(item,"code");
        const std::string prefix=text(s->common,"portalCode");
        if((code!=prefix+"_movies"&&code!=prefix+"_series")||protected_item(item))continue;
        const int id=column_id(item);if(id<0)continue;
        Entry e;e.title=code==prefix+"_movies"?"Peliculas":"Series";e.kind=EntryKind::Section;
        e.column_id=id;e.program_type=code==prefix+"_movies"?"1":"0";out.catalog.entries.push_back(std::move(e));
    }
    std::stable_partition(out.catalog.entries.begin(),out.catalog.entries.end(),[](const Entry& e){return e.kind==EntryKind::Section;});
    if(out.catalog.entries.empty()){out.status="Activacion aceptada; no hay seccion TV, peliculas o series accesible.";return out;}
    out.ok=true;out.status="Catalogo cargado. A: abrir; X: buscar peliculas y series.";
    return out;
}
BrowseResult service_channels(const std::shared_ptr<ServiceSession>& s,int id) {
    BrowseResult out;if(!s||id<0){out.status="Categoria sin sesion valida.";return out;}
    CurlScope curl;if(!curl.ready){out.status="No se pudo iniciar cURL.";return out;}
    const auto cached=s->channels.find(id);if(cached!=s->channels.end()){out.ok=true;out.catalog=cached->second;out.status="Canales cargados.";return out;}
    Json payload=identity(*s);
    payload.update({{"columnId",id},{"dataVersion",""},{"pageSize",3000},{"pageNum",1},{"expireTimeStr",""}});
    Json data;if(!api_post(s->config,s->common,"/api/portalCore/v6/getLiveData",payload,data,out.status))return out;
    auto channels=data.find("channelList");
    if(channels==data.end()||!channels->is_array()){out.status="TV: respuesta sin canales reconocidos.";return out;}
    for(const auto& item:*channels) {
        const std::string code=text(item,"channelCode");
        if(code.empty()||code.size()>512||protected_item(item)){out.catalog.warnings.push_back("Canal restringido o incompatible omitido.");continue;}
        if(out.catalog.entries.size()>=3000){out.catalog.warnings.push_back("Limite de canales alcanzado.");break;}
        Entry e;e.title=title(item);e.kind=EntryKind::Channel;e.column_id=id;e.channel_code=code;artwork(item,e);
        out.catalog.entries.push_back(std::move(e));
    }
    s->channels[id]=out.catalog;
    size_t cached_count=0;for(const auto& c:s->channels)cached_count+=c.second.entries.size();
    while(s->channels.size()>1&&(cached_count>12000||s->channels.size()>64)){
        auto old=s->channels.begin();if(old->first==id)++old;cached_count-=old->second.entries.size();s->channels.erase(old);
    }
    out.ok=true;out.status=std::to_string(out.catalog.entries.size())+" canales. A: solicitar reproduccion; B: categorias.";
    return out;
}
static SourceResult vod_source(const std::shared_ptr<ServiceSession>& s,const Entry& e,int variant);
SourceResult service_source(const std::shared_ptr<ServiceSession>& s,const Entry& e,int variant) {
    if(e.kind==EntryKind::Episode)return vod_source(s,e,variant);
    SourceResult out;
    if(!s||e.kind!=EntryKind::Channel||e.channel_code.empty()){out.status="Canal sin sesion valida.";return out;}
    CurlScope curl;if(!curl.ready){out.status="No se pudo iniciar cURL.";return out;}
    Json payload=identity(*s);payload.update({{"type","1"},{"columnId",nullptr},{"channelCode",e.channel_code}});
    Json data;if(!api_post(s->config,s->common,"/api/portalCore/v4/startPlayLive",payload,data,out.status))return out;
    const auto addresses=data.find("liveAddressList");
    if(addresses!=data.end()&&addresses->is_array())for(size_t i=0;i<addresses->size();++i)
        out.variants.push_back("Fuente "+std::to_string(i+1));
    if(variant<0||static_cast<size_t>(variant)>=out.variants.size()){out.status="Fuente de TV no disponible.";return out;}
    if(addresses!=data.end()&&addresses->is_array())for(size_t i=0;i<addresses->size();++i) {
        if(static_cast<int>(i)!=variant)continue;
        const auto& address=(*addresses)[i];
        const std::string code=text(address,"playCode"),license=text(address,"license");
        if(!license.empty()){auto resolved=resolve_ranger(s,RangerMode::Live,code,license,text(address,"tag"),text(address,"cdnType"),text(address,"AVFormat"));resolved.variants=out.variants;return resolved;}
        if(valid_source(code)&&(code.rfind("https://",0)==0||code.rfind("http://",0)==0)){
            out.ok=true;out.source=code;out.status="Fuente autorizada recibida.";return out;
        }
    }
    out.status="Ranger: no se recibio una URL directa ni una licencia utilizable.";return out;
}

static void append_assets(const Json& list,Catalog& catalog,int column,const std::string& fallback="") {
    if(!list.is_array())return;
    for(const auto& item:list) {
        const std::string id=text(item,"contentId");
        if(id.empty()||id.size()>512||protected_item(item)){catalog.warnings.push_back("Titulo restringido o incompatible omitido.");continue;}
        if(catalog.entries.size()>=500)break;
        Entry e;e.kind=EntryKind::Asset;e.title=title(item);e.content_id=id;e.column_id=column;artwork(item,e);
        // AssetList.type drives VodDetailsActivity; programType is analytics metadata.
        e.program_type=text(item,"type");
        if(e.program_type.empty())e.program_type=fallback;
        if(e.program_type!="0"&&e.program_type!="1"){catalog.warnings.push_back("Tipo de titulo no compatible.");continue;}
        catalog.entries.push_back(std::move(e));
    }
}
static bool more_pages(const Json& data,int page,size_t count,int page_size=100,const char* total_key="totalSize") {
    const std::string total=text(data,total_key);
    if(total.empty())return count>=static_cast<size_t>(page_size);
    if(total.size()>10||total.find_first_not_of("0123456789")!=std::string::npos)return false;
    return std::strtoll(total.c_str(),nullptr,10)>static_cast<long long>(page)*page_size;
}
BrowseResult service_search(const std::shared_ptr<ServiceSession>& s,const std::string& query,int page) {
    BrowseResult out;if(!s){out.status="Pulsa Y para activar antes de buscar.";return out;}
    if(query.empty()||query.size()>240||page<1||page>10000){out.status="Escribe un titulo para buscar.";return out;}
    CurlScope curl;if(!curl.ready){out.status="No se pudo iniciar cURL.";return out;}
    Json payload=identity(*s);payload.update({{"columnId","-1"},{"value",query},{"type","1"},
        {"pageSize",100},{"pageNum",page},{"filter",""}});
    Json data;if(!api_post(s->config,s->common,"/api/portalCore/v3/searchByName",payload,data,out.status))return out;
    const auto groups=data.find("searchItemList");
    if(groups==data.end()||!groups->is_array()){out.status="Busqueda: formato de resultados no reconocido.";return out;}
    size_t received=0;
    for(const auto& group:*groups){const auto list=group.find("itemList");if(list==group.end()||!list->is_array())continue;
        received+=list->size();append_assets(*list,out.catalog,-1,text(group,"programType"));}
    if(more_pages(data,page,received)){
        Entry next;next.title="Mas resultados >";next.kind=EntryKind::SearchPage;next.query=query;next.page=page+1;
        out.catalog.entries.push_back(std::move(next));
    }
    out.catalog.title="Resultados de busqueda";
    out.ok=true;out.status=received?"Busqueda: pagina "+std::to_string(page)+". A: abrir titulo; B: volver.":"Sin resultados para esa busqueda.";
    return out;
}
BrowseResult service_search_all(const std::shared_ptr<ServiceSession>& s,const std::string& query,const std::function<bool(size_t,size_t)>& progress){
    BrowseResult out;out.catalog.title="Resultados: "+query;
    if(!s||query.empty()||query.size()>240){out.status="Escribe un nombre para buscar.";return out;}
    const auto key=search_key(query);if(key.empty()){out.status="Escribe letras o numeros para buscar.";return out;}
    std::set<std::string> codes;size_t done=0,failed=0;
    for(int id:s->live_categories){
        if(progress&&!progress(done,s->live_categories.size())){out.catalog.warnings.push_back("Busqueda de TV cancelada: resultados parciales.");break;}
        auto channels=service_channels(s,id);++done;if(!channels.ok){++failed;continue;}
        for(const auto& e:channels.catalog.entries)if(search_key(e.title).find(key)!=std::string::npos&&codes.insert(e.channel_code).second){
            out.catalog.entries.push_back(e);if(out.catalog.entries.size()>=3000)break;
        }
    }
    auto vod=service_search(s,query);if(vod.ok){out.catalog.entries.insert(out.catalog.entries.end(),vod.catalog.entries.begin(),vod.catalog.entries.end());}
    else out.catalog.warnings.push_back("No se pudo consultar peliculas y series; reintenta la busqueda.");
    if(failed)out.catalog.warnings.push_back("Algunas categorias de TV no respondieron; reintenta para completar.");
    out.ok=vod.ok||!out.catalog.entries.empty()||(!s->live_categories.empty()&&!failed);
    out.status=std::to_string(out.catalog.entries.size())+" resultados: canales, peliculas y series.";
    if(!out.catalog.warnings.empty())out.status+=" Resultados parciales: "+out.catalog.warnings.front();
    return out;
}
static BrowseResult vod_list(const std::shared_ptr<ServiceSession>& s,const Entry& e) {
    BrowseResult out;Json payload=identity(*s);
    payload.update({{"columnId",e.column_id},{"columnType","2"},{"pageSize",24},
        {"pageNum",e.page},{"numDisplay",24},{"encryptVersion",0}});
    Json data;if(!api_post(s->config,s->common,"/api/portalCore/v3/getShelveData",payload,data,out.status))return out;
    const auto list=data.find("assetList");if(list==data.end()||!list->is_array()){
        out.status="Catalogo VOD: respuesta sin titulos reconocidos.";return out;}
    append_assets(*list,out.catalog,e.column_id,e.program_type);
    const size_t usable=out.catalog.entries.size();
    char stage[128];std::snprintf(stage,sizeof(stage),"vod:list page=%d received=%zu usable=%zu omitted=%zu",e.page,list->size(),usable,out.catalog.warnings.size());trace(stage);
    if(usable&&more_pages(data,e.page,list->size(),24,"assetListTotalSize")){Entry next=e;next.kind=EntryKind::VodPage;next.title="Mas titulos >";next.page=e.page+1;
        out.catalog.entries.push_back(std::move(next));}
    out.ok=true;
    if(!usable)out.status=list->empty()?"Esta categoria no devolvio titulos.":"Se recibieron "+std::to_string(list->size())+" titulos, pero faltan campos compatibles.";
    else out.status=std::to_string(usable)+" titulos. Pagina "+std::to_string(e.page)+". A: abrir; X: buscar; B: volver.";
    return out;
}
BrowseResult service_browse(const std::shared_ptr<ServiceSession>& s,const Entry& e) {
    if(e.kind==EntryKind::SearchPage)return service_search(s,e.query,e.page);
    BrowseResult out;if(!s){out.status="Pulsa Y para activar el dispositivo.";return out;}
    CurlScope curl;if(!curl.ready){out.status="No se pudo iniciar cURL.";return out;}
    if(e.kind==EntryKind::VodPage||e.kind==EntryKind::VodCategory)return vod_list(s,e);
    if(e.kind==EntryKind::Section) {
        if(e.column_id<0){out.status="Categoria VOD sin identificador valido.";return out;}
        Json payload=identity(*s);payload.update({{"columnId",e.column_id},{"specialFlag","0"},{"pageNum",1},
            {"pageSize",1000},{"numDisplay",nullptr},{"isAv1","0"}});
        Json data;if(!api_post(s->config,s->common,"/api/portalCore/v3/getColumnContents",payload,data,out.status))return out;
        const auto list=data.find("childColumnList");
        if(list==data.end()||!list->is_array()){out.status="VOD: respuesta de categorias no reconocida.";return out;}
        for(const auto& item:*list){const int id=column_id(item);if(id<0||protected_item(item)){
                out.catalog.warnings.push_back("Categoria restringida o incompatible omitida.");continue;}
            Entry category;category.title=title(item);category.kind=EntryKind::VodCategory;
            category.column_id=id;category.program_type=e.program_type;out.catalog.entries.push_back(std::move(category));
            if(out.catalog.entries.size()>=1000)break;
        }
        if(list->empty())return vod_list(s,e);
        out.ok=true;out.status="Categorias VOD. A: abrir; X: buscar; B: volver.";return out;
    }
    if(e.kind!=EntryKind::Asset||e.content_id.empty()||(e.program_type!="0"&&e.program_type!="1")) {
        out.status="Titulo no compatible.";return out;
    }
    Json payload=identity(*s);payload.update({{"contentId",e.content_id},{"type",e.program_type},
        {"sortType","0"},{"language",text(s->config,"language","es")},{"macAddr",text(s->config,"device_id")}});
    Json data;if(!api_post(s->config,s->common,"/api/portalCore/v4/getItemData",payload,data,out.status))return out;
    const auto asset=data.find("assetData");
    if(asset==data.end()||!asset->is_object()){out.status="Titulo sin ficha reconocida.";return out;}
    if(protected_item(*asset)){out.status="Titulo restringido: control de PIN pendiente.";return out;}
    const auto episodes=asset->find("simpleProgramList");
    if(episodes==asset->end()||!episodes->is_array()){out.status="Ficha recibida sin videos o episodios reconocidos.";return out;}
    for(const auto& item:*episodes){const std::string id=text(item,"contentId");if(id.empty()||protected_item(item))continue;
        Entry video;video.kind=EntryKind::Episode;video.content_id=id;video.column_id=e.column_id;
        artwork(*asset,video);if(video.artwork.empty()){video.artwork=e.artwork;video.artwork_cache=e.artwork_cache;}
        video.program_type=e.program_type;video.series_id=e.program_type=="0"?e.content_id:"";
        video.title=e.program_type=="0"?"Episodio "+text(item,"seriesNumber","?")+" - "+title(item):"Reproducir: "+title(*asset);
        out.catalog.entries.push_back(std::move(video));if(out.catalog.entries.size()>=10000)break;
    }
    out.catalog.title=title(*asset);out.catalog.description=clean_description(text(*asset,"description",text(*asset,"descriptionCompt")));
    Entry cover;artwork(*asset,cover);out.catalog.artwork=cover.artwork.empty()?e.artwork:cover.artwork;out.catalog.artwork_cache=cover.artwork_cache.empty()?e.artwork_cache:cover.artwork_cache;
    out.ok=true;out.status=e.program_type=="0"?"Episodios. A: solicitar reproduccion; B: volver.":"Pelicula. A: solicitar reproduccion; B: volver.";
    return out;
}
// Subtitles: the service does not always use the same layout ("subtitleList"/"file"/"srt").
// Search any key that mentions subtitles/captions and accept common text formats.
static std::string lower(std::string v){std::transform(v.begin(),v.end(),v.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});return v;}
static std::string first_text(const Json& o,std::initializer_list<const char*> keys){for(const char* k:keys){auto v=text(o,k);if(!v.empty())return v;}return "";}
static void subtitle_files(const Json& sub,size_t number,std::vector<SubtitleSource>& out,std::vector<std::string>& seen_types){
    if(!sub.is_object())return;
    std::string language=first_text(sub,{"language","languageName","lang","langName","name","title","label"});
    if(language.empty())language="Subtitulo "+std::to_string(number);
    if(language.size()>60)language.resize(60);
    std::vector<const Json*> files;
    for(const char* k:{"file","files","fileList","subtitleFile"}){auto f=sub.find(k);
        if(f!=sub.end()&&f->is_array())for(const auto& x:*f)files.push_back(&x);
        else if(f!=sub.end()&&f->is_object())files.push_back(&*f);}
    if(files.empty())files.push_back(&sub);
    const SubtitleSource* chosen=nullptr;std::vector<SubtitleSource> candidates;
    for(const Json* f:files){if(!f->is_object())continue;
        std::string url=first_text(*f,{"url","fileUrl","path","src","link","address","subtitleUrl"});
        std::string type=lower(first_text(*f,{"fileType","type","format","ext","extension"}));
        if(type.empty()){const auto path=url.substr(0,url.find('?'));const auto dot=path.rfind('.');if(dot!=std::string::npos)type=lower(path.substr(dot+1));}
        if(std::find(seen_types.begin(),seen_types.end(),type)==seen_types.end()&&seen_types.size()<8)seen_types.push_back(type.empty()?"?":type);
        if(url.rfind("http://",0)==0)url="https://"+url.substr(7); // subtitle requests stay on HTTPS
        if(url.rfind("https://",0)!=0)continue;
        if(!type.empty()&&type!="srt"&&type!="vtt"&&type!="webvtt"&&type!="ass"&&type!="ssa"&&type!="txt"&&type!="sub")continue;
        candidates.push_back({language,url,lower(text(*f,"md5"))});
        if(type=="srt"&&!chosen)chosen=&candidates.back();
    }
    if(candidates.empty())return;
    const SubtitleSource pick=chosen?*chosen:candidates.front();
    for(const auto& x:out)if(x.url==pick.url)return;
    if(out.size()<32)out.push_back(pick);
}
static void collect_subtitles(const Json& node,std::vector<SubtitleSource>& out,std::vector<std::string>& seen_types,int depth,const char* skip=nullptr){
    if(depth>6||out.size()>=32)return;
    if(node.is_array()){for(const auto& x:node)collect_subtitles(x,out,seen_types,depth+1,skip);return;}
    if(!node.is_object())return;
    for(auto it=node.begin();it!=node.end();++it){
        if(skip&&it.key()==skip)continue;
        const std::string key=lower(it.key());
        if(key.find("subtitle")!=std::string::npos||key.find("caption")!=std::string::npos||key=="subs"){
            size_t n=out.size()+1;
            if(it->is_array())for(const auto& x:*it)subtitle_files(x,n++,out,seen_types);
            else if(it->is_object())subtitle_files(*it,n,out,seen_types);
        }else if(it->is_object()||it->is_array())collect_subtitles(*it,out,seen_types,depth+1,skip);
    }
}
static std::string key_summary(const Json& o){std::string keys;if(!o.is_object())return keys;
    for(auto it=o.begin();it!=o.end()&&keys.size()<400;++it){keys+=it.key();keys+=',';}return keys;}
static SourceResult vod_source(const std::shared_ptr<ServiceSession>& s,const Entry& e,int variant) {
    SourceResult out;if(!s||e.content_id.empty()){out.status="Video sin sesion valida.";return out;}
    CurlScope curl;if(!curl.ready){out.status="No se pudo iniciar cURL.";return out;}
    Json payload=identity(*s);payload.update({{"columnId",-1},{"contentId",e.content_id},
        {"seriesContentId",e.series_id.empty()?Json(nullptr):Json(e.series_id)},{"type","1"},
        {"startTime",0},{"authType",nullptr},{"episodeNumberList",nullptr}});
    Json data;if(!api_post(s->config,s->common,"/api/portalCore/v10/startPlayVOD",payload,data,out.status))return out;
    const auto episodes=data.find("episodeList");
    if(episodes==data.end()||!episodes->is_array()){out.status="Video autorizado sin fuentes reconocidas.";return out;}
    struct Candidate {std::string code,license,tag,label;};std::vector<Candidate> candidates;
    std::vector<std::string> subtitle_types;std::string episode_keys;bool matched=false;
    for(const auto& episode:*episodes){if(text(episode,"programContentId")!=e.content_id)continue;
        collect_subtitles(episode,out.subtitles,subtitle_types,0);matched=true;
        if(out.subtitles.empty()&&episode_keys.empty())episode_keys=key_summary(episode);
        const auto groups=episode.find("totalMovieList");if(groups==episode.end()||!groups->is_array())continue;
        for(const auto& group:*groups){const auto movies=group.find("movieList");if(movies==group.end()||!movies->is_array())continue;
            for(const auto& movie:*movies){
                const auto licenses=movie.find("licenseList");if(licenses==movie.end()||!licenses->is_array())continue;
                for(const auto& item:*licenses)if(!text(item,"license").empty()){
                    std::string label=text(movie,"quality",text(group,"quality","Fuente"));
                    const std::string audio=text(movie,"audioInfo");if(!audio.empty())label+=" / "+audio;
                    if(label.size()>80)label.resize(80);
                    candidates.push_back({text(movie,"contentId"),text(item,"license"),text(item,"tag"),label});
                }
            }
        }
    }
    // Some responses keep subtitles next to the episode list instead of inside it.
    if(out.subtitles.empty())collect_subtitles(data,out.subtitles,subtitle_types,0,"episodeList");
    {std::string types;for(const auto& t:subtitle_types)types+=t+",";
        trace(("sub:found "+std::to_string(out.subtitles.size())+" episode_match="+(matched?"si":"no")+" types="+types).c_str());
        // Field names only (never values) so a report can show where subtitles live.
        if(out.subtitles.empty()){trace(("sub:episode_keys "+episode_keys).c_str());trace(("sub:response_keys "+key_summary(data)).c_str());}}
    for(size_t i=0;i<candidates.size();++i)out.variants.push_back(std::to_string(i+1)+". "+candidates[i].label);
    if(variant>=0&&static_cast<size_t>(variant)<candidates.size()){
        const auto& c=candidates[variant];auto resolved=resolve_ranger(s,RangerMode::Vod,c.code,c.license,c.tag,"1");
        resolved.variants=out.variants;resolved.subtitles=out.subtitles;return resolved;
    }
    out.status="El servicio no devolvio fuentes autorizadas para este video.";return out;
}
bool service_subtitle(const std::shared_ptr<ServiceSession>& s,const SubtitleSource& sub,std::string& data,std::string& error){
    data.clear();if(!s){error="No hay sesion para cargar subtitulos.";return false;}
    CurlScope curl;if(!curl.ready){error="No se pudo iniciar HTTP.";return false;}
    std::string received;
    // Subtitle host receives no media or portal authentication headers.
    if(!request(sub.url,nullptr,s->config,received,error,false))return false;
    if(received.empty()||received.size()>1024*1024){error="Subtitulo vacio o demasiado grande.";return false;}
    if(!sub.md5.empty()){
        unsigned char digest[16];if(mbedtls_md5_ret(reinterpret_cast<const unsigned char*>(received.data()),received.size(),digest)){error="No se pudo verificar el subtitulo.";return false;}
        const char* digits="0123456789abcdef";std::string actual;
        for(unsigned char value:digest){actual+=digits[value>>4];actual+=digits[value&15];}
        // The service may publish the digest in upper case.
        std::string expected=sub.md5;std::transform(expected.begin(),expected.end(),expected.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        if(actual!=expected){trace("sub:md5_mismatch");error="El subtitulo no coincide con la huella del servicio.";return false;}
    }
    data=std::move(received);return true;
}
}
