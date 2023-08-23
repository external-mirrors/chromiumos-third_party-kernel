.. SPDX-License-Identifier: GPL-2.0
.. Copyright 2023 Google LLC

=============
ISP Framework
=============

The ISP framework is intended to support development of Linux kernel
drivers for image signal processors, typically used to process frames
captured from image sensors. Historically, the drivers for such image
processors used an existing framework that only fits partially or were
either entirely on their own. This led to unnecessary variations of the
ISP stack.

The ISP framework intend to improve this situation. First, it exposes a
uniform interface to the user space. Second, it provides commonly
required features to kernel drivers to ease their development.

The ISP framework follows the Linux standard openness policy. It puts
the following requirement to the kernel and the user space stack.

* The kernel driver source code must be disclosed.
* There must be at least one open source implementation of user stack
  that can be used to provide the minimal functionalities to
  applications.

The ISP framework defines the minimal functionalities as follows. The
quality of the image must be good enough to be used in photo capturing
applications and/or video applications. What is not part of the
disclosure requirement is the disclosure of hardware interface
(registers).

Device Exposure Model
=====================

The ISP framework defines two types of objects that an ISP driver
registers to expose its device model to the user space.

Entity
    An entity is an object associated with a set of callbacks. This
    object acts as a point of operation like a classical device.

Event
    An event is an object that acts as a point of synchronization. A
    driver typically sends a signal on a hardware interrupt. This is
    typically used for synchronization of the user space with the
    completion of a request at the hardware level.


Entity Callbacks
================

When a driver registers an entity object, it passes a structure that
defines a set of callbacks (struct isp_entity_ops). These callbacks
must be a valid function.


Driver API Reference
====================
.. We are omitting some files intentionally, since they are not
.. necessary for driver development.

Buffers (isp/isp-buffer.h)
--------------------------
.. kernel-doc:: include/linux/isp/isp-buffer.h
    :internal:
.. kernel-doc:: drivers/isp/isp-buffer.c
    :export:

Device (isp/isp-device.h)
-------------------------
.. kernel-doc:: include/linux/isp/isp-device.h
    :internal:
.. kernel-doc:: drivers/isp/isp-device.c
    :export:

Entity (isp/isp-entity.h)
-------------------------
.. kernel-doc:: include/linux/isp/isp-entity.h
    :internal:
.. kernel-doc:: drivers/isp/isp-entity.c
    :export:

Namespace (isp/isp-namespace.h)
-------------------------------
.. kernel-doc:: include/linux/isp/isp-namespace.h
    :internal:
.. kernel-doc:: drivers/isp/isp-namespace.c
    :export:

