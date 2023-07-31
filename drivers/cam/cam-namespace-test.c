// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit test for cam namespace
 */

#include <kunit/test.h>

static void ns_init(struct kunit *test)
{
	struct cam_ns ns;
	int ret;
	enum cam_id_policy policy[] = {
		CAM_NS_POL_UNIQUE_ID,
		CAM_NS_POL_USER_ID };
	int i;

	/* id policy is mandatory */
	ret = cam_ns_init(&ns, 0);
	KUNIT_EXPECT_NE(test, ret, 0);
	if (!ret)
		cam_ns_release(&ns);

	/* Only one id policy can be used */
	ret = cam_ns_init(&ns, CAM_NS_POL_USER_ID | CAM_NS_POL_UNIQUE_ID);
	KUNIT_EXPECT_NE(test, ret, 0);
	if (!ret)
		cam_ns_release(&ns);

	/* Invalid id_policy */
	ret = cam_ns_init(&ns, CAM_NS_POL_USER_ID | BIT(31));
	KUNIT_EXPECT_NE(test, ret, 0);
	if (!ret)
		cam_ns_release(&ns);

	/* Normal use */
	for (i = 0; i < ARRAY_SIZE(policy); i++) {
		ret = cam_ns_init(&ns, policy[i]);
		KUNIT_EXPECT_EQ(test, ret, 0);
		if (!ret)
			cam_ns_release(&ns);
	}
}

static bool release_flag;
static void obj_release(struct cam_obj *nsobj)
{
	release_flag = true;
	kfree(nsobj);
}

static void dummy_obj_release(struct cam_obj *nsobj)
{
	release_flag = true;
}

static void ns_release(struct kunit *test)
{
	struct cam_obj *obj;
	struct cam_ns ns = {};
	int ret;

	//Invalid
	cam_ns_release(NULL);

	//Release and free
	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	KUNIT_ASSERT_PTR_NE(test, obj, (struct cam_obj *)NULL);
	ret = cam_ns_init(&ns, CAM_NS_POL_USER_ID);
	KUNIT_ASSERT_EQ(test, ret, 0);
	release_flag = false;
	cam_obj_init(obj, CAM_OBJ_TYPE_ENTITY, obj_release, &ns);
	ret = cam_obj_move(obj, NULL);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_FALSE(test, release_flag);
	cam_ns_release(&ns);
	KUNIT_EXPECT_TRUE(test, release_flag);
}

static void obj_init(struct kunit *test)
{
	struct cam_obj *obj;
	struct cam_ns ns;
	int ret;

	ret = cam_ns_init(&ns, CAM_NS_POL_USER_ID);
	KUNIT_ASSERT_EQ(test, ret, 0);

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	KUNIT_ASSERT_PTR_NE(test, obj, (struct cam_obj *)NULL);
	obj->id = 42;
	//Init must zero the id
	cam_obj_init(obj, CAM_OBJ_TYPE_ENTITY, obj_release, &ns);
	KUNIT_EXPECT_EQ(test, 0UL, cam_obj_id(obj));
	cam_obj_deinit(obj);

	cam_ns_release(&ns);
}

static void obj_get(struct kunit *test)
{
	struct cam_obj *obj;
	struct cam_ns ns;
	int ret;

	ret = cam_ns_init(&ns, CAM_NS_POL_USER_ID);
	KUNIT_ASSERT_EQ(test, ret, 0);

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	KUNIT_ASSERT_PTR_NE(test, obj, (struct cam_obj *)NULL);

	cam_obj_init(obj, CAM_OBJ_TYPE_ENTITY, dummy_obj_release, &ns);

	//Get and put
	KUNIT_EXPECT_PTR_EQ(test, obj, cam_obj_get(obj));
	cam_obj_put(obj);

	//Put a refcount=1
	release_flag = false;
	cam_obj_put(obj);
	KUNIT_EXPECT_PTR_EQ(test, (struct cam_obj *)NULL, cam_obj_get(obj));
	KUNIT_EXPECT_TRUE(test, release_flag);

	//Check double free
	release_flag = false;
	cam_obj_deinit(obj);
	KUNIT_EXPECT_PTR_EQ(test, (struct cam_obj *)NULL, cam_obj_get(obj));
	KUNIT_EXPECT_FALSE(test, release_flag);

	/* Do not do it this way - obj_release() should kfree() objects */
	kfree(obj);

	cam_ns_release(&ns);
}

static void obj_put(struct kunit *test)
{
	struct cam_obj *obj;
	struct cam_ns ns;
	int ret;

	ret = cam_ns_init(&ns, CAM_NS_POL_USER_ID);
	KUNIT_ASSERT_EQ(test, ret, 0);

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	KUNIT_ASSERT_PTR_NE(test, obj, (struct cam_obj *)NULL);
	cam_obj_init(obj, CAM_OBJ_TYPE_ENTITY, dummy_obj_release, &ns);

	//Check put
	release_flag = false;
	cam_obj_deinit(obj);
	KUNIT_EXPECT_TRUE(test, release_flag);

	//Check double free
	release_flag = false;
	cam_obj_put(obj);
	KUNIT_EXPECT_FALSE(test, release_flag);

	/* Do not do it this way - obj_release() should kfree() objects */
	kfree(obj);

	cam_ns_release(&ns);
}

static void id_set_get(struct kunit *test)
{
	struct cam_obj *obj;
	struct cam_ns ns;
	int ret;

	ret = cam_ns_init(&ns, CAM_NS_POL_USER_ID);
	KUNIT_ASSERT_EQ(test, ret, 0);

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	KUNIT_ASSERT_PTR_NE(test, obj, (struct cam_obj *)NULL);
	cam_obj_init(obj, CAM_OBJ_TYPE_ENTITY, obj_release, &ns);

	//Get initial id
	KUNIT_EXPECT_EQ(test, 0UL, cam_obj_id(obj));

	//Set id
	ret = cam_obj_set_id(obj, 42UL);
	KUNIT_EXPECT_EQ(test, 0, ret);
	KUNIT_EXPECT_EQ(test, 42UL, cam_obj_id(obj));

	cam_obj_deinit(obj);
	cam_ns_release(&ns);

	ret = cam_ns_init(&ns, CAM_NS_POL_UNIQUE_ID);
	KUNIT_ASSERT_EQ(test, ret, 0);

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	KUNIT_ASSERT_PTR_NE(test, obj, (struct cam_obj *)NULL);
	cam_obj_init(obj, CAM_OBJ_TYPE_ENTITY, obj_release, &ns);

	//Get initial id
	KUNIT_EXPECT_EQ(test, 0UL, cam_obj_id(obj));

	//Set id on non pol_user_id
	ret = cam_obj_set_id(obj, 42UL);
	KUNIT_EXPECT_NE(test, 0, ret);
	KUNIT_EXPECT_EQ(test, 0UL, cam_obj_id(obj));

	cam_obj_deinit(obj);
	cam_ns_release(&ns);
}

/* IDs must not overlap */
static void ids_checks(struct kunit *test)
{
	KUNIT_EXPECT_LT(test, CAM_NS_UNIQUE_ID_START, CAM_NS_UNIQUE_ID_END);
}

static void obj_add_user(struct kunit *test)
{
	struct cam_obj *obj, *obj2;
	unsigned long id;
	struct cam_ns ns;
	int ret;

	ret = cam_ns_init(&ns, CAM_NS_POL_USER_ID);
	KUNIT_ASSERT_EQ(test, ret, 0);

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	KUNIT_ASSERT_PTR_NE(test, obj, (struct cam_obj *)NULL);
	cam_obj_init(obj, CAM_OBJ_TYPE_ENTITY, obj_release, &ns);

	//Insert object 0
	ret = cam_obj_insert(obj);
	KUNIT_EXPECT_EQ(test, 0, ret);

	//Change an inserted object
	ret = cam_obj_set_id(obj, 1);
	KUNIT_EXPECT_NE(test, 0, ret);

	obj2 = kzalloc(sizeof(*obj), GFP_KERNEL);
	KUNIT_ASSERT_PTR_NE(test, obj2, (struct cam_obj *)NULL);
	cam_obj_init(obj2, CAM_OBJ_TYPE_ENTITY, obj_release, &ns);
	//Insert ducplicated id
	ret = cam_obj_move(obj2, &id);
	KUNIT_EXPECT_EQ(test, -EEXIST, ret);

	//Move object 1
	ret = cam_obj_set_id(obj2, 1);
	KUNIT_EXPECT_EQ(test, 0, ret);
	ret = cam_obj_move(obj2, &id);
	KUNIT_EXPECT_EQ(test, 0, ret);
	KUNIT_EXPECT_EQ(test, 1UL, id);

	cam_obj_remove(obj);
	cam_obj_deinit(obj);
	cam_ns_release(&ns);
}

static void obj_add_unique(struct kunit *test)
{
	struct cam_obj *obj, *obj2;
	unsigned long id;
	struct cam_ns ns;
	int ret;

	ret = cam_ns_init(&ns, CAM_NS_POL_UNIQUE_ID);
	KUNIT_ASSERT_EQ(test, ret, 0);

	//Move object in the ns and remove it
	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	KUNIT_ASSERT_PTR_NE(test, obj, (struct cam_obj *)NULL);
	cam_obj_init(obj, CAM_OBJ_TYPE_ENTITY, obj_release, &ns);
	ret = cam_obj_insert(obj);
	KUNIT_EXPECT_EQ(test, 0, ret);
	KUNIT_EXPECT_EQ(test, ns.next_id, CAM_NS_UNIQUE_ID_START+1);
	KUNIT_EXPECT_EQ(test, CAM_NS_UNIQUE_ID_START, cam_obj_id(obj));
	cam_obj_remove(obj);
	cam_obj_deinit(obj);

	KUNIT_EXPECT_EQ(test, ns.next_id, CAM_NS_UNIQUE_ID_START+1);

	//Move object into the ns and expect different id. then remove it
	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	KUNIT_ASSERT_PTR_NE(test, obj, (struct cam_obj *)NULL);
	cam_obj_init(obj, CAM_OBJ_TYPE_ENTITY, obj_release, &ns);
	ret = cam_obj_set_id(obj, 42UL);
	KUNIT_EXPECT_NE(test, 0, ret);
	ret = cam_obj_insert(obj);
	KUNIT_EXPECT_EQ(test, 0, ret);
	KUNIT_EXPECT_EQ(test, CAM_NS_UNIQUE_ID_START + 1, cam_obj_id(obj));
	cam_obj_remove(obj);
	cam_obj_deinit(obj);

	//Move object into the ns and expect different id
	obj2 = kzalloc(sizeof(*obj), GFP_KERNEL);
	KUNIT_ASSERT_PTR_NE(test, obj2, (struct cam_obj *)NULL);
	cam_obj_init(obj2, CAM_OBJ_TYPE_ENTITY, obj_release, &ns);
	ret = cam_obj_move(obj2, &id);
	KUNIT_EXPECT_EQ(test, 0, ret);
	KUNIT_EXPECT_EQ(test, CAM_NS_UNIQUE_ID_START + 2, id);

	cam_ns_release(&ns);
}

static void obj_add_invalid(struct kunit *test)
{
	struct cam_obj *obj;
	int ret;

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	KUNIT_ASSERT_PTR_NE(test, obj, (struct cam_obj *)NULL);
	ret = cam_obj_insert(obj);
	KUNIT_EXPECT_EQ(test, -EINVAL, ret);
	kfree(obj);
}

static void obj_lookup(struct kunit *test)
{
	struct cam_obj *obj;
	struct cam_obj *pobj;
	unsigned long id;
	struct cam_ns ns;
	int ret;

	ret = cam_ns_init(&ns, CAM_NS_POL_USER_ID);
	KUNIT_ASSERT_EQ(test, ret, 0);

	// lookup id not present
	pobj = cam_obj_lookup(&ns, CAM_OBJ_TYPE_ENTITY, 42);
	KUNIT_EXPECT_PTR_EQ(test, pobj, (struct cam_obj *)NULL);

	// lookup id
	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	KUNIT_ASSERT_PTR_NE(test, obj, (struct cam_obj *)NULL);
	cam_obj_init(obj, CAM_OBJ_TYPE_ENTITY, obj_release, &ns);
	ret = cam_obj_set_id(obj, 42UL);
	KUNIT_ASSERT_EQ(test, ret, 0);
	ret = cam_obj_move(obj, &id);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_ASSERT_EQ(test, id, 42UL);
	pobj = cam_obj_lookup(&ns, CAM_OBJ_TYPE_ENTITY, 42);
	KUNIT_EXPECT_PTR_EQ(test, pobj, obj);
	cam_obj_put(pobj);
	cam_obj_remove(obj);

	// lookup removed id
	pobj = cam_obj_lookup(&ns, CAM_OBJ_TYPE_ENTITY, 42);
	KUNIT_EXPECT_PTR_EQ(test, pobj, (struct cam_obj *)NULL);

	cam_ns_release(&ns);
}

static void obj_remove(struct kunit *test)
{
	struct cam_obj *obj;
	struct cam_obj *pobj;
	struct cam_ns ns;
	unsigned long id;
	int ret;

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	KUNIT_ASSERT_PTR_NE(test, obj, (struct cam_obj *)NULL);
	// remove not initialised object
	cam_obj_remove(obj);

	ret = cam_ns_init(&ns, CAM_NS_POL_UNIQUE_ID);
	KUNIT_ASSERT_EQ(test, ret, 0);

	// remove not added object
	release_flag = false;
	cam_obj_init(obj, CAM_OBJ_TYPE_ENTITY, obj_release, &ns);
	cam_obj_remove(obj);
	KUNIT_EXPECT_FALSE(test, release_flag);
	cam_obj_deinit(obj);
	KUNIT_EXPECT_TRUE(test, release_flag);

	// remove added object
	release_flag = false;
	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	cam_obj_init(obj, CAM_OBJ_TYPE_ENTITY, obj_release, &ns);
	ret = cam_obj_insert(obj);
	KUNIT_ASSERT_EQ(test, ret, 0);
	id = cam_obj_id(obj);
	cam_obj_remove(obj);
	cam_obj_deinit(obj);
	KUNIT_EXPECT_TRUE(test, release_flag);
	pobj = cam_obj_lookup(&ns, CAM_OBJ_TYPE_ENTITY, CAM_NS_UNIQUE_ID_END);
	KUNIT_EXPECT_PTR_EQ(test, (struct cam_obj *)NULL, pobj);

	cam_ns_release(&ns);
}

static void obj_remove_id(struct kunit *test)
{
	struct cam_obj *obj;
	struct cam_ns ns;
	unsigned long id;
	int ret;

	// remove invalid params
	ret = cam_obj_remove_id(NULL, CAM_OBJ_TYPE_ENTITY, 42);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	ret = cam_ns_init(&ns, CAM_NS_POL_UNIQUE_ID);
	KUNIT_ASSERT_EQ(test, ret, 0);

	// remove non existent obj
	ret = cam_obj_remove_id(&ns, CAM_OBJ_TYPE_ENTITY, 42);
	KUNIT_EXPECT_EQ(test, ret, -ENOENT);

	release_flag = false;
	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	KUNIT_ASSERT_PTR_NE(test, obj, (struct cam_obj *)NULL);
	cam_obj_init(obj, CAM_OBJ_TYPE_ENTITY, obj_release, &ns);
	ret = cam_obj_move(obj, &id);
	KUNIT_ASSERT_EQ(test, ret, 0);

	// remove invalid type
	ret = cam_obj_remove_id(&ns, CAM_OBJ_TYPE_ROOT, id);
	KUNIT_EXPECT_EQ(test, ret, -ENOENT);

	// normal remove
	ret = cam_obj_remove_id(&ns, CAM_OBJ_TYPE_ENTITY, id);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_TRUE(test, release_flag);

	//double remove
	release_flag = false;
	ret = cam_obj_remove_id(&ns, CAM_OBJ_TYPE_ENTITY, id);
	KUNIT_EXPECT_EQ(test, ret, -ENOENT);
	KUNIT_EXPECT_FALSE(test, release_flag);

	cam_ns_release(&ns);
}

static void obj_move(struct kunit *test)
{
	struct cam_obj *obj;
	struct cam_ns ns;
	unsigned long id;
	int ret;

	//Invalid
	ret = cam_obj_move(NULL, &id);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	//Non initialised
	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	KUNIT_ASSERT_PTR_NE(test, obj, (struct cam_obj *)NULL);
	ret = cam_obj_move(obj, &id);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	ret = cam_ns_init(&ns, CAM_NS_POL_USER_ID);
	KUNIT_ASSERT_EQ(test, ret, 0);

	//Normal insert
	release_flag = false;
	cam_obj_init(obj, CAM_OBJ_TYPE_ENTITY, obj_release, &ns);
	ret = cam_obj_move(obj, &id);
	KUNIT_EXPECT_EQ(test, ret, 0);

	//Double insert
	ret = cam_obj_move(obj, &id);
	KUNIT_EXPECT_EQ(test, ret, -EEXIST);

	//Release after remove
	ret = cam_obj_remove_id(&ns, CAM_OBJ_TYPE_ENTITY, id);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_TRUE(test, release_flag);

	//Null ID
	release_flag = false;
	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	KUNIT_ASSERT_PTR_NE(test, obj, (struct cam_obj *)NULL);
	cam_obj_init(obj, CAM_OBJ_TYPE_ENTITY, obj_release, &ns);
	ret = cam_obj_move(obj, NULL);
	KUNIT_EXPECT_EQ(test, ret, 0);

	//Release after ns_release
	KUNIT_EXPECT_FALSE(test, release_flag);
	cam_ns_release(&ns);
	KUNIT_EXPECT_TRUE(test, release_flag);
}

static void obj_insert(struct kunit *test)
{
	struct cam_obj *obj;
	struct cam_ns ns;
	int ret;

	//Invalid
	ret = cam_obj_insert(NULL);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	//Non initialised
	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	KUNIT_ASSERT_PTR_NE(test, obj, (struct cam_obj *)NULL);
	ret = cam_obj_insert(obj);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	ret = cam_ns_init(&ns, CAM_NS_POL_USER_ID);
	KUNIT_ASSERT_EQ(test, ret, 0);

	//Normal insert
	release_flag = false;
	cam_obj_init(obj, CAM_OBJ_TYPE_ENTITY, obj_release, &ns);
	ret = cam_obj_insert(obj);
	KUNIT_EXPECT_EQ(test, ret, 0);

	//Double insert
	ret = cam_obj_insert(obj);
	KUNIT_EXPECT_EQ(test, ret, -EEXIST);

	//Release after remove
	cam_obj_remove(obj);
	KUNIT_EXPECT_FALSE(test, release_flag);
	cam_obj_deinit(obj);
	KUNIT_EXPECT_TRUE(test, release_flag);

	//Release after ns_release
	release_flag = false;
	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	KUNIT_ASSERT_PTR_NE(test, obj, (struct cam_obj *)NULL);
	cam_obj_init(obj, CAM_OBJ_TYPE_ENTITY, obj_release, &ns);
	ret = cam_obj_insert(obj);
	KUNIT_EXPECT_EQ(test, ret, 0);

	KUNIT_EXPECT_FALSE(test, release_flag);
	cam_ns_release(&ns);
	KUNIT_EXPECT_FALSE(test, release_flag);
	cam_obj_deinit(obj);
	KUNIT_EXPECT_TRUE(test, release_flag);
}

struct for_each_test {
	unsigned long n_objs;
	unsigned long sum_ids;
};

static bool for_each_test_callback(struct cam_obj *obj,
				   struct cam_ns_walk_control *ctl)
{
	struct for_each_test *fet;

	fet = ctl->data;
	fet->n_objs++;
	fet->sum_ids += cam_obj_id(obj);
	return false;
}

#define N_OBJS 10UL
static void ns_for_each(struct kunit *test)
{
	struct cam_ns_walk_control ctl = {};
	struct for_each_test fet = {};
	struct cam_obj *obj;
	unsigned long sum_ids = 0;
	unsigned long id0, id;
	struct cam_ns ns;
	int ret;
	int i;

	// Invalid calls
	cam_ns_for_each(NULL, NULL);

	ret = cam_ns_init(&ns, CAM_NS_POL_UNIQUE_ID);
	KUNIT_ASSERT_EQ(test, ret, 0);

	// Invalid calls
	cam_ns_for_each(&ns, NULL);

	// Invalid calls
	cam_ns_for_each(&ns, &ctl);

	for (i = 0; i < N_OBJS; i++) {
		obj = kzalloc(sizeof(*obj), GFP_KERNEL);
		KUNIT_ASSERT_PTR_NE(test, obj, (struct cam_obj *)NULL);
		cam_obj_init(obj, CAM_OBJ_TYPE_ENTITY, obj_release, &ns);
		ret = cam_obj_move(obj, &id);
		KUNIT_ASSERT_EQ(test, ret, 0);
		KUNIT_ASSERT_PTR_NE(test, obj, (struct cam_obj *)NULL);
		sum_ids += id;
		if (i == 1)
			id0 = id;
	}

	// Valid call
	ctl.data = &fet;
	ctl.cb = for_each_test_callback;
	cam_ns_for_each(&ns, &ctl);
	KUNIT_EXPECT_EQ(test, fet.n_objs, N_OBJS);
	KUNIT_EXPECT_EQ(test, fet.sum_ids, sum_ids);

	// Valid call - 1
	sum_ids -= id0;
	cam_obj_remove_id(&ns, CAM_OBJ_TYPE_ENTITY, id0);
	memset(&fet, 0, sizeof(fet));
	cam_ns_for_each(&ns, &ctl);
	KUNIT_EXPECT_EQ(test, fet.n_objs, N_OBJS - 1);
	KUNIT_EXPECT_EQ(test, fet.sum_ids, sum_ids);

	cam_ns_release(&ns);
}

static struct kunit_case cam_namespace_test_cases[] = {
	KUNIT_CASE(ns_init),
	KUNIT_CASE(ns_release),
	KUNIT_CASE(ids_checks),
	KUNIT_CASE(obj_init),
	KUNIT_CASE(obj_get),
	KUNIT_CASE(obj_put),
	KUNIT_CASE(id_set_get),
	KUNIT_CASE(obj_add_user),
	KUNIT_CASE(obj_add_unique),
	KUNIT_CASE(obj_add_invalid),
	KUNIT_CASE(obj_lookup),
	KUNIT_CASE(obj_remove),
	KUNIT_CASE(obj_remove_id),
	KUNIT_CASE(obj_move),
	KUNIT_CASE(obj_insert),
	KUNIT_CASE(ns_for_each),
	{}
};

static struct kunit_suite cam_namespace_test_suite = {
	.name = "cam_namespace_test",
	.test_cases = cam_namespace_test_cases,
};

kunit_test_suites(&cam_namespace_test_suite);

MODULE_LICENSE("GPL v2");
