"""
.. module:: ug96

***********
UG96 Module
***********

This module implements the Zerynth driver for the Quectel UG95/UG96 UMTS/HSPA chip (`Product page <https://www.quectel.com/product/2g3g.htm>`_).

The driver must be used together with the standard library :ref:`GSM Module <stdlib_gsm>`.

The following functionalities are implemented:

    * attach/detach from gprs network
    * retrieve and set available operators
    * retrieve signal strength
    * retrieve network and device info
    * socket abstraction
    * secure sockets
    * RTC clock
    * SM
    * SMS

Listening sockets for TCP and UDP protocols are not implemented due to the nature of GSM networks. 
Moreover, UDP sockets must be connected or bind explicitly in the code to select which kind of function to perform (send vs sendto and recv vs recvfrom).

The communication with UG96 is performed via UART without hardware flow control at 115200 baud.

This module provides the :samp:`ug96Exception` to signal errors related to the hardware initialization and management.

   """
import streams


new_exception(ug96Exception, Exception)
_kill_pin=None

def init(serial,dtr,rts,poweron,reset,status,kill,status_on=1,pe=None):
    """
.. function:: init(serial,dtr,rts,poweron,reset,status,kill, status_on=1, pe=None)

    Initialize the UG96 device given the following parameters:

    * *serial*, the serial port connected to the UG96 (:samp;`SERIAL1`,:samp:`SERIAL2`, etc..)
    * *dtr*, the DTR pin of UG96 (not used yet)
    * *rts*, the RTS pin of UG96 (not used yet)
    * *poweron*, the power up pin of UG96
    * *reset*, the reset pin of UG96
    * *status*, the status pin of UG96
    * *kill*, the kill pin of UG96
    * *status_on*, the value of status pin indicating successful poweron (can be zero in some pcb designs)
    * *pe*, a port expander implementation

    If *pe* is not None, the UG96 initialization is performed using *pe* to control the pins.
    *pe* should be an instance of the :ref:`GPIO module <stdlib_gpio>`.

    """
    global _kill_pin
    _kill_pin=kill
    _init(serial,dtr,rts,poweron,reset,status,kill,status_on,__nameof(ug96Exception))
    if pe:
        _startup_py(serial,dtr,rts,poweron,reset,status,kill,status_on,pe)
        _startup(1)
    else:
        _startup(0)
    __builtins__.__default_net["gsm"] = __module__
    __builtins__.__default_net["ssl"] = __module__
    __builtins__.__default_net["sock"][0] = __module__ #AF_INET

@c_native("_ug96_init",[
    "csrc/ug96.c",
    "csrc/ug96_ifc.c",
    "#csrc/misc/zstdlib.c",
    "#csrc/zsockets/*",
#-if ZERYNTH_SSL
    "#csrc/tls/mbedtls/library/*",
    "#csrc/misc/snprintf.c",
#-endif
    ],
    ["ZERYNTH_SOCKETS"],
    [
#-if ZERYNTH_SSL
    "-I#csrc/tls/mbedtls/include",
    "-I#csrc/misc",
#-endif
    "-I#csrc/zsockets"
    ])
def _init(serial,dtr,rst,poweron,reset,status,kill,status_on,exc):
    pass

@c_native("_ug96_shutdown",["csrc/ug96.c"])
def _shutdown(skip_poweroff):
    pass

def shutdown(pe=None):
    """
.. function:: shutdown(pe=None)

    Power of the module by toggling the kill pin. If *pe* is given, a port expander is used to switch the pin.
    """
    if pe:
        _shutdown(1)
        pe.set(_kill_pin,1)
        sleep(5000)
        pe.set(_kill_pin,0)
    else:
        _shutdown(0)

@c_native("_ug96_startup",["csrc/ug96.c"])
def _startup(skip_poweron):
    pass

def _startup_py(serial,dtr,rts,poweron,reset,status,kill,status_on,pe):
    # print("Setting Pins...");
    pe.mode(status,INPUT);
    pe.mode(kill,OUTPUT_PUSHPULL);
    pe.mode(poweron,OUTPUT_PUSHPULL);
    pe.mode(reset,OUTPUT_PUSHPULL);

    # print("Powering off...");
    pe.set(kill,1)
    for i in range(50):
        if pe.get(status)!=status_on:
            pe.set(kill,0)
            break
        sleep(100)
    else:
        pe.set(kill,0)
        raise HardwareInitializationError

    # print("Powering on...")
    pe.set(poweron,1)
    sleep(1000)
    pe.set(poweron, 0)
    sleep(1000)
    pe.set(poweron, 1)
    for i in range(50):
        if pe.get(status)==status_on:
            # print("STA!")
            break
        else:
            # print("!STA")
            sleep(100)
    else:
        raise HardwareInitializationError



@c_native("_ug96_attach",[])
def attach(apn,username,password,authmode,timeout):
    pass

@c_native("_ug96_detach",[])
def detach():
    pass

@c_native("_ug96_network_info",[])
def network_info():
    pass

@c_native("_ug96_mobile_info",[])
def mobile_info():
    pass

@c_native("_ug96_link_info",[])
def link_info():
    pass

@c_native("_ug96_operators",[])
def operators():
    pass

@c_native("_ug96_set_operator",[])
def set_operator(opname):
    pass

@native_c("_ug96_socket_bind",[])
def bind(sock,addr):
    pass

def listen(sock,maxlog=2):
    raise UnsupportedError

def accept(sock):
    raise UnsupportedError


@native_c("_ug96_resolve",[])
def gethostbyname(hostname):
    pass


@native_c("_ug96_socket_create",[])
def socket(family,type,proto):
    pass

def setsockopt(sock,level,optname,value):
    pass

@native_c("_ug96_socket_connect",[])
def connect(sock,addr):
    pass

@native_c("_ug96_socket_close",[])
def close(sock):
    pass


@native_c("_ug96_socket_sendto",[])
def sendto(sock,buf,addr,flags=0):
    pass

@native_c("_ug96_socket_send",[])
def send(sock,buf,flags=0):
    pass

def sendall(sock,buf,flags=0):
    send(sock,buf,flags)

@native_c("_ug96_socket_recv_into",[])
def recv_into(sock,buf,bufsize,flags=0,ofs=0):
    pass

@native_c("_ug96_socket_recvfrom_into",[])
def recvfrom_into(sock,buf,bufsize,flags=0):
    pass

@native_c("_ug96_secure_socket",[],[])
def secure_socket(family, type, proto, ctx):
    pass

@native_c("_ug96_socket_select",[])
def select(rlist,wist,xlist,timeout):
    pass

@native_c("_ug96_rtc",[])
def rtc():
    """
------------
Network Time
------------

The UG96 has an internal Real Time Clock that is automatically synchronized with the Network Time.
The current time can be retrieved with the following function:

.. function:: rtc()
    
    Return a tuple of seven elements:

        * current year
        * current month (1-12)
        * current day (1-31)
        * current hour (0-23)
        * current minute (0-59)
        * current second (0-59)
        * current timezone in minutes away from GMT 0

    The returned time is always UTC time with a timezone indication.

    """
    pass

@c_native("_ug96_rssi",[])
def rssi():
    pass

@c_native("_ug96_sms_send",[])
def send_sms(num,txt):
    pass

@c_native("_ug96_sms_delete",[])
def delete_sms(index):
    pass

@c_native("_ug96_sms_list",[])
def list_sms(unread,maxsms,offset):
    pass

@c_native("_ug96_sms_pending",[])
def pending_sms():
    pass

@c_native("_ug96_sms_get_scsa",[])
def get_smsc():
    pass

@c_native("_ug96_sms_set_scsa",[])
def set_smsc(scsa):
    pass

