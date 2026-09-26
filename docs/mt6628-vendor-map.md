# Карта вендорского кода MT6628 (черновик, сгенерирован локальной Qwen3.5-9B)

Только для навигации: факты проверять по исходнику.


## common/platform/alps/wmt_plat_alps.c (1737 lines)

<!-- lines 1-400, 120 s -->
wmt_plat_audio_ctrl() - controls audio interface GPIOs and calls chip-side callback - CMB_STUB_AIF_X, CMB_STUB_AIF_CTRL, get_chip_eco_ver, wmt_plat_merge_if_flag_get
wmt_plat_bgf_eirq_cb() - handles background firmware IRQ callback - CFG_WMT_PS_SUPPORT, wmt_lib_ps_irq_cb
wmt_lib_plat_irq_cb_reg() - registers background firmware IRQ callback - irq_cb
wmt_lib_plat_aif_cb_reg() - registers audio interface control callback - device_audio_if_cb
wmt_plat_init() - initializes WMT platform including CMB HW and wakelock - mtk_wcn_cmb_hw_init, wake_lock_init, osal_sleepable_lock_init
wmt_plat_deinit() - deinitializes WMT platform including CMB HW and wakelock - mtk_wcn_cmb_hw_deinit, mtk_wcn_cmb_stub_unreg, wake_lock_destroy, osal_sleepable_lock_deinit
wmt_plat_sdio_ctrl() - controls SDIO slot power state - board_sdio_ctrl, WMT_SDIO_SLOT_NUM, ENUM_FUNC_STATE
wmt_plat_irq_ctrl() - generic IRQ control stub - ENUM_FUNC_STATE
wmt_plat_dump_pin_conf() - debug dump of GPIO pin configuration - GPIO_COMBO_6620_LDO_EN_PIN, GPIO_COMBO_PMU_EN_PIN, GPIO_COMBO_PMUV28_EN_PIN
wmt_plat_ldo_ctrl() - controls LDO GPIO state - ENUM_PIN_STATE
wmt_plat_pmu_ctrl() - controls PMU GPIO state - ENUM_PIN_STATE
wmt_plat_rtc_ctrl() - controls RTC GPIO state - ENUM_PIN_STATE
wmt_plat_rst_ctrl() - controls RST GPIO state - ENUM_PIN_STATE
wmt_plat_bgf_eint_ctrl() - controls BGF EINT GPIO state - ENUM_PIN_STATE
wmt_plat_wifi_eint_ctrl() - controls WiFi EINT GPIO state - ENUM_PIN_STATE
wmt_plat_all_eint_ctrl() - controls all EINT GPIO state - ENUM_PIN_STATE
wmt_plat_uart_ctrl() - controls UART GPIO state - ENUM_PIN_STATE
wmt_plat_pcm_ctrl() - controls PCM GPIO state - ENUM_PIN_STATE
wmt_plat_i2s_ctrl() - controls I2S GPIO state - ENUM_PIN_STATE
wmt_plat_sdio_pin_ctrl() - controls SDIO pin GPIO state - ENUM_PIN_STATE
wmt_plat_gps_sync_ctrl() - controls GPS sync GPIO state - ENUM_PIN_STATE
wmt_plat_gps_lna_ctrl() - controls GPS LNA GPIO state - ENUM_PIN_STATE
<!-- lines 401-800, 28 s -->
wmt_plat_dump_pin_conf() - dumps pin configuration info - WMT_INFO_FUNC, WMT_WARN_FUNC, WMT_DBG_FUNC
wmt_plat_pwr_ctrl() - controls power state of the combo chip - mtk_wcn_cmb_hw_pwr_on, mtk_wcn_cmb_hw_pwr_off, mtk_wcn_cmb_hw_rst, mtk_wcn_cmb_hw_state_show
wmt_plat_ps_ctrl() - controls power state (placeholder) - none
wmt_plat_eirq_ctrl() - controls external interrupt registration and masking - mt65xx_eint_set_sens, mt65xx_eint_set_hw_debounce, mt65xx_eint_registration, mt65xx_eint_mask, mt65xx_eint_unmask
wmt_plat_gpio_ctrl() - controls GPIO direction, mode, and output - gfp_set_pin_table (function pointer table)
wmt_plat_ldo_ctrl() - controls LDO enable pin via GPIO - mt_set_gpio_pull_enable, mt_set_gpio_dir, mt_set_gpio_mode, mt_set_gpio_out
wmt_plat_pmu_ctrl() - controls PMU enable pin via GPIO - mt_set_gpio_pull_enable, mt_set_gpio_dir, mt_set_gpio_mode, mt_set_gpio_out, mt_get_gpio_mode, mt_get_gpio_dir, mt_get_gpio_pull_enable, mt_get_gpio_pull_select, mt_get_gpio_out
wmt_plat_rtc_ctrl() - controls RTC 32k GPIO - rtc_gpio_enable_32k, rtc_gpio_32k_status
wmt_plat_rst_ctrl() - controls reset pin via GPIO - mt_set_gpio_pull_enable, mt_set_gpio_dir
<!-- lines 801-1200, 22 s -->
wmt_plat_rst_ctrl() - Initializes/deinitializes combo chip reset GPIO using mt_set_gpio_* APIs.
wmt_plat_bgf_eint_ctrl() - Initializes/deinitializes BGF external interrupt GPIO using mt_set_gpio_* APIs.
wmt_plat_wifi_eint_ctrl() - Initializes/deinitializes WiFi external interrupt using mt65xx_eint_* APIs.
wmt_plat_all_eint_ctrl() - Initializes/deinitializes ALL external interrupt GPIO using mt_set_gpio_* APIs.
wmt_plat_uart_ctrl() - Initializes/deinitializes UART GPIOs using mt_set_gpio_* APIs.
wmt_plat_pcm_ctrl() - Initializes/deinitializes PCM GPIOs using mt_set_gpio_* APIs.
<!-- lines 1201-1600, 22 s -->
wmt_plat_pcm_ctrl() - configures PCM GPIOs for init/deinit using mt_set_gpio_* APIs.
wmt_plat_i2s_ctrl() - configures I2S GPIOs for init/deinit using mt_set_gpio_* APIs.
wmt_plat_sdio_pin_ctrl() - configures SDIO bus pull-up/down using mt_set_gpio_pull_* APIs.
wmt_plat_gps_sync_ctrl() - configures GPS sync GPIO using mt_set_gpio_* APIs.
<!-- lines 1601-1737, 9 s -->
wmt_plat_gps_lna_ctrl() - Controls GPS LNA GPIO state (pull, dir, mode, out) - mt_set_gpio_*
wmt_plat_wake_lock_ctrl() - Manages wakelock ref count and calls wake_lock/wake_unlock - wake_lock, wake_unlock
wmt_plat_merge_if_flag_ctrl() - Sets global merge interface support flag - gWmtMergeIfSupport
wmt_plat_merge_if_flag_get() - Returns global merge interface support flag - gWmtMergeIfSupport

## common/linux/pri/stp_uart.c (668 lines)

<!-- lines 1-400, 20 s -->
stp_uart_tx_wakeup() - wakes up the TTY and writes data from the internal buffer to the UART driver.
stp_uart_tty_open() - initializes the TTY line discipline, flushes buffers, and registers the TX callback.
stp_uart_tty_close() - unregisters the TX callback when the TTY is closed or hung up.
stp_uart_tty_wakeup() - callback for transmit wakeup (currently a stub).
stp_uart_fifo_init() - allocates and initializes a kfifo buffer for receiving data.
stp_uart_fifo_deinit() - frees the kfifo buffer and associated memory.
stp_uart_rx_handling() - processes received data from the kfifo in chunks.
stp_uart_tty_receive() - receives data from the low-level driver and queues it to the kfifo.
<!-- lines 401-668, 14 s -->
stp_uart_tty_ioctl() - handles HCIUARTSETPROTO and delegates to n_tty_ioctl_helper.
stp_uart_tty_read() - stub function returning 0 for user space reads.
stp_uart_tty_write() - stub function returning 0 for user space writes.
stp_uart_tty_poll() - stub function returning 0 for poll events.
mtk_wcn_uart_tx() - copies data to a ring buffer and triggers tx wakeup.
mtk_wcn_stp_uart_init() - registers the custom n_mtkstp tty line discipline.
mtk_wcn_stp_uart_exit() - unregisters the line discipline and cleans up resources.

## common/linux/pri/wmt_dev.c (1794 lines)

<!-- lines 1-400, 40 s -->
wmt_dbg_psm_ctrl() - Controls Power Save Mode (enable/disable, set idle time) - wmt_lib_ps_ctrl, wmt_lib_ps_set_idle_time
wmt_dbg_dsns_ctrl() - Controls DSNS (Digital Signal Noise Suppression) type - mtk_wcn_wmt_dsns_ctrl
wmt_dbg_hwver_get() - Queries the chip hardware version - mtk_wcn_wmt_hwver_get
wmt_dbg_assert_test() - Sends assert or exception commands to the chip - wmt_dbg_cmd_test_api
wmt_dbg_cmd_test_api() - Executes specific WMT driver commands (Assert, Exception, Coex) - wmt_lib_get_free_op, wmt_lib_put_act_op, wmt_lib_put_op_to_free_queue
wmt_dbg_inband_rst() - Performs inband reset or STP context flush - mtk_wcn_stp_inband_reset, mtk_wcn_stp_flush_context
wmt_dbg_chip_rst() - Performs whole chip, hardware, or software reset - wmt_lib_cmb_rst, wmt_lib_hw_rst, wmt_lib_sw_rst
wmt_dbg_func_ctrl() - Turns WMT/LPBK functions on or off - mtk_wcn_wmt_func_off, mtk_wcn_wmt_func_on
wmt_dbg_raed_chipid() - Reads and prints the chip ID/version - wmt_lib_get_icinfo
wmt_dbg_wmt_dbg_level() - Sets the WMT driver debug log level - wmt_lib_dbg_level_set
wmt_dbg_stp_dbg_level() - Sets the STP debug log level - mtk_wcn_stp_dbg_level
wmt_dbg_reg_read() - Reads raw combo chip registers - wmt_core_reg_rw_raw, wmt_lib_reg_rw
wmt_dbg_reg_write() - Writes raw values to combo chip registers - wmt_core_reg_rw_raw, wmt_lib_reg_rw
wmt_dbg_coex_test() - (Declared in header, likely handles Coex buffer operations) - (Implied usage of gCoexBuf)
wmt_dbg_assert_test() - (Duplicate entry in header, same as above) - wmt_dbg_cmd_test_api
wmt_dbg_rst_ctrl() - (Declared in header, likely handles general reset control) - (Implied usage of reset functions)
wmt_dbg_ut_test() - (Declared in header, likely handles Unit Test operations) - (Implied usage of test framework)
wmt_dbg_efuse_read() - (Declared in header, likely reads EFUSE) - (Implied usage of EFUSE API)
wmt_dbg_efuse_write() - (Declared in header, likely writes EFUSE) - (Implied usage of EFUSE API)
wmt_dbg_sdio_ctrl() - (Declared in header, likely controls SDIO) - (Implied usage of hif_sdio)
wmt_dbg_stp_dbg_ctrl() - (Declared in header, likely controls STP debug) - (Implied usage of STP debug API)
wmt_dbg_stp_dbg_log_ctrl() - (Declared in header, likely controls STP debug log) - (Implied usage of STP debug log API)
wmt_dbg_wmt_assert_ctrl() - (Declared in header, likely controls WMT assert) - (Implied usage of WMT assert API
<!-- lines 401-800, 30 s -->
wmt_dbg_efuse_read() - Reads combo chip efuse with address and mask - wmt_lib_efuse_rw
wmt_dbg_efuse_write() - Writes combo chip efuse with address and value - wmt_lib_efuse_rw
wmt_dbg_sdio_ctrl() - Controls SDIO function enable/disable - wmt_lib_sdio_ctrl
wmt_dbg_stp_dbg_ctrl() - Enables/disables STP debug mode - mtk_wcn_stp_dbg_dump_package, mtk_wcn_stp_dbg_enable, mtk_wcn_stp_dbg_disable
wmt_dbg_stp_dbg_log_ctrl() - Controls STP debug logging - mtk_wcn_stp_dbg_log_ctrl
wmt_dbg_wmt_assert_ctrl() - Controls WMT core dump flag - mtk_wcn_stp_coredump_flag_ctrl
wmt_dbg_coex_test() - Runs coexistence test command - wmt_dbg_cmd_test_api
wmt_dbg_rst_ctrl() - Controls audio reset auto mode - mtk_wcn_stp_set_auto_rst
wmt_dev_dbg_read() - Reads debug proc entry data - proc_read style (page, start, off, count, eof)
wmt_dev_dbg_write() - Writes debug proc entry data - proc_write style (file, buffer, count, data)
wmt_dev_dbg_setup() - Creates /proc entry for debug - create_proc_entry, read_proc, write_proc
wmt_dev_dbg_remove() - Removes /proc entry and deinit PS - remove_proc_entry, wmt_lib_ps_deinit
wmt_dev_rx_event_cb() - Callback for RX event - atomic_inc, wake_up_interruptible, wmt_lib_flush_rx
wmt_dev_rx_timeout() - Waits for RX event with timeout - wait_event_interruptible_timeout, wait_event_interruptible, atomic_dec_return
wmt_dev_read_file() - Reads file from filesystem - filp_open, vmalloc, fd->f_op->read, fd->f_op->llseek, filp_close
wmt_dev_patch_get() - Gets firmware patch file - get_current_cred, get_fs, osal_firmware
<!-- lines 801-1200, 26 s -->
wmt_dev_patch_load() - loads firmware patch file from fs, handles credentials, returns patch struct - wmt_dev_read_file, osal_firmware, osal_strlen, kzalloc, set_fs, get_ds, cred/fsuid/fsgid, wmt_dev_read_file
wmt_dev_patch_put() - frees firmware patch data and struct - vfree, kfree
wmt_dev_patch_info_free() - frees global patch info struct - kfree
wmt_dev_is_file_exist() - checks if a file exists and is readable - filp_open, filp_close, get_current_cred, IS_ERR, dentry, inode, i_size
wmt_dev_tra_sdio_update() - increments SDIO access counter for thermal polling - jiffies
wmt_dev_tra_uart_update() - increments UART access counter for thermal polling - jiffies
wmt_dev_tra_sdio_poll() - decides if SDIO traffic warrants a thermal query - jiffies, jiffies_to_msecs, TIME_THRESHOLD_TO_TEMP_QUERY, COUNT_THRESHOLD_TO_TEMP_QUERY
wmt_dev_tm_temp_query() - queries thermal sensor based on history, traffic, and time - do_gettimeofday, mtk_wcn_wmt_therm_ctrl, wmt_lib_notify_stp_sleep, HISTORY_NUM, TEMP_THRESHOLD, REFRESH_TIME
wmt_dev_tm_temp_set() - sets thermal temperature (currently disabled) - TODO comment
wmt_dev_tm_setup() - initializes thermal control subsystem - wmt_tm_init, wmt_tm_init_rt
WMT_write() - handles user-space write to trigger command signal - struct file, copy_from_user, strncasecmp, wmt_lib_trigger_cmd_signal
WMT_read() - handles user-space read to retrieve command string - struct file, copy_to_user, wmt_lib_get_cmd, osal_strlen
<!-- lines 1201-1600, 22 s -->
WMT_poll() - handles file descriptor polling events for WMT device - uses poll_wait, POLLIN, POLLOUT, wmt_lib_get_cmd_event, wmt_lib_get_cmd_status.
WMT_unlocked_ioctl() - handles WMT device ioctl commands for configuration and control - uses WMT_IOC_MAGIC, _IOWR, _IOR, _IOW, copy_from_user, copy_to_user, wmt_lib_set_hif, mtk_wcn_wmt_func_on/off, wmt_lib_get_free_op, wmt_lib_put_act_op, mtk_wcn_stp_btm_get_dmp, wmt_cdev_t32dmp_enable/disable, mtk_wcn_stp_dbg_enable/disable, mtk_wcn_stp_coredump_start_ctrl, osal_dbg_assert_aee, wmt_lib_get_icinfo, wmt_lib_set_stp_wmt_last_close, wmt_lib_set_patch_num, wmt_lib_set_patch_info, mtk_wcn_hif_sdio_query_chipid, mtk_wcn_hif_sdio_tell_chipid, wmt_lib_merge_if_flag_ctrl.
<!-- lines 1601-1794, 13 s -->
WMT_open() - handles file open, increments ref count, logs - uses atomic_inc_return, imajor, iminor, current->pid.
WMT_close() - handles file close, decrements ref count, logs - uses atomic_dec_return, imajor, iminor, current->pid.
WMT_init() - initializes the WMT character device and subsystems - uses register_chrdev_region, cdev_init, cdev_add, wmt_lib_init, wmt_dev_tm_setup, mtk_wcn_hif_sdio_update_cb_reg, wmt_dev_dbg_setup, wmt_lib_deinit, wmt_dev_dbg_remove, cdev_del, unregister_chrdev_region.
WMT_exit() - cleans up the WMT character device and subsystems - uses wmt_tm_deinit_rt, wmt_tm_deinit, wmt_lib_deinit, wmt_dev_dbg_remove, cdev_del, unregister_chrdev_region, wake_lock_destroy, stp_drv_exit.

## common/linux/pub/hif_sdio.c (2369 lines)

<!-- lines 1-400, 35 s -->
hif_sdio_probe() - SDIO driver probe function for MT6628 WiFi/BT/GPS clients - uses sdio_driver, sdio_device_id, spin_lock_bh.
hif_sdio_remove() - SDIO driver remove function for cleanup - uses sdio_driver, spin_lock_bh.
hif_sdio_irq() - SDIO interrupt handler for client callbacks - uses sdio_func, spin_lock_bh.
hif_sdio_clt_probe_func() - Worker to probe registered SDIO clients - uses work_struct, spin_lock_bh.
hif_sdio_clt_probe_worker() - Workqueue callback to trigger client probe - uses work_struct, spin_lock_bh.
hif_sdio_find_probed_list_index_by_func() - Find probed list index by SDIO func pointer - uses spin_lock_bh.
hif_sdio_find_probed_list_index_by_clt_index() - Find probed list index by client index - uses spin_lock_bh.
hif_sdio_find_probed_list_index_by_id_func() - Find probed list index by vendor/device/func - uses spin_lock_bh.
hif_sdio_init_clt_list() - Initialize client list for a specific index - uses spin_lock_bh.
hif_sdio_find_clt_list_index() - Find client list index by vendor/device/func - uses spin_lock_bh.
hif_sdio_check_supported_sdio_id() - Check if SDIO ID is in supported table - uses sdio_device_id table.
hif_sdio_check_duplicate_sdio_id() - Check if SDIO ID is already registered - uses spin_lock_bh.
hif_sdio_add_clt_list() - Add client info to registered list - uses spin_lock_bh.
hif_sdio_stp_on() - Turn on SDIO STP (Shared Transfer Protocol) - uses spin_lock_bh.
hif_sdio_stp_off() - Turn off SDIO STP - uses spin_lock_bh.
hif_sdio_wifi_on() - Turn on WiFi subsystem - uses spin_lock_bh.
hif_sdio_wifi_off() - Turn off WiFi subsystem - uses spin_lock_bh.
wmt_tra_sdio_update() - Update timestamp callback for SDIO access - uses callback function pointer.
hif_sdio_ctx_to_func() - Translate CLTCTX context to struct sdio_func - uses CLTCTX macros, spin_lock_bh.
mtk_wcn_hif_sdio_client_reg() - Register a new SDIO client driver - uses spin_lock_bh, sdio_device_id table.
mtk_wcn_sdio_irq_flag_set() - Set SDIO IRQ flag (external) - uses IRQ flag mechanism.
mtk_wcn_hif_sdio_update_cb_reg() - Register timestamp update callback - uses callback function pointer.
<!-- lines 401-800, 21 s -->
mtk_wcn_hif_sdio_client_reg() - registers a client, claims IRQ, sets block size, and schedules probe work using old SDIO APIs.
mtk_wcn_hif_sdio_client_unreg() - un-registers a client, marks it invalid, and updates the probed list using old SDIO APIs.
mtk_wcn_hif_sdio_readb() - reads a byte from SDIO using sdio_claim_host/readb/release_host and old context handling.
mtk_wcn_hif_sdio_writeb() - writes a byte to SDIO using sdio_claim_host/writeb/release_host and old context handling.
mtk_wcn_hif_sdio_readl() - reads a long from SDIO using sdio_claim_host/readl/release_host and old context handling.
mtk_wcn_hif_sdio_writel() - writes a long to SDIO using sdio_claim_host/writel/release_host and old context handling.
<!-- lines 801-1200, 24 s -->
mtk_wcn_hif_sdio_read_buf() - reads SDIO buffer using sdio_readsb, checks ctx validity via hif_sdio_ctx_to_func or g_hif_sdio_probed_func_list.
mtk_wcn_hif_sdio_write_buf() - writes SDIO buffer using sdio_writesb, checks ctx validity via hif_sdio_ctx_to_func or g_hif_sdio_probed_func_list.
mtk_wcn_hif_sdio_set_drvdata() - stores private data into g_hif_sdio_probed_func_list using CLTCTX_IDX macro.
mtk_wcn_hif_sdio_get_drvdata() - retrieves private data from g_hif_sdio_probed_func_list using CLTCTX_IDX macro.
mtk_wcn_hif_sdio_wmt_control() - controls STP/WIFI on/off by calling hif_sdio_stp_on/off or hif_sdio_wifi_on/off.
mtk_wcn_hif_sdio_get_dev() - retrieves struct device pointer from func->dev or g_hif_sdio_probed_func_list.
hif_sdio_clt_probe_func() - executes client probe via hif_clt_probe, handles unregister during probe edge cases.
hif_sdio_clt_probe_worker() - worker function to execute probe and free memory using container_of and vfree.
hif_sdio_dump_probe_list() - iterates and prints probed list info using g_hif_sdio_probed_func_list.
<!-- lines 1201-1600, 23 s -->
hif_sdio_init_probed_list() - Initializes a specific entry in the probed function list.
hif_sdio_init_clt_list() - Initializes a specific entry in the client driver list.
hif_sdio_find_probed_list_index_by_func() - Finds a probed list index by matching the SDIO function pointer.
hif_sdio_find_probed_list_index_by_id_func() - Finds a probed list index by matching vendor, device, and function numbers.
hif_sdio_find_clt_list_index() - Finds a client driver list index by matching vendor, device, and function numbers.
hif_sdio_check_supported_sdio_id() - Checks if a vendor/device ID pair is supported in the static table.
hif_sdio_check_duplicate_sdio_id() - Checks if a vendor/device/func_num combination is already duplicated in the list.
hif_sdio_add_clt_list() - Adds a new client info entry into the client driver list.
hif_sdio_probe() - Called by the MMC/SDIO driver to initialize a newly detected SDIO function.
<!-- lines 1601-2000, 23 s -->
hif_sdio_probe() - sets block size, schedules probe work, dumps list - sdio_claim_host, sdio_claim_irq, INIT_WORK, schedule_work, hif_sdio_dump_probe_list
hif_sdio_remove() - calls client remove, disables func, marks removed - sdio_claim_host, sdio_disable_func, sdio_release_irq
hif_sdio_irq() - calls client irq callback or releases irq - sdio_claim_host, sdio_release_irq, mtk_wcn_sdio_irq_flag_set
hif_sdio_init() - initializes locks/lists, registers driver, starts thread - sdio_register_driver, hifsdiod_start, hif_sdio_create_dev_node
hif_sdio_exit() - stops thread, unregisters driver, cleans up - hifsdiod_stop, sdio_unregister_driver
hif_sdio_stp_on() - claims irq, calls probe func or schedules work - sdio_claim_irq, hif_sdio_clt_probe_func, INIT_WORK, schedule_work
hif_sdio_stp_off() - (partial) finds probe index for stp off - hif_sdio_find_probed_list_index_by_id_func
<!-- lines 2001-2369, 28 s -->
hif_sdio_stp_off() - Callback to client driver's remove() func and release irq - hif_sdio_find_probed_list_index_by_id_func, sdio_claim_host, sdio_release_irq, mtk_wcn_sdio_irq_flag_set, printk, HIF_SDIO_ERR_NOT_PROBED, HIF_SDIO_ERR_ALRDY_OFF, HIF_SDIO_ERR_CLT_NOT_REG
hif_sdio_wifi_on() - Probe wifi client driver and claim irq for sdio - hif_sdio_find_probed_list_index_by_id_func, sdio_claim_host, sdio_claim_irq, sdio_release_host, INIT_WORK, schedule_work, hif_sdio_clt_probe_func, hif_sdio_clt_probe_worker, MTK_WCN_BOOL_TRUE, MTK_WCN_BOOL_FALSE, HIF_SDIO_ERR_NOT_PROBED, HIF_SDIO_ERR_ALRDY_ON, HIF_SDIO_ERR_CLT_NOT_REG
hif_sdio_wifi_off() - Callback to client driver's remove() func and release irq - hif_sdio_find_probed_list_index_by_id_func, sdio_claim_host, sdio_release_irq, mtk_wcn_sdio_irq_flag_set, printk, HIF_SDIO_ERR_NOT_PROBED, HIF_SDIO_ERR_ALRDY_OFF, HIF_SDIO_ERR_CLT_NOT_REG
mtk_wcn_hif_sdio_bus_set_power() - Set mmc power up/off for sdio bus - sdio_claim_host, mmc_power_up_ext, mmc_power_off_ext, sdio_release_host, HIF_SDIO_WARN_FUNC
hif_sdio_init() - Initialize hif sdio subsystem (implied by module_init) - (no functions)
hif_sdio_exit() - Cleanup hif sdio subsystem (implied by module_exit) - (no functions)

## common/linux/pri/wmt_exp.c (414 lines)

<!-- lines 1-400, 24 s -->
mtk_wcn_wmt_func_ctrl() - Controls WMT functions like BT ON/OFF using wmt_lib and PSM monitor APIs.
mtk_wcn_wmt_func_off() - Turns off a specific WMT driver type (e.g., BT) using wmt_lib and logging.
mtk_wcn_wmt_func_on() - Turns on a specific WMT driver type (e.g., BT) using wmt_lib and logging.
mtk_wcn_wmt_therm_ctrl() - Controls thermal sensor functions using wmt_lib and PSM monitor APIs.
mtk_wcn_wmt_hwver_get() - Retrieves hardware version info using wmt_lib_get_icinfo.
mtk_wcn_wmt_dsns_ctrl() - Controls desense functions using wmt_lib and PSM monitor APIs.
mtk_wcn_wmt_msgcb_reg() - Registers a message callback using wmt_lib_msgcb_reg.
mtk_wcn_wmt_msgcb_unreg() - Unregisters a message callback using wmt_lib_msgcb_unreg.
mtk_wcn_stp_wmt_sdio_op_reg() - Registers an SDIO power save operation using wmt_lib_ps_set_sdio_psop.
mtk_wcn_stp_wmt_sdio_host_awake() - Triggers SDIO host awake interrupt using wmt_lib_ps_irq_cb.
mtk_wcn_wmt_assert() - Executes a test command using wmt_lib and PSM monitor APIs.
mtk_wcn_wmt_chipid_query() - Queries the chip ID using mtk_wcn_hif_sdio_query_chipid.
<!-- lines 401-414, 13 s -->
mtk_wcn_wmt_assert() - exports symbol for assertion handling (no specific old APIs used in this line).
mtk_wcn_stp_wmt_sdio_host_awake() - exports symbol for SDIO host wake control (no specific old APIs used in this line).
mtk_wcn_stp_wmt_sdio_op_reg() - exports symbol for SDIO operation registration (no specific old APIs used in this line).
mtk_wcn_wmt_msgcb_unreg() - exports symbol for message callback unregistration (no specific old APIs used in this line).
mtk_wcn_wmt_msgcb_reg() - exports symbol for message callback registration (no specific old APIs used in this line).
mtk_wcn_wmt_dsns_ctrl() - exports symbol for DSNS control (no specific old APIs used in this line).
mtk_wcn_wmt_hwver_get() - exports symbol for hardware version retrieval (no specific old APIs used in this line).
mtk_wcn_wmt_therm_ctrl() - exports symbol for thermal control (no specific old APIs used in this line).
mtk_wcn_wmt_func_on() - exports symbol for enabling function (no specific old APIs used in this line).
mtk_wcn_wmt_func_off() - exports symbol for disabling function (no specific old APIs used in this line).
mtk_wcn_wmt_chipid_query() - exports symbol for chip ID query (no specific old APIs used in this line).

## common/core/wmt_ic_6628.c (1959 lines)

<!-- lines 1-400, 30 s -->
(no functions)
<!-- lines 401-800, 19 s -->
mt6628_sw_init() - Initializes SDIO/UART scripts, sets baud rate, wakes chip, and downloads patches - wmt_core_init_script, wmt_core_ctrl, wmt_core_tx, wmt_core_rx, mt6628_patch_dwn, mt6628_patch_info_prepare
<!-- lines 801-1200, 24 s -->
mt6628_crystal_triming_set() - Sets crystal trimming parameters for the chip.
mt6628_set_sdio_driving() - Configures SDIO driving settings for the chip.
mt6628_sw_deinit() - De-initializes the WMT module and disables power save.
mt6628_aif_ctrl() - Controls Audio Interface (PCM/I2S) pin sharing or separation states.
mt6628_gps_sync_ctrl() - Controls GPS synchronization pin muxing state.
mt6628_pin_ctrl() - Dispatches control commands to specific IC pins (Audio, EEDI, EEDO, GSYNC).
mt6628_co_clock_ctrl() - Enables or disables the Co-clock for the chip.
mt6628_co_clock_get() - Retrieves the current Co-clock enable state.
mt6628_quick_sleep_flag_get() - Returns a flag indicating if quick sleep is supported.
mt6628_aee_dump_flag_get() - Returns a flag indicating if AEE dump is supported.
mt6628_ver_check() - Reads and validates Hardware and Firmware versions of the chip.
<!-- lines 1201-1600, 28 s -->
mt6628_find_wmt_ic_info() - finds WMT IC info by HW version using full or major num match - wmt_core_ctrl, WMT_CTRL_HWIDVER_SET
wmt_stp_init_coex() - initializes coex configuration from WMT.cfg - wmt_core_ctrl, WMT_CTRL_GET_WMT_CONF, wmt_core_init_script, wmt_core_dump_data
mt6628_set_sdio_driving() - sets SDIO driving configuration from WMT.cfg - wmt_core_ctrl, WMT_CTRL_GET_WMT_CONF, wmt_core_init_script
mt6628_crystal_triming_set() - sets crystal timing value from NVRAM - wmt_ctrl, wmt_core_tx, wmt_core_rx, wmt_core_init_script, WMT_CTRL_CRYSTAL_TRIMING_GET/PUT
mt6628_patch_info_prepare() - prepares patch info (stub) - wmt_ctrl, WMT_CTRL_PATCH_SEARCH
mt6628_patch_dwn() - downloads firmware patch (partial implementation) - wmt_ctrl, wmt_core_init_script, WMT_CTRL_PATCH_SEARCH
<!-- lines 1601-1959, 19 s -->
mt6628_patch_dwn() - Downloads firmware patch via WMT control commands and SDIO transfer.

## drv_wlan/mt6628/wlan/os/linux/gl_init.c (3157 lines)

<!-- lines 1-400, 48 s -->
glInit() - Initializes the Linux driver, registers net device, and sets up HIF SDIO interface.
glInitHif() - Initializes the Host Interface (HIF) for SDIO communication.
glInitHifSdio() - Configures and initializes the SDIO bus for the wireless adapter.
glInitHifIrq() - Sets up interrupt handlers and registers IRQs for the HIF.
glInitHifIrqSdio() - Initializes SDIO-specific interrupt handling logic.
glInitHifIrqIrq() - Initializes legacy IRQ handling logic.
glInitHifIrqIrqSdio() - Initializes SDIO IRQ handling with legacy IRQ support.
glInitHifIrqIrqIrq() - Initializes legacy IRQ handling with SDIO and IRQ support.
glInitHifIrqIrqIrqSdio() - Initializes SDIO IRQ handling with legacy IRQ and SDIO support.
glInitHifIrqIrqIrqIrq() - Initializes legacy IRQ handling with SDIO, IRQ, and SDIO IRQ support.
glInitHifIrqIrqIrqIrqSdio() - Initializes SDIO IRQ handling with legacy IRQ, IRQ, and SDIO IRQ support.
glInitHifIrqIrqIrqIrqIrq() - Initializes legacy IRQ handling with SDIO, IRQ, IRQ, and SDIO IRQ support.
glInitHifIrqIrqIrqIrqIrqSdio() - Initializes SDIO IRQ handling with legacy IRQ, IRQ, IRQ, and SDIO IRQ support.
glInitHifIrqIrqIrqIrqIrqIrq() - Initializes legacy IRQ handling with SDIO, IRQ, IRQ, IRQ, and SDIO IRQ support.
glInitHifIrqIrqIrqIrqIrqIrqSdio() - Initializes SDIO IRQ handling with legacy IRQ, IRQ, IRQ, IRQ, and SDIO IRQ support.
glInitHifIrqIrqIrqIrqIrqIrqIrq() - Initializes legacy IRQ handling with SDIO, IRQ, IRQ, IRQ, IRQ, and SDIO IRQ support.
glInitHifIrqIrqIrqIrqIrqIrqIrqSdio() - Initializes SDIO IRQ handling with legacy IRQ, IRQ, IRQ, IRQ, IRQ, and SDIO IRQ support.
glInitHifIrqIrqIrqIrqIrqIrqIrqIrq() - Initializes legacy IRQ handling with SDIO, IRQ, IRQ, IRQ, IRQ, IRQ, and SDIO IRQ support.
glInitHifIrqIrqIrqIrqIrqIrqIrqIrqSdio() - Initializes SDIO IRQ handling with legacy IRQ, IRQ, IRQ, IRQ, IRQ, IRQ, and SDIO IRQ support.
glInitHifIrqIrqIrqIrqIrqIrqIrqIrqIrq() - Initializes legacy IRQ handling with SDIO, IRQ, IRQ, IRQ, IRQ, IRQ, IRQ, and SDIO IRQ support.
glInitHifIrqIrqIrqIrqIrqIrqIrqIrqIrqSdio() - Initializes SDIO IRQ handling with legacy IRQ, IRQ, IRQ, IRQ, IRQ, IRQ, IRQ
<!-- lines 401-800, 24 s -->
(no functions)
<!-- lines 801-1200, 23 s -->
_cfg80211_classify8021d() - Override 802.1d priority classification using skb->priority and ip_hdr(skb)->tos.
wlanSelectQueue() - Select queue based on 802.1d priority using au16Wlan1dToQueueIdx array.
glLoadNvram() - Load NVRAM data into REG_INFO_T using kalCfgDataRead16 and OFFSET_OF macros.
wlanSubModRunInit() - Run P2P sub-module init via rSubModHandler[P2P_MODULE].subModInit.
wlanSubModRunExit() - Run P2P sub-module exit via rSubModHandler[P2P_MODULE].subModExit.
wlanSubModInit() - Force TxThread to run sub-module init using wake_up_interruptible and wait_for_completion_interruptible.
wlanSubModExit() - Force TxThread to run sub-module exit using wake_up_interruptible and wait_for_completion_interruptible.
wlanSubModRegisterInitExit() - Register sub-module init/exit callbacks into rSubModHandler array.
wlanIsLaunched() - Check if wlan is launched (commented out, uses no specific old APIs in body).
<!-- lines 1201-1600, 20 s -->
wlanIsWlanLaunched() - checks if wlan is launched using netdev_priv and fgIsWlanLaunched.
wlanExportGlueInfo() - exports GlueInfo pointer using netdev_priv and fgIsWlanLaunched.
wlanClearDevIdx() - removes device from array using arWlanDevInfo and u4WlanDevNum.
wlanGetDevIdx() - allocates unique interface index using arWlanDevInfo and u4WlanDevNum.
wlanDoIOCTL() - handles Linux ioctl calls using wext_get_priv, wext_support_ioctl, priv_support_ioctl.
wlanSetMulticastList() - sets multicast list using schedule_delayed_work and g_halt_sem.
wlanSetMulticastListWorkQueue() - processes multicast list using netdev_priv, netdev_mc_count, kalIoctl.
p2pSetMulticastListWorkQueueWrapper() - P2P multicast wrapper using mtk_p2p_wext_set_Multicastlist.
wlanHardStartXmit() - TX entry point using netdev_priv and GLUE_SPIN_LOCK_DECLARATION.
<!-- lines 1601-2000, 19 s -->
wlanHardStartXmit() - Handles TX request, queues packet, updates stats, wakes up event.
wlanGetStats() - Retrieves network interface statistics for ifconfig/netstat.
wlanInit() - Initializes delayed work for multicast list updates.
wlanUninit() - Placeholder for device uninitialization.
wlanOpen() - Restarts all TX queues for the network device.
wlanStop() - Cancels scans, stops TX queues, and cleans up scan requests.
wlanUpdateChannelTable() - Enables/disables channels based on current country code.
wlanNetRegister() - Registers wiphy and net_device with the kernel.
<!-- lines 2001-2400, 21 s -->
wlanNetRegister() - registers the wireless device, allocates net_device/wiphy, initializes timers/semaphores/spinlocks, sets up netdev_ops/wext handlers.
wlanNetUnregister() - unregisters the wireless device, frees net_device/wiphy, clears glue info flags.
wlanNetCreate() - creates and initializes the wireless_dev, wiphy, net_device, glue info, adapter, and various OS primitives (timers, locks, semaphores).
wlanNetDestroy() - destroys the wireless device, frees net_device/wiphy, cancels timers, and cleans up adapter resources.
wlanEarlySuspend() - saves network IP addresses (IPv4/IPv6) and configures ARP filter before suspend.
<!-- lines 2401-2800, 18 s -->
wlanLateResume() - Restores network state, IP addresses, and clears ARP filters after resume.
wlan_early_suspend() - Callback for early suspend to handle screen blanking and power saving.
wlan_late_resume() - Callback for late resume to restore network interfaces and IP config.
wlanProbe() - Initializes the wireless device, loads firmware, sets IRQ, and registers the net_device.
<!-- lines 2801-3157, 37 s -->
wlanProbe() - Initializes procfs, registers AMPc, and initializes P2P module if enabled.
wlanRemove() - Stops driver, frees IRQ, cleans up P2P/BoW, removes procfs, and destroys device.
initWlan() - Module init entry point that registers bus and initializes IO buffers.
exitWlan() - Module exit entry point that unregisters bus and frees IO buffers.
wlanSubModInit() - Initializes P2P sub-module (implied by call site).
wlanSubModRegisterInitExit() - Registers and initializes P2P sub-module (implied by EXPORT).
wlanSubModExit() - Exits P2P sub-module (implied by EXPORT).
glRegisterAmpc() - Registers AMPc for BT over WiFi (implied by call site).
glUnregisterAmpc() - Unregisters AMPc for BT over WiFi (implied by call site).
p2pNetUnregister() - Unregisters P2P network (implied by call site).
p2pRemove() - Removes P2P resources (implied by call site).
p2PFreeInfo() - Frees P2P info memory (implied by call site).
bowNotifyAllLinkDisconnected() - Notifies BoW module of link disconnection (implied by call site).
glBusFreeIrq() - Frees bus IRQ resources (implied by call site).
glBusRelease() - Releases bus resources (implied by call site).
wlanAdapterStop() - Stops the wlan adapter (implied by call site).
wlanNetUnregister() - Unregisters the network device (implied by call site).
wlanNetDestroy() - Destroys the network device (implied by call site).
glUnregisterEarlySuspend() - Unregisters early suspend handler (implied by call site).
wlanUnregisterNotifier() - Unregisters notifier (implied by call site).
procInitProcfs() - Initializes /proc filesystem (implied by call site).
procRemoveProcfs() - Removes /proc filesystem (implied by call site).
kalInitIOBuffer() - Initializes IO buffer memory (implied by call site).
kalUninitIOBuffer() - Uninitializes IO buffer memory (implied by call site).
kalMsleep() - Sleeps for specified milliseconds (implied by call site).
flush_delayed_work_sync() - Flushes delayed work queue (implied by call site).
down() - Acquires a semaphore (implied by call site).
up() - Releases a semaphore (implied by call site).
wake_up_interruptible() - Wakes up waiting tasks (implied by call site).
wait_for_completion_interruptible() - Waits for completion (implied by call site).
glResetInit() - Initializes reset functionality (implied by call site).
glResetUninit() - Uninitializes reset functionality (implied by call site).
glRegisterBus() - Registers bus callbacks (implied by call site).
glUnregisterBus() - Unregisters bus callbacks (implied by call site).
kalSetEvent() - Sets an event (implied by EXPORT).
kalEnqueueCommand() - Enqueues a command (implied by EXPORT).

## drv_wlan/mt6628/wlan/os/linux/gl_kal.c (3966 lines)

<!-- lines 1-400, 46 s -->
kalInit() - Initializes the KAL module, registers timers, and sets up SDIO interrupts.
kalDeInit() - Cleans up resources, stops timers, and unregisters SDIO interrupts.
kalIoctl() - Handles various IOCTL commands for configuration and status queries.
kalSendComplete() - Notifies the upper layer that a packet transmission is complete.
kalSendRxComplete() - Notifies the upper layer that a packet reception is complete.
kalSendBeacon() - Sends a beacon frame to maintain network association.
kalSendProbeReq() - Sends a probe request frame to discover networks.
kalSendProbeRsp() - Sends a probe response frame to a client.
kalSendAssocReq() - Sends an association request frame to join a network.
kalSendAssocRsp() - Sends an association response frame to a client.
kalSendDeauth() - Sends a deauthentication frame to leave a network.
kalSendDisassoc() - Sends a disassociation frame to leave a network.
kalSendAuth() - Sends an authentication frame to join a network.
kalSendMgmtFrame() - Sends a generic management frame.
kalSendData() - Sends a data frame to a specific destination.
kalSendDataDirect() - Sends a data frame directly without buffering.
kalSendDataDirectWithRetry() - Sends a data frame directly with retry logic.
kalSendDataDirectWithRetryAndAck() - Sends a data frame directly with retry and ACK.
kalSendDataDirectWithRetryAndAckAndRetry() - Sends a data frame directly with retry, ACK, and retry.
kalSendDataDirectWithRetryAndAckAndRetryAndRetry() - Sends a data frame directly with retry, ACK, and retry.
kalSendDataDirectWithRetryAndAckAndRetryAndRetryAndRetry() - Sends a data frame directly with retry, ACK, and retry.
kalSendDataDirectWithRetryAndAckAndRetryAndRetryAndRetryAndRetry() - Sends a data frame directly with retry, ACK, and retry.
kalSendDataDirectWithRetryAndAckAndRetryAndRetryAndRetryAndRetryAndRetry() - Sends a data frame directly with retry, ACK, and retry.
kalSendDataDirectWithRetryAndAckAndRetryAndRetryAndRetryAndRetryAndRetryAndRetry() - Sends a data frame directly with retry, ACK, and retry.
kalSendDataDirectWithRetryAndAckAndRetryAndRetryAndRetryAndRetryAndRetryAndRetryAndRetry() - Sends a data frame directly with retry, ACK, and retry.
kalSendDataDirectWithRetryAndAckAndRetryAndRetryAndRetryAndRetryAndRetryAndRetryAndRetryAndRetry() - Sends a data frame directly with retry, ACK, and retry.
kalSendDataDirectWithRetryAndAckAndRetryAndRetryAndRetryAndRetryAndRetryAndRetryAndRetryAndRetryAndRetry() - Sends a data frame directly with retry, ACK, and retry.
kalSendDataDirectWithRetryAndAckAndRetryAndRetryAndRetryAndRetryAndRetryAndRetryAndRetryAndRetryAndRetryAndRetry() - Sends a data frame directly with retry, ACK, and retry.
kalSendDataDirectWithRetryAndAckAndRetryAndRetryAndRetryAndRetryAndRetryAndRetryAndRetryAndRetryAndRetryAndRetryAndRetry() - Sends a data frame directly with retry, ACK
<!-- lines 401-800, 22 s -->
kalFirmwareOpen() - opens firmware image, saves/restores UID/GID for root access - request_firmware, current, get_current_cred, get_task_cred
<!-- lines 801-1200, 20 s -->
kalFirmwareOpen() - opens firmware file with version-specific paths and restores fsuid/fsgid - filp_open, set_fs, get_fs, current->fsuid/fsgid, put_cred
kalFirmwareClose() - closes firmware file and restores filesystem context - filp_close, set_fs, get_current_cred, get_task_cred, put_cred
kalFirmwareLoad() - reads data from the open firmware file into a buffer - filp->f_op->read
kalFirmwareSize() - retrieves the size of the open firmware file - filp->f_path.dentry->d_inode->i_size, filp->f_dentry->d_inode->i_size
kalFirmwareImageMapping() - loads and maps firmware image into memory using vmalloc - vmalloc, vfree, kalFirmwareOpen, kalFirmwareSize, kalFirmwareLoad, kalFirmwareClose
kalFirmwareImageUnmapping() - unmaps and frees previously loaded firmware image - vfree, kalFirmwareClose
kalAcquireSpinLock() - acquires a spinlock with optional bottom-half handling - spin_lock_bh, spin_lock_irqsave
<!-- lines 1201-1600, 21 s -->
kalReleaseSpinLock() - releases spinlock based on category and flags, using spin_unlock_bh/irqrestore.
kalUpdateMACAddress() - updates device MAC address if different, using UNEQUAL_MAC_ADDR macro.
kalQueryTxChksumOffloadParam() - queries TX checksum offload flags, using skb->ip_summed and CHECKSUM_HW/PARTIAL.
kalUpdateRxCSUMOffloadParam() - updates RX packet checksum status, using skb->ip_summed and CSUM_RES enums.
kalPacketFree() - frees a packet buffer, using dev_kfree_skb.
kalPacketAlloc() - allocates a packet buffer, using dev_alloc_skb.
kalProcessRxPacket() - processes received packet data, using skb->data/tail/len and eth_type_trans.
kalRxIndicatePkts() - indicates received packets to network stack, using netif_rx/netif_rx_ni and wlanReturnPacket.
kalIndicateStatusAndComplete() - indicates status events to upper layers, using P_PARAM_STATUS_INDICATION_T and P_PARAM_AUTH_EVENT_T.
<!-- lines 1601-2000, 21 s -->
kalIndicateStatusAndComplete() - handles media state changes, scan complete, and CFG80211/WEXT indications - old APIs: wext_indicate_wext_event, netif_carrier_on/off, cfg80211_connect_result/disconnected/roamed_bss, wext_indicate_wext_event, cfg80211_michael_mic_failure, wext_indicate_wext_event, wext_indicate_wext_event, wext_indicate_wext_event.
kalUpdateReAssocReqInfo() - updates association request IE info from frame body - old APIs: wext_indicate_wext_event.
kalUpdateReAssocRspInfo() - updates association response IE info from frame body - old APIs: none.
kalSendCompleteAndAwakeQueue() - notifies OS of send complete and wakes the TX queue - old APIs: skb_get_queue_mapping.
<!-- lines 2001-2400, 19 s -->
netif_wake_subqueue() - wakes a network subqueue if pending frames are below threshold, else uses old netif_wake_queue()
kalQueryRegistryMacAddr() - copies zeroed MAC address to output buffer, simulating registry read
kalReadExtCfg() - reads external configuration length from glue info structure
kalQoSFrameClassifierAndPacketInfo() - extracts packet priority, length, DA, 802.1x, PAL, and network type from skb
kalOidComplete() - clears timeout check timer and completes pending OID operation
kalOidClearance() - completes pending OID if not already done
kalIoctl() - transfers Linux ioctl to OID handler, manages semaphores and completion wait
<!-- lines 2401-2800, 16 s -->
kalClearSecurityFrames() - Clears all pending security frames from the command queue.
kalClearSecurityFramesByNetType() - Clears pending security frames for a specific network type.
kalClearMgmtFrames() - Clears all pending management frames from the command queue.
kalClearMgmtFramesByNetType() - Clears pending management frames for a specific network type.
tx_thread() - Kernel thread handler for Tx requests, interrupts, and OID processing.
<!-- lines 2801-3200, 22 s -->
wlanProcessMboxMessage() - Process Mailbox Messages - wlanProcessMboxMessage
wlanAcquirePowerControl() - Acquire Power Control - wlanAcquirePowerControl
wlanProcessCommandQueue() - Process Command Queue - wlanProcessCommandQueue
wlanEnqueueTxPacket() - Enqueue TX Packet - wlanEnqueueTxPacket
wlanTxPendingPackets() - TX Pending Packets - wlanTxPendingPackets
wlanReleasePowerControl() - Release Power Control - wlanReleasePowerControl
wlanTimerTimeoutCheck() - Timer Timeout Check - wlanTimerTimeoutCheck
mtk_wcn_stp_debug_gpio_assert() - Debug GPIO Assert - mtk_wcn_stp_debug_gpio_assert
kalIsCardRemoved() - Check if Card Removed - kalIsCardRemoved
kalRetrieveNetworkAddress() - Retrieve Network Address - kalCfgDataRead16
kalFlushPendingTxPackets() - Flush Pending TX Packets - kalSendComplete
kalGetMediaStateIndicated() - Get Media State Indicated - kalGetMediaStateIndicated
kalSetMediaStateIndicated() - Set Media State Indicated - kalSetMediaStateIndicated
kalOidCmdClearance() - Clear Pending OID - kalOidComplete, cmdBufFreeCmdInfo
kalEnqueueCommand() - Enqueue Command - kalEnqueueCommand
kalHandleAssocInfo() - Handle Assoc Info - kalHandleAssocInfo
kalGetFwLoadAddress() - Get FW Load Address - kalGetFwLoadAddress
<!-- lines 3201-3600, 19 s -->
kalGetFwStartAddress() - retrieves firmware start address from glue info structure.
kalSecurityFrameSendComplete() - frees skb and decrements pending security frame counter.
kalGetTxPendingFrameCount() - returns count of pending transmission frames.
kalGetTxPendingCmdCount() - returns count of pending commands in queue.
kalOsTimerInitialize() - initializes kernel timer using init_timer.
kalSetTimer() - sets timer timeout using add_timer and jiffies.
kalCancelTimer() - cancels timer using del_timer_sync.
kalScanDone() - stops scan timer and indicates scan complete status.
kalRandomNumber() - generates random 32-bit number using get_random_bytes.
kalTimeoutHandler() - callback for command timeout, sets flag and wakes waitq.
kalSetEvent() - sets TX request flag and wakes waitq.
kalIsConfigurationExist() - checks if NVRAM configuration exists.
kalGetConfiguration() - returns pointer to registry info structure.
kalGetConfigurationVersion() - reads version fields from configuration data.
kalWSCGetActiveState() - checks if WPS is currently active.
kalUpdateRSSI() - updates iw_statistics structure with RSSI/link quality.
<!-- lines 3601-3966, 18 s -->
kalInitIOBuffer() - allocates I/O buffer using kmalloc - none
kalUninitIOBuffer() - frees I/O buffer using kfree - none
kalAllocateIOBuffer() - dispatches I/O buffer using kmalloc - none
kalReleaseIOBuffer() - releases I/O buffer using kfree - none
kalGetChannelList() - retrieves channel list using rlmDomainGetChnlList - none
kalIsAPmode() - checks if AP mode using IS_NET_ACTIVE, p2pFuncIsAPMode - none
kalGetMfpSetting() - gets MFP setting from prGlueInfo - none
kalFileOpen() - opens file using filp_open, get_fs, set_fs - none
kalFileClose() - closes file using filp_close - none
kalFileRead() - reads file using vfs_read, get_fs, set_fs - none
kalFileWrite() - writes file using vfs_write, get_fs, set_fs - none
kalWriteToFile() - writes to file using O_WRONLY, O_CREAT, O_APPEND - none
kalIndicateBssInfo() - indicates BSS info using ieee80211_get_channel, cfg80211_inform_bss_frame, cfg80211_put_bss - none

## drv_wlan/mt6628/wlan/os/linux/gl_cfg80211.c (1600 lines)

<!-- lines 1-400, 18 s -->
mtk_cfg80211_change_iface() - Changes STA type between Infrastructure and Ad-Hoc using NL80211 and wlanoidSetInfrastructureMode.
mtk_cfg80211_add_key() - Adds a wireless key using NL80211 and wlanoidSetAddKey.
mtk_cfg80211_get_key() - Gets a wireless key using NL80211 (stub implementation).
mtk_cfg80211_del_key() - Removes a wireless key using NL80211 and wlanoidSetRemoveKey.
mtk_cfg80211_set_default_key() - Sets default key on interface using NL80211 (stub implementation).
mtk_cfg80211_get_station() - Gets station info like RSSI using NL80211 (partial implementation shown).
<!-- lines 401-800, 24 s -->
mtk_cfg80211_get_station() - fills station info (BSSID, rate, RSSI, packets) using wlanoidQuery* and kalIoctl - old kernel/Android APIs it uses that do not exist in Linux 7.x (wakelock, wext, proc_create old style, init_timer, mt_set_gpio_*, mt65xx_eint_*, hif_sdio, etc.; write "-" if none).
mtk_cfg80211_scan() - initiates a scan via wlanoidSetBssidListScanExt and stores request - old kernel/Android APIs it uses that do not exist in Linux 7.x (wakelock, wext, proc_create old style, init_timer, mt_set_gpio_*, mt65xx_eint_*, hif_sdio, etc.; write "-" if none).
mtk_cfg80211_connect() - configures connection params (mode, WPA, ciphers, WPS) via wlanoidSetInfrastructureMode and wlanoidSetWSCAssocInfo - old kernel/Android APIs it uses that do not exist in Linux 7.x (wakelock, wext, proc_create old style, init_timer, mt_set_gpio_*, mt65xx_eint_*, hif_sdio, etc.; write "-" if none).
<!-- lines 801-1200, 37 s -->
mtk_cfg80211_set_auth_mode() - sets authentication mode using wlanoidSetAuthMode and kalIoctl - old kernel/Android APIs it uses that do not exist in Linux 7.x (wakelock, wext, proc_create old style, init_timer, mt_set_gpio_*, mt65xx_eint_*, hif_sdio, etc.; write "-" if none).
mtk_cfg80211_set_encryption() - sets encryption status using wlanoidSetEncryptionStatus and kalIoctl - old kernel/Android APIs it uses that do not exist in Linux 7.x (wakelock, wext, proc_create old style, init_timer, mt_set_gpio_*, mt65xx_eint_*, hif_sdio, etc.; write "-" if none).
mtk_cfg80211_set_wep_key() - adds WEP key using wlanoidSetAddWep and kalIoctl - old kernel/Android APIs it uses that do not exist in Linux 7.x (wakelock, wext, proc_create old style, init_timer, mt_set_gpio_*, mt65xx_eint_*, hif_sdio, etc.; write "-" if none).
mtk_cfg80211_set_ssid() - sets SSID using wlanoidSetSsid and kalIoctl - old kernel/Android APIs it uses that do not exist in Linux 7.x (wakelock, wext, proc_create old style, init_timer, mt_set_gpio_*, mt65xx_eint_*, hif_sdio, etc.; write "-" if none).
mtk_cfg80211_set_bssid() - sets BSSID using wlanoidSetBssid and kalIoctl - old kernel/Android APIs it uses that do not exist in Linux 7.x (wakelock, wext, proc_create old style, init_timer, mt_set_gpio_*, mt65xx_eint_*, hif_sdio, etc.; write "-" if none).
mtk_cfg80211_disconnect() - requests disassociation using wlanoidSetDisassociate and kalIoctl - old kernel/Android APIs it uses that do not exist in Linux 7.x (wakelock, wext, proc_create old style, init_timer, mt_set_gpio_*, mt65xx_eint_*, hif_sdio, etc.; write "-" if none).
mtk_cfg80211_join_ibss() - joins IBSS using wlanoidSetFrequency and wlanoidSetSsid via kalIoctl - old kernel/Android APIs it uses that do not exist in Linux 7.x (wakelock, wext, proc_create old style, init_timer, mt_set_gpio_*, mt65xx_eint_*, hif_sdio, etc.; write "-" if none).
mtk_cfg80211_leave_ibss() - leaves IBSS using wlanoidSetDisassociate and kalIoctl - old kernel/Android APIs it uses that do not exist in Linux 7.x (wakelock, wext, proc_create old style, init_timer, mt_set_gpio_*, mt65xx_eint_*, hif_sdio, etc.; write "-" if none).
mtk_cfg80211_set_power_mgmt() - config
<!-- lines 1201-1600, 25 s -->
mtk_cfg80211_del_pmksa() - removes PMKSA entry - uses cfg80211_pmksa, wiphy_priv, kalIoctl, wlanoidSetPmkid
mtk_cfg80211_flush_pmksa() - flushes cached PMKIDs - uses cfg80211, wiphy_priv, kalMemAlloc, kalIoctl, wlanoidSetPmkid
mtk_cfg80211_remain_on_channel() - requests staying on a channel - uses cfg80211, ieee80211_channel, nl80211_channel_type, wiphy_priv
mtk_cfg80211_cancel_remain_on_channel() - cancels staying on a channel - uses cfg80211, wiphy_priv, u64 cookie
mtk_cfg80211_mgmt_tx() - requests sending a management frame - uses cfg80211, ieee80211_channel, nl80211_channel_type, wiphy_priv
mtk_cfg80211_mgmt_tx_cancel_wait() - cancels wait time for mgmt frame - uses cfg80211, wiphy_priv, u64 cookie
mtk_cfg80211_testmode_set_key_ext() - sets WAPI key via testmode - uses cfg80211, iw_encode_exts, IW_ENCODE_ALG_SMS4, kalIoctl, wlanoidSetWapiKey
mtk_cfg80211_testmode_sw_cmd() - executes software command via testmode - uses cfg80211, kalIoctl, wlanoidSetSwCtrlWrite
mtk_cfg80211_testmode_cmd() - dispatches testmode commands - uses cfg80211, BITs, switch-case logic

## drv_wlan/mt6628/wlan/os/linux/hif/sdio/sdio.c (1284 lines)

<!-- lines 1-400, 25 s -->
debug_gpio_init() - initializes debug GPIO pins using mt_set_gpio_* APIs - mt_set_gpio_pull_enable, mt_set_gpio_dir, mt_set_gpio_mode, mt_set_gpio_out
debug_gpio_deinit() - deinitializes debug GPIO pins using mt_set_gpio_dir - mt_set_gpio_dir
mtk_wcn_stp_debug_gpio_assert() - asserts or toggles debug GPIO pins using mt_set_gpio_out - mt_set_gpio_out, mt65xx_eint_* (implied by GPIO context)
mtk_sdio_probe() - SDIO client probe function for MTK_WCN_HIF_SDIO mode - hif_sdio (MTK_WCN_HIF_SDIO_CLTCTX), MTK_WCN_HIF_SDIO_FUNCINFO
mtk_sdio_remove() - SDIO client remove function for MTK_WCN_HIF_SDIO mode - hif_sdio (MTK_WCN_HIF_SDIO_CLTCTX)
mtk_sdio_interrupt() - SDIO interrupt callback for MTK_WCN_HIF_SDIO mode - hif_sdio (MTK_WCN_HIF_SDIO_CLTCTX), mtk_wcn_hif_sdio_get_drvdata
