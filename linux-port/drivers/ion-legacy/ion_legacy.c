// SPDX-License-Identifier: GPL-2.0
/*
 * Small Android 4.4 / MTK ION ABI compatibility allocator for B8000-H.
 *
 * This is deliberately not an M4U port.  Every accepted heap mask uses the
 * same zeroed order-0 page allocator and exports the result as a dma-buf.
 */
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/fs.h>
#include <linux/gfp.h>
#include <linux/idr.h>
#include <linux/ioctl.h>
#include <linux/kref.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pagemap.h>
#include <linux/scatterlist.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#define ION_IOC_MAGIC 'I'

#define ION_HEAP_SYSTEM_MASK        BIT(0)
#define ION_HEAP_SYSTEM_CONTIG_MASK BIT(1)
#define ION_HEAP_CARVEOUT_MASK      BIT(2)
#define ION_HEAP_MULTIMEDIA_MASK    BIT(3)
#define ION_HEAP_SUPPORTED_MASK     (ION_HEAP_SYSTEM_MASK | \
	ION_HEAP_SYSTEM_CONTIG_MASK | ION_HEAP_CARVEOUT_MASK | \
	ION_HEAP_MULTIMEDIA_MASK)

#define ION_FLAG_CACHED            BIT(0)
#define ION_FLAG_CACHED_NEEDS_SYNC BIT(1)

/* Exact 32-bit Android 4.4 userspace layouts from vendor linux/ion.h. */
struct ion_allocation_data32 {
	__u32 len;
	__u32 align;
	__u32 heap_mask;
	__u32 flags;
	__u32 handle;
};

struct ion_fd_data32 {
	__u32 handle;
	__s32 fd;
};

struct ion_handle_data32 {
	__u32 handle;
};

struct ion_custom_data32 {
	__u32 cmd;
	__u32 arg;
};

#define ION_IOC_ALLOC  _IOWR(ION_IOC_MAGIC, 0, struct ion_allocation_data32)
#define ION_IOC_FREE   _IOWR(ION_IOC_MAGIC, 1, struct ion_handle_data32)
#define ION_IOC_MAP    _IOWR(ION_IOC_MAGIC, 2, struct ion_fd_data32)
#define ION_IOC_SHARE  _IOWR(ION_IOC_MAGIC, 4, struct ion_fd_data32)
#define ION_IOC_IMPORT _IOWR(ION_IOC_MAGIC, 5, struct ion_fd_data32)
#define ION_IOC_CUSTOM _IOWR(ION_IOC_MAGIC, 6, struct ion_custom_data32)
#define ION_IOC_SYNC   _IOWR(ION_IOC_MAGIC, 7, struct ion_fd_data32)

enum ion_mtk_cmd {
	ION_CMD_SYSTEM = 0,
	ION_CMD_MULTIMEDIA = 1,
};

enum ion_mtk_system_cmd {
	ION_SYS_CACHE_SYNC = 0,
	ION_SYS_GET_PHYS = 1,
	ION_SYS_GET_CLIENT = 2,
	ION_SYS_RECORD = 3,
};

enum ion_mtk_mm_cmd {
	ION_MM_CONFIG_BUFFER = 0,
	/* Observed in the stock gralloc, absent from the supplied old header. */
	ION_MM_GET_DEBUG_INFO = 2,
};

/* Exact ARM32 MTK nested layouts, including the large RECORD union member. */
struct ion_mtk_mapping32 {
	__u32 name;
	__u32 address;
	__u32 size;
};

struct ion_mtk_record32 {
	__s32 group_id;
	__s32 pid;
	__u32 action;
	__u32 address_type;
	__u32 address;
	__u32 length;
	__u32 backtrace[10];
	__u32 kernel_symbol[10];
	struct ion_mtk_mapping32 mapping_record[10];
	__u32 backtrace_num;
	__u32 handle;
	__u32 client;
	__u32 buffer;
	__s32 fd;
};

struct ion_mtk_system_data32 {
	__u32 sys_cmd;
	union {
		struct { __u32 handle; __u32 sync_type; } cache_sync;
		struct { __u32 handle; __u32 phys; __u32 len; } get_phys;
		struct { __u32 client; } get_client;
		struct ion_mtk_record32 record;
	};
};

struct ion_mtk_mm_data32 {
	__u32 mm_cmd;
	union {
		struct {
			__u32 handle;
			__s32 module_id;
			__u32 security;
			__u32 coherent;
		} config;
	};
};

struct ion_legacy_buffer {
	struct kref ref;
	struct page **pages;
	unsigned int nr_pages;
	size_t len;
	unsigned int flags;
};

struct ion_legacy_handle {
	struct ion_legacy_buffer *buffer;
	unsigned int refs;
};

struct ion_legacy_client {
	struct mutex lock;
	struct idr handles;
};

static const struct dma_buf_ops ion_legacy_dma_buf_ops;
static struct miscdevice ion_legacy_miscdev;

/*
 * Clean and invalidate the CPU caches for every page of the buffer.
 * flush_dcache_page() is a no-op on VIPT non-aliasing ARMv7 (Cortex-A7), so
 * use a map/unmap round trip of the DMA API on our own device instead.
 */
static void ion_legacy_sync_pages(struct ion_legacy_buffer *buffer)
{
	struct device *dev = ion_legacy_miscdev.this_device;
	unsigned int i;
	dma_addr_t addr;

	for (i = 0; i < buffer->nr_pages; i++) {
		addr = dma_map_page(dev, buffer->pages[i], 0, PAGE_SIZE,
				    DMA_BIDIRECTIONAL);
		if (!dma_mapping_error(dev, addr))
			dma_unmap_page(dev, addr, PAGE_SIZE, DMA_BIDIRECTIONAL);
	}
}

static void ion_legacy_buffer_release(struct kref *ref)
{
	struct ion_legacy_buffer *buffer = container_of(ref,
		struct ion_legacy_buffer, ref);
	unsigned int i;

	for (i = 0; i < buffer->nr_pages; i++)
		__free_page(buffer->pages[i]);
	kfree(buffer->pages);
	kfree(buffer);
}

static void ion_legacy_buffer_put(struct ion_legacy_buffer *buffer)
{
	kref_put(&buffer->ref, ion_legacy_buffer_release);
}

/*
 * The display (mediatek-drm, no IOMMU) can only scan out physically
 * contiguous buffers, and the GPU renders into gralloc buffers from here.
 * So try one high-order block for buffers of contig_min bytes and up (a
 * 1280x800 frame fits order 10) and split it into order-0 pages, keeping
 * the rest of the driver page based.  Falls back to single pages.
 */
static unsigned int contig_min = SZ_256K;
module_param(contig_min, uint, 0644);
MODULE_PARM_DESC(contig_min, "Allocate buffers >= this many bytes contiguously (0: never)");

static bool ion_legacy_alloc_contig(struct ion_legacy_buffer *buffer)
{
	unsigned int order = get_order(buffer->len), i;
	struct page *page;

	if (!contig_min || buffer->len < contig_min || order > MAX_PAGE_ORDER)
		return false;
	page = alloc_pages(GFP_KERNEL | __GFP_ZERO | __GFP_NOWARN |
			   __GFP_NORETRY, order);
	if (!page)
		return false;
	split_page(page, order);
	for (i = 0; i < (1U << order); i++) {
		if (i < buffer->nr_pages)
			buffer->pages[i] = page + i;
		else
			__free_page(page + i);	/* tail beyond the buffer */
	}
	return true;
}

static struct ion_legacy_buffer *ion_legacy_buffer_alloc(size_t len,
	unsigned int flags)
{
	struct ion_legacy_buffer *buffer;
	unsigned int i;

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer)
		return ERR_PTR(-ENOMEM);

	buffer->len = PAGE_ALIGN(len);
	buffer->nr_pages = buffer->len >> PAGE_SHIFT;
	buffer->flags = flags;
	buffer->pages = kcalloc(buffer->nr_pages, sizeof(*buffer->pages),
		GFP_KERNEL);
	if (!buffer->pages) {
		kfree(buffer);
		return ERR_PTR(-ENOMEM);
	}

	kref_init(&buffer->ref);
	if (ion_legacy_alloc_contig(buffer))
		goto sync;
	for (i = 0; i < buffer->nr_pages; i++) {
		buffer->pages[i] = alloc_page(GFP_KERNEL | __GFP_ZERO);
		if (!buffer->pages[i]) {
			buffer->nr_pages = i;
			ion_legacy_buffer_put(buffer);
			return ERR_PTR(-ENOMEM);
		}
	}
sync:
	/*
	 * __GFP_ZERO wrote through the cached linear map: push the zeroes out
	 * so stale dirty lines cannot later overwrite non-cached user or GPU
	 * writes.
	 */
	ion_legacy_sync_pages(buffer);

	return buffer;
}

static struct ion_legacy_buffer *ion_legacy_handle_get(
	struct ion_legacy_client *client, __u32 id)
{
	struct ion_legacy_handle *handle;
	struct ion_legacy_buffer *buffer = NULL;

	if (!id)
		return NULL;
	mutex_lock(&client->lock);
	handle = idr_find(&client->handles, id);
	if (handle) {
		buffer = handle->buffer;
		kref_get(&buffer->ref);
	}
	mutex_unlock(&client->lock);
	return buffer;
}

static int ion_legacy_add_handle(struct ion_legacy_client *client,
	struct ion_legacy_buffer *buffer, bool reuse, __u32 *id_out,
	bool *created)
{
	struct ion_legacy_handle *handle;
	int id;
	int ret;

	mutex_lock(&client->lock);
	if (reuse) {
		idr_for_each_entry(&client->handles, handle, id) {
			if (handle->buffer == buffer) {
				handle->refs++;
				*id_out = (__u32)id;
				if (created)
					*created = false;
				mutex_unlock(&client->lock);
				return 0;
			}
		}
	}
	mutex_unlock(&client->lock);

	handle = kzalloc(sizeof(*handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;
	handle->buffer = buffer;
	handle->refs = 1;

	mutex_lock(&client->lock);
	ret = idr_alloc(&client->handles, handle, 1, 0, GFP_KERNEL);
	mutex_unlock(&client->lock);
	if (ret < 0) {
		kfree(handle);
		return ret;
	}
	*id_out = ret;
	if (created)
		*created = true;
	return 0;
}

static int ion_legacy_free_handle(struct ion_legacy_client *client, __u32 id)
{
	struct ion_legacy_handle *handle;

	mutex_lock(&client->lock);
	handle = idr_find(&client->handles, id);
	if (!handle) {
		mutex_unlock(&client->lock);
		return -EINVAL;
	}
	if (--handle->refs) {
		mutex_unlock(&client->lock);
		return 0;
	}
	idr_remove(&client->handles, id);
	mutex_unlock(&client->lock);

	ion_legacy_buffer_put(handle->buffer);
	kfree(handle);
	return 0;
}

static struct sg_table *ion_legacy_map_dma_buf(
	struct dma_buf_attachment *attachment, enum dma_data_direction direction)
{
	struct ion_legacy_buffer *buffer = attachment->dmabuf->priv;
	struct sg_table *sgt;
	int ret;

	sgt = kzalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt)
		return ERR_PTR(-ENOMEM);
	ret = sg_alloc_table_from_pages(sgt, buffer->pages, buffer->nr_pages,
		0, buffer->len, GFP_KERNEL);
	if (ret)
		goto err_free;
	ret = dma_map_sgtable(attachment->dev, sgt, direction, 0);
	if (ret)
		goto err_sg;
	return sgt;

err_sg:
	sg_free_table(sgt);
err_free:
	kfree(sgt);
	return ERR_PTR(ret);
}

static void ion_legacy_unmap_dma_buf(struct dma_buf_attachment *attachment,
	struct sg_table *sgt, enum dma_data_direction direction)
{
	dma_unmap_sgtable(attachment->dev, sgt, direction, 0);
	sg_free_table(sgt);
	kfree(sgt);
}

static void ion_legacy_dma_buf_release(struct dma_buf *dmabuf)
{
	ion_legacy_buffer_put(dmabuf->priv);
}

static void ion_legacy_flush_pages(struct ion_legacy_buffer *buffer)
{
	if (buffer->flags & ION_FLAG_CACHED)
		ion_legacy_sync_pages(buffer);
}

static int ion_legacy_begin_cpu_access(struct dma_buf *dmabuf,
	enum dma_data_direction direction)
{
	ion_legacy_flush_pages(dmabuf->priv);
	return 0;
}

static int ion_legacy_end_cpu_access(struct dma_buf *dmabuf,
	enum dma_data_direction direction)
{
	ion_legacy_flush_pages(dmabuf->priv);
	return 0;
}

static int ion_legacy_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct ion_legacy_buffer *buffer = dmabuf->priv;

	/*
	 * Like the vendor ION: uncached means write-combined Normal memory.
	 * pgprot_noncached() is Strongly-ordered on ARMv7 and faults on the
	 * unaligned accesses memcpy() makes.
	 */
	if (!(buffer->flags & ION_FLAG_CACHED))
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	return vm_map_pages(vma, buffer->pages, buffer->nr_pages);
}

static int ion_legacy_vmap(struct dma_buf *dmabuf, struct iosys_map *map)
{
	struct ion_legacy_buffer *buffer = dmabuf->priv;
	pgprot_t prot = PAGE_KERNEL;
	void *addr;

	if (!(buffer->flags & ION_FLAG_CACHED))
		prot = pgprot_writecombine(PAGE_KERNEL);
	addr = vmap(buffer->pages, buffer->nr_pages, VM_MAP, prot);
	if (!addr)
		return -ENOMEM;
	iosys_map_set_vaddr(map, addr);
	return 0;
}

static void ion_legacy_vunmap(struct dma_buf *dmabuf, struct iosys_map *map)
{
	vunmap(map->vaddr);
}

static const struct dma_buf_ops ion_legacy_dma_buf_ops = {
	.map_dma_buf = ion_legacy_map_dma_buf,
	.unmap_dma_buf = ion_legacy_unmap_dma_buf,
	.release = ion_legacy_dma_buf_release,
	.begin_cpu_access = ion_legacy_begin_cpu_access,
	.end_cpu_access = ion_legacy_end_cpu_access,
	.mmap = ion_legacy_mmap,
	.vmap = ion_legacy_vmap,
	.vunmap = ion_legacy_vunmap,
};

static int ion_legacy_export_fd(struct ion_legacy_buffer *buffer)
{
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	struct dma_buf *dmabuf;
	int fd;

	exp_info.ops = &ion_legacy_dma_buf_ops;
	exp_info.size = buffer->len;
	exp_info.flags = O_RDWR;
	exp_info.priv = buffer;
	kref_get(&buffer->ref);
	dmabuf = dma_buf_export(&exp_info);
	if (IS_ERR(dmabuf)) {
		ion_legacy_buffer_put(buffer);
		return PTR_ERR(dmabuf);
	}
	fd = dma_buf_fd(dmabuf, O_CLOEXEC);
	if (fd < 0)
		dma_buf_put(dmabuf);
	return fd;
}

static int ion_legacy_ioctl_alloc(struct ion_legacy_client *client,
	void __user *argp)
{
	struct ion_allocation_data32 data;
	struct ion_legacy_buffer *buffer;
	int ret;

	if (copy_from_user(&data, argp, sizeof(data)))
		return -EFAULT;
	if (!data.len || !(data.heap_mask & ION_HEAP_SUPPORTED_MASK))
		return -EINVAL;
	if (!data.align)
		data.align = PAGE_SIZE;
	if (!is_power_of_2(data.align) || data.align > PAGE_SIZE)
		return -EINVAL;
	if (data.len > SIZE_MAX - (PAGE_SIZE - 1))
		return -EOVERFLOW;

	buffer = ion_legacy_buffer_alloc(data.len, data.flags);
	if (IS_ERR(buffer))
		return PTR_ERR(buffer);
	ret = ion_legacy_add_handle(client, buffer, false, &data.handle, NULL);
	if (ret) {
		ion_legacy_buffer_put(buffer);
		return ret;
	}
	if (copy_to_user(argp, &data, sizeof(data))) {
		ion_legacy_free_handle(client, data.handle);
		return -EFAULT;
	}
	return 0;
}

static int ion_legacy_ioctl_free(struct ion_legacy_client *client,
	void __user *argp)
{
	struct ion_handle_data32 data;

	if (copy_from_user(&data, argp, sizeof(data)))
		return -EFAULT;
	return ion_legacy_free_handle(client, data.handle);
}

static int ion_legacy_ioctl_share(struct ion_legacy_client *client,
	void __user *argp)
{
	struct ion_fd_data32 data;
	struct ion_legacy_buffer *buffer;
	int fd;

	if (copy_from_user(&data, argp, sizeof(data)))
		return -EFAULT;
	buffer = ion_legacy_handle_get(client, data.handle);
	if (!buffer)
		return -EINVAL;
	fd = ion_legacy_export_fd(buffer);
	ion_legacy_buffer_put(buffer);
	if (fd < 0)
		return fd;
	data.fd = fd;
	if (copy_to_user(argp, &data, sizeof(data))) {
		close_fd(fd);
		return -EFAULT;
	}
	return 0;
}

static int ion_legacy_ioctl_import(struct ion_legacy_client *client,
	void __user *argp)
{
	struct ion_fd_data32 data;
	struct ion_legacy_buffer *buffer;
	struct dma_buf *dmabuf;
	bool created;
	int ret;

	if (copy_from_user(&data, argp, sizeof(data)))
		return -EFAULT;
	dmabuf = dma_buf_get(data.fd);
	if (IS_ERR(dmabuf))
		return PTR_ERR(dmabuf);
	if (dmabuf->ops != &ion_legacy_dma_buf_ops) {
		dma_buf_put(dmabuf);
		return -EINVAL;
	}
	buffer = dmabuf->priv;
	ret = ion_legacy_add_handle(client, buffer, true, &data.handle, &created);
	if (!ret && created)
		kref_get(&buffer->ref);
	dma_buf_put(dmabuf);
	if (ret)
		return ret;
	if (copy_to_user(argp, &data, sizeof(data))) {
		ion_legacy_free_handle(client, data.handle);
		return -EFAULT;
	}
	return 0;
}

static int ion_legacy_ioctl_sync(void __user *argp)
{
	struct ion_fd_data32 data;
	struct dma_buf *dmabuf;
	int ret;

	if (copy_from_user(&data, argp, sizeof(data)))
		return -EFAULT;
	dmabuf = dma_buf_get(data.fd);
	if (IS_ERR(dmabuf))
		return PTR_ERR(dmabuf);
	if (dmabuf->ops != &ion_legacy_dma_buf_ops) {
		ret = -EINVAL;
		goto out;
	}
	ret = dma_buf_begin_cpu_access(dmabuf, DMA_BIDIRECTIONAL);
	if (!ret)
		ret = dma_buf_end_cpu_access(dmabuf, DMA_BIDIRECTIONAL);
out:
	dma_buf_put(dmabuf);
	return ret;
}

static bool ion_legacy_phys_contiguous(struct ion_legacy_buffer *buffer,
	__u32 *phys)
{
	unsigned int i;
	unsigned long pfn = page_to_pfn(buffer->pages[0]);

	for (i = 1; i < buffer->nr_pages; i++)
		if (page_to_pfn(buffer->pages[i]) != pfn + i)
			return false;
	*phys = (__u32)page_to_phys(buffer->pages[0]);
	return true;
}

static int ion_legacy_custom_system(struct ion_legacy_client *client,
	void __user *argp)
{
	struct ion_mtk_system_data32 data;
	struct ion_legacy_buffer *buffer;
	int ret = 0;

	if (copy_from_user(&data, argp, sizeof(data)))
		return -EFAULT;
	switch (data.sys_cmd) {
	case ION_SYS_CACHE_SYNC:
		buffer = ion_legacy_handle_get(client, data.cache_sync.handle);
		if (!buffer)
			return -EINVAL;
		ion_legacy_flush_pages(buffer);
		ion_legacy_buffer_put(buffer);
		break;
	case ION_SYS_GET_PHYS:
		buffer = ion_legacy_handle_get(client, data.get_phys.handle);
		if (!buffer)
			return -EINVAL;
		if (!ion_legacy_phys_contiguous(buffer, &data.get_phys.phys)) {
			data.get_phys.phys = 0;
			data.get_phys.len = 0;
			ret = -EFAULT;
		} else {
			data.get_phys.len = buffer->len;
		}
		ion_legacy_buffer_put(buffer);
		break;
	case ION_SYS_GET_CLIENT:
		/* Do not expose a kernel pointer as the legacy driver did. */
		data.get_client.client = 0;
		break;
	case ION_SYS_RECORD:
		/* Vendor allocation debugger metadata is intentionally a no-op. */
		break;
	default:
		pr_warn_ratelimited("ion_legacy: unknown SYSTEM command %u\n",
			data.sys_cmd);
		return -ENOTTY;
	}
	if (copy_to_user(argp, &data, sizeof(data)))
		return -EFAULT;
	return ret;
}

static int ion_legacy_custom_mm(struct ion_legacy_client *client,
	void __user *argp)
{
	struct ion_mtk_mm_data32 data;
	struct ion_legacy_buffer *buffer;

	if (copy_from_user(&data, argp, sizeof(data)))
		return -EFAULT;
	switch (data.mm_cmd) {
	case ION_MM_CONFIG_BUFFER:
		buffer = ion_legacy_handle_get(client, data.config.handle);
		if (!buffer)
			return -EINVAL;
		ion_legacy_buffer_put(buffer);
		break;
	case ION_MM_GET_DEBUG_INFO:
		/* Observed stock-gralloc query: no allocator-debug state to return. */
		break;
	default:
		pr_warn_ratelimited("ion_legacy: unknown MULTIMEDIA command %u\n",
			data.mm_cmd);
		return -ENOTTY;
	}
	if (copy_to_user(argp, &data, sizeof(data)))
		return -EFAULT;
	return 0;
}

static int ion_legacy_ioctl_custom(struct ion_legacy_client *client,
	void __user *argp)
{
	struct ion_custom_data32 data;
	void __user *nested;

	if (copy_from_user(&data, argp, sizeof(data)))
		return -EFAULT;
	nested = (void __user *)(uintptr_t)data.arg;
	switch (data.cmd) {
	case ION_CMD_SYSTEM:
		return ion_legacy_custom_system(client, nested);
	case ION_CMD_MULTIMEDIA:
		return ion_legacy_custom_mm(client, nested);
	default:
		pr_warn_ratelimited("ion_legacy: unknown CUSTOM command %u\n",
			data.cmd);
		return -ENOTTY;
	}
}

static long ion_legacy_ioctl(struct file *file, unsigned int cmd,
	unsigned long arg)
{
	struct ion_legacy_client *client = file->private_data;
	void __user *argp = (void __user *)arg;

	switch (cmd) {
	case ION_IOC_ALLOC:
		return ion_legacy_ioctl_alloc(client, argp);
	case ION_IOC_FREE:
		return ion_legacy_ioctl_free(client, argp);
	case ION_IOC_MAP:
	case ION_IOC_SHARE:
		return ion_legacy_ioctl_share(client, argp);
	case ION_IOC_IMPORT:
		return ion_legacy_ioctl_import(client, argp);
	case ION_IOC_SYNC:
		return ion_legacy_ioctl_sync(argp);
	case ION_IOC_CUSTOM:
		return ion_legacy_ioctl_custom(client, argp);
	default:
		pr_warn_ratelimited("ion_legacy: unknown ioctl %#x\n", cmd);
		return -ENOTTY;
	}
}

static int ion_legacy_open(struct inode *inode, struct file *file)
{
	struct ion_legacy_client *client;

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return -ENOMEM;
	mutex_init(&client->lock);
	idr_init(&client->handles);
	file->private_data = client;
	return 0;
}

static int ion_legacy_release(struct inode *inode, struct file *file)
{
	struct ion_legacy_client *client = file->private_data;
	struct ion_legacy_handle *handle;
	int id;

	idr_for_each_entry(&client->handles, handle, id) {
		ion_legacy_buffer_put(handle->buffer);
		kfree(handle);
	}
	idr_destroy(&client->handles);
	kfree(client);
	return 0;
}

static const struct file_operations ion_legacy_fops = {
	.owner = THIS_MODULE,
	.open = ion_legacy_open,
	.release = ion_legacy_release,
	.unlocked_ioctl = ion_legacy_ioctl,
	.compat_ioctl = ion_legacy_ioctl,
	.llseek = noop_llseek,
};

static struct miscdevice ion_legacy_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "ion",
	.fops = &ion_legacy_fops,
	.mode = 0666,
};

static int __init ion_legacy_init(void)
{
	int ret;

	BUILD_BUG_ON(sizeof(struct ion_allocation_data32) != 20);
	BUILD_BUG_ON(sizeof(struct ion_fd_data32) != 8);
	BUILD_BUG_ON(sizeof(struct ion_handle_data32) != 4);
	BUILD_BUG_ON(sizeof(struct ion_custom_data32) != 8);
	BUILD_BUG_ON(sizeof(struct ion_mtk_mm_data32) != 20);
	BUILD_BUG_ON(sizeof(struct ion_mtk_system_data32) != 248);
	ret = misc_register(&ion_legacy_miscdev);
	if (ret)
		return ret;
	/* Needed for the cache maintenance in ion_legacy_sync_pages(). */
	ret = dma_coerce_mask_and_coherent(ion_legacy_miscdev.this_device,
					   DMA_BIT_MASK(32));
	if (ret)
		misc_deregister(&ion_legacy_miscdev);
	return ret;
}

static void __exit ion_legacy_exit(void)
{
	misc_deregister(&ion_legacy_miscdev);
}

module_init(ion_legacy_init);
module_exit(ion_legacy_exit);

MODULE_DESCRIPTION("Android 4.4 legacy ION dma-buf compatibility allocator");
MODULE_AUTHOR("B8000-H Linux port");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("DMA_BUF");
