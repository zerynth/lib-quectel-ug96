"""
.. module:: ug96

***********
UG96 Module
***********

This module implements the Zerynth driver for the Quectel UG95/UG96 UMTS/HSPA modem (`Product page <https://www.quectel.com/product/2g3g.htm>`_).

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
Moreover, UDP sockets must be connected or bound explicitly in the code to select which kind of function to perform (send vs sendto and recv vs recvfrom).

The communication with UG96 is performed via UART without hardware flow control at 115200 baud.

This module provides the :samp:`ug96Exception` to signal errors related to the hardware initialization and management.

   """
import gpio

new_exception(ug96Exception, Exception)
_reset_pin=None
_reset_on=None
_power_pin=None
_power_on=None
_status_pin=None
_status_on=None

def init(serial,dtr,rts,power,kill,status,power_on=LOW,kill_on=LOW,status_on=HIGH):
    """
.. function:: init(serial,dtr,rts,power,kill,status,power_on=LOW,kill_on=LOW,status_on=HIGH)

    Initialize the UG96 device given the following parameters:

    * *serial*, the serial port connected to the UG96 (:samp:`SERIAL1`, :samp:`SERIAL2`, etc...)
    * *dtr*, the DTR pin of UG96 (not used yet)
    * *rts*, the RTS pin of UG96 (not used yet)
    * *power*, the power up pin of UG96
    * *kill*, the emergency off (kill) pin of UG96
    * *status*, the status pin of UG96
    * *power_on*, the active level of the power up pin
    * *kill_on*, the active level of the kill pin
    * *status_on*, the value of status pin indicating successful power on (can be zero in some pcb designs)

    """
    global _kill_pin, _kill_on, _power_pin, _power_on, _status_pin, _status_on
    _kill_pin=kill
    _kill_on=kill_on
    _power_pin=power
    _power_on=power_on
    _status_pin=status
    _status_on=status_on

    # print("Setting Pins...");
    gpio.mode(_status_pin, INPUT_PULLDOWN if _status_on else INPUT_PULLUP)
    gpio.mode(_kill_pin, OUTPUT_PUSHPULL)
    gpio.set(_kill_pin, HIGH^ _kill_on)
    gpio.mode(_power_pin, OUTPUT_PUSHPULL)
    gpio.set(_power_pin, HIGH^ _power_on)

    _init(serial,dtr,rts,__nameof(ug96Exception))
    __builtins__.__default_net["gsm"] = __module__
    __builtins__.__default_net["ssl"] = __module__
    __builtins__.__default_net["sock"][0] = __module__ #AF_INET

    shutdown(True)

@c_native("_ug96_init",[ 
        "csrc/ug96.c",
        "csrc/ug96_ifc.c",
        "#csrc/misc/zstdlib.c",
        "#csrc/misc/snprintf.c",
        "#csrc/zsockets/*",
        "#csrc/hwcrypto/*",
        #-if ZERYNTH_SSL
        ##-if !HAS_BUILTIN_MBEDTLS
        "#csrc/tls/mbedtls/library/*",
        ##-endif
        #-endif
    ],
    [
        "VHAL_WIFI"
    ],
    [
        "-I#csrc/zsockets",
        "-I#csrc/misc",
        "-I#csrc/hwcrypto",
        #-if ZERYNTH_SSL
        ##-if !HAS_BUILTIN_MBEDTLS
        "-I#csrc/tls/mbedtls/include"
        ##-endif
        #-endif
    ])
def _init(serial,dtr,rst,exc):
    pass

@c_native("_ug96_shutdown",[])
def _shutdown():
    pass

@c_native("_ug96_startup",[])
def _startup():
    pass

@c_native("_ug96_bypass",[])
def bypass(mode):
    """
.. function:: bypass(mode)

    Bypass the modem driver to use the serial port directly. It has one parameter:

    * *mode*, can be *1* (non-zero) to enter bypass mode, or *0* (zero) to exit.
    
    """
    pass

def shutdown(forced=False):
    """
.. function:: shutdown(forced=False)

    Power off the module by sending AT command (clean power-down).

    If *forced* is given, use emergency off (faster, do not detach from network).
    """
    timeout = 125
    if _shutdown():
        # normal shutdown attempted
        for i in range(timeout):
            if gpio.get(_status_pin)!=_status_on:
                # print("!STA")
                break
            # print("STA!")
            sleep(100)
    # print("Powering off...")
    if gpio.get(_status_pin)==_status_on and forced:
        gpio.set(_kill_pin, HIGH^ _kill_on)
        sleep(500)
        gpio.set(_kill_pin, _kill_on)
        sleep(200)
        gpio.set(_kill_pin, HIGH^ _kill_on)
        timeout = 15 # shorter

    for i in range(timeout):
        if gpio.get(_status_pin)!=_status_on:
            # print("!STA")
            break
        # print("STA!")
        sleep(100)
    else:
        raise HardwareInitializationError
    sleep(500)

def startup():
    """
.. function:: startup()

    Power on the module by pulsing the power pin. 
    """
    # print("Powering on...")
    if gpio.get(_status_pin)!=_status_on:
        gpio.set(_power_pin, HIGH^ _power_on)
        sleep(500)
        gpio.set(_power_pin, _power_on)
        sleep(200)
        gpio.set(_power_pin, HIGH^ _power_on)

    for i in range(50):
        if gpio.get(_status_pin)==_status_on:
            # print("STA!")
            break
        # print("!STA")
        sleep(100)
    else:
        raise HardwareInitializationError
    sleep(1500)
    _startup()

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

@native_c("py_net_bind",[])
def bind(sock,addr):
    pass

def listen(sock,maxlog=2):
    raise UnsupportedError

def accept(sock):
    raise UnsupportedError


@native_c("_ug96_resolve",[])
def gethostbyname(hostname):
    pass


@native_c("py_net_socket",[])
def socket(family,type,proto):
    pass

def setsockopt(sock,level,optname,value):
    pass

@native_c("py_net_connect",[])
def connect(sock,addr):
    pass

@native_c("py_net_close",[])
def close(sock):
    pass


@native_c("py_net_sendto",[])
def sendto(sock,buf,addr,flags=0):
    pass

@native_c("py_net_send",[])
def send(sock,buf,flags=0):
    pass

def sendall(sock,buf,flags=0):
    send(sock,buf,flags)

@native_c("py_net_recv_into",[])
def recv_into(sock,buf,bufsize,flags=0,ofs=0):
    pass

@native_c("py_net_recvfrom_into",[])
def recvfrom_into(sock,buf,bufsize,flags=0,ofs=0):
    pass

@native_c("py_secure_socket",[],[])
def secure_socket(family, type, proto, ctx):
    pass

@native_c("py_net_select",[])
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

