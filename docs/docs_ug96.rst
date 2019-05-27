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

    
.. function:: shutdown(pe=None)

    Power of the module by toggling the kill pin. If *pe* is given, a port expander is used to switch the pin.
    
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

    
