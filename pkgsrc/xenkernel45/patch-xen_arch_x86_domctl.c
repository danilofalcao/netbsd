$NetBSD: patch-xen_arch_x86_domctl.c,v 1.1 2015/04/24 13:01:10 dfs Exp $

--- xen/arch/x86/domctl.c.orig	2015-04-24 13:01:10.000000000 +0100
+++ xen/arch/x86/domctl.c	2015-04-24 13:01:11.000000000 +0100
domctl: don't allow a toolstack domain to call domain_pause() on itself

These DOMCTL subops were accidentally declared safe for disaggregation
in the wake of XSA-77.

This is XSA-127.

Signed-off-by: Andrew Cooper <andrew.cooper3@citrix.com>
Reviewed-by: Jan Beulich <jbeulich@suse.com>
Acked-by: Ian Campbell <ian.campbell@citrix.com>

--- xen/arch/x86/domctl.c.orig
+++ xen/arch/x86/domctl.c
@@ -888,6 +888,10 @@ long arch_do_domctl(
     {
         xen_guest_tsc_info_t info;
 
+        ret = -EINVAL;
+        if ( d == current->domain ) /* no domain_pause() */
+            break;
+
         domain_pause(d);
         tsc_get_info(d, &info.tsc_mode,
                         &info.elapsed_nsec,
@@ -903,6 +907,10 @@ long arch_do_domctl(
 
     case XEN_DOMCTL_settscinfo:
     {
+        ret = -EINVAL;
+        if ( d == current->domain ) /* no domain_pause() */
+            break;
+
         domain_pause(d);
         tsc_set_info(d, domctl->u.tsc_info.info.tsc_mode,
                      domctl->u.tsc_info.info.elapsed_nsec,
--- xen/common/domctl.c.orig
+++ xen/common/domctl.c
@@ -522,8 +522,10 @@ long do_domctl(XEN_GUEST_HANDLE_PARAM(xe
 
     case XEN_DOMCTL_resumedomain:
     {
-        domain_resume(d);
-        ret = 0;
+        if ( d == current->domain ) /* no domain_pause() */
+            ret = -EINVAL;
+        else
+            domain_resume(d);
     }
     break;
 
