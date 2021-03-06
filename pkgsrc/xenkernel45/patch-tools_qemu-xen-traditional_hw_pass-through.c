$NetBSD: patch-tools_qemu-xen-traditional_hw_pass-through.c,v 1.1 2015/04/24 13:01:10 dfs Exp $

--- tools/qemu-xen-traditional/hw/pass-through.c.orig	2015-04-24 13:01:10.000000000 +0100
+++ tools/qemu-xen-traditional/hw/pass-through.c	2015-04-24 13:01:11.000000000 +0100
@@ -172,9 +172,6 @@ static int pt_word_reg_read(struct pt_de
 static int pt_long_reg_read(struct pt_dev *ptdev,
     struct pt_reg_tbl *cfg_entry,
     uint32_t *value, uint32_t valid_mask);
-static int pt_cmd_reg_read(struct pt_dev *ptdev,
-    struct pt_reg_tbl *cfg_entry,
-    uint16_t *value, uint16_t valid_mask);
 static int pt_bar_reg_read(struct pt_dev *ptdev,
     struct pt_reg_tbl *cfg_entry,
     uint32_t *value, uint32_t valid_mask);
@@ -286,9 +283,9 @@ static struct pt_reg_info_tbl pt_emu_reg
         .size       = 2,
         .init_val   = 0x0000,
         .ro_mask    = 0xF880,
-        .emu_mask   = 0x0740,
+        .emu_mask   = 0x0743,
         .init       = pt_common_reg_init,
-        .u.w.read   = pt_cmd_reg_read,
+        .u.w.read   = pt_word_reg_read,
         .u.w.write  = pt_cmd_reg_write,
         .u.w.restore  = pt_cmd_reg_restore,
     },
@@ -1905,7 +1902,7 @@ static int pt_dev_is_virtfn(struct pci_d
     return rc;
 }
 
-static int pt_register_regions(struct pt_dev *assigned_device)
+static int pt_register_regions(struct pt_dev *assigned_device, uint16_t *cmd)
 {
     int i = 0;
     uint32_t bar_data = 0;
@@ -1925,17 +1922,26 @@ static int pt_register_regions(struct pt
 
             /* Register current region */
             if ( pci_dev->base_addr[i] & PCI_ADDRESS_SPACE_IO )
+            {
                 pci_register_io_region((PCIDevice *)assigned_device, i,
                     (uint32_t)pci_dev->size[i], PCI_ADDRESS_SPACE_IO,
                     pt_ioport_map);
+                *cmd |= PCI_COMMAND_IO;
+            }
             else if ( pci_dev->base_addr[i] & PCI_ADDRESS_SPACE_MEM_PREFETCH )
+            {
                 pci_register_io_region((PCIDevice *)assigned_device, i,
                     (uint32_t)pci_dev->size[i], PCI_ADDRESS_SPACE_MEM_PREFETCH,
                     pt_iomem_map);
+                *cmd |= PCI_COMMAND_MEMORY;
+            }
             else
+            {
                 pci_register_io_region((PCIDevice *)assigned_device, i,
                     (uint32_t)pci_dev->size[i], PCI_ADDRESS_SPACE_MEM,
                     pt_iomem_map);
+                *cmd |= PCI_COMMAND_MEMORY;
+            }
 
             PT_LOG("IO region registered (size=0x%08x base_addr=0x%08x)\n",
                 (uint32_t)(pci_dev->size[i]),
@@ -3263,27 +3269,6 @@ static int pt_long_reg_read(struct pt_de
    return 0;
 }
 
-/* read Command register */
-static int pt_cmd_reg_read(struct pt_dev *ptdev,
-        struct pt_reg_tbl *cfg_entry,
-        uint16_t *value, uint16_t valid_mask)
-{
-    struct pt_reg_info_tbl *reg = cfg_entry->reg;
-    uint16_t valid_emu_mask = 0;
-    uint16_t emu_mask = reg->emu_mask;
-
-    if ( ptdev->is_virtfn )
-        emu_mask |= PCI_COMMAND_MEMORY;
-    if ( pt_is_iomul(ptdev) )
-        emu_mask |= PCI_COMMAND_IO;
-
-    /* emulate word register */
-    valid_emu_mask = emu_mask & valid_mask;
-    *value = PT_MERGE_VALUE(*value, cfg_entry->data, ~valid_emu_mask);
-
-    return 0;
-}
-
 /* read BAR */
 static int pt_bar_reg_read(struct pt_dev *ptdev,
         struct pt_reg_tbl *cfg_entry,
@@ -3418,19 +3403,13 @@ static int pt_cmd_reg_write(struct pt_de
     uint16_t writable_mask = 0;
     uint16_t throughable_mask = 0;
     uint16_t wr_value = *value;
-    uint16_t emu_mask = reg->emu_mask;
-
-    if ( ptdev->is_virtfn )
-        emu_mask |= PCI_COMMAND_MEMORY;
-    if ( pt_is_iomul(ptdev) )
-        emu_mask |= PCI_COMMAND_IO;
 
     /* modify emulate register */
     writable_mask = ~reg->ro_mask & valid_mask;
     cfg_entry->data = PT_MERGE_VALUE(*value, cfg_entry->data, writable_mask);
 
     /* create value for writing to I/O device register */
-    throughable_mask = ~emu_mask & valid_mask;
+    throughable_mask = ~reg->emu_mask & valid_mask;
 
     if (*value & PCI_COMMAND_DISABLE_INTx)
     {
@@ -4211,6 +4190,7 @@ static struct pt_dev * register_real_dev
     struct pt_dev *assigned_device = NULL;
     struct pci_dev *pci_dev;
     uint8_t e_device, e_intx;
+    uint16_t cmd = 0;
     char *key, *val;
     int msi_translate, power_mgmt;
 
@@ -4300,7 +4280,7 @@ static struct pt_dev * register_real_dev
         assigned_device->dev.config[i] = pci_read_byte(pci_dev, i);
 
     /* Handle real device's MMIO/PIO BARs */
-    pt_register_regions(assigned_device);
+    pt_register_regions(assigned_device, &cmd);
 
     /* Setup VGA bios for passthroughed gfx */
     if ( setup_vga_pt(assigned_device) < 0 )
@@ -4378,6 +4358,10 @@ static struct pt_dev * register_real_dev
     }
 
 out:
+    if (cmd)
+        pci_write_word(pci_dev, PCI_COMMAND,
+            *(uint16_t *)(&assigned_device->dev.config[PCI_COMMAND]) | cmd);
+
     PT_LOG("Real physical device %02x:%02x.%x registered successfuly!\n"
            "IRQ type = %s\n", r_bus, r_dev, r_func,
            assigned_device->msi_trans_en? "MSI-INTx":"INTx");
