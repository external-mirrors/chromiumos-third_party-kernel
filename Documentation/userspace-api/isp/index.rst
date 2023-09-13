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

 * obtaining a list of enumerated devices in the system
 * registering a DMA-BUF
 * submitting a processing request

The ISP device
==============

The ISP subsystem exposes one device (``/dev/isp``) to the user-space.
All the drivers under the ISP subsystem is accessed through this device.

A user application interacts with the ISP device in the following
manner. It first opens the ISP device to obtain a file descriptor. With
this file descriptor, it issues requests through ``ioctl()``. The
requests can be a set of queries or a set of operations. For operation
requests that are successfully accepted, the application obtains the
result through ``read()``. Finally, when it is done with using the ISP,
it closes the file descriptor.

The ISP file descriptor is used as a unit of resource management. Any
resource allocated for a file descriptor cannot be accessed from other
file descriptors. For example, the reservation of a processing block is
done per file descriptor and a reserved block cannot be access from
other file descriptors. Another example of a resource is a DMA-BUF
imported to the ISP subsystem. It cannot be accessed from other file
descriptor, unless the same DMA-BUF is explicitly imported again for
that file descriptor. It is not recommended to share the ISP file
descriptor with a different process. The ISP subsystem does not
guarantee serialization of requests, so those processes have to
synchronize on their own. It could also lead to a security/privacy issue
where an image frame is mistakenly leaked to unintended recipients.

Representation of Specific Hardware
===================================

Each specific hardware on the system is modeled as one or more ISP
objects by a driver. These objects have a unique ID and a unique name
within the entire ISP subsystem. The ID is dynamically assigned by the
ISP subsystem and is what a user application use to interact with these
objects. On the other hand, the name stays the same for these types of
objects and is considered as part of the user-space API. An application
can use it to identify the underlying hardware and driver.

Entity
------

An entity is typically a physical or logical representation of a
hardware block. Each entity typically defines its own set of commands
that it can accept in addition to the common ISP commands.

Event
-----

An event object is used to synchronize the user application with a
status change (event) at the hardware or driver level. For example, a
driver may register an event object that will be signaled when it
receives an interrupt from hardware.

Representation of Dynamic Information
=====================================

The ISP subsystem also creates other types of objects. They are created
as a result of processing a user request. When the file descriptor is
closed, these objects will be released. These objects will have an ID
passed from the user-space, except for the fence objects described
below. The fences are identified by an file descriptor, because they
have to work with external subsystems.

Instance
--------

An instance object is created dynamically for an entity. It serves as
the execution context for a request posted to the entity. An instance is
useful when a device has more than one identical processing blocks. For
example, for a hardware block with four identical processing
capabilities, an entity representing that block may allow creation of up
to four instances.

Operation
---------

An operation is a unit of request. A user application can submit more
than one operations together, possibly with dependencies.

An operation can have dependency to another operation, an event, or a
DMA fence. It only becomes eligible when its dependency is satisfied.
For example, if an operation depends on another operation, it will only
be executed only after its dependency operation has finished. If it has
more than one dependencies, it will only be executed after all the
dependencies are satisfied.

Buffer
------

A buffer object is created when a DMA-BUF is imported by an instruction.
It is only accessible with the file descriptor used to import the
DMA-BUF, for the sake of privacy.

Fence
-----

A fence object is created either when a DMA fence is imported or a DMA
fence is exported. A fence is implicitly imported when its file
descriptor is given as the dependency of an operation. On the other
hand, a fence is only exported explicitly by an instruction. The
self-importing of an exported fence is also allowed.

Header File
===========
The data structures that are common to the ISP subsystem are defined in
``uapi/linux/isp.h``.

Request Commands
================

Requests to the ISP subsystem are posted by ``ioctl()`` whose command is
one of the follwoing:

* ``ISP_IOC_QUERY`` -- used to submit a set of queries
* ``ISP_IOC_OPERATION`` -- used to submit a set of operations

A query command can be used to figure out about the underlying hardware and
objects associated with a file descriptor. An operation command can be used to
actually post requests to the ISP subsystem.

The argument for these ``ioctl()`` consists of a comon header data
structure (struct isp_header) and a specific body data structure. The
body must be an array of either queries or operations that follows the
header immediately. A query and an operation cannot be mixed in the same
request.

``ISP_IOC_QUERY(size)``
-----------------------

A query command submits a set of queries to the ISP subsystem. The
argument (``size``) is the size of the entire data passed to the kernel,
including the size of the header. The body of this command is an array
of query structures (struct isp_query). Each one of the queries targets
one of the ISP object types. If a user wants to query about both entity
and event, the command should contain two queries: one query for entity
and one for event.

The general use cases of query are as follows.

* For objects that are registered by an ISP driver, a query is useful in
  figuring out the underlying device by obtaining the names and the
  hierarchical relationship between objects. A query result also
  includes an ID that can be specified for operation commands.
* For objects that are created as a result of processing user requests,
  a query can be used to obtain their dynamic attributes (e.g. state).

The result of a query comes in a type that depends on the target object.
The types of data structure used for a query and the corresponding
output entry is summarized in the table below.

+--------------+-----------------------------+----------------------------------+
| query target | query type                  | output type                      |
+==============+=============================+==================================+
| entity       | struct isp_query_entities   | struct isp_query_entity_entry    |
+--------------+-----------------------------+----------------------------------+
| event        | struct isp_query_events     | struct isp_query_event_entry     |
+--------------+-----------------------------+----------------------------------+
| operation    | struct isp_query_operations | struct isp_query_operation_entry |
+--------------+-----------------------------+----------------------------------+
| buffer       | struct isp_query_dmabuf     | struct isp_query_dmabuf_entry    |
+--------------+-----------------------------+----------------------------------+

The result of a query command is stored in an output buffer attached to
the query. The output buffer should be allocated in the user-space. If
the command contains more than one query, the outputs are written in the
same order as the queries. The number of result entries per query is
returned in each query data structure, so the user can use this
information to parse the output buffer from the top.

If the size of the buffer happens to be insufficient to hold all the
result, the query command returns with an error. To calculate the size
of the output buffer, the user can post a preliminary query command
without attaching a buffer. For this case, the kernel returns the number
of entries for each query. Then, the user can calculate the size of the
output buffer, allocate it, and attach it to another query command.

If an error occurred, the ``errno`` variable is set to indicate the
reason. There are a few errors that can be returned.

* ``EINVAL`` -- when the arguments to ``ioctl()`` was invalid.
* ``EFAULT`` -- when a user buffer could not be accessed.
* ``ENOENT`` -- when an ID in the query could not be resolved to an object.

``ISP_IOC_OPERATION(size)``
---------------------------

An operation command submits a set of operation requests to the ISP
subsystem. The argument (``size``) is the size of the entire data passed
to the kernel, including the size of the header. The body for this
command is an array of request structures (struct isp_operation). A
request either adds or removes of an operation. For all the added
operations, the user-space need to make sure that the data structures
referenced from them remain valid and unchanged until their completion
is read.

If an opration has dependencies, they must be set at the time of adding
the dependent operation. It cannot be changed after the request is
submitted. Similarly, if fences exported should be signaled on
completion of an operation, they must also be set at the time of
addition.

A dependency is specified by the type and the identifier of an object.
The identifier is mostly the ISP object's ID, except for fences. To
specify fence dependencies, their file descriptor should be used. For
cases where the ISP subsystem fails to resolve a given identifier into
an object, its behavior depends on the type of the dependency object.
For operation dependency, it is regarded as satisfied. This is because
the operation given as dependency might have completed and its
corresponding object released by the time the new request reaches the
ISP subsystem. For all the other types, the operation request
immediately returns with an error.

Some operations require an instruction. An instruction may have a
driver specific data structure . For example, if an instruction type
(enum isp_rw_instruction_type) is ``ISP_READ_INSTRUCTION`` or
``ISP_WRITE_INSTRUCTION``, it could have a driver specific data
structure.

If an error occurred, it becomes available to the user-space either when
``ioctl()`` returns or when the operation's completion is read out. For
the first case where ``ioctl()`` returns with an error, the ``errno``
variable is set to indicate the reason. If the operation that caused the
error had an instruction (struct isp_rw_instruction), its error field is
also set. For the second case, the error reason is returned from the
driver and it only becomes available to the user-space in the
instruction error field on its completion. The errors that are not
specific to drivers are listed below.

* ``EINVAL`` -- when the arguments to ``ioctl()`` was invalid.
* ``EFAULT`` -- when a user buffer could not be accessed.
* ``ENOMEM`` -- when the kernel failed to allocate memory needed to
  perform the operation.

It is up to the user space whether to cancel other in-flight opertions
when one of the operations in the same set failed with an error.

Data Structure Reference
========================

.. kernel-doc:: include/uapi/linux/isp.h

ISP Driver Documentation
------------------------

.. toctree::
   :maxdepth: 2

   ipu6.rst

