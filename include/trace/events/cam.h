/* SPDX-License-Identifier: GPL-2.0 */
/*
 * CAM tracepoints
 *
 * Copyright (C) 2022 Google LLC
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM cam

#if !defined(_TRACE_CAM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_CAM_H

#include <linux/tracepoint.h>
#include <linux/cam/cam-entity.h>
#include <linux/cam/cam-pipeline.h>

DECLARE_EVENT_CLASS(cam_operation_class,
	TP_PROTO(struct cam_obj_op *op),
	TP_ARGS(op),

	TP_STRUCT__entry(
		__field(unsigned long, id)
		__field(enum cam_operation_state, state)
		__field(u64, delay_ns)
		__field(int, num_blockers)
		__field(int, pipeline_id)
	),

	TP_fast_assign(
		__entry->id = op->nsobj.id;
		__entry->state = op->state;
		__entry->delay_ns = op->delay_ns;
		__entry->num_blockers = atomic_read(&op->num_blockers);
		__entry->pipeline_id = op->pipeline->id;
	),

	TP_printk("id = %lu, state = %d, delay_ns = %llu, num_blockers = %d, pipeline_id = %d",
		  __entry->id,
		  __entry->state,
		  __entry->delay_ns,
		  __entry->num_blockers,
		  __entry->pipeline_id
	)
)

DEFINE_EVENT(cam_operation_class, cam_operation_add,
	TP_PROTO(struct cam_obj_op *op),
	TP_ARGS(op)
);

DEFINE_EVENT(cam_operation_class, cam_operation_set_state,
	TP_PROTO(struct cam_obj_op *op),
	TP_ARGS(op)
);

DECLARE_EVENT_CLASS(cam_event_class,
	TP_PROTO(struct cam_obj_entity *entity, struct cam_obj_event *event),
	TP_ARGS(entity, event),

	TP_STRUCT__entry(
		__field(unsigned long, entity_id)
		__array(char, entity_name, CAM_ENTITY_NAME_SZ)
		__field(unsigned long, event_id)
		__array(char, event_name, CAM_EVENT_NAME_SZ)
	),

	TP_fast_assign(
		__entry->entity_id = entity->nsobj.id;
		memcpy(__entry->entity_name, entity->name, CAM_ENTITY_NAME_SZ);
		__entry->event_id = event->nsobj.id;
		memcpy(__entry->event_name, event->name, CAM_EVENT_NAME_SZ);
	),

	TP_printk(
		  "entity_id = %lu, entity_name = %s, event_id = %lu, event_name = %s",
		  __entry->entity_id,
		  __entry->entity_name,
		  __entry->event_id,
		  __entry->event_name
	)
)

DEFINE_EVENT(cam_event_class, cam_event_trigger,
	TP_PROTO(struct cam_obj_entity *entity, struct cam_obj_event *event),
	TP_ARGS(entity, event)
);

#define KCAM_OP(obj)	(container_of(obj, struct cam_obj_op, nsobj))

DECLARE_EVENT_CLASS(cam_signal_class,
	TP_PROTO(struct cam_op_signal *signal),
	TP_ARGS(signal),

	TP_STRUCT__entry(
		__field(unsigned long, source_id)
		__field(enum cam_obj_type, source_type)
		__field(unsigned long, target_id)
		__field(int, pipeline_id)
	),

	/* we don't use accessors to bypass the checks */
	TP_fast_assign(
		__entry->source_id = signal->source->id;
		__entry->source_type = signal->source->type;
		__entry->target_id = signal->target->id;
		__entry->pipeline_id = KCAM_OP(signal->target)->pipeline->id;
	),

	TP_printk(
		  "source_id = %lu, source_type = %d, target_id = %lu, pipeline_id = %d",
		  __entry->source_id,
		  __entry->source_type,
		  __entry->target_id,
		  __entry->pipeline_id
	)
)

DEFINE_EVENT(cam_signal_class, cam_signal_add_pending,
	TP_PROTO(struct cam_op_signal *signal),
	TP_ARGS(signal)
);

DEFINE_EVENT(cam_signal_class, cam_signal_fire_active,
	TP_PROTO(struct cam_op_signal *signal),
	TP_ARGS(signal)
);

DECLARE_EVENT_CLASS(cam_io_worker_class,
	TP_PROTO(struct cam_pipeline *pipeline),
	TP_ARGS(pipeline),

	TP_STRUCT__entry(
		__field(int, pipeline_id)
	),

	TP_fast_assign(
		__entry->pipeline_id = pipeline->id;
	),

	TP_printk("pipeline_id = %d", __entry->pipeline_id)
)

DEFINE_EVENT(cam_io_worker_class, cam_io_worker_sleep,
	TP_PROTO(struct cam_pipeline *pipeline),
	TP_ARGS(pipeline)
);

DEFINE_EVENT(cam_io_worker_class, cam_io_worker_wakeup,
	TP_PROTO(struct cam_pipeline *pipeline),
	TP_ARGS(pipeline)
);

#endif /* _TRACE_CAM_H */

/* Leave this outside guards */
#include <trace/define_trace.h>
