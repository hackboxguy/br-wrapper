#!/bin/sh
systemctl stop systemd-mpvdemo.service
/etc/init.d/S60DialsDemo stop
/etc/init.d/S61FingerpaintDemo stop
/etc/init.d/S62PinchzoomDemo stop
/etc/init.d/S63ScribbleDemo stop

