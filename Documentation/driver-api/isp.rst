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

A driver can choose an execution mode for read or write instructions:
direct or deferred. In the direct mode, the instruction is completed
when the callback returns. The ISP framework immediately makes the
completion visible to the user-space. In the deferred mode, the
instruction is not completed yet on return. The ISP framework waits for
a signal from the driver. The framework only makes the completion
visible to the user-space when it has received the waiting signal.

Event
-----

An event is an object that acts as a point of synchronization. A driver
that supports deferred mode of execution must register an event object.
This event object is only internally used by the driver, but it is still
visible to the user-space. A driver can also register an event object
for explicit synchronization between the user-space and the
kernel/hardware. A typical use case is to schedule the execution of an
operation after a certain event. This can be achieved by the user-space
setting the dependency of an operation to the event object and the
driver signaling the event on a synchronization point.

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

The ISP subsystem has some requirements before a driver can be included.

It is expected that all ISP drivers can be used with an *open source* user-space
stack. That stack might have some binary blobs for the "Auto Algorithms", but
the input and the output of such blobs should be well documented; and the blobs
should not access the ISP by themselves.

The open source user-space should be ready when the driver is merged and should
be good enough to use the ISPs for video-conferencing or still imaging.
User-spaces based on libcamera are preferred than other custom implementations.

This open source stack is the only way that users have a warranty that their
hardware will work after the vendors have stopped their support.

Vendors can also provide proprietary user-spaces, using the same uAPI exposed
by the ISP subsystem. Vendors can have exclusive features in these user-spaces,
allowing them to compete in consumer distros like Android.

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

