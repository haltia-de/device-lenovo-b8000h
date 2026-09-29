OPTIM := -Os

ccflags-y := \
	-I$(MTK_PATH_PLATFORM)/drivers/gpu/pvr/include4 \
	-I$(MTK_PATH_PLATFORM)/drivers/gpu/pvr/services4/include \
	-I$(MTK_PATH_PLATFORM)/drivers/gpu/pvr/services4/system/include \
	-I$(MTK_PATH_PLATFORM)/drivers/gpu/pvr/services4/srvkm/hwdefs \
	-I$(MTK_PATH_PLATFORM)/drivers/gpu/pvr/services4/srvkm/bridged \
	-I$(MTK_PATH_PLATFORM)/drivers/gpu/pvr/services4/srvkm/bridged/sgx \
	-I$(MTK_PATH_PLATFORM)/drivers/gpu/pvr/services4/srvkm/common \
	-I$(MTK_PATH_PLATFORM)/drivers/gpu/pvr/services4/srvkm/devices/sgx \
	-I$(MTK_PATH_PLATFORM)/drivers/gpu/pvr/services4/srvkm/env/linux \
	-I$(MTK_PATH_PLATFORM)/drivers/gpu/pvr/services4/srvkm/include \
	-I$(MTK_PATH_PLATFORM)/drivers/gpu/pvr/services4/3rdparty/dc_nohw \
	-I$(MTK_PATH_PLATFORM)/drivers/gpu/pvr/services4/system/mt6589 \
	-I$(MTK_PATH_PLATFORM)/drivers/gpu/pvr/services4/system/mtk_common

ccflags-y += \
	-fno-strict-aliasing \
	-W -Wall \
	-Wno-missing-field-initializers \
	-Wmissing-format-attribute \
	-Wdeclaration-after-statement \
	-Wno-format-zero-length \
	-Wmissing-prototypes \
	-Wstrict-prototypes \
	-Wno-unused-parameter \
	-Wno-sign-compare \
	-Wno-type-limits \
	-D__linux__ $(OPTIM) -g \
	-include $(MTK_PATH_PLATFORM)/drivers/gpu/pvr/include4/config_kernel_user.h \
	-DDC_NOHW_DISCONTIG_BUFFERS \
	-DDC_NOHW_BUFFER_WIDTH=1280 \
	-DDC_NOHW_BUFFER_HEIGHT=800

dc_nohw-y := \
	services4/3rdparty/dc_nohw/dc_nohw_displayclass.o \
	services4/3rdparty/dc_nohw/dc_nohw_linux.o

obj-m += dc_nohw.o
