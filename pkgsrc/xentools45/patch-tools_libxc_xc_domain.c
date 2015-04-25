$NetBSD: patch-xen_common_domctl.c.c,v 1.1 2015/04/24 13:10:01 dfs Exp $

From 98670acc98cad5aee0e0714694a64d3b96675c36 Mon Sep 17 00:00:00 2001
From: Konrad Rzeszutek Wilk <konrad.wilk@oracle.com>
Date: Wed, 19 Nov 2014 12:57:11 -0500
Subject: [PATCH] Limit XEN_DOMCTL_memory_mapping hypercall to only process up
 to 64 GFNs (or less)

Said hypercall for large BARs can take quite a while. As such
we can require that the hypercall MUST break up the request
in smaller values.

Another approach is to add preemption to it - whether we do the
preemption using hypercall_create_continuation or returning
EAGAIN to userspace (and have it re-invocate the call) - either
way the issue we cannot easily solve is that in 'map_mmio_regions'
if we encounter an error we MUST call 'unmap_mmio_regions' for the
whole BAR region.

Since the preemption would re-use input fields such as nr_mfns,
first_gfn, first_mfn - we would lose the original values -
and only undo what was done in the current round (i.e. ignoring
anything that was done prior to earlier preemptions).

Unless we re-used the return value as 'EAGAIN|nr_mfns_done<<10' but
that puts a limit (since the return value is a long) on the amount
of nr_mfns that can provided.

This patch sidesteps this problem by:
 - Setting an hard limit of nr_mfns having to be 64 or less.
 - Toolstack adjusts correspondingly to the nr_mfn limit.
 - If the there is an error when adding the toolstack will call the
   remove operation to remove the whole region.

The need to break this hypercall down is for large BARs can take
more than the guest (initial domain usually) time-slice. This has
the negative result in that the guest is locked out for a long
duration and is unable to act on any pending events.

We also augment the code to return zero if nr_mfns instead
of trying to the hypercall.

Suggested-by: Jan Beulich <jbeulich@suse.com>
Acked-by: Jan Beulich <jbeulich@suse.com>
Signed-off-by: Konrad Rzeszutek Wilk <konrad.wilk@oracle.com>
Acked-by: Ian Campbell <ian.campbell@citrix.com>

--- libxc/xc_domain.c.orig
+++ libxc/xc_domain.c
@@ -1988,6 +1988,8 @@ int xc_domain_memory_mapping(
 {
     DECLARE_DOMCTL;
     xc_dominfo_t info;
+    int ret = 0, err;
+    unsigned long done = 0, nr, max_batch_sz;
 
     if ( xc_domain_getinfo(xch, domid, 1, &info) != 1 ||
          info.domid != domid )
@@ -1998,14 +2000,50 @@ int xc_domain_memory_mapping(
     if ( !xc_core_arch_auto_translated_physmap(&info) )
         return 0;
 
+    if ( !nr_mfns )
+        return 0;
+
     domctl.cmd = XEN_DOMCTL_memory_mapping;
     domctl.domain = domid;
-    domctl.u.memory_mapping.first_gfn = first_gfn;
-    domctl.u.memory_mapping.first_mfn = first_mfn;
-    domctl.u.memory_mapping.nr_mfns = nr_mfns;
     domctl.u.memory_mapping.add_mapping = add_mapping;
+    max_batch_sz = nr_mfns;
+    do
+    {
+        nr = min(nr_mfns - done, max_batch_sz);
+        domctl.u.memory_mapping.nr_mfns = nr;
+        domctl.u.memory_mapping.first_gfn = first_gfn + done;
+        domctl.u.memory_mapping.first_mfn = first_mfn + done;
+        err = do_domctl(xch, &domctl);
+        if ( err && errno == E2BIG )
+        {
+            if ( max_batch_sz <= 1 )
+                break;
+            max_batch_sz >>= 1;
+            continue;
+        }
+        /* Save the first error... */
+        if ( !ret )
+            ret = err;
+        /* .. and ignore the rest of them when removing. */
+        if ( err && add_mapping != DPCI_REMOVE_MAPPING )
+            break;
 
-    return do_domctl(xch, &domctl);
+        done += nr;
+    } while ( done < nr_mfns );
+
+    /*
+     * Undo what we have done unless unmapping, by unmapping the entire region.
+     * Errors here are ignored.
+     */
+    if ( ret && add_mapping != DPCI_REMOVE_MAPPING )
+        xc_domain_memory_mapping(xch, domid, first_gfn, first_mfn, nr_mfns,
+                                 DPCI_REMOVE_MAPPING);
+
+    /* We might get E2BIG so many times that we never advance. */
+    if ( !done && !ret )
+        ret = -1;
+
+    return ret;
 }
 
 int xc_domain_ioport_mapping(

--- ../xen/common/domctl.c.orig
+++ ../xen/common/domctl.c
@@ -1027,6 +1027,11 @@ long do_domctl(XEN_GUEST_HANDLE_PARAM(xen_domctl_t) u_domctl)
              (gfn + nr_mfns - 1) < gfn ) /* wrap? */
             break;

+        ret = -E2BIG;
+        /* Must break hypercall up as this could take a while. */
+        if ( nr_mfns > 64 )
+            break;
+
         ret = -EPERM;
         if ( !iomem_access_permitted(current->domain, mfn, mfn_end) ||
              !iomem_access_permitted(d, mfn, mfn_end) )

--- ../xen/include/public/domctl.h.orig
+++ ../xen/include/public/domctl.h
@@ -543,6 +543,7 @@ DEFINE_XEN_GUEST_HANDLE(xen_domctl_bind_pt_irq_t);
 
 
 /* Bind machine I/O address range -> HVM address range. */
+/* If this returns -E2BIG lower nr_mfns value. */
 /* XEN_DOMCTL_memory_mapping */
 #define DPCI_ADD_MAPPING         1
 #define DPCI_REMOVE_MAPPING      0
