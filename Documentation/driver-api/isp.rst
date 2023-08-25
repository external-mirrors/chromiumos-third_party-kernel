.. SPDX-License-Identifier: GPL-2.0
.. Copyright 2023 Google LLC

=============
ISP Framework
=============

The ISP framework is intended to support development of kernel drivers
for image signal processors, typically used to process frames captured
from image sensors. Historically, the drivers for such image processors
used an existing framework that only fits partially or were either
entirely on their own. This has led to unnecessary variations of the ISP
stack. The ISP framework intends to improve this situation in two ways.

* It exposes a uniform interface to the user-space.
* It provides commonly required features to kernel drivers to ease their
  development.

The ISP framework provides a unique model for the interaction between
the kernel drivers and the user applications. The drivers that plug
themselves into the ISP framework register ISP objects. The user
application figures out the objects dynamically and posts requests to
the objects. Each driver can decide on the abstraction model. One may
decide to have one object that provides a highly abstracted view of the
hardware, or one may decide to have many objects that provide much lower
abstraction (like register level). This provides the flexibility
necessary to model diverse image signal processors.

The ISP requests come from the user-space in the form of a dependency
graph, each node being a request. The dependency can reach outside the
ISP subsystem by a DMA fence. The ISP framework checks the dependency
and executes operations as their dependencies are satisfied. The drivers
don't have to worry about the dependencies.

Device Representation
=====================

The ISP framework defines two types of objects that an ISP driver can
register to expose its device model to the user-space. The driver must
provide a name for these objects. The name must be considered as a part
of the user-space API and should not change for the same object.

Entity
------

An entity is an object associated with a set of callbacks. This
object acts as a point of operation like a classical device.

When a driver registers an entity object, it passes a structure that
defines a set of callbacks (struct isp_entity_ops). These callbacks must
be a valid function. Some of the callbacks are given a pointer to an
instance that can be used to determine the context for request. For
example, for hardware that has two identical processing blocks, its
driver can allow creation of two instances for the representing entity.
An application wanting to use the block will first obtain an instance
and then post some requests with its ID. The ISP framework will resolve
the ID into the instance and pass its pointer to the callback.

Event
-----

An event is an object that acts as a point of synchronization. A driver
typically sends a signal to this object on a hardware interrupt. This
can be used for synchronization of the user-space with the completion of
a request at the hardware level.


Object Lifetime Management
==========================

All the ISP objects have a reference counter. When the user-space
supplies an ID of an object, the ISP framework resolves it into an
object. The object's reference count is incremented before the pointer
is passed to the driver and is decremented after the execution returns
from the driver.

If the driver needs to ensure that the object is not released until a
certain point after the callback, it needs to increment the reference
count by using the ISP driver interface. For example, if a driver needs
to pass a DMA-BUF to the firmware in a callback and obtains the result
later, it needs to ensure that the DMA-BUF is not released until the
firmware is done with it. In that case, the driver should increment the
reference counter of the buffer object in the callback. Once the
firmware is done, the driver should decrement the reference count in an
interrupt handler.

Openness Policy
===============

The ISP framework follows the Linux standard openness policy. It puts
the following requirement to the kernel and the user-space stack.

* The kernel driver source code must be disclosed.
* There must be at least one open source implementation of user stack
  that can be used to provide the minimal functionalities to
  applications.

The ISP framework defines the minimal functionalities as follows. The
quality of the image must be good enough to be used in photo capturing
applications and/or video applications. What is not part of the
disclosure requirement is the disclosure of hardware interface
(registers).

Driver API Reference
====================
.. We are omitting some files intentionally, since they are not
.. necessary for driver development.

Buffers (linux/isp/isp-buffer.h)
--------------------------------
.. kernel-doc:: include/linux/isp/isp-buffer.h
    :internal:
.. kernel-doc:: drivers/isp/isp-buffer.c
    :export:

Device (linux/isp/isp-device.h)
-------------------------------
.. kernel-doc:: include/linux/isp/isp-device.h
    :internal:
.. kernel-doc:: drivers/isp/isp-device.c
    :export:

Entity / Event (linux/isp/isp-entity.h)
---------------------------------------
.. kernel-doc:: include/linux/isp/isp-entity.h
    :internal:
.. kernel-doc:: drivers/isp/isp-entity.c
    :export:

