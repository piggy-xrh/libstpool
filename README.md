.简介
----
libstpool是一个开源的轻便的跨平台的动态c/c++线程池，任务池库, 支持Windows, Linux, Unix, ARM, MAC/OSX, NDK，提供丰富的特性支持.
			    

.特性
-----
* `跨平台`                      (支持）
* `优先级任务`                  (支持)
* `动态线程池`                  (支持)
* 静态线程池                    (支持)
* `任务分组并对组进行隔离控制`  (支持)
* 线程池数目随时增减控制        (支持)
* 任务过载控制                  (支持)

.编译安装
----
>Linux/MAC
  * ./configure --prefix=/usr/local
  * make -j2 && make install

>windows
  * 使用vs打开stpool_win_proj/stpool.sln
  * 编译msglog工程
  * 编译libstpool工程
  * 最后编译测试demo

>ARM 
  (假设交叉编译链为arm-linux-gcc, 库安装目录放到当前目录下的install_dir中)
  * ./configure --prefix=./install_dir --host=arm-linux-
  * make -j2 && make install

>NDK
 * 执行./configure生成配置 os/os_config.h
 * 打开os/os_config.h 注释掉ndk不支持的属性配置, 比如:  
 //#define HAS_PTHREAD_CONDATTR_SETCLOCK 1  
 //#define HAS_PTHREAD_ATTR_GETINHERITSCHED 1
 * 执行ndk-build, 生成的库将会保成在obj下

.库说明
----
头文件: 
	
	msglog.h        (简单的log API头文件)
	stpool_caps.h   (能力集定义头文件)
	stpool.h        (基础API头文件)
	stpool_group.h  (针对任务分组的API头文件)

库文件:

  使用Makefile生成的库 (一般为非windows系统)

	  libmsglog.a     (生成的log静态库)
	  libmsglog.so    (生成的log动态库)

	  libstpool.a     (生成的libstpool静态库)
	  libstpool.so    (生成的libstpool动态库)

  使用VS生成的库 (windows系统)

	  libmsglog.lib   (生成的log静态库)
	  libstpool.lib   (生成的libstpool静态库)
	
.使用范例
-----
```c++
    /** 定义期望获得的能力集 */
    eCAPs = eCAP_F_DYNAMIC|eCAP_F_ROUTINE|eCAP_F_TASK_WAIT_ALL;

	/** 创建线程池 */
	pool = stpool_create("mypool", /** 线程池名                      */
						 eCAPs,    /** 期望libstpool提供的的功能特性 */
	                      5,	   /** 线程池中运行的最大线程数目    */
				          0,	   /** 预启动提供服务的的线程数目    */
				          0,	   /** 保持线程池创建后调度任务状态  */
				          1		   /** 优先级队列数目                */
					   );
	
	/** 添加10个任务*/
	for (i=0; i<10; i++) {
		stpool_add_routine(pool, "mytask", task_run, task_err_handler, NULL, NULL);
	}

	/** 等待所有任务执行完成 */
	stpool_wait_all(pool, -1);
	
	/** 打印线程池的运行状态信息 */
	puts( stpool_stat_print(pool) );

	/** 释放线程池 */
	stpool_release(pool);
	
```
##.问题反馈
* 邮件(piggy_xrh@163.com)
* QQ: 1169732280
* QQ群: 535135194
		
					
