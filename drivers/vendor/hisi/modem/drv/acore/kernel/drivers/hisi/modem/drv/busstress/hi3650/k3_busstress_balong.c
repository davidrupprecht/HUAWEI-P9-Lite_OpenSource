/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2012-2015. All rights reserved.
 * foss@huawei.com
 *
 * If distributed as part of the Linux kernel, the following license terms
 * apply:
 *
 * * This program is free software; you can redistribute it and/or modify
 * * it under the terms of the GNU General Public License version 2 and 
 * * only version 2 as published by the Free Software Foundation.
 * *
 * * This program is distributed in the hope that it will be useful,
 * * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * * GNU General Public License for more details.
 * *
 * * You should have received a copy of the GNU General Public License
 * * along with this program; if not, write to the Free Software
 * * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 *
 * Otherwise, the following license terms apply:
 *
 * * Redistribution and use in source and binary forms, with or without
 * * modification, are permitted provided that the following conditions
 * * are met:
 * * 1) Redistributions of source code must retain the above copyright
 * *    notice, this list of conditions and the following disclaimer.
 * * 2) Redistributions in binary form must reproduce the above copyright
 * *    notice, this list of conditions and the following disclaimer in the
 * *    documentation and/or other materials provided with the distribution.
 * * 3) Neither the name of Huawei nor the names of its contributors may 
 * *    be used to endorse or promote products derived from this software 
 * *    without specific prior written permission.
 * 
 * * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/semaphore.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include "drv_comm.h"
#include "mdrv_memory.h"
#include "bsp_busstress.h"
#include <bsp_hardtimer.h>
#include "bsp_ipf.h"

BSP_S32 stress_test_rate = 2;
extern BSP_S32 SOCP_ST_ENCODE_STRESS(void);
extern BSP_S32 SOCP_ST_DECODE_STRESS(void);
//extern BSP_S32 ipf_ul_stress_test_start(BSP_S32 task_priority);
extern BSP_VOID ipf_ul_stress_test_stop();

unsigned int busstress_ddr_memcpy_count = 0;
int busstress_ddr_memcpy_delay = 10;
bool busstress_ddr_memcpy_run  = false;
int g_busstress_show_bandwidth_delay = 60000; /* 60000ms,1min */

int busstress_memcpy_threadfunc(void* data)
{
    void* pSrcAddr = NULL;
    void* pDstAddr = NULL;

    int delay =  *((int*)data);

    pSrcAddr = kmalloc(DDR_MEM_SIZE_FOR_AP, GFP_KERNEL);
    if(NULL == pSrcAddr)
    {
        return ERROR;
    }

    pDstAddr = kmalloc(DDR_MEM_SIZE_FOR_AP, GFP_KERNEL);
    if(NULL == pDstAddr)
    {
        kfree(pSrcAddr);
        return ERROR;
    }

    while(busstress_ddr_memcpy_run)
	{
		if(0 != memcpyTestProcess(pSrcAddr, pDstAddr, DDR_MEM_SIZE_FOR_AP))
		{
			printk("busstress_ddr_memcpy_stress_test failed.\n");
			break;
		}

        //dma_map_single(NULL, pSrcAddr, s32Size, DMA_TO_DEVICE);
	    //dma_map_single(NULL, pDstAddr, s32Size, DMA_TO_DEVICE);
	    
		busstress_ddr_memcpy_count++;
		if(0 == (busstress_ddr_memcpy_count%5))
		{
			msleep(delay);
		}
	}

    kfree(pSrcAddr);
    kfree(pDstAddr);
    
	return BSP_OK;
}

void busstress_ddr_memcpy_test_start(int delay)
{
    busstress_ddr_memcpy_delay = delay;
    busstress_ddr_memcpy_run   = true;       
    kthread_run(busstress_memcpy_threadfunc, &busstress_ddr_memcpy_delay, "ddrmemcpytask", 0, 0);
}

void busstress_ddr_memcpy_test_stop()
{
    busstress_ddr_memcpy_run   = false;
    busstress_ddr_memcpy_count = 0;
}

void busstress_info_print()
{
    printk("ddr_memcpy_test info begin:\n");
    printk("busstress_ddr_memcpy_count = %d\n", busstress_ddr_memcpy_count);
    printk("busstress_ddr_memcpy_delay = %d\n", busstress_ddr_memcpy_delay);
    printk("busstress_ddr_memcpy_run   = %d\n", busstress_ddr_memcpy_run);
    printk("ddr_memcpy_test info end:\n");
}

enum busstress_module_no{
	BUSSTRESS_TEST_IPF,                      /* 0 */
	BUSSTRESS_TEST_SOCP,                     /* 1 */
	BUSSTRESS_TEST_AP,                       /* 2 */
	BUSSTRESS_TEST_BUTTOM                      
};

unsigned int g_busstress_test_en_run[BUSSTRESS_TEST_BUTTOM] = 
{
	1,	/* BUSSTRESS_TEST_IPF 					  */
	1,	/* BUSSTRESS_TEST_SOCP				      */
	1 	/* BUSSTRESS_TEST_AP					  */
};


static unsigned int g_print_cnt = 0;

struct ap_memcpy_bandwidth{
	unsigned int slice_delta;  /* 此次统计使用的slice，单位: 1/32768 秒 */
	unsigned int bytes_delta;  /* 此次的数据量，单位:字节 */
};

#define AP_MEMCPY_CNT_MAX   1500
struct ap_memcpy_bandwidth g_ap_memcpy_bandwidth[AP_MEMCPY_CNT_MAX];

int busstress_ap_memcpy_bandwidth_threadfunc(void* data)
{
    unsigned int memcpy_cnt_start = 0;
	unsigned int memcpy_cnt_end = 0;
	unsigned int memcpy_start_slice = 0;
	unsigned int memcpy_end_slice = 0;
	unsigned int cnt_delta = 0;
	unsigned int bytes_delta = 0;  
	unsigned int slice_delta = 0; 
	
    int delay =  *((int*)data);

    for ( ; ; )
	{
		
	    memcpy_cnt_start = busstress_ddr_memcpy_count;
		memcpy_start_slice = bsp_get_slice_value();
		msleep(delay);
		memcpy_cnt_end = busstress_ddr_memcpy_count;
		memcpy_end_slice = bsp_get_slice_value();
		slice_delta = get_timer_slice_delta(memcpy_start_slice, memcpy_end_slice);
		g_ap_memcpy_bandwidth[g_print_cnt % AP_MEMCPY_CNT_MAX].slice_delta = slice_delta;

		printk("-----------------ap print_cnt=%d\n", g_print_cnt);
		printk("-----------------ap memcpy slice_delta %d \n", slice_delta);

		/* 计算带宽 */
		cnt_delta = memcpy_cnt_end - memcpy_cnt_start;
		bytes_delta = cnt_delta * DDR_MEM_SIZE_FOR_AP;
		g_ap_memcpy_bandwidth[g_print_cnt % AP_MEMCPY_CNT_MAX].bytes_delta = bytes_delta;
		
		printk("-----------------ap memcpy bytes_delta %d \n", bytes_delta);

		
		g_print_cnt++;
		
	}
	


    return 0;
}

int busstress_ap_memcpy_bandwidth_show(void)
{
    unsigned int i = 0;

	for (i = 0; i < g_print_cnt; i++)
	{
		printk("cnt %04d slice_delta %d bytes_delta %d\n", i, g_ap_memcpy_bandwidth[i % AP_MEMCPY_CNT_MAX].slice_delta, 
			                                                g_ap_memcpy_bandwidth[i % AP_MEMCPY_CNT_MAX].bytes_delta);
	}

    return 0;
}
BSP_S32 busstress_test_start(void)
{
#if 0
	int ret = 0;

	/* IPF */
	if (1 == g_busstress_test_en_run[BUSSTRESS_TEST_IPF])
	{
		//(void)ipf_dl_stress_test_start();
		ipf_ul_stress_test_start(stress_test_rate);
		printk("acore ipf busstress test start ++++++ok\n");
	}
	else
	{
		printk("acore ipf busstress test no run******\n");
	}

    /* SOCP */
	if (1 == g_busstress_test_en_run[BUSSTRESS_TEST_SOCP])
	{
		//ret = SOCP_ST_ENCODE_STRESS();
		ret = SOCP_ST_BUSSTRESS_TEST();
		if (0 != ret)
		{
			printk("acore socp busstress test start ------fail\n");
		}
		else
		{
			printk("acore socp busstress test start ++++++ok\n");
		}
	}
	else
	{
		printk("acore socp busstress test no run******\n");
	}

#if 0
	if(0 != SOCP_ST_DECODE_STRESS())
        printk("acore socp decode stress test fail\n");
#endif
#endif

    /* ap */
	if (1 == g_busstress_test_en_run[BUSSTRESS_TEST_AP])
	{
		busstress_ddr_memcpy_test_start(stress_test_rate);
		printk("ap busstress test start ++++++ok\n");
	}
	else
	{
		printk("ap busstress test no run******\n");
	}


	/* 起任务，打印带宽 */
    kthread_run(busstress_ap_memcpy_bandwidth_threadfunc, &g_busstress_show_bandwidth_delay, "ShowApBandWidth", 0, 0);
	return OK;
}

BSP_S32 busstress_test_stop(void)
{
	ipf_ul_stress_test_stop();
    busstress_ddr_memcpy_test_stop();

	return OK;
}



