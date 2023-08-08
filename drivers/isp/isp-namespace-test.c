// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit test for isp namespace
 */

#include <kunit/test.h>

static void ns_init(struct kunit *test)
{
	struct isp_ns ns;
	int ret;
	enum isp_id_policy policy[] = {
		ISP_NS_POL_UNIQUE_ID,
		ISP_NS_POL_USER_ID };
	int i;

	/* id policy is mandatory */
	ret = isp_ns_init(&ns, 0);
	KUNIT_EXPECT_NE(test, ret, 0);
	if (!ret)
		isp_ns_release(&ns);

	/* Only one id policy can be used */
	ret = isp_ns_init(&ns, ISP_NS_POL_USER_ID | ISP_NS_POL_UNIQUE_ID);
	KUNIT_EXPECT_NE(test, ret, 0);
	if (!ret)
		isp_ns_release(&ns);

	/* Invalid id_policy */
	ret = isp_ns_init(&ns, ISP_NS_POL_USER_ID | BIT(31));
	KUNIT_EXPECT_NE(test, ret, 0);
	if (!ret)
		isp_ns_release(&ns);

	/* Normal use */
	for (i = 0; i < ARRAY_SIZE(policy); i++) {
		ret = isp_ns_init(&ns, policy[i]);
		KUNIT_EXPECT_EQ(test, ret, 0);
		if (!ret)
			isp_ns_release(&ns);
	}
}

static bool release_flag;
static void obj_release(struct isp_obj *nsobj)
{
	release_flag = true;
	kfree(nsobj);
}

static void dummy_obj_release(struct isp_obj *nsobj)
{
	release_flag = true;
}

static void ns_release(struct kunit *test)
{
	struct isp_obj *obj;
	struct isp_ns ns = {};
	int ret;

	//Invalid
	isp_ns_release(NULL);

	//Release and free
	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	KUNIT_ASSERT_PTR_NE(test, obj, (struct isp_obj *)NULL);
	ret = isp_ns_init(&ns, ISP_NS_POL_USER_ID);
	KUNIT_ASSERT_EQ(test, ret, 0);
	release_flag = false;
	isp_obj_init(obj, ISP_OBJ_TYPE_ENTITY, obj_release, &ns);
	ret = isp_obj_move(obj, NULL);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_FALSE(test, release_flag);
	isp_ns_release(&ns);
	KUNIT_EXPECT_TRUE(test, release_flag);
}

static void obj_init(struct kunit *test)
{
	struct isp_obj *obj;
	struct isp_ns ns;
	int ret;

	ret = isp_ns_init(&ns, ISP_NS_POL_USER_ID);
	KUNIT_ASSERT_EQ(test, ret, 0);

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	KUNIT_ASSERT_PTR_NE(test, obj, (struct isp_obj *)NULL);
	obj->id = 42;
	//Init must zero the id
	isp_obj_init(obj, ISP_OBJ_TYPE_ENTITY, obj_release, &ns);
	KUNIT_EXPECT_EQ(test, 0UL, isp_obj_id(obj));
	isp_obj_deinit(obj);

	isp_ns_release(&ns);
}

static void obj_get(struct kunit *test)
{
	struct isp_obj *obj;
	struct isp_ns ns;
	int ret;

	ret = isp_ns_init(&ns, ISP_NS_POL_USER_ID);
	KUNIT_ASSERT_EQ(test, ret, 0);

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	KUNIT_ASSERT_PTR_NE(test, obj, (struct isp_obj *)NULL);

	isp_obj_init(obj, ISP_OBJ_TYPE_ENTITY, dummy_obj_release, &ns);

	//Get and put
	KUNIT_EXPECT_PTR_EQ(test, obj, isp_obj_get(obj));
	isp_obj_put(obj);

	//Put a refcount=1
	release_flag = false;
	isp_obj_put(obj);
	KUNIT_EXPECT_PTR_EQ(test, (struct isp_obj *)NULL, isp_obj_get(obj));
	KUNIT_EXPECT_TRUE(test, release_flag);

	//Check double free
	release_flag = false;
	isp_obj_deinit(obj);
	KUNIT_EXPECT_PTR_EQ(test, (struct isp_obj *)NULL, isp_obj_get(obj));
	KUNIT_EXPECT_FALSE(test, release_flag);

	/* Do not do it this way - obj_release() should kfree() objects */
	kfree(obj);

	isp_ns_release(&ns);
}

static void obj_put(struct kunit *test)
{
	struct isp_obj *obj;
	struct isp_ns ns;
	int ret;

	ret = isp_ns_init(&ns, ISP_NS_POL_USER_ID);
	KUNIT_ASSERT_EQ(test, ret, 0);

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	KUNIT_ASSERT_PTR_NE(test, obj, (struct isp_obj *)NULL);
	isp_obj_init(obj, ISP_OBJ_TYPE_ENTITY, dummy_obj_release, &ns);

	//Check put
	release_flag = false;
	isp_obj_deinit(obj);
	KUNIT_EXPECT_TRUE(test, release_flag);

	//Check double free
	release_flag = false;
	isp_obj_put(obj);
	KUNIT_EXPECT_FALSE(test, release_flag);

	/* Do not do it this way - obj_release() should kfree() objects */
	kfree(obj);

	isp_ns_release(&ns);
}

static void id_set_get(struct kunit *test)
{
	struct isp_obj *obj;
	struct isp_ns ns;
	int ret;

	ret = isp_ns_init(&ns, ISP_NS_POL_USER_ID);
	KUNIT_ASSERT_EQ(test, ret, 0);

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	KUNIT_ASSERT_PTR_NE(test, obj, (struct isp_obj *)NULL);
	isp_obj_init(obj, ISP_OBJ_TYPE_ENTITY, obj_release, &ns);

	//Get initial id
	KUNIT_EXPECT_EQ(test, 0UL, isp_obj_id(obj));

	//Set id
	ret = isp_obj_set_id(obj, 42UL);
	KUNIT_EXPECT_EQ(test, 0, ret);
	KUNIT_EXPECT_EQ(test, 42UL, isp_obj_id(obj));

	isp_obj_deinit(obj);
	isp_ns_release(&ns);

	ret = isp_ns_init(&ns, ISP_NS_POL_UNIQUE_ID);
	KUNIT_ASSERT_EQ(test, ret, 0);

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	KUNIT_ASSERT_PTR_NE(test, obj, (struct isp_obj *)NULL);
	isp_obj_init(obj, ISP_OBJ_TYPE_ENTITY, obj_release, &ns);

	//Get initial id
	KUNIT_EXPECT_EQ(test, 0UL, isp_obj_id(obj));

	//Set id on non pol_user_id
	ret = isp_obj_set_id(obj, 42UL);
	KUNIT_EXPECT_NE(test, 0, ret);
	KUNIT_EXPECT_EQ(test, 0UL, isp_obj_id(obj));

	isp_obj_deinit(obj);
	isp_ns_release(&ns);
}

/* IDs must not overlap */
static void ids_checks(struct kunit *test)
{
	KUNIT_EXPECT_LT(test, ISP_NS_UNIQUE_ID_START, ISP_NS_UNIQUE_ID_END);
}

static void obj_add_user(struct kunit *test)
{
	struct isp_obj *obj, *obj2;
	unsigned long id;
	struct isp_ns ns;
	int ret;

	ret = isp_ns_init(&ns, ISP_NS_POL_USER_ID);
	KUNIT_ASSERT_EQ(test, ret, 0);

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	KUNIT_ASSERT_PTR_NE(test, obj, (struct isp_obj *)NULL);
	isp_obj_init(obj, ISP_OBJ_TYPE_ENTITY, obj_release, &ns);

	//Insert object 0
	ret = isp_obj_insert(obj);
	KUNIT_EXPECT_EQ(test, 0, ret);

	//Change an inserted object
	ret = isp_obj_set_id(obj, 1);
	KUNIT_EXPECT_NE(test, 0, ret);

	obj2 = kzalloc(sizeof(*obj), GFP_KERNEL);
	KUNIT_ASSERT_PTR_NE(test, obj2, (struct isp_obj *)NULL);
	isp_obj_init(obj2, ISP_OBJ_TYPE_ENTITY, obj_release, &ns);
	//Insert ducplicated id
	ret = isp_obj_move(obj2, &id);
	KUNIT_EXPECT_EQ(test, -EEXIST, ret);

	//Move object 1
	ret = isp_obj_set_id(obj2, 1);
	KUNIT_EXPECT_EQ(test, 0, ret);
	ret = isp_obj_move(obj2, &id);
	KUNIT_EXPECT_EQ(test, 0, ret);
	KUNIT_EXPECT_EQ(test, 1UL, id);

	isp_obj_remove(obj);
	isp_obj_deinit(obj);
	isp_ns_release(&ns);
}

static void obj_add_unique(struct kunit *test)
{
	struct isp_obj *obj, *obj2;
	unsigned long id;
	struct isp_ns ns;
	int ret;

	ret = isp_ns_init(&ns, ISP_NS_POL_UNIQUE_ID);
	KUNIT_ASSERT_EQ(test, ret, 0);

	//Move object in the ns and remove it
	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	KUNIT_ASSERT_PTR_NE(test, obj, (struct isp_obj *)NULL);
	isp_obj_init(obj, ISP_OBJ_TYPE_ENTITY, obj_release, &ns);
	ret = isp_obj_insert(obj);
	KUNIT_EXPECT_EQ(test, 0, ret);
	KUNIT_EXPECT_EQ(test, ns.next_id, ISP_NS_UNIQUE_ID_START+1);
	KUNIT_EXPECT_EQ(test, ISP_NS_UNIQUE_ID_START, isp_obj_id(obj));
	isp_obj_remove(obj);
	isp_obj_deinit(obj);

	KUNIT_EXPECT_EQ(test, ns.next_id, ISP_NS_UNIQUE_ID_START+1);

	//Move object into the ns and expect different id. then remove it
	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	KUNIT_ASSERT_PTR_NE(test, obj, (struct isp_obj *)NULL);
	isp_obj_init(obj, ISP_OBJ_TYPE_ENTITY, obj_release, &ns);
	ret = isp_obj_set_id(obj, 42UL);
	KUNIT_EXPECT_NE(test, 0, ret);
	ret = isp_obj_insert(obj);
	KUNIT_EXPECT_EQ(test, 0, ret);
	KUNIT_EXPECT_EQ(test, ISP_NS_UNIQUE_ID_START + 1, isp_obj_id(obj));
	isp_obj_remove(obj);
	isp_obj_deinit(obj);

	//Move object into the ns and expect different id
	obj2 = kzalloc(sizeof(*obj), GFP_KERNEL);
	KUNIT_ASSERT_PTR_NE(test, obj2, (struct isp_obj *)NULL);
	isp_obj_init(obj2, ISP_OBJ_TYPE_ENTITY, obj_release, &ns);
	ret = isp_obj_move(obj2, &id);
	KUNIT_EXPECT_EQ(test, 0, ret);
	KUNIT_EXPECT_EQ(test, ISP_NS_UNIQUE_ID_START + 2, id);

	isp_ns_release(&ns);
}

static void obj_add_invalid(struct kunit *test)
{
	struct isp_obj *obj;
	int ret;

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	KUNIT_ASSERT_PTR_NE(test, obj, (struct isp_obj *)NULL);
	ret = isp_obj_insert(obj);
	KUNIT_EXPECT_EQ(test, -EINVAL, ret);
	kfree(obj);
}

static void obj_lookup(struct kunit *test)
{
	struct isp_obj *obj;
	struct isp_obj *pobj;
	unsigned long id;
	struct isp_ns ns;
	int ret;

	ret = isp_ns_init(&ns, ISP_NS_POL_USER_ID);
	KUNIT_ASSERT_EQ(test, ret, 0);

	// lookup id not present
	pobj = isp_obj_lookup(&ns, ISP_OBJ_TYPE_ENTITY, 42);
	KUNIT_EXPECT_PTR_EQ(test, pobj, (struct isp_obj *)NULL);

	// lookup id
	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	KUNIT_ASSERT_PTR_NE(test, obj, (struct isp_obj *)NULL);
	isp_obj_init(obj, ISP_OBJ_TYPE_ENTITY, obj_release, &ns);
	ret = isp_obj_set_id(obj, 42UL);
	KUNIT_ASSERT_EQ(test, ret, 0);
	ret = isp_obj_move(obj, &id);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_ASSERT_EQ(test, id, 42UL);
	pobj = isp_obj_lookup(&ns, ISP_OBJ_TYPE_ENTITY, 42);
	KUNIT_EXPECT_PTR_EQ(test, pobj, obj);
	isp_obj_put(pobj);
	isp_obj_remove(obj);

	// lookup removed id
	pobj = isp_obj_lookup(&ns, ISP_OBJ_TYPE_ENTITY, 42);
	KUNIT_EXPECT_PTR_EQ(test, pobj, (struct isp_obj *)NULL);

	isp_ns_release(&ns);
}

static void obj_remove(struct kunit *test)
{
	struct isp_obj *obj;
	struct isp_obj *pobj;
	struct isp_ns ns;
	unsigned long id;
	int ret;

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	KUNIT_ASSERT_PTR_NE(test, obj, (struct isp_obj *)NULL);
	// remove not initialised object
	isp_obj_remove(obj);

	ret = isp_ns_init(&ns, ISP_NS_POL_UNIQUE_ID);
	KUNIT_ASSERT_EQ(test, ret, 0);

	// remove not added object
	release_flag = false;
	isp_obj_init(obj, ISP_OBJ_TYPE_ENTITY, obj_release, &ns);
	isp_obj_remove(obj);
	KUNIT_EXPECT_FALSE(test, release_flag);
	isp_obj_deinit(obj);
	KUNIT_EXPECT_TRUE(test, release_flag);

	// remove added object
	release_flag = false;
	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	isp_obj_init(obj, ISP_OBJ_TYPE_ENTITY, obj_release, &ns);
	ret = isp_obj_insert(obj);
	KUNIT_ASSERT_EQ(test, ret, 0);
	id = isp_obj_id(obj);
	isp_obj_remove(obj);
	isp_obj_deinit(obj);
	KUNIT_EXPECT_TRUE(test, release_flag);
	pobj = isp_obj_lookup(&ns, ISP_OBJ_TYPE_ENTITY, ISP_NS_UNIQUE_ID_END);
	KUNIT_EXPECT_PTR_EQ(test, (struct isp_obj *)NULL, pobj);

	isp_ns_release(&ns);
}

static void obj_remove_id(struct kunit *test)
{
	struct isp_obj *obj;
	struct isp_ns ns;
	unsigned long id;
	int ret;

	// remove invalid params
	ret = isp_obj_remove_id(NULL, ISP_OBJ_TYPE_ENTITY, 42);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	ret = isp_ns_init(&ns, ISP_NS_POL_UNIQUE_ID);
	KUNIT_ASSERT_EQ(test, ret, 0);

	// remove non existent obj
	ret = isp_obj_remove_id(&ns, ISP_OBJ_TYPE_ENTITY, 42);
	KUNIT_EXPECT_EQ(test, ret, -ENOENT);

	release_flag = false;
	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	KUNIT_ASSERT_PTR_NE(test, obj, (struct isp_obj *)NULL);
	isp_obj_init(obj, ISP_OBJ_TYPE_ENTITY, obj_release, &ns);
	ret = isp_obj_move(obj, &id);
	KUNIT_ASSERT_EQ(test, ret, 0);

	// remove invalid type
	ret = isp_obj_remove_id(&ns, ISP_OBJ_TYPE_ROOT, id);
	KUNIT_EXPECT_EQ(test, ret, -ENOENT);

	// normal remove
	ret = isp_obj_remove_id(&ns, ISP_OBJ_TYPE_ENTITY, id);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_TRUE(test, release_flag);

	//double remove
	release_flag = false;
	ret = isp_obj_remove_id(&ns, ISP_OBJ_TYPE_ENTITY, id);
	KUNIT_EXPECT_EQ(test, ret, -ENOENT);
	KUNIT_EXPECT_FALSE(test, release_flag);

	isp_ns_release(&ns);
}

static void obj_move(struct kunit *test)
{
	struct isp_obj *obj;
	struct isp_ns ns;
	unsigned long id;
	int ret;

	//Invalid
	ret = isp_obj_move(NULL, &id);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	//Non initialised
	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	KUNIT_ASSERT_PTR_NE(test, obj, (struct isp_obj *)NULL);
	ret = isp_obj_move(obj, &id);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	ret = isp_ns_init(&ns, ISP_NS_POL_USER_ID);
	KUNIT_ASSERT_EQ(test, ret, 0);

	//Normal insert
	release_flag = false;
	isp_obj_init(obj, ISP_OBJ_TYPE_ENTITY, obj_release, &ns);
	ret = isp_obj_move(obj, &id);
	KUNIT_EXPECT_EQ(test, ret, 0);

	//Double insert
	ret = isp_obj_move(obj, &id);
	KUNIT_EXPECT_EQ(test, ret, -EEXIST);

	//Release after remove
	ret = isp_obj_remove_id(&ns, ISP_OBJ_TYPE_ENTITY, id);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_TRUE(test, release_flag);

	//Null ID
	release_flag = false;
	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	KUNIT_ASSERT_PTR_NE(test, obj, (struct isp_obj *)NULL);
	isp_obj_init(obj, ISP_OBJ_TYPE_ENTITY, obj_release, &ns);
	ret = isp_obj_move(obj, NULL);
	KUNIT_EXPECT_EQ(test, ret, 0);

	//Release after ns_release
	KUNIT_EXPECT_FALSE(test, release_flag);
	isp_ns_release(&ns);
	KUNIT_EXPECT_TRUE(test, release_flag);
}

static void obj_insert(struct kunit *test)
{
	struct isp_obj *obj;
	struct isp_ns ns;
	int ret;

	//Invalid
	ret = isp_obj_insert(NULL);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	//Non initialised
	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	KUNIT_ASSERT_PTR_NE(test, obj, (struct isp_obj *)NULL);
	ret = isp_obj_insert(obj);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	ret = isp_ns_init(&ns, ISP_NS_POL_USER_ID);
	KUNIT_ASSERT_EQ(test, ret, 0);

	//Normal insert
	release_flag = false;
	isp_obj_init(obj, ISP_OBJ_TYPE_ENTITY, obj_release, &ns);
	ret = isp_obj_insert(obj);
	KUNIT_EXPECT_EQ(test, ret, 0);

	//Double insert
	ret = isp_obj_insert(obj);
	KUNIT_EXPECT_EQ(test, ret, -EEXIST);

	//Release after remove
	isp_obj_remove(obj);
	KUNIT_EXPECT_FALSE(test, release_flag);
	isp_obj_deinit(obj);
	KUNIT_EXPECT_TRUE(test, release_flag);

	//Release after ns_release
	release_flag = false;
	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	KUNIT_ASSERT_PTR_NE(test, obj, (struct isp_obj *)NULL);
	isp_obj_init(obj, ISP_OBJ_TYPE_ENTITY, obj_release, &ns);
	ret = isp_obj_insert(obj);
	KUNIT_EXPECT_EQ(test, ret, 0);

	KUNIT_EXPECT_FALSE(test, release_flag);
	isp_ns_release(&ns);
	KUNIT_EXPECT_FALSE(test, release_flag);
	isp_obj_deinit(obj);
	KUNIT_EXPECT_TRUE(test, release_flag);
}

struct for_each_test {
	unsigned long n_objs;
	unsigned long sum_ids;
};

static bool for_each_test_callback(struct isp_obj *obj,
				   struct isp_ns_walk_control *ctl)
{
	struct for_each_test *fet;

	fet = ctl->data;
	fet->n_objs++;
	fet->sum_ids += isp_obj_id(obj);
	return false;
}

#define N_OBJS 10UL
static void ns_for_each(struct kunit *test)
{
	struct isp_ns_walk_control ctl = {};
	struct for_each_test fet = {};
	struct isp_obj *obj;
	unsigned long sum_ids = 0;
	unsigned long id0, id;
	struct isp_ns ns;
	int ret;
	int i;

	// Invalid calls
	isp_ns_for_each(NULL, NULL);

	ret = isp_ns_init(&ns, ISP_NS_POL_UNIQUE_ID);
	KUNIT_ASSERT_EQ(test, ret, 0);

	// Invalid calls
	isp_ns_for_each(&ns, NULL);

	// Invalid calls
	isp_ns_for_each(&ns, &ctl);

	for (i = 0; i < N_OBJS; i++) {
		obj = kzalloc(sizeof(*obj), GFP_KERNEL);
		KUNIT_ASSERT_PTR_NE(test, obj, (struct isp_obj *)NULL);
		isp_obj_init(obj, ISP_OBJ_TYPE_ENTITY, obj_release, &ns);
		ret = isp_obj_move(obj, &id);
		KUNIT_ASSERT_EQ(test, ret, 0);
		KUNIT_ASSERT_PTR_NE(test, obj, (struct isp_obj *)NULL);
		sum_ids += id;
		if (i == 1)
			id0 = id;
	}

	// Valid call
	ctl.data = &fet;
	ctl.cb = for_each_test_callback;
	isp_ns_for_each(&ns, &ctl);
	KUNIT_EXPECT_EQ(test, fet.n_objs, N_OBJS);
	KUNIT_EXPECT_EQ(test, fet.sum_ids, sum_ids);

	// Valid call - 1
	sum_ids -= id0;
	isp_obj_remove_id(&ns, ISP_OBJ_TYPE_ENTITY, id0);
	memset(&fet, 0, sizeof(fet));
	isp_ns_for_each(&ns, &ctl);
	KUNIT_EXPECT_EQ(test, fet.n_objs, N_OBJS - 1);
	KUNIT_EXPECT_EQ(test, fet.sum_ids, sum_ids);

	isp_ns_release(&ns);
}

static struct kunit_case isp_namespace_test_cases[] = {
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

static struct kunit_suite isp_namespace_test_suite = {
	.name = "isp_namespace_test",
	.test_cases = isp_namespace_test_cases,
};

kunit_test_suites(&isp_namespace_test_suite);

MODULE_LICENSE("GPL v2");
