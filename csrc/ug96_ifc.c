//#define ZERYNTH_PRINTF
#include "zerynth.h"
#include "ug96_ifc.h"


//a reference to a Python exception to be returned on error (g350Exception)
int32_t ug96exc;

///////// CNATIVES
// The following functions are callable from Python.
// Functions starting with "_" are utility functions called by CNatives


/**
 * @brief strings for network types
 */
uint8_t *_urats = "GSMUMTS";
uint8_t _uratpos[] = {0,3};
uint8_t _uratlen[] = {3,4};

/**
 * @brief _g350_init calls _gs_init, _gs_poweron and _gs_config0
 *
 * As last parameter, requires an integer saved to global ug96exc, representing the name assigned to ug96Exception
 * so that it can be raised by returning ug96exc. If modules initialization is successful, the next step is calling startup
 *
 */
C_NATIVE(_ug96_init){
    NATIVE_UNWARN();
    int32_t serial;
    int32_t rx;
    int32_t tx;
    int32_t rts;
    int32_t dtr;
    int32_t poweron;
    int32_t reset;
    int32_t status;
    int32_t status_on;
    int32_t kill;
    int32_t err = ERR_OK;
    int32_t exc;


    if (parse_py_args("iiiiiiiii", nargs, args, &serial, &dtr, &rts, &poweron, &reset, &status, &kill, &status_on, &exc) != 9)
        return ERR_TYPE_EXC;

    ug96exc = exc;

    *res = MAKE_NONE();


    RELEASE_GIL();
    _gs_init();
    gs.serial = serial&0xff;
    gs.rx = _vm_serial_pins[gs.serial].rxpin;
    gs.tx = _vm_serial_pins[gs.serial].txpin;
    gs.dtr = dtr;
    gs.rts = rts;
    gs.poweron = poweron;
    gs.reset = reset;
    gs.status = status;
    gs.status_on = status_on;
    gs.kill = kill;
    printf("After init\n");
    ACQUIRE_GIL();

    return err;
}



C_NATIVE(_ug96_startup){
    NATIVE_UNWARN();
    int32_t err = ERR_OK;
    int32_t skip_poweron;
    *res = MAKE_NONE();
    
    if(parse_py_args("i",nargs,args,&skip_poweron)!=1) return ERR_TYPE_EXC;

    RELEASE_GIL();
    //Init serial
    gs.talking=0;
    vhalSerialDone(gs.serial);
    vhalSerialInit(gs.serial, 115200, SERIAL_CFG(SERIAL_PARITY_NONE,SERIAL_STOP_ONE, SERIAL_BITS_8,0,0), gs.rx, gs.tx);
    if (!skip_poweron) {
        //setup pins
        printf("Setting pins\n");
        vhalPinSetMode(gs.status,PINMODE_INPUT_PULLUP);
        vhalPinSetMode(gs.poweron,PINMODE_OUTPUT_PUSHPULL);
        // vhalPinSetMode(gs.reset,PINMODE_OUTPUT_PUSHPULL);
        if (!_gs_poweron()) err = ERR_HARDWARE_INITIALIZATION_ERROR;
    }

    if(!_gs_config0()) err = ERR_HARDWARE_INITIALIZATION_ERROR;

    ACQUIRE_GIL();
    if(err==ERR_OK && gs.thread==NULL){
        //let's start modem thread (if not already started)
        printf("Starting modem thread with size %i\n",VM_DEFAULT_THREAD_SIZE);
        gs.thread = vosThCreate(VM_DEFAULT_THREAD_SIZE,VOS_PRIO_NORMAL,_gs_loop,NULL,NULL);
        vosThResume(gs.thread);
        vosThSleep(TIME_U(1000,MILLIS)); // let modem thread have a chance to start
    }
    return err;
}


C_NATIVE(_ug96_shutdown){
    NATIVE_UNWARN();
    int32_t err = ERR_OK;
    int32_t skip_poweroff;
    *res = MAKE_NONE();
    
    if(parse_py_args("i",nargs,args,&skip_poweroff)!=1) return ERR_TYPE_EXC;

    RELEASE_GIL();
    gs.talking=0;
    if (!skip_poweroff) {
        //setup pins
        printf("Setting pins\n");
        _gs_poweroff();
    }

    ACQUIRE_GIL();
    return err;
}





// /**
//  * @brief _ug96_detach removes the link with the APN while keeping connected to the GSM network
//  *
//  *
//  */
C_NATIVE(_ug96_detach){
    NATIVE_UNWARN();
    int err = ERR_OK;
    int r,timeout=10000;
    *res = MAKE_NONE();
    RELEASE_GIL();
    if(!_gs_control_psd(0)) goto exit;

    r = _gs_attach(0);
    if(r) {
        if (r==GS_ERR_TIMEOUT) err = ERR_TIMEOUT_EXC;
        else err = ug96exc;
        goto exit;
    }
    while(timeout>0){
        _gs_check_network();
        _gs_is_attached();
        if (!gs.attached) break;
        vosThSleep(TIME_U(100,MILLIS));
        timeout-=100;
    }
    if(timeout<0) {
        err = ERR_TIMEOUT_EXC;
        goto exit;
    }

exit:
    ACQUIRE_GIL();
    return err;
}



// /**
//  * @brief _ug96_attach tries to link to the given APN
//  *
//  * This function can block for a very long time (up to 2 minutes) due to long timeout of used AT commands
//  *
//  *
//  */
C_NATIVE(_ug96_attach){
    NATIVE_UNWARN();
    uint8_t *apn;
    uint32_t apn_len;
    uint8_t *user;
    uint32_t user_len;
    uint8_t *password;
    uint32_t password_len;
    uint32_t authmode;
    int32_t timeout;
    int32_t wtimeout;
    int32_t err=ERR_OK;

    int i,j;


    if(parse_py_args("sssii",nargs,args,&apn,&apn_len,&user,&user_len,&password,&password_len,&authmode,&wtimeout)!=5) return ERR_TYPE_EXC;

    *res = MAKE_NONE();
    RELEASE_GIL();

    //Attach to GPRS
    for (j=0;j<5;j++){
        i = _gs_attach(1);
        if(i && j==4) {
            if (i==GS_ERR_TIMEOUT) err = ERR_TIMEOUT_EXC;
            else err = ug96exc;
            goto exit;
        } else if(i) {
            vosThSleep(TIME_U(1000*(j+1),MILLIS));
        } else {
            break;
        }
    }
    timeout=wtimeout;
    while(timeout>0){
        _gs_check_network();
        _gs_is_attached();
        if ((gs.registered==GS_REG_OK || gs.registered==GS_REG_ROAMING)&&(gs.attached)) break;
        vosThSleep(TIME_U(100,MILLIS));
        timeout-=100;
    }
    if(timeout<0) {
        err = ERR_TIMEOUT_EXC;
        goto exit;
    }

    //configure PSD
    err = ug96exc;
    if(!_gs_configure_psd(apn,apn_len,user,user_len,password,password_len,authmode)) goto exit;


    //activate PSD
    gs.attached = 0;
    if(!_gs_control_psd(1)) goto exit;

    err = ERR_OK;

    exit:
    ACQUIRE_GIL();
    *res = MAKE_NONE();
    return err;
}



/**
 * @brief _ug96_operators retrieve the operator list and converts it to a tuple
 *
 *
 */
C_NATIVE(_ug96_operators){
    NATIVE_UNWARN();
    int i;

    RELEASE_GIL();
    i = _gs_list_operators();
    ACQUIRE_GIL();
    if (i){
        *res = MAKE_NONE();
        return ERR_OK;
    }
    PTuple *tpl = ptuple_new(gsopn,NULL);
    for(i=0;i<gsopn;i++){
        PTuple *tpi = ptuple_new(4,NULL);
        PTUPLE_SET_ITEM(tpi,0,PSMALLINT_NEW(gsops[i].type));
        PTUPLE_SET_ITEM(tpi,1,pstring_new(gsops[i].fmtl_l,gsops[i].fmt_long));
        PTUPLE_SET_ITEM(tpi,2,pstring_new(gsops[i].fmts_l,gsops[i].fmt_short));
        PTUPLE_SET_ITEM(tpi,3,pstring_new(gsops[i].fmtc_l,gsops[i].fmt_code));
        PTUPLE_SET_ITEM(tpl,i,tpi);
    }

    *res = tpl;
    return ERR_OK;
}


/**
 * @brief _ug96_set_operator try to set the current operator given its name
 *
 *
 */
C_NATIVE(_ug96_set_operator){
    NATIVE_UNWARN();
    int i;
    uint8_t *opname;
    uint32_t oplen;

    if(parse_py_args("s",nargs,args,&opname,&oplen)!=1) return ERR_TYPE_EXC;

    RELEASE_GIL();
    i = _gs_set_operator(opname,oplen);
    ACQUIRE_GIL();
    *res = MAKE_NONE();
    if (i){
        return ug96exc;
    }
    return ERR_OK;
}





/**
 * @brief _g350_rssi return the signal strength as reported by +CIEV urc
 *
 *
 */
C_NATIVE(_ug96_rssi){
    NATIVE_UNWARN();
    int32_t rssi;

    RELEASE_GIL();
    rssi=_gs_rssi();
    ACQUIRE_GIL();

    if (rssi==99) rssi=0;
    else if (rssi<=31) rssi = -113 +2*rssi;

    *res = PSMALLINT_NEW(rssi);
    return ERR_OK;
}





/**
 * @brief _g450_network_info retrieves network information through +URAT and *CGED
 *
 *
 */
C_NATIVE(_ug96_network_info){
    NATIVE_UNWARN();
    int p0,l0,mcc,mnc;
    PString *urat;
    PTuple *tpl = ptuple_new(8,NULL);
    uint8_t bsi[5];

    //RAT  : URAT
    //CELL : UCELLINFO
    RELEASE_GIL();
    _gs_check_network();
    _gs_is_attached();
    
    // get tech string (slice) from pre-allocated buffer
    if (gs.tech==0) p0=0;
    else if (gs.tech==2) p0=1;
    else p0=0;
    urat = pstring_new(_uratlen[p0],_urats+_uratpos[p0]);
    PTUPLE_SET_ITEM(tpl,0,urat);

    if (_gs_cell_info(&mcc, &mnc) <= 0) {
        mcc = -1;
        mnc = -1;
    }
    PTUPLE_SET_ITEM(tpl,1,PSMALLINT_NEW(mcc));
    PTUPLE_SET_ITEM(tpl,2,PSMALLINT_NEW(mnc));
    urat = pstring_new(0,NULL);
    PTUPLE_SET_ITEM(tpl,3,urat);

    urat = pstring_new(strlen(gs.lac),gs.lac);
    PTUPLE_SET_ITEM(tpl,4,urat);
    urat = pstring_new(strlen(gs.ci),gs.ci);
    PTUPLE_SET_ITEM(tpl,5,urat);

    //registered to network
    PTUPLE_SET_ITEM(tpl,6,(gs.registered==GS_REG_OK || gs.registered==GS_REG_ROAMING) ? PBOOL_TRUE():PBOOL_FALSE());
    //attached to APN
    PTUPLE_SET_ITEM(tpl,7,gs.attached ? PBOOL_TRUE():PBOOL_FALSE());

    ACQUIRE_GIL();
    *res = tpl;
    return ERR_OK;
}

/**
 * @brief _g350_mobile_info retrieves info on IMEI and SIM card by means of +CGSN and *CCID
 *
 *
 */
C_NATIVE(_ug96_mobile_info){
    NATIVE_UNWARN();
    uint8_t imei[16];
    uint8_t iccid[22];
    int im_len;
    int ic_len;

    PTuple *tpl = ptuple_new(2,NULL);
    RELEASE_GIL();
    im_len = _gs_imei(imei);
    ic_len = _gs_iccid(iccid);
    if(im_len<=0) {
        PTUPLE_SET_ITEM(tpl,0,pstring_new(0,NULL));
    } else {
        PTUPLE_SET_ITEM(tpl,0,pstring_new(im_len,imei));
    }
    if(ic_len<=0) {
        PTUPLE_SET_ITEM(tpl,1,pstring_new(0,NULL));
    } else {
        PTUPLE_SET_ITEM(tpl,1,pstring_new(ic_len,iccid));
    }
    ACQUIRE_GIL();
    *res =tpl;
    return ERR_OK;
}

/**
 * @brief _g350_link_info retrieves ip and dns by means of +UPSND
 *
 *
 */
C_NATIVE(_ug96_link_info){
    NATIVE_UNWARN();
    PString *ips;
    PString *dns;
    uint32_t addrlen;
    uint8_t addrbuf[16];

    RELEASE_GIL();

    addrlen = _gs_local_ip(addrbuf);
    if(addrlen>0){
        ips = pstring_new(addrlen,addrbuf);
    } else {
    ips = pstring_new(0,NULL);
    }

    addrlen = _gs_dns(addrbuf);
    if(addrlen>0){
        dns = pstring_new(addrlen,addrbuf);
    } else {
        dns = pstring_new(0,NULL);
    }

    ACQUIRE_GIL();
    PTuple *tpl = ptuple_new(2,NULL);
    PTUPLE_SET_ITEM(tpl,0,ips);
    PTUPLE_SET_ITEM(tpl,1,dns);
    *res = tpl;
    return ERR_OK;
}



#define DRV_SOCK_DGRAM 1
#define DRV_SOCK_STREAM 0
#define DRV_AF_INET 0

C_NATIVE(_ug96_socket_create){
    NATIVE_UNWARN();
    int err = ERR_OK;
    int32_t family;
    int32_t type;
    int32_t proto;
    if (parse_py_args("III", nargs, args, DRV_AF_INET, &family, DRV_SOCK_STREAM, &type, 6 /*tcp*/, &proto) != 3) return ERR_TYPE_EXC;
    if (type != DRV_SOCK_DGRAM && type != DRV_SOCK_STREAM)
        return ERR_TYPE_EXC;
    if (family != DRV_AF_INET)
        return ERR_UNSUPPORTED_EXC;
    proto = (type == DRV_SOCK_DGRAM) ? 17 : 6;

    RELEASE_GIL();
    err = _gs_socket_new(proto,0);
    if(err<0) {
        err = ERR_IOERROR_EXC;
    } else {
        *res = PSMALLINT_NEW(err);
        err = ERR_OK;
    }
    ACQUIRE_GIL();

    return err;
}

C_NATIVE(_ug96_socket_connect) {
    C_NATIVE_UNWARN();
    int32_t sock,err=ERR_OK;
    GSocket *ssock;
    GSSlot *slot;
    NetAddress addr;

    if (parse_py_args("in", nargs, args, &sock, &addr) != 2)
        return ERR_TYPE_EXC;

    *res = MAKE_NONE();
    RELEASE_GIL();
    sock = _gs_socket_connect(sock,&addr);
    if(sock) {
        err = ERR_IOERROR_EXC;
    }
    ACQUIRE_GIL();
    return err;
}

C_NATIVE(_ug96_socket_close) {
    C_NATIVE_UNWARN();
    int32_t sock;
    GSocket *ssock;
    GSSlot *slot;
    int err = ERR_OK;
    int rr;
    if (parse_py_args("i", nargs, args, &sock) != 1)
        return ERR_TYPE_EXC;
    RELEASE_GIL();
    sock = _gs_socket_close(sock);
    //ignore result
    ACQUIRE_GIL();
    *res = PSMALLINT_NEW(sock);
    return err;
}

C_NATIVE(_ug96_socket_send) {
    C_NATIVE_UNWARN();
    uint8_t *buf;
    int32_t len;
    int32_t flags;
    int32_t sock;
    int32_t wrt;
    int32_t tsnd;
    int err = ERR_OK;
    if (parse_py_args("isi", nargs, args,
                &sock,
                &buf, &len,
                &flags) != 3) return ERR_TYPE_EXC;
    RELEASE_GIL();
    wrt=0;
    while(wrt<len && err==ERR_OK){
        tsnd = MIN(MAX_SOCK_BUF,(len-wrt));
        tsnd = _gs_socket_send(sock,buf+wrt,tsnd);
        if (tsnd<0) {
            err=ERR_IOERROR_EXC;
            break;
        }
        wrt+=tsnd;
    }
    ACQUIRE_GIL();
    *res = PSMALLINT_NEW(wrt);
    return err;
}

C_NATIVE(_ug96_socket_sendto){
    C_NATIVE_UNWARN();
    uint8_t* buf;
    int32_t len;
    int32_t flags;
    int32_t sock;
    int32_t wrt=0;
    int32_t tsnd;
    int32_t err=ERR_OK;
    NetAddress addr;

    if (parse_py_args("isni", nargs, args,
            &sock,
            &buf, &len,
            &addr,
            &flags)
        != 4)
        return ERR_TYPE_EXC;

    RELEASE_GIL();
    wrt=0;
    while(wrt<len && err==ERR_OK){
        tsnd = MIN(MAX_SOCK_BUF,(len-wrt));
        tsnd = _gs_socket_sendto(sock,buf+wrt,tsnd,&addr);
        if (tsnd<0) {
            err=ERR_IOERROR_EXC;
            break;
        }
        wrt+=tsnd;
    }
    ACQUIRE_GIL();

    *res = PSMALLINT_NEW(wrt);
    return err;
}

C_NATIVE(_ug96_socket_recv_into){
    C_NATIVE_UNWARN();
    uint8_t* buf;
    int32_t len;
    int32_t sz;
    int32_t flags;
    int32_t ofs;
    int32_t sock;
    int rb;
    int trec;
    int err = ERR_OK;
    uint8_t *hex;
    if (parse_py_args("isiiI", nargs, args,
            &sock,
            &buf, &len,
            &sz,
            &flags,
            0,
            &ofs)
        != 5)
        return ERR_TYPE_EXC;
    buf += ofs;
    len -= ofs;
    len = (sz < len) ? sz : len;
    RELEASE_GIL();

    rb=0;
    while(rb<len && err==ERR_OK){
        printf("Reading %i\n",len-rb);
        trec = _gs_socket_recv(sock,buf+rb,len-rb);
        printf("Read %i\n",trec);
        if(trec<0) {
            err=ERR_IOERROR_EXC;
        } else {
            rb+=trec;
        }
    }
    ACQUIRE_GIL();
    *res = PSMALLINT_NEW(rb);
    return err;
}

C_NATIVE(_ug96_socket_recvfrom_into){
    C_NATIVE_UNWARN();
    uint8_t* buf;
    int32_t len;
    int32_t sz;
    int32_t flags;
    int32_t ofs;
    int32_t sock;
    int rb;
    int trec;
    int err = ERR_OK;
    uint8_t addr[16];
    uint32_t addrlen;
    PString *oaddr = NULL;
    int32_t port;
    int gotip=0;

    if (parse_py_args("isiiI", nargs, args,
            &sock,
            &buf, &len,
            &sz,
            &flags,
            0,
            &ofs)
        != 5)
        return ERR_TYPE_EXC;
    buf += ofs;
    len -= ofs;
    len = (sz < len) ? sz : len;
    RELEASE_GIL();
    rb=0;
    while(rb<len && err==ERR_OK){
        trec = _gs_socket_recvfrom(sock,buf+rb,len-rb,addr,&addrlen,&port);
        if (trec==0 && rb==0){
            //no data yet
            continue;
        } else if(trec<0) {
            err=ERR_IOERROR_EXC;
        } else {
            //got udp packet
            rb+=trec;
            break;
        }
    }
    ACQUIRE_GIL();
    if(err==ERR_OK){
        PTuple* tpl = (PTuple*)psequence_new(PTUPLE, 2);
        PTUPLE_SET_ITEM(tpl, 0, PSMALLINT_NEW(rb));
        oaddr = pstring_new(addrlen,addr);
        PTuple* ipo = ptuple_new(2,NULL);
        PTUPLE_SET_ITEM(ipo,0,oaddr);
        PTUPLE_SET_ITEM(ipo,1,PSMALLINT_NEW(port));
        PTUPLE_SET_ITEM(tpl, 1, ipo);
        *res = tpl;
    }
    return err;
}



C_NATIVE(_ug96_socket_bind){
    C_NATIVE_UNWARN();
    int32_t sock;
    NetAddress addr;
    GSocket *ssock;
    GSSlot *slot;
    int err = ERR_OK;
    if (parse_py_args("in", nargs, args, &sock, &addr) != 2)
        return ERR_TYPE_EXC;
    RELEASE_GIL();
    if(_gs_socket_bind(sock,&addr)){
        err = ERR_IOERROR_EXC;
    }
    ACQUIRE_GIL();
    *res = MAKE_NONE();
    return err;
}


C_NATIVE(_ug96_socket_select){
    C_NATIVE_UNWARN();
    int32_t timeout;
    int32_t tmp, i, j, sock = -1,r;
    uint32_t tstart;
    PObject *tobj;

    if (nargs < 4)
        return ERR_TYPE_EXC;

    PObject* rlist = args[0];
    PObject* wlist = args[1];
    PObject* xlist = args[2];
    PObject* tm = args[3];
    int rls = PSEQUENCE_ELEMENTS(rlist);

    if (tm == MAKE_NONE()) {
        timeout = -1;
    } else if (IS_PSMALLINT(tm)) {
        timeout = PSMALLINT_VALUE(tm);
    } else {
        return ERR_TYPE_EXC;
    }

    uint8_t *rlready = gc_malloc(rls);

    RELEASE_GIL();
    i=-1;
    tstart = vosMillis();
    while(1){
        i++;
        if(i>=rls) {
            //reset and check timeout
            i=0;
            if(timeout>=0 && (vosMillis()-tstart>timeout)) break;
            vosThSleep(TIME_U(100,MILLIS)); //sleep a bit
            //TODO: consider using RD URC to signal data ready for each socket
            //and suspend here, avoiding polling (quite cumbersome tough)
        }
        tobj = PSEQUENCE_OBJECTS(rlist)[i];
        sock = PSMALLINT_VALUE(tobj);
        printf("S0 %i\n",sock);
        if(sock>=0&&sock<MAX_SOCKS){
            r = _gs_socket_available(sock);
            if (r<=0) rlready[i]=0;
            else rlready[i]=1;
        }
    }

    PTuple* tpl = (PTuple*)psequence_new(PTUPLE, 3);
    //count the number of ready sockets
    tmp = 0;
    for (j = 0; j < rls; j++) {
        if (rlready[j]) tmp++;
    }
    //fill ready socket list
    PTuple *rpl = ptuple_new(tmp,NULL);
    tmp = 0;
    for (j = 0; j < rls; j++) {
        if (rlready[j]) {
            PTUPLE_SET_ITEM(rpl,tmp,PSEQUENCE_OBJECTS(rlist)[j]);
            tmp++;
        }
    }
    PTUPLE_SET_ITEM(tpl,0,rpl);
    //ignore wlist and elist: TODO, add this functionality
    rpl = ptuple_new(0,NULL);
    PTUPLE_SET_ITEM(tpl,1,rpl);
    PTUPLE_SET_ITEM(tpl,2,rpl);
    gc_free(rlready);
    ACQUIRE_GIL();

    *res = tpl;
    return ERR_OK;
}

// /////////////////////SSL/TLS

#define _CERT_NONE 1
#define _CERT_OPTIONAL 2
#define _CERT_REQUIRED 4
#define _CLIENT_AUTH 8
#define _SERVER_AUTH 16

C_NATIVE(_ug96_secure_socket)
{
    C_NATIVE_UNWARN();
    int32_t err = ERR_OK;
    int32_t family = DRV_AF_INET;
    int32_t type = DRV_SOCK_STREAM;
    int32_t proto = 6;
    int32_t sock;
    int32_t i;
    int32_t ssocknum = 0;
    int32_t ctxlen;
    uint8_t* certbuf = NULL;
    uint16_t certlen = 0;
    uint8_t* clibuf = NULL;
    uint16_t clilen = 0;
    uint8_t* pkeybuf = NULL;
    uint16_t pkeylen = 0;
    uint32_t options = _CLIENT_AUTH | _CERT_NONE;
    uint8_t* hostbuf = NULL;
    uint16_t hostlen = 0;
    int amode;

    PTuple* ctx;
    ctx = (PTuple*)args[nargs - 1];
    nargs--;
    if (parse_py_args("III", nargs, args, DRV_AF_INET, &family, DRV_SOCK_STREAM, &type, 6, &proto) != 3){
        return ERR_TYPE_EXC;
    }
    if (type != DRV_SOCK_DGRAM && type != DRV_SOCK_STREAM){
        return ERR_TYPE_EXC;
    }
    if (family != DRV_AF_INET)
        return ERR_UNSUPPORTED_EXC;
    if(proto!=6)
        return ERR_UNSUPPORTED_EXC;

    ctxlen = PSEQUENCE_ELEMENTS(ctx);
    if (ctxlen && ctxlen != 5)
        return ERR_TYPE_EXC;

    if (ctxlen) {
        //ssl context passed
        PObject* cacert = PTUPLE_ITEM(ctx, 0);
        PObject* clicert = PTUPLE_ITEM(ctx, 1);
        PObject* ppkey = PTUPLE_ITEM(ctx, 2);
        PObject* host = PTUPLE_ITEM(ctx, 3);
        PObject* iopts = PTUPLE_ITEM(ctx, 4);
        certbuf = PSEQUENCE_BYTES(cacert);
        certlen = PSEQUENCE_ELEMENTS(cacert);
        clibuf = PSEQUENCE_BYTES(clicert);
        clilen = PSEQUENCE_ELEMENTS(clicert);
        hostbuf = PSEQUENCE_BYTES(host);
        hostlen = PSEQUENCE_ELEMENTS(host);
        pkeybuf = PSEQUENCE_BYTES(ppkey);
        pkeylen = PSEQUENCE_ELEMENTS(ppkey);
        options = PSMALLINT_VALUE(iopts);
    }

    GSocket* sslsock = NULL;


    RELEASE_GIL();
    sock = _gs_socket_new(proto,1);  //request secure socket
    if(sock<0) {
        err = ERR_IOERROR_EXC;
        *res = MAKE_NONE();
    } else {
        *res = PSMALLINT_NEW(sock);
        err = ERR_OK;
    }
    if(err==ERR_OK) {
        //let's configure tls


        //NOTE: ZERYNTH CERTS END with \0
        if(options&_CERT_NONE) {
           //NO CACERT VERIFICATION
           amode = 0;
        } else {
            //REQUIRED OR OPTIONAL AUTH
            if(certlen) {
                //CACERT GIVEN
                amode=1;
                certlen--;
            }else {
                //NO CACERT
                amode=1;
            }
        }
        if(clilen) {
            //load clicert
            amode=2;
            clilen--;
        }
        if(pkeylen){
            //load clipkey
            amode=2;
            pkeylen--;
        }

        if (_gs_socket_tls(sock,certbuf,certlen,clibuf,clilen,pkeybuf,pkeylen,amode)) {
            //oops, close socket
            _gs_socket_closing(sock);
            err = ERR_IOERROR_EXC;
        }
    }

    ACQUIRE_GIL();

    return err;
}



// /////////////////////DNS

C_NATIVE(_ug96_resolve){
    C_NATIVE_UNWARN();
    uint8_t* url;
    uint32_t len;
    uint8_t saddr[16];
    uint32_t saddrlen;
    int err = ERR_OK;
    if (parse_py_args("s", nargs, args, &url, &len) != 1)
        return ERR_TYPE_EXC;
    RELEASE_GIL();
    saddrlen = _gs_resolve(url,len,saddr);
    if(saddrlen>0) {
        *res = pstring_new(saddrlen,saddr);
    } else {
        err = ERR_IOERROR_EXC;
    }
    ACQUIRE_GIL();
    return err;
}



// /////////////////////RTC

C_NATIVE(_ug96_rtc){
    C_NATIVE_UNWARN();
    int err = ERR_OK;
    uint8_t time[20];
    *res = MAKE_NONE();
    memset(time,0,20);
    RELEASE_GIL();
    if(!_gs_get_rtc(time)) err=ERR_RUNTIME_EXC;
    ACQUIRE_GIL();
    if (err==ERR_OK) {
        PTuple* tpl = ptuple_new(7,NULL);
        int yy,MM,dd,hh,mm,ss,tz;
        yy = 2000+((time[0]-'0')*10+(time[1]-'0'));
        MM = (time[3]-'0')*10+(time[4]-'0');
        dd = (time[6]-'0')*10+(time[7]-'0');
        hh = (time[9]-'0')*10+(time[10]-'0');
        mm = (time[12]-'0')*10+(time[13]-'0');
        ss = (time[15]-'0')*10+(time[16]-'0');
        tz = ((time[18]-'0')*10+(time[19]-'0'))*15*((time[17]=='-')?-1:1);
        PTUPLE_SET_ITEM(tpl,0,PSMALLINT_NEW(yy));
        PTUPLE_SET_ITEM(tpl,1,PSMALLINT_NEW(MM));
        PTUPLE_SET_ITEM(tpl,2,PSMALLINT_NEW(dd));
        PTUPLE_SET_ITEM(tpl,3,PSMALLINT_NEW(hh));
        PTUPLE_SET_ITEM(tpl,4,PSMALLINT_NEW(mm));
        PTUPLE_SET_ITEM(tpl,5,PSMALLINT_NEW(ss));
        PTUPLE_SET_ITEM(tpl,6,PSMALLINT_NEW(tz));
        *res = tpl;
    }
    return err;
}


///////////////////////SMS
C_NATIVE(_ug96_sms_send){
    NATIVE_UNWARN();
    int32_t err = ERR_OK;
    int32_t numlen;
    int32_t txtlen;
    int32_t mr;
    uint8_t* num;
    uint8_t* txt;
    *res = MAKE_NONE();
    
    if(parse_py_args("ss",nargs,args,&num,&numlen,&txt,&txtlen)!=2) return ERR_TYPE_EXC;

    RELEASE_GIL();
    mr = _gs_sms_send(num,numlen,txt,txtlen);
    if (mr==-1) {
        *res = PSMALLINT_NEW(-1);
    } else if (mr<0) err = ug96exc;
    else *res = pinteger_new(mr);
    ACQUIRE_GIL();
    return err;
}

C_NATIVE(_ug96_sms_list){
    NATIVE_UNWARN();
    int32_t err = ERR_OK;
    int32_t unread;
    int32_t maxsms;
    int32_t offset;
    int32_t msgcnt;
    int i;
    *res = MAKE_NONE();
    
    if(parse_py_args("iii",nargs,args,&unread,&maxsms,&offset)!=3) return ERR_TYPE_EXC;

    GSSMS *sms = gc_malloc(sizeof(GSSMS)*maxsms);
    RELEASE_GIL();
    msgcnt = _gs_sms_list(unread,sms,maxsms,offset);
    ACQUIRE_GIL();

    PTuple *tpl = ptuple_new(msgcnt,NULL);
    for(i=0;i<msgcnt;i++){
        GSSMS *sm = &sms[i];
        PTuple *pres = ptuple_new(4,NULL);
        PTUPLE_SET_ITEM(pres,0,pstring_new(sm->txtlen,sm->txt));
        PTUPLE_SET_ITEM(pres,1,pstring_new(sm->oaddrlen,sm->oaddr));
        if (sm->tslen<22) {
            //bad ts
            PTUPLE_SET_ITEM(pres,2,ptuple_new(0,NULL));
        } else {
            PTuple *tm = ptuple_new(7,NULL);
            int nn=0;

            nn = (sm->ts[0]-'0')*1000+(sm->ts[1]-'0')*100+(sm->ts[2]-'0')*10+(sm->ts[3]-'0');
            PTUPLE_SET_ITEM(tm,0,PSMALLINT_NEW(nn));
            nn = (sm->ts[5]-'0')*10+(sm->ts[6]-'0');
            PTUPLE_SET_ITEM(tm,1,PSMALLINT_NEW(nn));
            nn = (sm->ts[8]-'0')*10+(sm->ts[9]-'0');
            PTUPLE_SET_ITEM(tm,2,PSMALLINT_NEW(nn));
            nn = (sm->ts[11]-'0')*10+(sm->ts[12]-'0');
            PTUPLE_SET_ITEM(tm,3,PSMALLINT_NEW(nn));
            nn = (sm->ts[14]-'0')*10+(sm->ts[15]-'0');
            PTUPLE_SET_ITEM(tm,4,PSMALLINT_NEW(nn));
            nn = (sm->ts[17]-'0')*10+(sm->ts[18]-'0');
            PTUPLE_SET_ITEM(tm,5,PSMALLINT_NEW(nn));
            nn = (sm->ts[20]-'0')*10+(sm->ts[21]-'0');
            PTUPLE_SET_ITEM(tm,6,PSMALLINT_NEW(nn*15));
            PTUPLE_SET_ITEM(pres,2,tm);
        }
    
        PTUPLE_SET_ITEM(pres,3,PSMALLINT_NEW(sm->index));

        PTUPLE_SET_ITEM(tpl,i,pres);
    }
    *res= tpl;
    gc_free(sms);
    return err;
}

C_NATIVE(_ug96_sms_pending){
    NATIVE_UNWARN();
    *res = PSMALLINT_NEW(gs.pendingsms);
    return ERR_OK;
}

C_NATIVE(_ug96_sms_delete){
    NATIVE_UNWARN();
    int32_t err = ERR_OK;
    int32_t index, rd;
    *res = PBOOL_TRUE();

    if(parse_py_args("i",nargs,args,&index)!=1) return ERR_TYPE_EXC;

    RELEASE_GIL();
    rd = _gs_sms_delete(index);
    if (rd<0) *res=PBOOL_FALSE();
    ACQUIRE_GIL();
    return err;

}

C_NATIVE(_ug96_sms_get_scsa){
    NATIVE_UNWARN();
    int32_t err = ERR_OK;
    int32_t scsalen;
    uint8_t scsa[32];


    RELEASE_GIL();
    scsalen = _gs_sms_get_scsa(scsa);
    if (scsalen<0) scsalen = 0;
    *res = pstring_new(scsalen,scsa);
    ACQUIRE_GIL();
    return err;
}

C_NATIVE(_ug96_sms_set_scsa){
    NATIVE_UNWARN();
    int32_t err = ERR_OK;
    int32_t scsalen,rd;
    uint8_t *scsa;
    *res = PBOOL_TRUE();

    if(parse_py_args("s",nargs,args,&scsa,&scsalen)!=1) return ERR_TYPE_EXC;

    RELEASE_GIL();
    rd = _gs_sms_set_scsa(scsa,scsalen);
    if (rd<0) *res = PBOOL_FALSE();
    ACQUIRE_GIL();
    return err;
}


