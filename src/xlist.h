#ifndef __XLIST_H__	
#define __XLIST_H__
#include <assert.h>

 /* piggy_xrh@163.com <lst interface> */

#define XOFFSET(cs,m)       (size_t)(&(((cs*)0)->m))
#define XCOBJ(mptr,cs)      ((cs *)((size_t)mptr - XOFFSET(cs,link)))
#define XCOBJEX(mptr,cs, m) ((cs *)((size_t)mptr - XOFFSET(cs,m)))

struct xlink {
	struct xlink  *pre;
	struct xlink  *next;
};

typedef struct {
	struct xlink  *link;
	int     size;
} XLIST;

#define LIST_VERIFY_ENABLE
#ifdef LIST_VERIFY_ENABLE
#define LIST_assert(expr) assert(expr)
#else
#define LIST_assert(expr) 
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define XLIST_INIT(lst) \
	do {\
		(lst)->link = NULL; \
		(lst)->size = 0; \
	} while(0)

#define XLIST_SIZE(lst) ((lst)->size)
#define XLIST_EMPTY(lst) (0 == (lst)->size)
#define XLIST_FRONT(lst) ((lst)->link)
#define XLIST_BACK(lst)  (XLIST_FRONT(lst)->pre)
#define XLIST_NEXT(link)  ((link)->next)
#define XLIST_PRE(link)   ((link)->pre)
#define XLIST_SWAP(lst1, lst2) \
	do {\
		XLIST temp = *(lst1); \
		*(lst1) = *(lst2); \
		*(lst2) = temp; \
	} while (0)

#define XLIST_MERGE(lstDst, lstSrc) \
	do {\
		if ((lstSrc)->size) {\
			if (!(lstDst)->size) \
				XLIST_SWAP(lstDst, lstSrc); \
			else { \
				 struct xlink *dstb, *dstf , *srcb, *srcf;\
				 (lstDst)->size += (lstSrc)->size;\
				dstb = XLIST_BACK(lstDst);\
				dstf = XLIST_FRONT(lstDst);\
				srcb = XLIST_BACK(lstSrc);\
				srcf = XLIST_FRONT(lstSrc);\
				dstb->next = srcf;\
				srcf->pre  = dstb;\
				dstf->pre  = srcb;\
				srcb->next = dstf;\
				XLIST_INIT(lstSrc); \
			} \
		} \
	} while (0)

#define XLIST_FOREACH_EX(lst, link, pplink) \
	for ((*pplink)=(link); *(pplink); *(pplink)=((link) == (*(pplink))->next ? NULL : (*(pplink))->next))

#define XLIST_RFOREACH_EX(lst, link, pplink) \
	for ((*pplink)=(link); *(pplink); *(pplink)=((link) == (*(pplink))->pre ? NULL : (*(pplink))->pre))

#define XLIST_FOREACH(lst, pplink)  XLIST_FOREACH_EX(lst, XLIST_FRONT(lst), pplink)
#define XLIST_RFOREACH(lst, pplink) XLIST_RFOREACH_EX(lst, XLIST_BACK(lst), pplink)

static int XLIST_belong(XLIST *lst, struct xlink *link) {
	struct xlink *iter;

	XLIST_FOREACH(lst, &iter) 
		if (link == iter)
			break;
	
	return (int)iter;
}

#define XLIST_PUSHBACK(lst, node) \
	do {\
		LIST_assert(!XLIST_belong(lst, node)); \
		if (XLIST_EMPTY(lst)) {\
			(lst)->link = node; \
			(node)->pre = (node)->next = node; \
		} else { \
			(node)->pre  = XLIST_BACK(lst); \
			(node)->next = XLIST_FRONT(lst); \
			XLIST_BACK(lst)->next = node; \
			XLIST_FRONT(lst)->pre = node; \
		}\
		++ (lst)->size; \
		LIST_assert(XLIST_belong(lst, node)); \
	} while(0)

#define XLIST_PUSHFRONT(lst, node)\
	do {\
		LIST_assert(!XLIST_belong(lst, node)); \
		if (XLIST_EMPTY(lst))  \
			(node)->pre  = (node)->next = node; \
		else {\
			(node)->pre  = XLIST_BACK(lst);\
			(node)->next = XLIST_FRONT(lst); \
			XLIST_BACK(lst)->next = node; \
			XLIST_FRONT(lst)->pre = node; \
		}\
		(lst)->link = node; \
		++ (lst)->size;\
		LIST_assert(XLIST_belong(lst, node));\
	} while(0)


#define XLIST_INSERTBEFORE(lst, pos, link)\
	do {\
		LIST_assert(pos && XLIST_belong(lst, pos) && !XLIST_belong(lst, link)); \
		if (XLIST_FRONT(lst) == (pos)) \
			XLIST_PUSHFRONT(lst, link);\
		else { \
			(link)->pre  = (pos)->pre; \
			(link)->next = pos; \
			(pos)->pre->next = link;\
			(pos)->pre   = link; \
			++ (lst)->size; \
		}\
		LIST_assert(XLIST_belong(lst, pos) && XLIST_belong(lst, link));\
	} while(0)

#define XLIST_INSERTAFTER(lst, pos, link) \
	do {\
		LIST_assert(pos && XLIST_belong(lst, pos) && !XLIST_belong(lst, link)); \
		if (XLIST_BACK(lst) == pos) {\
			XLIST_PUSHBACK(lst, link); \
		} else {\
			(link)->pre  = pos;\
			(link)->next = (pos)->next;\
			(pos)->next->pre = link;\
			(pos)->next  = link; \
			++ (lst)->size; \
		}\
		LIST_assert(XLIST_belong(lst, link) && XLIST_belong(lst, pos));\
	} while(0)

#define XLIST_REMOVE(lst, rmlink) \
	do {\
		LIST_assert((lst)->size > 0 && XLIST_belong(lst, rmlink)); \
		if (0 == -- (lst)->size) \
			(lst)->link = NULL;\
		else  {\
			(rmlink)->pre->next = (rmlink)->next; \
			(rmlink)->next->pre = (rmlink)->pre; \
			if ((lst)->link == (rmlink))\
				(lst)->link = (rmlink)->next;\
		} \
		LIST_assert((lst)->size >= 0 && !XLIST_belong(lst, rmlink)); \
	} while(0)

#define XLIST_REMOVE2(lst, rmlink, n) \
	do {\
		struct xlink *_link;\
		LIST_assert((lst)->size > 0 && XLIST_belong(lst, rmlink)); \
		_link = ((lst)->size > 1) ? (rmlink)->next : NULL; \
		if (0 == -- (lst)->size) \
			(lst)->link = NULL;\
		else  {\
			(rmlink)->pre->next = (rmlink)->next; \
			(rmlink)->next->pre = (rmlink)->pre; \
			if ((lst)->link == (rmlink))\
				(lst)->link = (rmlink)->next;\
		} \
		LIST_assert((lst)->size >= 0 && !XLIST_belong(lst, rmlink)); \
		(n) = _link;\
	} while(0)

#define XLIST_POPFRONT(lst, link) \
	do {\
		LIST_assert((lst)->size > 0);\
		(link) = XLIST_FRONT(lst); \
		XLIST_REMOVE(lst, link); \
	} while(0)

#define XLIST_POPBACK(lst, link) \
	do {\
		LIST_assert((lst)->size > 0); \
		(link) = XLIST_BACK(lst); \
		XLIST_REMOVE(lst, link); \
	} while (0)
		
#ifdef __cplusplus
}
#endif

#endif
