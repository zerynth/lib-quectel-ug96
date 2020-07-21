#include "zerynth.h"
#include "zerynth_sockets.h"
#include "ug96.h"

//a reference to a Python exception to be returned on error (ug96Exception)
int32_t ug96exc;

///////// CNATIVES
// The following functions are callable from Python.
// Functions starting with "_" are utility functions called by CNatives


SocketAPIPointers ug96_api;
/**
 * @brief Init modem driver, calls _gs_init, gzsock_init
 *
 * As last parameter, requires an integer saved to global ug96exc, representing the name assigned to ug96Exception
 * so that it can be raised by returning ug96exc. If modules initialization is successful, the next step is calling startup
 *
 */
C_NATIVE(_ug96_init){
    NATIVE_UNWARN();
    int32_t serial;
    int32_t rts;
    int32_t dtr;
    int32_t err = ERR_OK;
    int32_t exc;


    if (parse_py_args("iiii", nargs, args, &serial, &dtr, &rts, &exc) != 4)
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

    ACQUIRE_GIL();
    ug96_api.socket = ug96_gzsock_socket;
    ug96_api.connect = ug96_gzsock_connect;
    ug96_api.setsockopt = ug96_gzsock_setsockopt;
    ug96_api.getsockopt = ug96_gzsock_getsockopt;
    ug96_api.send = ug96_gzsock_send;
    ug96_api.sendto = ug96_gzsock_sendto;
    ug96_api.write = ug96_gzsock_write;
    ug96_api.recv = ug96_gzsock_recv;
    ug96_api.recvfrom = ug96_gzsock_recvfrom;
    ug96_api.read = ug96_gzsock_read;
    ug96_api.close = ug96_gzsock_close;
    ug96_api.shutdown = ug96_gzsock_shutdown;
    ug96_api.bind = ug96_gzsock_bind;
    ug96_api.accept = ug96_gzsock_accept;
    ug96_api.listen = ug96_gzsock_listen;
    ug96_api.select = ug96_gzsock_select;
    ug96_api.fcntl = ug96_gzsock_fcntl;
    ug96_api.ioctl = ug96_gzsock_ioctl;
    ug96_api.getaddrinfo = ug96_gzsock_getaddrinfo;
    ug96_api.freeaddrinfo = ug96_gzsock_freeaddrinfo;
    ug96_api.inet_addr = ug96_gzsock_inet_addr;
    ug96_api.inet_ntoa = ug96_gzsock_inet_ntoa;

    printf("After init\n");
    gzsock_init(&ug96_api);
    printf("After gzsock_init\n");
    return err;
}

/**
 * @brief Setup modem serial port, AT configuration and start modem thread
 *
 */
C_NATIVE(_ug96_startup){
    NATIVE_UNWARN();
    int32_t err = ERR_OK;
    *res = MAKE_NONE();
    
    RELEASE_GIL();
    vosSemWait(gs.slotlock);

    if (_gs_stop() != 0)
        err = ERR_HARDWARE_INITIALIZATION_ERROR;
    else
    if (vhalSerialInit(gs.serial, 115200, SERIAL_CFG(SERIAL_PARITY_NONE,SERIAL_STOP_ONE, SERIAL_BITS_8,0,0), gs.rx, gs.tx) != 0)
        err = ERR_HARDWARE_INITIALIZATION_ERROR;
    else
    if (!_gs_config0())
        err = ERR_HARDWARE_INITIALIZATION_ERROR;
    else {
        if (gs.thread==NULL){
            //let's start modem thread (if not already started)
            printf("Starting modem thread with size %i\n",VM_DEFAULT_THREAD_SIZE);
            gs.thread = vosThCreate(VM_DEFAULT_THREAD_SIZE,VOS_PRIO_NORMAL,_gs_loop,NULL,NULL);
            vosThResume(gs.thread);
            vosThSleep(TIME_U(1000,MILLIS)); // let modem thread have a chance to start
        }
    }
    // reset driver status (assuming modem has restarted)
    gs.attached = 0;
    gs.registered = 0;
    gs.gsm_status = 0;
    gs.gprs_status = 0;
    gs.registration_status_time = (uint32_t)(vosMillis() / 1000);

    // start loop and wait
    if (_gs_start() != 0)
        err = ERR_HARDWARE_INITIALIZATION_ERROR;

    vosSemSignal(gs.slotlock);
    ACQUIRE_GIL();
    return err;
}

/**
 * @brief Stop modem thread and close serial port
 *
 * Optionally power-down modem (since hw method not available)
 */
C_NATIVE(_ug96_shutdown){
    NATIVE_UNWARN();
    int32_t use_atcmd;
    int32_t err = ERR_OK;
    *res = MAKE_NONE();
    
    RELEASE_GIL();
    vosSemWait(gs.slotlock);

    if (_gs_stop() != 0)
        err = ERR_HARDWARE_INITIALIZATION_ERROR;

    // attempt normal shutdown
    vhalSerialInit(gs.serial, 115200, SERIAL_CFG(SERIAL_PARITY_NONE,SERIAL_STOP_ONE, SERIAL_BITS_8,0,0), gs.rx, gs.tx);
    // check alive
    vhalSerialWrite(gs.serial, "ATE0\r\n", 6);
    if (_gs_wait_for_ok(500)) {
        //enter minimal functionality
        vhalSerialWrite(gs.serial, "AT+CFUN=0\r\n", 11);
        _gs_wait_for_ok(15000);
        //power down
        vhalSerialWrite(gs.serial, "AT+QPOWD\r\n", 10);
        *res = PSMALLINT_NEW(1);
    }
    vhalSerialDone(gs.serial);

    vosSemSignal(gs.slotlock);
    ACQUIRE_GIL();
    return err;
}

/**
 * @brief Stop/restart modem thread
 *
 * Give direct access to modem serial port
 */
C_NATIVE(_ug96_bypass){
    NATIVE_UNWARN();
    int32_t mode;
    int32_t err=ERR_OK;

    if(parse_py_args("i",nargs,args,&mode)!=1) return ERR_TYPE_EXC;

    *res = MAKE_NONE();
    if (mode) {
        vosSemWait(gs.slotlock);
        if (_gs_stop() != 0)
            err = ERR_HARDWARE_INITIALIZATION_ERROR;
    }
    else {
        if (_gs_start() != 0)
            err = ERR_HARDWARE_INITIALIZATION_ERROR;
        vosSemSignal(gs.slotlock);
    }
    return err;
}


/**
 * @brief _ug96_detach removes the link with the APN while keeping connected to the GSM network
 *
 *
 */
C_NATIVE(_ug96_detach){
    NATIVE_UNWARN();
    int err = ERR_OK;
    *res = MAKE_NONE();
    RELEASE_GIL();

    if(!_gs_control_psd(0))
        err = ug96exc;

    ACQUIRE_GIL();
    return err;
}


/**
 * @brief _ug96_attach tries to link to the given APN
 *
 * This function can block for a very long time (up to 2 minutes) due to long timeout of used AT commands
 *
 *
 */
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
    int32_t err=ERR_OK;

    if(parse_py_args("sssii",nargs,args,&apn,&apn_len,&user,&user_len,&password,&password_len,&authmode,&timeout)!=5) return ERR_TYPE_EXC;

    *res = MAKE_NONE();
    RELEASE_GIL();

    //Wait for registration
    _gs_check_network();
    while(timeout>0){
        if (gs.registered>=GS_REG_OK) break;
        vosThSleep(TIME_U(1000,MILLIS));
        timeout-=1000;
        _gs_check_network();
    }
    if (gs.registered<GS_REG_OK) {
        err = ERR_TIMEOUT_EXC;
        goto exit;
    }
    //configure PSD
    err = ug96exc;
    if(!_gs_configure_psd(apn,apn_len,user,user_len,password,password_len,authmode)) goto exit;

    //activate PSD
    if(!_gs_control_psd(1)) goto exit;

    err = ERR_OK;

    exit:
    ACQUIRE_GIL();
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

    ACQUIRE_GIL();
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
 * @brief _ug96_rssi return the signal strength as reported by +CIEV urc
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
 * @brief strings for network types (must have the same order as bits in GS_RAT_xxx)
 */
const uint8_t * const _urats[] = { "", "GSM","GPRS","UMTS" };
#define MAX_URATS (sizeof(_urats)/sizeof(*_urats))

/**
 * @brief _ug96_network_info retrieves network information through +URAT and *CGED
 *
 *
 */
C_NATIVE(_ug96_network_info){
    NATIVE_UNWARN();
    int p0,l0,mcc,mnc;
    PString *str;
    PTuple *tpl = ptuple_new(8,NULL);
    uint8_t rats[40];
    int ratslen;

    //RAT  : URAT
    //CELL : UCELLINFO
    RELEASE_GIL();
    _gs_check_network();
    _gs_is_attached();
    if (_gs_cell_info(&mcc, &mnc) <= 0) {
        mcc = -1;
        mnc = -1;
    }
    
    // build RAT (radio access technology) string from static buffer
    // e.g. "GSM+LTE Cat M1"
    ratslen = 0;
    for (p0=1,l0=1; p0<MAX_URATS; ++p0,l0<<=1) {
        if (gs.tech & l0) {
            int len = strlen(_urats[p0]);
            if (ratslen + len + 1 > sizeof(rats)) break; // for safety
            if (ratslen > 0)
                rats[ratslen++] = '+';
            memcpy(rats+ratslen, _urats[p0], len);
            ratslen += len;
        }
    }
    str = pstring_new(ratslen,rats);
    PTUPLE_SET_ITEM(tpl,0,str);

    PTUPLE_SET_ITEM(tpl,1,PSMALLINT_NEW(mcc));
    PTUPLE_SET_ITEM(tpl,2,PSMALLINT_NEW(mnc));
    str = pstring_new(0,NULL);
    PTUPLE_SET_ITEM(tpl,3,str);

    str = pstring_new(strlen(gs.lac),gs.lac);
    PTUPLE_SET_ITEM(tpl,4,str);
    str = pstring_new(strlen(gs.ci),gs.ci);
    PTUPLE_SET_ITEM(tpl,5,str);

    //registered to network
    PTUPLE_SET_ITEM(tpl,6,(gs.registered==GS_REG_OK || gs.registered==GS_REG_ROAMING) ? PBOOL_TRUE():PBOOL_FALSE());
    //attached to APN
    PTUPLE_SET_ITEM(tpl,7,gs.attached ? PBOOL_TRUE():PBOOL_FALSE());

    ACQUIRE_GIL();
    *res = tpl;
    return ERR_OK;
}

/**
 * @brief _ug96_mobile_info retrieves info on IMEI and SIM card by means of +CGSN and *CCID
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
 * @brief _ug96_link_info retrieves ip and dns by means of +UPSND
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

    PTuple *tpl = ptuple_new(2,NULL);
    PTUPLE_SET_ITEM(tpl,0,ips);
    PTUPLE_SET_ITEM(tpl,1,dns);

    ACQUIRE_GIL();
    *res = tpl;
    return ERR_OK;
}

// /////////////////////DNS

C_NATIVE(_ug96_resolve){
    C_NATIVE_UNWARN();
    uint8_t* url;
    uint32_t len;
    uint8_t saddr[16];
    uint32_t saddrlen;
    int ret;
    if (parse_py_args("s", nargs, args, &url, &len) != 1)
        return ERR_TYPE_EXC;

    // if arg is already numeric IP address, return it!
    struct sockaddr_in addr;
    ret = zs_string_to_addr(url,len,&addr);
    if (ret == ERR_OK) {
        *res = args[0];
        return ERR_OK;
    }
    // otherwise resolve IP address
    struct addrinfo *ip;
    uint8_t *node = gc_malloc(len+1);
    memcpy(node,url,len);
    node[len] = 0; //get a zero terminated string
    RELEASE_GIL();
    ret = zsock_getaddrinfo(node,NULL,NULL,&ip);
    gc_free(node);
    ACQUIRE_GIL();
    if (ret==ERR_OK) {
        saddrlen=zs_addr_to_string(ip->ai_addr,saddr);
        zsock_freeaddrinfo(ip);
        if(saddrlen>0) {
            *res = pstring_new(saddrlen,saddr);
            return ERR_OK;
        } 
    }
    return ERR_IOERROR_EXC;
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
    ACQUIRE_GIL();

    if (mr == -1)
        *res = PSMALLINT_NEW(-1);
    else if (mr < 0)
        err = ug96exc;
    else
        *res = pinteger_new(mr);
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
    ACQUIRE_GIL();

    if (rd<0) *res=PBOOL_FALSE();
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
    ACQUIRE_GIL();

    if (rd<0) *res = PBOOL_FALSE();
    return err;
}


