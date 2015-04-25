$NetBSD: patch-tools_qemu-xen_hw_xen_pt.c,v 1.1 2015/04/24 13:01:10 dfs Exp $

--- tools/qemu-xen/hw/xen/xen_pt.c.orig	2015-04-24 13:01:10.000000000 +0100
+++ tools/qemu-xen/hw/xen/xen_pt.c	2015-04-24 13:01:11.000000000 +0100
@@ -388,7 +388,7 @@ static const MemoryRegionOps ops = {
     .write = xen_pt_bar_write,
 };
 
-static int xen_pt_register_regions(XenPCIPassthroughState *s)
+static int xen_pt_register_regions(XenPCIPassthroughState *s, uint16_t *cmd)
 {
     int i = 0;
     XenHostPCIDevice *d = &s->real_device;
@@ -406,6 +406,7 @@ static int xen_pt_register_regions(XenPC
 
         if (r->type & XEN_HOST_PCI_REGION_TYPE_IO) {
             type = PCI_BASE_ADDRESS_SPACE_IO;
+            *cmd |= PCI_COMMAND_IO;
         } else {
             type = PCI_BASE_ADDRESS_SPACE_MEMORY;
             if (r->type & XEN_HOST_PCI_REGION_TYPE_PREFETCH) {
@@ -414,6 +415,7 @@ static int xen_pt_register_regions(XenPC
             if (r->type & XEN_HOST_PCI_REGION_TYPE_MEM_64) {
                 type |= PCI_BASE_ADDRESS_MEM_TYPE_64;
             }
+            *cmd |= PCI_COMMAND_MEMORY;
         }
 
         memory_region_init_io(&s->bar[i], OBJECT(s), &ops, &s->dev,
@@ -638,6 +640,7 @@ static int xen_pt_initfn(PCIDevice *d)
     XenPCIPassthroughState *s = DO_UPCAST(XenPCIPassthroughState, dev, d);
     int rc = 0;
     uint8_t machine_irq = 0;
+    uint16_t cmd = 0;
     int pirq = XEN_PT_UNASSIGNED_PIRQ;
 
     /* register real device */
@@ -672,7 +675,7 @@ static int xen_pt_initfn(PCIDevice *d)
     s->io_listener = xen_pt_io_listener;
 
     /* Handle real device's MMIO/PIO BARs */
-    xen_pt_register_regions(s);
+    xen_pt_register_regions(s, &cmd);
 
     /* reinitialize each config register to be emulated */
     if (xen_pt_config_init(s)) {
@@ -736,6 +739,11 @@ static int xen_pt_initfn(PCIDevice *d)
     }
 
 out:
+    if (cmd) {
+        xen_host_pci_set_word(&s->real_device, PCI_COMMAND,
+                              pci_get_word(d->config + PCI_COMMAND) | cmd);
+    }
+
     memory_listener_register(&s->memory_listener, &address_space_memory);
     memory_listener_register(&s->io_listener, &address_space_io);
     XEN_PT_LOG(d,
--- tools/qemu-xen/hw/xen/xen_pt_config_init.c.orig
+++ tools/qemu-xen/hw/xen/xen_pt_config_init.c
@@ -286,23 +286,6 @@ static int xen_pt_irqpin_reg_init(XenPCI
 }
 
 /* Command register */
-static int xen_pt_cmd_reg_read(XenPCIPassthroughState *s, XenPTReg *cfg_entry,
-                               uint16_t *value, uint16_t valid_mask)
-{
-    XenPTRegInfo *reg = cfg_entry->reg;
-    uint16_t valid_emu_mask = 0;
-    uint16_t emu_mask = reg->emu_mask;
-
-    if (s->is_virtfn) {
-        emu_mask |= PCI_COMMAND_MEMORY;
-    }
-
-    /* emulate word register */
-    valid_emu_mask = emu_mask & valid_mask;
-    *value = XEN_PT_MERGE_VALUE(*value, cfg_entry->data, ~valid_emu_mask);
-
-    return 0;
-}
 static int xen_pt_cmd_reg_write(XenPCIPassthroughState *s, XenPTReg *cfg_entry,
                                 uint16_t *val, uint16_t dev_value,
                                 uint16_t valid_mask)
@@ -310,18 +293,13 @@ static int xen_pt_cmd_reg_write(XenPCIPa
     XenPTRegInfo *reg = cfg_entry->reg;
     uint16_t writable_mask = 0;
     uint16_t throughable_mask = 0;
-    uint16_t emu_mask = reg->emu_mask;
-
-    if (s->is_virtfn) {
-        emu_mask |= PCI_COMMAND_MEMORY;
-    }
 
     /* modify emulate register */
     writable_mask = ~reg->ro_mask & valid_mask;
     cfg_entry->data = XEN_PT_MERGE_VALUE(*val, cfg_entry->data, writable_mask);
 
     /* create value for writing to I/O device register */
-    throughable_mask = ~emu_mask & valid_mask;
+    throughable_mask = ~reg->emu_mask & valid_mask;
 
     if (*val & PCI_COMMAND_INTX_DISABLE) {
         throughable_mask |= PCI_COMMAND_INTX_DISABLE;
@@ -605,9 +583,9 @@ static XenPTRegInfo xen_pt_emu_reg_heade
         .size       = 2,
         .init_val   = 0x0000,
         .ro_mask    = 0xF880,
-        .emu_mask   = 0x0740,
+        .emu_mask   = 0x0743,
         .init       = xen_pt_common_reg_init,
-        .u.w.read   = xen_pt_cmd_reg_read,
+        .u.w.read   = xen_pt_word_reg_read,
         .u.w.write  = xen_pt_cmd_reg_write,
     },
     /* Capabilities Pointer reg */
