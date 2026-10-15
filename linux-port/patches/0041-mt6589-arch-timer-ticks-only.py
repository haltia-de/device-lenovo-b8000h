# SPDX-License-Identifier: GPL-2.0
# Apply to /opt/b8000h/linux-drm: bring the Cortex-A7 generic timer back
# on MT6589, for per-CPU ticks only.
#
# The system counter is GPT6, 64-bit and free-running, but the CPUs see its
# high word through a latch that only moves when GPT6's registers are read.
# Read by nobody, CNTPCT jumps back 2^32 ticks (330 s at 13 MHz) at every
# wrap of the low word: that, not a 32-bit counter, hung the tablet in
# September (see docs/power.md). The comparators compare against the same
# visible count, so per-CPU clockevents work through any wrap; only time
# must not be read from CNTPCT. So on MT6589 the arch timer registers its
# clockevents and its delay timer, but no clocksource and no sched_clock:
# time and sched_clock stay on the MediaTek GPT. With a real timer on every
# CPU, NO_HZ_IDLE and high-resolution timers can work (patch 0042).
K = '/opt/b8000h/linux-drm/'


def ed(p, old, new, marker):
    s = open(K + p).read()
    if marker in s:
        print('already', p)
        return
    assert s.count(old) == 1, (p, old[:60])
    open(K + p, 'w').write(s.replace(old, new))
    print('patched', p)


ed('drivers/clocksource/arm_arch_timer.c', '''	arch_timer_read_counter = rd;
	clocksource_counter.vdso_clock_mode = vdso_default;
''', '''	arch_timer_read_counter = rd;

	/*
	 * MT6589: CNTPCT's high word only follows GPT6 when GPT6 is read, so
	 * the count jumps back 2^32 at each wrap of the low word. Ticks are
	 * fine (the comparators see the same count); time is not. Leave the
	 * clocksource and sched_clock to the MediaTek GPT.
	 */
	if (of_machine_is_compatible("mediatek,mt6589")) {
		pr_info("arch_timer: MT6589, clockevents only; time from the GPT\\n");
		return;
	}

	clocksource_counter.vdso_clock_mode = vdso_default;
''', 'clockevents only; time from the GPT')

ed('arch/arm/boot/dts/mediatek/mt6589-lenovo-b8000-h.dts', '''/*
 * mediatek-gpt-tick: the arch timer comparator dies after the 32-bit counter
 * wrap (~330 s at 13 MHz). Tick, keep time and sched_clock from the MediaTek
 * GPT (timer@10008000) as the vendor 3.4 kernel does.
 */
&{/timer} {
	status = "disabled";
};
''', '''/*
 * The arch timer gives each CPU its own tick (patch 0041); time and
 * sched_clock come from the MediaTek GPT (timer@10008000), because CNTPCT's
 * high word lags GPT6 until GPT6 is read. The GPT also stays the broadcast
 * device.
 */
&{/timer} {
	status = "okay";
};
''', 'gives each CPU its own tick')

# An event must not cross the wrap of the visible low word: CVAL would then
# carry into H+1 while the visible count goes back to H:0 and, with nobody
# reading GPT6, never reaches it. That stopped a CPU's timer at the second
# wrap (660 s): the haptic pulse never ended and the watchdog reset the
# tablet. So before each event: read GPT6 to bring the high word up to
# date, then end the event 5 ms before the wrap: the compare condition
# holds only until the wrap, and an event on the last tick (the first
# version, k23w) was missed at the 8th wrap. A cut event only fires early;
# the next programming, after the wrap, latches the new high word.
ed('drivers/clocksource/arm_arch_timer.c', '''static __always_inline void set_next_event(const int access, unsigned long evt,
					   struct clock_event_device *clk)
{
	unsigned long ctrl;
	u64 cnt;

	ctrl = arch_timer_reg_read_cp15(access, ARCH_TIMER_REG_CTRL);
	ctrl |= ARCH_TIMER_CTRL_ENABLE;
	ctrl &= ~ARCH_TIMER_CTRL_IT_MASK;

	if (access == ARCH_TIMER_PHYS_ACCESS)
		cnt = __arch_counter_get_cntpct();
	else
		cnt = __arch_counter_get_cntvct();
''', '''/* MT6589: the GPT block; reading GPT6 latches CNTPCT's high word. */
static void __iomem *mt6589_gpt;
#define MT6589_WRAP_GUARD	65000	/* 5 ms at 13 MHz */

static __always_inline void set_next_event(const int access, unsigned long evt,
					   struct clock_event_device *clk)
{
	unsigned long ctrl;
	u64 cnt;

	ctrl = arch_timer_reg_read_cp15(access, ARCH_TIMER_REG_CTRL);
	ctrl |= ARCH_TIMER_CTRL_ENABLE;
	ctrl &= ~ARCH_TIMER_CTRL_IT_MASK;

	if (mt6589_gpt) {
		readl_relaxed(mt6589_gpt + 0x78);	/* GPT6 CNT_H */
		readl(mt6589_gpt + 0x68);		/* GPT6 CNT_L */
	}

	if (access == ARCH_TIMER_PHYS_ACCESS)
		cnt = __arch_counter_get_cntpct();
	else
		cnt = __arch_counter_get_cntvct();

	/*
	 * Keep the event 5 ms clear of the wrap of the visible low word. The
	 * compare condition only holds until the wrap (then the count is back
	 * at H:0), so an event on the last tick was lost by a CPU that had
	 * interrupts off at that moment: k23w hung at the 8th wrap. Within the
	 * last 5 ms, fire at once; the CPU reprograms until the wrap has passed
	 * and the GPT6 read above has moved the high word on.
	 */
	if (mt6589_gpt) {
		u32 left = 0xffffffffU - (u32)cnt;

		if (left <= MT6589_WRAP_GUARD)
			evt = 0;
		else if (evt > left - MT6589_WRAP_GUARD)
			evt = left - MT6589_WRAP_GUARD;
	}
''', 'mt6589_gpt + 0x78')

ed('drivers/clocksource/arm_arch_timer.c', '''	arch_timer_c3stop = !of_property_read_bool(np, "always-on");
''', '''	arch_timer_c3stop = !of_property_read_bool(np, "always-on");

	if (of_machine_is_compatible("mediatek,mt6589")) {
		struct device_node *gpt;

		gpt = of_find_compatible_node(NULL, NULL, "mediatek,mt6577-timer");
		mt6589_gpt = gpt ? of_iomap(gpt, 0) : NULL;
		of_node_put(gpt);
		if (!mt6589_gpt)
			pr_warn("MT6589 without its GPT: events may stall at a counter wrap\\n");
	}
''', 'mt6589_gpt = gpt ? of_iomap')
