.. SPDX-License-Identifier: GPL-2.0
.. Copyright 2023 Google LLC

================
IPU6 PSYS Driver
================

The IPU6 PSYS is the processing block in the Intel IPU6 hardware. The
PSYS processes a frame captured by its counterpart called ISYS. The ISYS
and PSYS are hardware blocks inside a single PCI device. There are three
frameworks used in this driver: the PCI framework for controlling the
IPU6 PCI block, V4L2 for ISYS block, and ISP for PSYS block.

Device Hierarchy
----------------

The IPU6 driver contains two driver buses. The first bus is a PCI bus
used to model the PCI interface of IPU6. The second bus is an auxiliary
bus that connects ISYS and PSYS. With the auxiliary bus, the ISYS driver and
the PSYS driver share some common codes, for example for IOMMU controls.

PSYS Objects
------------

The PSYS driver registers one entity object and one event object. Both
objects have the same name "PSYS0". The event object is registered under
the entity object.

Requests
^^^^^^^^

The PSYS entity accepts a few types of ISP read/write requests.

+-------------------+---------------+
| request           | direction     |
+===================+===============+
| Query Capability  | READ          |
+-------------------+---------------+
| Get Manifest      | READ          |
+-------------------+---------------+
| Queue Command     | WRITE         |
+-------------------+---------------+
| Dequeue Event     | READ          |
+-------------------+---------------+

Expected Usage Flow
-------------------

The PSYS driver expects a user application to make requests in the
following manner.

1. Query the ISP framework to obtain the ID for the PSYS entity and
   event.
2. Create an instance of the PSYS entity. The driver allocates data
   structures needed to accept subsequent requests. It also disables
   automatic suspension of the PSYS block.
3. Queue a command to start processing. The driver allocates actual
   hardware resources and configures them.
4. Queue a command to post parameters and image buffers. The driver
   posts requests and once completed notifies the user application
   using the PSYS event.
5. Queue a command to stop processing. The driver frees the hardware
   resources.

Data Structure Reference
------------------------

.. kernel-doc:: include/uapi/linux/ipu-psys.h
