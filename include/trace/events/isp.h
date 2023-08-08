/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ISP tracepoints
 *
 * Copyright (C) 2022 Google LLC
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM isp

#if !defined(_TRACE_ISP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_ISP_H

#include <linux/tracepoint.h>
#include <linux/isp/isp-entity.h>
#include <linux/isp/isp-pipeline.h>

#include <uapi/linux/isp.h>

/* export enums for trace-cmd */
TRACE_DEFINE_ENUM(ISP_OBJ_TYPE_ENTITY);
TRACE_DEFINE_ENUM(ISP_OBJ_TYPE_EVENT);
TRACE_DEFINE_ENUM(ISP_OBJ_TYPE_OPERATION);
TRACE_DEFINE_ENUM(ISP_OBJ_TYPE_BUFFER);
TRACE_DEFINE_ENUM(ISP_OBJ_TYPE_IN_SYNCFILE);
TRACE_DEFINE_ENUM(ISP_OBJ_TYPE_OUT_SYNCFILE);
TRACE_DEFINE_ENUM(ISP_OBJ_TYPE_ROOT);

TRACE_DEFINE_ENUM(ISP_OPERATION_STATE_SLEEP);
TRACE_DEFINE_ENUM(ISP_OPERATION_STATE_QUEUED);
TRACE_DEFINE_ENUM(ISP_OPERATION_STATE_RUNNING);
TRACE_DEFINE_ENUM(ISP_OPERATION_STATE_EXECUTED);
TRACE_DEFINE_ENUM(ISP_OPERATION_STATE_DELETED);

#define type_name(type)	{ ISP_OBJ_TYPE_##type, #type }
#define show_type_name(val)				\
	__print_symbolic(val,				\
			 type_name(ENTITY),		\
			 type_name(EVENT),		\
			 type_name(OPERATION),		\
			 type_name(BUFFER),		\
			 type_name(IN_SYNCFILE),	\
			 type_name(OUT_SYNCFILE),	\
			 type_name(ROOT))

#define state_name(state)	{ ISP_OPERATION_STATE_##state, #state }
#define show_state_name(val)			\
	__print_symbolic(val,			\
			state_name(SLEEP),	\
			state_name(QUEUED),	\
			state_name(RUNNING),	\
			state_name(EXECUTED),	\
			state_name(DELETED))

DECLARE_EVENT_CLASS(isp_operation_class,
	TP_PROTO(struct isp_obj_op *op),
	TP_ARGS(op),

	TP_STRUCT__entry(
		__field(unsigned long, id)
		__field(enum isp_operation_state, state)
		__field(u64, delay_ns)
		__field(int, num_blockers)
		__field(int, pipeline_id)
		__field(unsigned long, instance_id)
	),

	TP_fast_assign(
		__entry->id = op->nsobj.id;
		__entry->state = op->state;
		__entry->delay_ns = op->delay_ns;
		__entry->num_blockers = atomic_read(&op->num_blockers);
		__entry->pipeline_id = op->pipeline->id;
		if (op->exec_instance)
			__entry->instance_id = op->exec_instance->nsobj.id;
		else
			__entry->instance_id = -1;
	),

	TP_printk("id = 0x%lx, state = %s, delay_ns = %llu, num_blockers = %d, instance_id = 0x%lx, pipeline_id = 0x%x",
		  __entry->id,
		  show_state_name(__entry->state),
		  __entry->delay_ns,
		  __entry->num_blockers,
		  __entry->instance_id,
		  __entry->pipeline_id
	)
)

DEFINE_EVENT(isp_operation_class, isp_operation_add,
	TP_PROTO(struct isp_obj_op *op),
	TP_ARGS(op)
);

DEFINE_EVENT(isp_operation_class, isp_operation_set_state,
	TP_PROTO(struct isp_obj_op *op),
	TP_ARGS(op)
);

DECLARE_EVENT_CLASS(isp_event_class,
	TP_PROTO(struct isp_obj_entity *entity,
		 struct isp_obj_instance *instance,
		 struct isp_obj_event *event),
	TP_ARGS(entity, instance, event),

	TP_STRUCT__entry(
		__field(unsigned long, entity_id)
		__field(unsigned long, instance_id)
		__array(char, entity_name, ISP_ENTITY_NAME_SZ)
		__field(unsigned long, event_id)
		__array(char, event_name, ISP_EVENT_NAME_SZ)
	),

	TP_fast_assign(
		__entry->entity_id = entity->nsobj.id;
		if (instance)
			__entry->instance_id = instance->nsobj.id;
		else
			__entry->instance_id = -1;
		memcpy(__entry->entity_name, entity->name, ISP_ENTITY_NAME_SZ);
		__entry->event_id = event->nsobj.id;
		memcpy(__entry->event_name, event->name, ISP_EVENT_NAME_SZ);
	),

	TP_printk(
		  "entity_id = 0x%lx, instance_id = 0x%lx, entity_name = %s, event_id = 0x%lx, event_name = %s",
		  __entry->entity_id,
		  __entry->instance_id,
		  __entry->entity_name,
		  __entry->event_id,
		  __entry->event_name
	)
)

DEFINE_EVENT(isp_event_class, isp_event_trigger,
	TP_PROTO(struct isp_obj_entity *entity,
		 struct isp_obj_instance *instance,
		 struct isp_obj_event *event),
	TP_ARGS(entity, instance, event)
);

#define KISP_OP(obj)	(container_of(obj, struct isp_obj_op, nsobj))

DECLARE_EVENT_CLASS(isp_signal_class,
	TP_PROTO(struct isp_op_signal *signal),
	TP_ARGS(signal),

	TP_STRUCT__entry(
		__field(unsigned long, source_id)
		__field(enum isp_obj_type, source_type)
		__field(unsigned long, target_id)
		__field(unsigned long, instance_id)
		__field(int, pipeline_id)
	),

	/* we don't use accessors to bypass the checks */
	TP_fast_assign(
		__entry->source_id = signal->source->id;
		__entry->source_type = signal->source->type;
		__entry->target_id = signal->target->id;
		__entry->instance_id = signal->instance;
		__entry->pipeline_id = KISP_OP(signal->target)->pipeline->id;
	),

	TP_printk(
		  "source_id = 0x%lx, source_type = %s, target_id = 0x%lx, instance_id = 0x%lx, pipeline_id = 0x%x",
		  __entry->source_id,
		  show_type_name(__entry->source_type),
		  __entry->target_id,
		  __entry->instance_id,
		  __entry->pipeline_id
	)
)

DEFINE_EVENT(isp_signal_class, isp_signal_add_pending,
	TP_PROTO(struct isp_op_signal *signal),
	TP_ARGS(signal)
);

DEFINE_EVENT(isp_signal_class, isp_signal_add_active,
	TP_PROTO(struct isp_op_signal *signal),
	TP_ARGS(signal)
);

DEFINE_EVENT(isp_signal_class, isp_signal_fire_active,
	TP_PROTO(struct isp_op_signal *signal),
	TP_ARGS(signal)
);

DECLARE_EVENT_CLASS(isp_io_worker_class,
	TP_PROTO(struct isp_pipeline *pipeline),
	TP_ARGS(pipeline),

	TP_STRUCT__entry(
		__field(int, pipeline_id)
	),

	TP_fast_assign(
		__entry->pipeline_id = pipeline->id;
	),

	TP_printk("pipeline_id = 0x%x", __entry->pipeline_id)
)

DEFINE_EVENT(isp_io_worker_class, isp_io_worker_sleep,
	TP_PROTO(struct isp_pipeline *pipeline),
	TP_ARGS(pipeline)
);

DEFINE_EVENT(isp_io_worker_class, isp_io_worker_wakeup,
	TP_PROTO(struct isp_pipeline *pipeline),
	TP_ARGS(pipeline)
);

#endif /* _TRACE_ISP_H */

/* Leave this outside guards */
#include <trace/define_trace.h>
