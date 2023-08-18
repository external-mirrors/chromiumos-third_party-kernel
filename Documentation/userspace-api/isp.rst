.. SPDX-License-Identifier: GPL-2.0
.. Copyright 2023 Google LLC

===========================
ISP Subsystem Userspace API
===========================

Introduction
============

The ISP subsystem controls image signal processing devices that are used to
process frames captured from a sensor. Some of the features the ISP subsystem
provides are:

 * obtaining a list of enumerated devices in the system.
 * registering a DMA-BUF
 * submitting a request

Please refer to Documentation/driver-api/isp/index.rst for details.

The ISP device
==============

The ISP subsystem exposes one device (``/dev/isp``) to the user space. All the
drivers under the ISP subsystem is accessed through this device.

A user application performs requests in the following manner. It first
opens the ISP common device to obtain a file descriptor. With this file
descriptor, it issues requests through ``ioctl()``. The requests can be
a set of queries or a set of operations. For operation requests that are
successfully accepted, the application obtains the result through
``read()``. Finally, when it is done with using the ISP, it closes the
file descriptor.

The ISP file descriptor is used as a unit of resource management. For
example, the reservation of a processing block is done per file
descriptor. A reserved processing block cannot be accessed from any
other file descriptors. Consequently, it is not recommended to share the
ISP file descriptor with a different process. The ISP subsystem does not
guarantee serialization of requests, so those processes have to
synchronize on their own. It could also lead to a security issue where
an image frame is mistakenly leaked to unintended recipients.

Representation of Specific Hardware
===================================

Each specific hardware on the system is modeled as one or more objects
by a ISP driver. An ISP object registered by a driver can be one of the
following. These objects have a unique ID within the entire ISP subsystem.

* entity - represents a point of operation (request processing)
* event - represents a point of synchronization

An entity object allows creation of at least one instance. An instance
acts as separation of contexts and is useful to model hardware with
identical processing blocks.

Representation of Dynamic Information
=====================================

The ISP subsystem also creates other types of objects. They are created
as a result of processing certain user requests and are managed under a
file descriptor. When the file descriptor is closed, these managed
objects will be released. The ID of these objects are given from the
user space and must be unique within a file desriptor's scope.

* operation - represents an in-flight operation
* buffer - represents a DMA-BUF
* fence - represents a DMA fence

Header File
===========
The data structures that are common to the ISP subsystem are defined in
``uapi/linux/isp.h``.

Request Commands
================

Requests to the ISP subsystem are posted by ``ioctl()`` whose command is
one of the follwoing:

* ``ISP_IOC_QUERY`` used to submit a set of queries
* ``ISP_IOC_OPERATION`` used to submit a set of operations

A query command can be used to figure out about the underlying hardware and
objects associated with a file descriptor. An operation command can be used to
actually post requests to the ISP subsystem.

The argument for these ``ioctl()`` consists of a comon header data
structure (struct isp_header) and a specific body data structure. The
body must be an array of either queries or operations that follows the
header without any gaps between them. A query and an operation cannot be
mixed in the same request.

``ISP_IOC_QUERY(size)``
-----------------------

A query command submits a set of queries to the ISP subsystem. The
argument (``size``) is the size of the entire data passed to the kernel,
including the size of the header. Each one of the queries in the body
targets one of the ISP object types. If a user wants to query about both
entity and event, the command should contain two queries: one query for
entity and one for event.

The types of a query and the corresponding output entry is summarized in
the table below.

+-------------+-----------------------------+----------------------------------+
| Object type | query type                  | output type                      |
+=============+=============================+==================================+
| entity      | struct isp_query_entities   | struct isp_query_entity_entry    |
+-------------+-----------------------------+----------------------------------+
| event       | struct isp_query_events     | struct isp_query_event_entry     |
+-------------+-----------------------------+----------------------------------+
| operation   | struct isp_query_operations | struct isp_query_operation_entry |
+-------------+-----------------------------+----------------------------------+
| buffer      | struct isp_query_dmabuf     | struct isp_query_dmabuf_entry    |
+-------------+-----------------------------+----------------------------------+

The result of a query command is stored in an output buffer attached to
the query. The output buffer must be allocated in the user space. If the
command contains more than one queries, the outputs are written in the
same order as the queries. Each output entry is of a type that is
specific to the matched object type. The number of result entries per
query is returned in each query data structure, so the user can use this
information to parse the output buffer from the top.

If the size of the buffer happens to be insufficient to hold all the
result, the only ones that fit are returned. The user also gets the size
of output buffer needed to store all the matching objects. This
information can be used to check whether the buffer was sufficient to
hold all the entries. It can also be used to allocate a buffer large
enough to get all the results in the next request. It is also possible
to not attach any buffer to a query command. In that case, the details
are not returned but the user can still get the size of an output buffer
required to store the matching object details.


``ISP_IOC_OPERATION(size)``
---------------------------

An operation command submits a set of operation requests to the ISP
subsystem. The argument (``size``) is the size of the entire data passed
to the kernel, including the size of the header. The body for this
command is an array of request structures (struct isp_operation). A
request is either an addtion or removal of an operation.

An operation can be added with one or more dependencies. An operation
with dependencies will not be eligible for execution until all its
dependencies are met. A dependency can be another operation, an event,
or a DMA fence imported from another subsystem. If the dependency is of
operation, the depending operation must be completed to meet the
dependency. It is also regarded as satisfied when the depending
operation object was not found. This can happen when a wrong ID was
specified or when the operation object is already deleted by that time.
If the dependency is of an event, the driver need to signal the event
with an appropriate signal. If the dependency is of a DMA fence, it must
be signaled. If a wrong ID was specified for an event or DMA fence
dependency, the operation request immediately returns with an error.

An operation can have a list of instructions, each of which represents
the actual work to be performed. Depending on its type (enum
isp_rw_instruction_type), an instruction is either handled by the ISP
common framework or by a specific driver. An instruction whose type is
``ISP_READ_INSTRUCTION`` or ``ISP_WRITE_INSTRUCTION`` is handled by a
driver, so it contains a driver defined data structure.

Data Structure References
=========================

.. kernel-doc:: include/uapi/linux/isp.h

